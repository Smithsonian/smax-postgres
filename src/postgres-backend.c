/**
 * @file
 *
 * @date Created  on Nov 5, 2023
 * @author Attila Kovacs
 *
 *  SQL backend module for smaxLogger. It supports most common SQL flavors. The specific SQL flavor to
 *  use can be selected via an appropriate compiler constant.
 */

#define _GNU_SOURCE           ///< C source code standard

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdint.h>
#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <pthread.h>
#include <time.h>
#include <semaphore.h>
#include <search.h>
#include <fnmatch.h>
#include <popt.h>
#include <libpq-fe.h>

#if USE_SYSTEMD
#  include <systemd/sd-daemon.h>
#endif

#include "smax-postgres.h"

#ifndef FIX_SCALAR_DIMS
#  define FIX_SCALAR_DIMS       0                         ///< Whether singled-element 1D data should be stored as scalars
#endif

#define POSTGRES                1                         ///< Use PostgreSQL data types from sql-types.h
#include "sql-types.h"

#define MIN_CMD_SIZE            16384                     ///< (bytes) Initial size of the command buffer

#define MASTER_TABLE            "titles"                  ///< table name in which to store variable name -> id pairings
#define VARNAME_ID              "name"                    ///< column name/id for variable names

#define TABLE_NAME_PATTERN      "var_%06d"                ///< pattern for tables names that store data for variables

#define META_NAME_PATTERN       TABLE_NAME_PATTERN "_meta"  ///< pattern for metadata table names
#define META_SERIAL_ID           "serial"                 ///< column name/id for metadata serial numbers
#define META_SHAPE_LEN          X_MAX_STRING_DIMS         ///< Maximum number of dimensions to store
#define META_UNIT_LEN           32                        ///< Maximum size for sotring physical units.

#define COL_NAME_STEM           "c"                       ///< prefix for array data columns

#define SQL_TYPE_LEN            64                        ///< (bytes) Maximum length of SQL data type names
#define SQL_TABLE_NAME_LEN      32                        ///< (bytes) Maximum length for table names
#define SQL_COL_NAME_LEN        32                        ///< (bytes) Maximum length for column names
#define DEFAULT_STRING_LEN      16                        ///< (bytes) default initial size for variable-length strings

#define SQL_SEP                 ", "                      ///< List separator

/**
 * Locally cached information of the current set of SQL variables stored
 *
 */
typedef struct {
  char *id;                     ///< SMA-X variable ID
  int index;                    ///< Unique table id (serial)
  int cols;                     ///< Number of array elements (columns)
  char sqlType[SQL_TYPE_LEN];   ///< The SQL storage type

  boolean hasMeta;              ///< (boolean) if we have metadata available
  int metaVersion;              ///< metadata serial number
  int sampling;                 ///< the current sampling interval for array data
  int ndim;                     ///< array dimensions (may be 0 for scalars)
  int sizes[X_MAX_DIMS];        ///< array sizes along each dimension
  char unit[META_UNIT_LEN];     ///< physical unit in which data is expressed
} TableDescriptor;


// Local prototypes -------------------------------------------------------->
static void initCache();

static void lockQueue();
static void unlockQueue();

static int ensureCommandCapacity(int n);

static char *printSQLString(const char *s, int l, char *dst);
static int getStringSize(XType type);

static TableDescriptor *getTableDescriptor(const Variable *u);
static TableDescriptor *getCachedTableDescriptor(const char *name);
static TableDescriptor *addVariable(const char *id, const Variable *u);
static int sqlCreateTable(const Variable *u, int id);
static int sqlConvertToHyperTable(int id);
static int sqlCreateMetaTable(int id);
static int sqlGetLastMeta(TableDescriptor *t);

static int sqlExec(char *sql, PGresult **resp);
static int sqlExecSimple(char *sql);
static int sqlConnect(const char *userName, const char *auth, const char *dbName);
static void sqlDisconnect();
static int sqlConnectRetry(int attempts);
static int sqlInsertVariable(const Variable *u);
static int sqlAddValues(const Variable *u);

static int printColumnFormat(int ncols, char *fmt);
static int printSQLType(XType type, char *dst);
static int cmpSQLType(const char *a, const char *b);
static char *appendValues(const Variable *u, char *dst);
static char *appendValue(const void *data, XType type, char *dst);

// Local variables --------------------------------------------------------->
static pthread_mutex_t qMutex = PTHREAD_MUTEX_INITIALIZER;  ///< Queue mutex
static sem_t qAvailable;                                    ///< {mut} Counting semaphore for the queue
static Variable *first = NULL, *last = NULL;                ///< {mut} Queue head and tail elements

static PGconn *sql_db;      ///< The current SQL connection information
static char *cmd;           ///< Buffer for assembling long SQL commands in.

static struct hsearch_data lookup;                          ///< Local cache hash table for stored variabled
static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;   ///< mutex for atomic transaction blocks.


static int getStringType(int maxlen, char *buf) {
  if(!buf || maxlen < 1) {
    errno = EINVAL;
    return -1;
  }

  strcpy(buf, SQL_TEXT);
  return 0;
}

/**
 * The main processing thread, which pulls values from the queue and inserts them into the
 * database asynchronously. It is started up by initialize();
 *
 * @return    NULL
 */
void *SQLThread() {
  printf("SQLThread has started\n");

  if(sqlConnectRetry(CONNECT_RETRY_ATTEMPTS) != SUCCESS_RETURN) exit(ERROR_EXIT);

# if FIX_SCALAR_DIMS
  ensureCommandCapacity(100 + SQL_TABLE_NAME_LEN)

  fprintf(stderr, "!FIX! all scalar dims -> 0.\n");
  sprintf(cmd, "UPDATE " META_NAME_PATTERN " SET ndim = 0, shape = NULL WHERE ndim = 1 AND shape = '1';", t->index);
  sqlExecSimple(cmd);
# endif

  initCache();

  // Initialize a the counting sempahore for the queue.
  sem_init(&qAvailable, 0, 0);

# if USE_SYSTEMD
  sd_notify(0, "READY=1");
  setSDState(IDLE_STATE);
# endif

  // The main processing loop.
  while(TRUE) {
    Variable *u;

    // Wait until something has been placed on the queue
    while(sem_wait(&qAvailable));

    // Take the first element from the queue...
    lockQueue();
    u = first;
    first = first->next;
    if(!first) last = NULL;
    unlockQueue();

    // ... Send to the SQL database ...
    sqlAddValues(u);

    // ... and deallocate.
    destroyVariable(u);
  }

  return NULL;
}


/**
 * Gets exclusive access for adding or removing variables to/from the queue.
 *
 */
static void lockQueue() {
  if (pthread_mutex_lock(&qMutex) != 0) perror("lockQueue()");
}


/**
 * Relinquishes exclusive access after adding or removing variables to/from the queue.
 *
 */
static void unlockQueue() {
  if (pthread_mutex_unlock(&qMutex) != 0) perror("unlockQueue()");
}


/**
 * Add the variable to the queue for database insertion.
 *
 * @param u   Pointer to the variable data structure
 * @return    SUCCESS_RETURN (0) if successful, or else ERROR_RETURN (-1; errno will indicate the type
 *            of error).
 */
int insertQueue(Variable *u) {
  if(!u) {
    errno = EINVAL;
    return ERROR_RETURN;
  }

  /* Insert in queue */
  lockQueue();
  if(!first) first = u;
  else last->next = u;
  last = u;
  sem_post(&qAvailable);
  unlockQueue();

  return SUCCESS_RETURN;
}


static int shorten(char *str, const char *match, const char *replacement) {
  if(!str || !match || !replacement) {
    errno = EINVAL;
    return -1;
  }

  if(strlen(match) < strlen(replacement)) {
    errno = EINVAL;
    return -1;
  }

  char *from = strstr(str, match);
  if(from) {
    char *rest = strdup(from + strlen(match));
    sprintf(from, "%s%s", replacement, rest);
    free(rest);
  }

  return 0;
}


/**
 * Initializes the local table ID lookup for variables, for efficient data insertions.
 * It queries the SQL database for existing tables (variables) to create the cache.
 * The cache can accomodate up to CACHE_SIZE variables.in the lookup, so make sure
 * CACHE_SIZE is defined appropriately.
 *
 */
static void initCache() {
  PGresult *tables;
  int nTitles, i, success;

  // Get all existing Titles, and cache them
  success = sqlExec("SELECT name, tid FROM " MASTER_TABLE ";", &tables);

  if (!success) {
    PQfinish(sql_db);
    exit(ERROR_EXIT);
  }

  nTitles = PQntuples(tables);
  printf("Found %d titles in DB\n", nTitles);

  // Create a hashtable for caching/lookup of translation tables.
  if(!hcreate_r(CACHE_SIZE, &lookup)) {
    fprintf(stderr, "ERROR! Could not create table lookup.\n");
    exit(ERROR_EXIT);
  }

  for (i = 0; i < nTitles; i++) {
    PGresult *columns;
    const char *id;
    char type[SQL_TYPE_LEN] = {};
    char colFmt[SQL_COL_NAME_LEN];
    int k, firstDataCol = 2, nCols, table = 0;
    ENTRY e, *added = NULL;
    TableDescriptor *desc;

    id = PQgetvalue(tables, i, 0);
    if(!id) {
      fprintf(stderr, "WARNING! NULL id in SQL entry.\n");
      continue;
    }

    id = strdup(id);
    if (id == NULL) {
      perror("ERROR: duplicate table ID");
      exit(errno);
    };

    if(1 != sscanf(PQgetvalue(tables, i, 1), "%d", &table)) {
      fprintf(stderr, "WARNING! Invalid table id '%s'.\n", PQgetvalue(tables, i, 1));
      continue;
    }

    ensureCommandCapacity(200 + SQL_TABLE_NAME_LEN);
    sprintf(cmd, "select COLUMN_NAME, DATA_TYPE from INFORMATION_SCHEMA.COLUMNS where TABLE_NAME = '" TABLE_NAME_PATTERN "';", table);
    success = sqlExec(cmd, &columns);
    if (!success) {
      PQclear(tables);
      PQfinish(sql_db);
      exit(ERROR_EXIT);
    }

    nCols = PQntuples(columns);

    for(k = 0; k < nCols; k++) {
      const char *colName = PQgetvalue(columns, k, 0);
      const int max = (int) (sizeof(type) - 1);

      // Use the first data column to determine how many data columns and what type they are.
      if(strncmp(COL_NAME_STEM "0", colName, sizeof(COL_NAME_STEM)) == 0) {
        char *storeType = PQgetvalue(columns, k, 1);
        int j;

        firstDataCol = k;
        nCols -= firstDataCol;

        // convert to upper case...
        for(j = 0; j < max && storeType[j]; j++) type[j] = toupper(storeType[j]);

        // Substitute short forms
        shorten(storeType, "CHARACTER VARIABLE", "VARCHAR");
        shorten(storeType, "CHARCTER", "CHAR");

        break;
      }
    }

    // Check and fix up column names.
    printColumnFormat(nCols, colFmt);

    for(k = 0; k < nCols; k++) {
      const char *name = PQgetvalue(columns, firstDataCol + k, 0);
      char colName[SQL_COL_NAME_LEN];

      sprintf(colName, colFmt, k);

      if(strcmp(name, colName) != 0) {
        fprintf(stderr, "!FIX! %s: column name %s -> %s\n", id, name, colName);
        sprintf(cmd, "ALTER TABLE " TABLE_NAME_PATTERN " RENAME COLUMN %s TO %s;", table, name, colName);
        sqlExecSimple(cmd);
      }
    }

    PQclear(columns);

    if(!type[0]) continue;   // Not a data table (contains no data)

    desc = (TableDescriptor *) calloc(1, sizeof(*desc));
    if(!desc) {
      perror("ERROR! alloc table format description");
      exit(errno);
    }

    desc->index = table;
    desc->cols = nCols;
    strncpy(desc->sqlType, type, sizeof(desc->sqlType) - 1);

    desc->hasMeta = sqlGetLastMeta(desc);
    desc->id = (char *) id;

    e.key = desc->id;
    e.data = desc;

    if(!hsearch_r(e, ENTER, &added, &lookup)) {
      free(e.data);
      fprintf(stderr, "WARNING! could not cache table id for '%s'.\n", id);
      break;
    }
  }

  PQclear(tables);

  printf("Created cache.\n");
}


/**
 *  Allocates or reallocates a sufficiently sized buffer for storing the SQL command.
 *
 *  \param n        Number of bytes needed in the cmd variable.
 *
 *  \return         SUCCESS_RETURN (0) if cmd can accomonade the requested number of
 *                  bytes, or ERROR_RETURN if the (re)allocation failed.
 */
static int ensureCommandCapacity(int n) {
  static int cmdSize = 0;

  // Don't bother with tiny buffers, start with an minimum...
  if(n < MIN_CMD_SIZE) n = MIN_CMD_SIZE;

  if(!cmd) {
    cmdSize = n;
    cmd = malloc(cmdSize);
  }
  else if(n > cmdSize) {
    char *old = cmd;
    cmdSize = n;
    cmd = realloc(cmd, cmdSize);
    if(!cmd) free(old);
    dprintf("Growing command buffer to %d bytes.\n", cmdSize);
  }

  if(!cmd) {
    fprintf(stderr, "ERROR! malloc command (%d bytes).\n", n);
    exit(ERROR_EXIT);
  }

  return SUCCESS_RETURN;
}




/**
 * Returns the maximum string size required for representing this type when sending it to
 * the database.
 *
 * \param type      Variable type, e.g. SHORT_TYPE
 *
 * \return          Maximum string size to represent a value of this type.
 *
 */
static int getStringSize(XType type) {
  if(xIsCharSequence(type)) return xElementSizeOf(type) + 2;  // including quotes
  return xStringElementSizeOf(type);
}


/**
 * Converts a string or fixed-length character array to representation in SQL, enclosing it in single quotes,
 * and escaping any single quotes within. It support the full set of extended ASCII characters.
 *
 * @param s     An ASCII string value
 * @param l     (bytes) The length of the string which may be unterminated.
 * @param dst   Destination buffer in which to write string.
 *
 * @return      Pointer to the position after the SQL-ized string in the destination buffer
 */
static char *printSQLString(const char *s, int l, char *dst) {
  int i;

  if(!s || !dst) {
    errno = EINVAL;
    return dst;
  }

  *(dst++) = '\'';

  for(i=0; i < l && s[i]; i++) {
    const char c = s[i];
    *(dst++) = c;
    if(c == '\'') *(dst++) = '\'';        // escape single quotes by doubling them
  }

  *(dst++) = '\'';

  return dst;
}


/**
 * Appends the SQL variable type for the given local data type at the given
 * string location, and returns the string location after the appended
 * variable type.
 *
 * \param type          The xchange data type, e.g. X_SHORT
 * \param dst           String location at which to append the corresponding
 *                      SQL type.
 *
 * \return              The number of characters printed
 *
 */
static int printSQLType(XType type, char *dst) {
  if(!dst) {
    errno = EINVAL;
    return ERROR_RETURN;
  }

  if(xIsCharSequence(type)) {
    return getStringType(xElementSizeOf(type), dst);
  }
  else switch (type) {
    case X_BOOLEAN:
      return sprintf(dst, SQL_BOOLEAN);
    case X_BYTE:
    case X_BYTE_HEX:
      return sprintf(dst, SQL_INT8);
    case X_SHORT:
    case X_SHORT_HEX:
      return sprintf(dst, SQL_INT16);
    case X_INT:
    case X_INT_HEX:
      return sprintf(dst, SQL_INT32);
    case X_LONG:
    case X_LONG_HEX:
      return sprintf(dst, SQL_INT64);
    case X_FLOAT:
      return sprintf(dst, SQL_FLOAT);
    case X_DOUBLE:
      return sprintf(dst, SQL_DOUBLE);
    default:
      fprintf(stderr, "WARNING! no matching SQL type (%d)\n", type);
  }

  errno = EBADR;
  return ERROR_RETURN;
}


static int cmpSQLType(const char *a, const char *b) {
  static const char *types = SQL_INT8 " " SQL_INT16 " " SQL_INT32 " " SQL_INT64 " " SQL_FLOAT " " SQL_DOUBLE;

  int na, nb;

  if(strcmp(a, b) == 0) return 0;

  // VARCHAR is deprecated in PostgreSQL now, but we'll consider it nevertheless -- just in case
  if(sscanf(a, "VARCHAR(%d)", &na) == 1) {
    if(sscanf(b, "VARCHAR(%d)", &nb) == 1) return na - nb;
    return -1;
  }

  return (int) (strstr(types, a) - strstr(types, b));
}


/**
 * Appends the values stored in the variable as a comma-separated list starting at the specified string
 * location, returning the string location immediately after the appended list.
 *
 * \param u     Pointer to the variable
 * \param dst   String location at which to append string list of elements
 *
 * \return      String location after the insertion (or same as dst if no string were appended).
 *
 */
static char *appendValues(const Variable *u, char *dst) {
  const XField *f;
  int eSize, step = 1;

  if(!u || !dst) {
    errno = EINVAL;
    return dst;
  }

  f = &u->field;
  eSize = xElementSizeOf(f->type);

  if(u->sampling > 1) step = u->sampling;

  if(f->ndim > 0) {
    char *data = (char *) f->value;
    int i, n = getSampleCount(u);
    for(i = 0; i < n; i++) dst = appendValue(&data[i * step * eSize], f->type, dst);
  }
  else dst = appendValue(f->value, f->type, dst);

  return dst;
}


/**
 * Appends a string representation of an elemental value (with a preceding comma)
 * at the specified location, returning the location immediately after the appended
 * string.
 *
 * \param data          Pointer to the binary element
 * \param type          Element type, e.g. SHORT_TYPE
 * \param elementSize   Bytes occupied by this element
 * \param dst           String location to append value at.
 *
 * \return              String location after the inserted element.
 */
static char *appendValue(const void *data, XType type, char *dst) {
  if(!dst) {
    errno = EINVAL;
    return dst;
  }

  dst += sprintf(dst, SQL_SEP);

  if(xIsCharSequence(type)) {
    return printSQLString((char *) data, xElementSizeOf(type), dst);
  }

  if (!data) return dst + sprintf(dst, "NULL");

  switch (type) {

    case X_BOOLEAN: return dst + sprintf(dst, "%s",*(boolean *) data ? "true" : "false");

    case X_BYTE:
    case X_BYTE_HEX: return dst + sprintf(dst, "%hhd", *(char *) data);

    case X_SHORT:
    case X_SHORT_HEX: return dst + sprintf(dst, "%hd", *(int16_t *) data);

    case X_INT:
    case X_INT_HEX: return dst + sprintf(dst, "%d", *(int32_t *) data);

    case X_LONG:
    case X_LONG_HEX: return dst + sprintf(dst, "%ld", *(int64_t *) data);

    case X_FLOAT: {
      float f = *(float *) data;
      if(isfinite(f)) return dst + sprintf(dst, "%.7g", f);      // Was %.7e
      return dst + sprintf(dst, "'NaN'");
    }
    break;

    case X_DOUBLE: {
      // SQL can be a bit picky with extreme doubles so apply some sanity here...
      double d = *(double *) data;
      if(isfinite(d)) {
        double a = fabs(d);
        if(a < SQL_MIN_DOUBLE) return dst + sprintf(dst, "0.0");
        if(a > SQL_MAX_DOUBLE) return dst + sprintf(dst, "'NaN'");
        return dst + sprintf(dst, "%.16lg", d);     // %.16e
      }
      return dst + sprintf(dst, "'NaN'");
    }

    case X_STRING: {
      const char *s = *(char **) data;
      return printSQLString(s, strlen(s), dst);
    }
    default:
      fprintf(stderr, "WARNING! addValue(): Unknown data type (%d)\n", type);
      return dst + sprintf(dst, "NULL");
  }

  return dst;
}


/**
 * Returns the cached table ID for a given compound variable name.
 *
 * \param name      The compund variable name, as returned by getLongName().
 *
 * \return          The cached table descriptor or else NULL if the table id for the
 *                  given compound name is not cached.
 *
 * \sa getLongName()
 */
static TableDescriptor *getCachedTableDescriptor(const char *name) {
  ENTRY e, *match;

  if(!name) {
    errno = EINVAL;
    return NULL;
  }

  e.key = (char *) name;
  if(!hsearch_r(e, FIND, &match, &lookup)) {
    dprintf("No cached entry for '%s'.\n", name);
    return NULL;
  }

  dprintf("Found cached table number: %d\n", *(int *) match->data);

  return (TableDescriptor *) match->data;
}


/**
 * Gets the table ID number by which the database refers to the variable.
 *
 * \param u     Pointer to the variable
 *
 * \return      The corresponfing database table descritor or NULL if there was an error.
 */
static TableDescriptor *getTableDescriptor(const Variable *u) {
  TableDescriptor *t;

  if(!u) {
    errno = EINVAL;
    return NULL;
  }

  t = getCachedTableDescriptor(u->id);
  if(!t) t = addVariable(u->id, u);
  return t;
}


static int getEnclosingStringLength(const XField *f, int sampling) {
  int i, n, max = DEFAULT_STRING_LEN;
  char **s;

  if(!f) {
    errno = EINVAL;
    return max;
  }

  if(sampling < 1) sampling = 1;

  s = (char **) f->value;
  n = xGetFieldCount(f);

  for(i = 0; i < n; i += sampling) if(s[i]) {
    int l = strlen(s[i]);
    while(l > max) max <<= 1;
  }

  return max;
}


/**
 * Adds a new table in the database to store data for the specified variable.
 *
 * \param id            The aggregated SMA-X variable ID.
 * \param u             Pointer to the variable
 *
 * return               The table descriptor for the newly created entry in the database.
 *                      or else NULL if there was an error.
 */
static TableDescriptor *addVariable(const char *id, const Variable *u) {
  ENTRY e = { NULL }, *added = NULL;
  TableDescriptor *desc;
  int idx;

  if(!id || !u) {
    errno = EINVAL;
    return NULL;
  }

  if(getSampleCount(u) > 128) {
    fprintf(stderr, "WARNING! %s: too many cols (%d).\n", id, getSampleCount(u));
  }

  idx = sqlInsertVariable(u);
  if(idx <= 0) return NULL;

  desc = calloc(1, sizeof(*desc));
  if(!desc) {
    perror("ERROR! alloc of cached table descriptor");
    exit(errno);
  }

  desc->id = strdup(id);
  if(!desc->id) {
    perror("ERROR! copy cached table id");
    exit(errno);
  }
  desc->index = idx;
  desc->cols = getSampleCount(u);

  if(u->field.type == X_STRING) getStringType(getEnclosingStringLength(&u->field, u->sampling), desc->sqlType);
  else if(printSQLType(u->field.type, desc->sqlType) < 0) {
    fprintf(stderr, "WARNING! add variable: unsupported xchange type '%c'.\n", u->field.type);
    goto add_table_cleanup; // @suppress("Goto statement used")
  }

  e.key = desc->id;
  e.data = desc;

  if(!hsearch_r(e, ENTER, &added, &lookup)) {
    fprintf(stderr, "WARNING! could not cache new variable.\n");
    goto add_table_cleanup; // @suppress("Goto statement used")
  }

  return desc;

  // ----------------------------------------------------------------
  add_table_cleanup:

  if(desc->id) free(desc->id);
  if(desc) free(desc);

  return NULL;
}


static int printColumnFormat(int ncols, char *fmt) {
  int digits;

  if(!fmt) {
    errno = EINVAL;
    return ERROR_RETURN;
  }

  if(ncols <= 1) ncols = 1;
  else ncols--;

  digits = 1 + (int) floor(log10(ncols));
  return sprintf(fmt, COL_NAME_STEM "%%0%dd", digits);
}


static int sqlBegin() {
  pthread_mutex_lock(&mutex);
  if(!sqlExecSimple("BEGIN")) {
    pthread_mutex_unlock(&mutex);
    return FALSE;
  }
  return TRUE;
}


static int sqlCommit() {
  int retval = sqlExecSimple("COMMIT;");
  pthread_mutex_unlock(&mutex);
  return retval;
}


static int sqlRollback() {
  int retval = sqlExecSimple("ROLLBACK;");
  pthread_mutex_unlock(&mutex);
  return retval;
}


static int sqlBootstrap(const char *owner, const char *passwd) {
  ensureCommandCapacity(200 + 2 * sizeof(MASTER_TABLE) + sizeof(VARNAME_ID) + sizeof(SQL_VARNAME) + sizeof(SQL_SERIAL));

  if(owner) {
    if(sqlConnect(owner, passwd, NULL) != SUCCESS_RETURN) return ERROR_RETURN;

    // Create the database and assign the database to the user
    sprintf(cmd, "CREATE DATABASE %s OWNER %s;", getSQLDatabaseName(), getSQLUserName());
    sqlExecSimple(cmd);
    sqlDisconnect();
  }

  // Now login to the database with our designated logger account...
  // Create the 'titles' table
  if(sqlConnect(getSQLUserName(), getSQLAuth(), getSQLDatabaseName()) != SUCCESS_RETURN) return ERROR_RETURN;

  // Enable the TimescaleDB extension (if configured)
  if(isUseHyperTables()) sqlExecSimple("CREATE EXTENSION IF NOT EXISTS timescaledb;");

  sprintf(cmd, "CREATE TABLE " MASTER_TABLE " (" VARNAME_ID " " SQL_VARNAME " PRIMARY KEY, tid " SQL_SERIAL " UNIQUE);");
  sqlExecSimple(cmd);

  // Add indexing to the 'titles' table for faster access.
  sprintf(cmd, "CREATE UNIQUE INDEX " MASTER_TABLE "_index_" VARNAME_ID " ON " MASTER_TABLE " (" VARNAME_ID ");");
  sqlExecSimple(cmd);

  sqlDisconnect();

  return SUCCESS_RETURN;
}


/**
 * Sets up (bootstraps) a clean new database
 *
 * @param owner   User that will own the database (it must have privileges for creating the database)
 * @param passwd  Password for oqner
 * @return  SUUCCESS_RETURN if successful, or else ERROR_RETURN.
 */
int setupDB(const char *owner, const char *passwd) {
  int status;

# if USE_SYSTEMD
  setSDState("BOOTSTRAP");
# endif

  status = sqlBootstrap(owner, passwd);

# if USE_SYSTEMD
  setSDState(IDLE_STATE);
# endif

  return status;
}


/**
 * Prints the SQL command, into the the static <code>cmd</code> string buffer, to create
 * a new table for the given variable in the database.
 *
 * \param u     Pointer to the new variable to insert in the database.
 * \param id    The table id to use.
 *
 * \return      SUCCESS_RETURN (0) if the variable has been successfully added to
 *              the database or else ERROR_RETURN (-1).
 *
 */
static int sqlCreateTable(const Variable *u, int id) {
  const XField *f = &u->field;
  XType type;
  char fmt[SQL_COL_NAME_LEN];
  char sqlType[SQL_TYPE_LEN];
  char colName[SQL_COL_NAME_LEN];
  char *next;
  int i, n = 1;

  if(!u || id < 0) {
    errno = EINVAL;
    return ERROR_RETURN;
  }

  n = getSampleCount(u);
  if(n < 1) return SUCCESS_RETURN;

  type = f->type;
  if(type == X_STRING) type = X_CHARS(getEnclosingStringLength(f, u->sampling));

  if(printSQLType(type, sqlType) < 0) {
    fprintf(stderr, "WARNING! create table: unsupported xchange type '%c'.\n", f->type);
    return ERROR_RETURN;
  }

  // Figure out the length of the last (possibly longest) column's name, incl. separator
  printColumnFormat(n, fmt);
  sprintf(colName, fmt, (n - 1));

  // The max. bytes per column definition in the SQL command...
  i = sizeof(SQL_SEP) + strlen(colName) + strlen(sqlType) + 1;

  // Make sure the command buffer is large enough...
  ensureCommandCapacity(200 + SQL_TABLE_NAME_LEN + sizeof(SQL_DATE) + sizeof(SQL_INT32) + (n * i));

  next = cmd;
  next += sprintf(next, "CREATE TABLE " TABLE_NAME_PATTERN " (time " SQL_DATE " PRIMARY KEY, age " SQL_INT32, id);

  for(i = 0; i < n; i++) {
    sprintf(colName, fmt, i);
    next += sprintf(next, SQL_SEP "%s %s", colName, sqlType);
  }

  sprintf(next, ");");

  if(!sqlExecSimple(cmd)) return ERROR_RETURN;

  return SUCCESS_RETURN;
}


static int sqlConvertToHyperTable(int id) {
  if(id < 0) {
    errno = EINVAL;
    return ERROR_RETURN;
  }

  ensureCommandCapacity(200 + SQL_TABLE_NAME_LEN + sizeof(TIMESCALE));

# if TIMESCALEDB_OLD
  // Old syntax
  sprintf(cmd, "SELECT create_hypertable('" TABLE_NAME_PATTERN "', 'time', chunk_time_interval => INTERVAL '" TIMESCALE "');", id);
# else
  // New syntax
  sprintf(cmd, "SELECT create_hypertable('" TABLE_NAME_PATTERN "', by_range('time', INTERVAL '" TIMESCALE "'));", id);
#endif

  if(!sqlExecSimple(cmd)) return ERROR_RETURN;

  return SUCCESS_RETURN;
}


static int sqlCreateMetaTable(int id) {
  char samplingType[SQL_TYPE_LEN];
  char dimType[SQL_TYPE_LEN];
  char shapeType[SQL_TYPE_LEN];
  char unitType[SQL_TYPE_LEN];

  if(id < 0) {
    errno = EINVAL;
    return ERROR_RETURN;
  }

  printSQLType(X_INT, samplingType);
  printSQLType(X_BYTE, dimType);
  printSQLType(X_CHARS(META_SHAPE_LEN - 1), shapeType);
  printSQLType(X_CHARS(META_UNIT_LEN - 1), unitType);

  // Make sure the command buffer is large enough...
  ensureCommandCapacity(200 + SQL_TABLE_NAME_LEN + sizeof(META_SERIAL_ID) + sizeof(SQL_SERIAL) + sizeof(SQL_DATE));

  // Create metadata table
  sprintf(cmd, "CREATE TABLE " META_NAME_PATTERN " (" META_SERIAL_ID " " SQL_SERIAL " PRIMARY KEY, time " SQL_DATE " NOT NULL, "
          "sampling %s DEFAULT 1, ndim %s DEFAULT 0, shape %s, unit %s);", id, samplingType, dimType, shapeType, unitType);

  if(!sqlExecSimple(cmd)) return ERROR_RETURN;

  return SUCCESS_RETURN;
}


static int sqlAddMeta(const Variable *u, TableDescriptor *t) {
  const XField *f = &u->field;
  char *next = cmd;
  int step, ndim;

  if(!u || !t) {
    errno = EINVAL;
    return ERROR_RETURN;
  }

  step = t->sampling;
  if(step < 1) step = 1;

  // Treat all singilar values as scalars.
  ndim = f->ndim;
  if(ndim < 1) ndim = 0;
  else if(ndim == 1 && t->sizes[0] <= 1) ndim = 0;

  ensureCommandCapacity(200 + META_SHAPE_LEN + META_UNIT_LEN);
  next += sprintf(next, "INSERT INTO " META_NAME_PATTERN " VALUES(DEFAULT" SQL_SEP, t->index);

  next += strftime(next, 100, SQL_DATE_FORMAT, gmtime(&u->updateTime));

  next += sprintf(next, SQL_SEP "%d", step);
  next += sprintf(next, SQL_SEP "%d", ndim);

  if(ndim > 0) {
    next += sprintf(next, SQL_SEP "'");
    next += xPrintDims(next, ndim, f->sizes);
    next += sprintf(next, "'");
  }
  else next += sprintf(next, SQL_SEP "NULL");

  if(*t->unit) next += sprintf(next, SQL_SEP "'%s'", t->unit);
  else next += sprintf(next, SQL_SEP "NULL");

  sprintf(next, ");");

  if(!sqlExecSimple(cmd)) return ERROR_RETURN;

  // Keep track of the the current associated metadata
  t->sampling = u->sampling;

  t->ndim = ndim;
  memcpy(t->sizes, f->sizes, sizeof(t->sizes));

  if(u->unit) strncpy(t->unit, u->unit, META_UNIT_LEN - 1);
  else t->unit[0] = '\0';

  t->hasMeta = TRUE;
  t->metaVersion++;

  return SUCCESS_RETURN;
}


static int sqlGetLastMeta(TableDescriptor *t) {
  PGresult *res;
  const char *val = NULL;

  if(!t) {
    errno = EINVAL;
    return FALSE;
  }

  ensureCommandCapacity(100 + SQL_TABLE_NAME_LEN + sizeof(SQL_LAST(META_SERIAL_ID)));

  sprintf(cmd, "SELECT * FROM " META_NAME_PATTERN " " SQL_LAST(META_SERIAL_ID) ";", t->index);
  if(!sqlExec(cmd, &res)) return FALSE;

  if(PQntuples(res) < 1) {
    PQclear(res);
    errno = ECHRNG;
    return FALSE;
  }

  val = PQgetvalue(res, 0, 0);
  if(val) sscanf(val, "%d", &t->metaVersion);

  t->sampling = 1;
  val = PQgetvalue(res, 0, 2);
  if(val) sscanf(val, "%d", &t->sampling);

  t->ndim = 0;
  memset(t->sizes, 0, sizeof(t->sizes));

  val = PQgetvalue(res, 0, 3);
  if(val) if(sscanf(val, "%d", &t->ndim) == 1) if(t->ndim > 0) {
    val = PQgetvalue(res, 0, 4);
    if(val) xParseDims(val, t->sizes);
  }

  val = PQgetvalue(res, 0, 5);
  if(val) if(*val) strncpy(t->unit, val, sizeof(t->unit) - 1);

  PQclear(res);

  return TRUE;
}


static boolean isMetaUpdate(const Variable *u, const TableDescriptor *t) {
  const XField *f = &u->field;
  int i;
  int ndim = f->ndim;

  if(!u || !t) {
    errno = EINVAL;
    return FALSE;
  }

  // If the table does not have associated metadata already, then let's add one.
  if(!t->hasMeta) {
    dprintf("! Initial metadata %s\n", u->id);
    return TRUE;
  }

  if(t->sampling != u->sampling) {
    dprintf("! Found new sampling for %s\n", u->id);
    return TRUE;
  }

  // Treat all singular values as scalar
  if(ndim < 1) ndim = 0;
  else if(ndim == 1 && f->sizes[0] <= 1) ndim = 0;

  if(ndim != u->field.ndim) {
    dprintf("! Found new dimensionality for %s\n", u->id);
    return TRUE;
  }

  for(i = 0; i < t->ndim; i++) if(t->sizes[i] != u->field.sizes[i]) {
    dprintf("! Found new shape for %s\n", u->id);
    return TRUE;
  }

  if(u->unit) {
    size_t n = strlen(u->unit);
    if(n >= sizeof(t->unit)) n = sizeof(t->unit) - 1;
    if(strncmp(u->unit, t->unit, n) != 0) {
      dprintf("! Found new physical unit for %s\n", u->id);
      return TRUE;
    }
  }

  return FALSE;
}


/**
 *  Execute an SQL command.
 *
 *  \param sql      Pointer to the command string
 *  \param resp     Reference to the object pointer containing the response.
 *
 *  \return         TRUE (non-zero) on success, or FALSE (0) on error.
 */
static int sqlExec(char *sql, PGresult **resp) {
  if(!sql || !resp) {
    errno = EINVAL;
    return FALSE;
  }

  if(!cmd[0]) {
    errno = EAGAIN;
    return FALSE;
  }

  *resp = PQexec(sql_db, sql);
  dprintf("SQL: %s\n", sql);
  if(PQresultStatus(*resp) != PGRES_COMMAND_OK && PQresultStatus(*resp) != PGRES_TUPLES_OK) {
    fprintf(stderr, "WARNING! %s SQL error: %s", sql,  PQerrorMessage(sql_db));
    errno = EBADE;
    return FALSE;
  }

  return TRUE;
}


/**
 *  Executes an SQL command, ignoring the response received from the server.
 *
 *  \param sql      Pointer to the command string
 *
 *  \return         TRUE (non-zero) on success, or FALSE (0) on error.
 */
static int sqlExecSimple(char *sql) {
  PGresult *reply = NULL;
  int success;

  if(!sql) {
    errno = EINVAL;
    return FALSE;
  }

  success = sqlExec(sql, &reply);
  if(success) PQclear(reply);
  return success;
}


static void sqlDisconnect() {
  dprintf("Disconnecting.\n");
  pthread_mutex_lock(&mutex);
  if(sql_db) {
    PQfinish(sql_db);
    sql_db = NULL;
  }
  pthread_mutex_unlock(&mutex);
}


/**
 * Connects to the SQL server.
 *
 * \param userName  Database user name (cannot be NULL)
 * \param dbName    Database name or NULL
 * \return      SUCCESS_RETURN (0) if the connected, or else ERROR_RETURN (-1).
 */
static int sqlConnect(const char *userName, const char *auth, const char *dbName) {
  int pos;

  if(!userName) {
    errno = EINVAL;
    return ERROR_RETURN;
  }

  ensureCommandCapacity(200);

  pos = sprintf(cmd, "host=%s user=%s", getSQLServerAddress(), userName);

  if(dbName) pos += sprintf(&cmd[pos], " dbname=%s", dbName);

  // Add password if needed
  if(auth) sprintf(&cmd[pos], " password=%s", auth);

  dprintf("connect %s\n", cmd);
  sql_db = PQconnectdb(cmd);

  // Check to see that the backend connection was successfully made
  if (PQstatus(sql_db) != CONNECTION_OK) {
    fprintf(stderr, "WARNING! connect to '%s' failed: %s\n", cmd, PQerrorMessage(sql_db));
    PQfinish(sql_db);
    return ERROR_RETURN;
  }
  printf("Connected to SQL server\n");

  atexit(sqlDisconnect);

  return SUCCESS_RETURN;
}


/**
 * Connects to the SQL server. If not successful, it will keep trying to connect the specified
 * number of times, at intervals defined by CONNECT_RETRY_SECONDS.
 *
 * \param       Number of retry attempts.
 *
 * \return      SUCCESS_RETURN (0) if the connection has been established, or ERROR_RETURN (-1)
 *              if could not connect even after the requested number of retries.
 *
 */
static int sqlConnectRetry(int attempts) {
  int i;
  // Connect to the database (and keep retrying...)
  for(i = 1; i < attempts; i++) {
    if(sqlConnect(getSQLUserName(), getSQLAuth(), getSQLDatabaseName()) == SUCCESS_RETURN) return SUCCESS_RETURN;
    fprintf(stderr, "Will retry connecting to SQL server in %d seconds (%d of %d)...\n", CONNECT_RETRY_SECONDS, i, attempts);
    sleep(CONNECT_RETRY_SECONDS);
  }

  fprintf(stderr, "ERROR! SQL connection failed after %d attemps. Exiting.\n", i);
  errno = ENOTCONN;
  return ERROR_RETURN;
}


/**
 *  Create a new variable in the database.  Returns new table id on
 *  success, zero on error.  is_dsm should be true if the variable is
 *  from DSM (will cause a unix timestamp column to be added)
 *
 *  \param      Pointer to the variable for which to create a new entity in the database.
 *
 *  \return     SUCCESS_RETURN (0) if the variable was successfully added in the databsase
 *              or ERROR_RETURN (-1) otherwise.
 */
static int sqlInsertVariable(const Variable *u) {
  PGresult *reply;
  int tid;

  if(!u) {
    errno = EINVAL;
    return ERROR_RETURN;
  }

  // do it atomically
  if(!sqlBegin()) return ERROR_RETURN;

  fprintf(stderr, "!ADD! %s\n", u->id);

  ensureCommandCapacity(100 + strlen(u->id));

  // Create a new title in the DB for the variable-name, assigning it a tid
  sprintf(cmd, "INSERT INTO titles VALUES('%s', DEFAULT) RETURNING tid;", u->id);
  if(!sqlExec(cmd, &reply)) goto cleanup; // @suppress("Goto statement used")

  if(1 != sscanf(PQgetvalue(reply, 0, 0), "%d", &tid)) goto cleanup; // @suppress("Goto statement used")

  PQclear(reply);

  // Create a table containing columns for each variable entry
  if(sqlCreateTable(u, tid) != SUCCESS_RETURN) goto cleanup; // @suppress("Goto statement used")

  if(isUseHyperTables()) if(sqlConvertToHyperTable(tid) != SUCCESS_RETURN) goto cleanup; // @suppress("Goto statement used")

  // Create an index for the table for faster data access
  ensureCommandCapacity(100 + 2 * SQL_TABLE_NAME_LEN);
  sprintf(cmd, "CREATE UNIQUE INDEX " TABLE_NAME_PATTERN "_index_time ON " TABLE_NAME_PATTERN " (time);", tid, tid);

  if(!sqlExecSimple(cmd)) goto cleanup; // @suppress("Goto statement used")

  if(sqlCreateMetaTable(tid) != SUCCESS_RETURN)  goto cleanup; // @suppress("Goto statement used")

  // Finish the atomic block
  if(!sqlCommit()) tid = ERROR_RETURN;

  return tid;

  // -------------------------------------------------------------------------------
  cleanup:

  sqlRollback();

  return ERROR_RETURN;
}


static int sqlAddColumns(TableDescriptor *t, const Variable *u) {
  char tabName[SQL_TABLE_NAME_LEN];
  char colName[SQL_COL_NAME_LEN];
  char oldFmt[SQL_COL_NAME_LEN], newFmt[SQL_COL_NAME_LEN];

  if(!t || !u) {
    errno = EINVAL;
    return ERROR_RETURN;
  }

  int i, nCols = getSampleCount(u);

  if(nCols <= t->cols) return SUCCESS_RETURN;

  fprintf(stderr, "!CHANGE! Add %d columns to %s\n", nCols - t->cols, t->id);

  sprintf(tabName, TABLE_NAME_PATTERN, t->index);

  ensureCommandCapacity(200 + sizeof(tabName) + 2 * sizeof(colName));

  // do it atomically
  if(!sqlBegin()) return ERROR_RETURN;

  // Check if column names need extra digits compared to what we had before
  if(printColumnFormat(t->cols, oldFmt) < printColumnFormat(nCols, newFmt)) for(i = 0; i < t->cols; i++) {
    char oldName[SQL_COL_NAME_LEN];

    sprintf(oldName, oldFmt, i);  // existing column name
    sprintf(colName, newFmt, i);  // new column name with extra digit

    // Change the old column name to add the extra digit
    sprintf(cmd, "ALTER TABLE %s RENAME COLUMN %s TO %s;", tabName, oldName, colName);
    if(!sqlExecSimple(cmd)) goto cleanup; // @suppress("Goto statement used")
  }

  while(t->cols < nCols) {
    sprintf(colName, newFmt, t->cols);  // added column name

    // Add a column to the table
    sprintf(cmd, "ALTER TABLE %s ADD COLUMN %s %s;", tabName, colName, t->sqlType);
    if(!sqlExecSimple(cmd)) goto cleanup; // @suppress("Goto statement used")

    t->cols++;
  }

  // Finish atomic block
  if(!sqlCommit()) return ERROR_RETURN;

  return SUCCESS_RETURN;

  // -------------------------------------------------------------------------------
  cleanup:

  sqlRollback();

  return ERROR_RETURN;
}


static int sqlChangeType(TableDescriptor *t, const char *newType) {
  char tabName[SQL_TABLE_NAME_LEN];
  char colName[SQL_COL_NAME_LEN];
  char fmt[SQL_COL_NAME_LEN];

  int i;

  fprintf(stderr, "!CHANGE! %s type to %s\n", t->id, newType);

  sprintf(tabName, TABLE_NAME_PATTERN, t->index);

  printColumnFormat(t->cols, fmt);

  ensureCommandCapacity(100 + sizeof(tabName) + 2 * sizeof(colName));

  // do it atomically
  if(!sqlBegin()) return ERROR_RETURN;

  for(i = 0; i < t->cols; i++) {
    sprintf(colName, fmt, i);

    // Change the column type -- the syntax is SQL flavor dependent
    sprintf(cmd, "ALTER TABLE %s ALTER COLUMN %s TYPE %s;", tabName, colName, newType);
    if(!sqlExecSimple(cmd)) goto cleanup; // @suppress("Goto statement used")
  }

  // Finish the atomic block
  if(!sqlCommit()) return ERROR_RETURN;

  strncpy(t->sqlType, newType, sizeof(t->sqlType) - 1);

  return SUCCESS_RETURN;

  // -------------------------------------------------------------------------------
  cleanup:

  sqlRollback();

  return ERROR_RETURN;
}


/**
 * Pushes the data from a variable into the database, creating a new table if necessary for new
 * variables.
 *
 * \param u     Pointer to the variable
 * \return      SUCCESS_RETURN (0) if successful, or else ERROR_RETURN (-1).
 */
static int sqlAddValues(const Variable *u) {
  const XField *f = &u->field;
  TableDescriptor *t;
  int len;
  char *next = cmd;
  char sqlType[SQL_TYPE_LEN];

  if(!u) {
    errno = EINVAL;
    return ERROR_RETURN;
  }

  t = getTableDescriptor(u);
  if(!t) {
    fprintf(stderr, "ERROR! No SQL table -- this is just great.\n");
    errno = EAGAIN;
    return ERROR_RETURN;
  }

  dprintf("Found cached translation entry, TID = %d\n", t->index);

  if(f->type == X_STRING) {
    len = getEnclosingStringLength(&u->field, u->sampling);

    if(strcmp(t->sqlType, SQL_TEXT) != 0) {
      int got = 0;
      sscanf(t->sqlType, "VARCHAR(%d)", &got);

      if(len <= got) {
        getStringType(getEnclosingStringLength(&u->field, u->sampling), sqlType);
        if(sqlChangeType(t, sqlType) < 0) return ERROR_RETURN;
      }
    }

    len += 2; // + quotes around string values....
  }
  else if(printSQLType(f->type, sqlType) < 0) {
    fprintf(stderr, "WARNING! add values: unsupported xchange type '%c' - skipping\n", f->type);
    return ERROR_RETURN;
  }
  else {
    if(cmpSQLType(sqlType, t->sqlType) > 0) if(sqlChangeType(t, sqlType) < 0) return ERROR_RETURN;

    len = getStringSize(f->type); // maximum size for each entry
    if(len < 0) {
      fprintf(stderr, "WARNING! Unknown string size of data type '%c' - skipping\n", f->type);
      return ERROR_RETURN;
    }
  }

  if(getSampleCount(u) > t->cols) if(sqlAddColumns(t, u) < 0) return ERROR_RETURN;

  len += sizeof(SQL_SEP); // + separator

  ensureCommandCapacity(200 + SQL_TABLE_NAME_LEN + getSampleCount(u) * len);

  /* Now insert the data */
  next += sprintf(next, "INSERT INTO " TABLE_NAME_PATTERN " VALUES(", t->index);
  next += strftime(next, 100, SQL_DATE_FORMAT, gmtime(&u->grabTime));
  next += sprintf(next, SQL_SEP "'%d'", (int) (u->grabTime - u->updateTime));
  next = appendValues(u, next);
  next += sprintf(next, ");");

  // Add values in an atomic block...
  if(!sqlBegin()) return ERROR_RETURN;

  if(!sqlExecSimple(cmd)) goto cleanup; // @suppress("Goto statement used")

  if(isMetaUpdate(u, t)) if(sqlAddMeta(u, t) != SUCCESS_RETURN) goto cleanup; // @suppress("Goto statement used")

  if(!sqlCommit()) return ERROR_RETURN;

  return SUCCESS_RETURN;

  // -------------------------------------------------------------------------------
  cleanup:

  sqlRollback();

  return ERROR_RETURN;
}


static boolean sqlDeleteVar(const char *id) {
  int tid, n = 0;

  if(!id) return FALSE;

  ensureCommandCapacity(100 + SQL_TABLE_NAME_LEN + sizeof(VARNAME_ID) + strlen(id));

  // Delete variable table
  sprintf(cmd, "DROP TABLE %s;", id);
  if(sqlExecSimple(cmd)) n++;

  // Delete metadata
  if(sscanf(id, TABLE_NAME_PATTERN, &tid) < 1) fprintf(stderr, "WARNING! Invalid " VARNAME_ID " = '%s'.\n", id);
  else {
    sprintf(cmd, "DROP TABLE " META_NAME_PATTERN ";", tid);
    if(sqlExecSimple(cmd)) n++;
  }

  // Remove variable from titles
  sprintf(cmd, "DELETE FROM " MASTER_TABLE " WHERE " VARNAME_ID " = %s;", id);
  if(sqlExecSimple(cmd)) n++;

  return n > 0;
}


/**
 * Deletes variables and metadata from the SQL DB, and removes them from the master table also.
 *
 * @param pattern   Glob variable name pattern
 * @return          The number of variables deleted from the SQL DB, or -1 if there was an error
 *                  (errno will indicate the type of error).
 */
int deleteVars(const char *pattern) {
  PGresult *tables;
  int nTitles, i, success, n = 0;

  if(!pattern) {
    errno = EINVAL;
    return -1;
  }

  // Get all existing titles, and cache them
  success = sqlExec("SELECT name FROM " MASTER_TABLE ";", &tables);

  if (!success) {
    fprintf(stderr, "ERROR! initialize: " MASTER_TABLE " query failed: %s\n", PQerrorMessage(sql_db));
    PQfinish(sql_db);
    errno = EAGAIN;
    return -1;
  }

  nTitles = PQntuples(tables);
  printf("Found %d titles in DB\n", nTitles);

  for (i = 0; i < nTitles; i++) {
    char *id = PQgetvalue(tables, i, 0);

    if(!id) {
      fprintf(stderr, "WARNING! NULL id in SQL entry.\n");
      continue;
    }

    if(fnmatch(pattern, id, 0)) if(sqlDeleteVar(id)) n++;
  }

  PQclear(tables);

  return n;
}

