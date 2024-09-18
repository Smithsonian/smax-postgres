/**
 * @file
 *
 * @date Created  on Nov 1, 2023
 * @author Attila Kovacs
 *
 *  Configuration support for smaxLogger.
 */

#define _GNU_SOURCE           ///< C source code standard

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <pthread.h>
#include <fnmatch.h>
#include <search.h>
#include <ctype.h>

#include "smax.h"
#include "smax-postgres.h"

#define LOOKUP_INITIAL_CAPACITY   65536   ///< Initial hashtable capacity

#define MIN_AGE     ( 1 * DAY )   ///< (s) Provide slow updates for unchanging variable at least this long
#define MIN_SIZE    8             ///< (bytes) Always log variable up to this size, no matter

typedef struct pattern_rule {
  char *pattern;              ///< glob variable name pattern
  int ival;                   ///< optional associated integer option
  struct pattern_rule *next;  ///< link to the next rule in the chain
} pattern_rule;


static pattern_rule *excludes;
static pattern_rule *force;
static pattern_rule *samplings;

static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

static struct hsearch_data lookup;
static int capacity = 0, nVars = 0;

static char *smaxServer;
static char *sqlServer;
static char *dbName;
static char *dbUser;
static char *dbAuth;
static boolean use_hyper_tables = FALSE;

static int update_interval = MINUTE;    ///< (s) The rate of fast updates for changing variables (min. 1m).
static int snapshot_interval = MINUTE;  ///< (s) The rate of snapshotting all variables (min. 1m).
static int max_age = DEFAULT_MAX_AGE;   ///< (s) Maximum age of variable to log if not changing.
static int max_size = DEFAULT_MAX_SIZE; ///< (bytes) Maximum byte size of variable to log


static void discardList(pattern_rule **list) {
  pattern_rule *r;

  if(!list) return;

  pthread_mutex_lock(&mutex);
  r = *list;
  *list = NULL;
  pthread_mutex_unlock(&mutex);

  while(r) {
    pattern_rule *next = r->next;
    free(r);
    r = next;
  }
}

static int add_rule(pattern_rule **list, const char *pattern, int ival) {
  if(!list || !pattern) {
    errno = EINVAL;
    return -1;
  }

  pattern_rule *r = (pattern_rule *) calloc(1, sizeof(pattern_rule));
  if(!r) {
    perror("ERROR! alloc of new logger rule");
    exit(errno);
  }

  pthread_mutex_lock(&mutex);
  r->pattern = strdup(pattern);
  r->ival = ival;
  r->next = *list;
  *list = r;
  pthread_mutex_unlock(&mutex);

  return 0;
}

static pattern_rule *get_last_rule_for(const char *name, pattern_rule *list) {
  if(!name || !list) {
    errno = EINVAL;
    return NULL;
  }

  pthread_mutex_lock(&mutex);

  for(; list; list = list->next) if(fnmatch(list->pattern, name, 0) == 0) {
    pthread_mutex_unlock(&mutex);
    return list;
  }

  pthread_mutex_unlock(&mutex);
  return NULL;
}

static void lc(char *value) {
  if(!value) return;

  for(; *value; value++) *value = tolower(*value);
}


static double parseTimeSpec(const char *str) {
  double value;
  char unit = 's';

  if(!str) {
    errno = EINVAL;
    return NAN;
  }

  if(strcasecmp(str, "none") == 0) return -1;

  if(sscanf(str, "%lf%c ", &value, &unit) < 1) return NAN;

  switch(unit) {
    case 's': return value;
    case 'm': return value * MINUTE;
    case 'h': return value * HOUR;
    case 'd': return value * DAY;
    case 'w': return value * WEEK;
    case 'y': return value * YEAR;
  }

  return NAN;
}


/**
 * Pases settings from a specified configuration file
 *
 * @param filename    the file name / path of the configuration to load.
 * @return            0 if successful, or else -1 (errno will indicate the type of error).
 */
int parseConfig(const char *filename) {
  FILE *f;
  char line[1024] = {'\0'};
  int l;

  if(!filename) {
    errno = EINVAL;
    return -1;
  }

  f = fopen(filename, "r");
  if(!f) {
    fprintf(stderr, "WARNING! %s: %s\n", filename, strerror(errno));
    return -1;
  }

  discardList(&excludes);

  // Always exclude all temp tables and fields
  add_rule(&excludes, "_*", TRUE);
  add_rule(&excludes, "*" X_SEP "_*", TRUE);

  // Always exclude meta tables and fields
  add_rule(&excludes, "<*", TRUE);
  add_rule(&excludes, "*" X_SEP "<*", TRUE);

  for(l = 1; fgets(line, sizeof(line) - 1, f) != NULL; l++) if(*line) if(*line != '#') {
    char *option = NULL, *arg = NULL;

    option = strtok(line, " \t\n=");
    if(!option) continue;

    arg = strtok(NULL, "#\n");
    if(!arg) {
      fprintf(stderr, "WARNING! [%s:%d] missing option argument: [%s]\n", filename, l, line);
      continue;
    }

    lc(option);

    dprintf(" O [%s] [%s]\n", option, arg);

    if(strcmp("smax_server", option) == 0) {
      setSMAXServerAddress(arg);
      continue;
    }

    if(strcmp("sql_server", option) == 0) {
      setSQLServerAddress(arg);
      continue;
    }

    if(strcmp("sql_db", option) == 0) {
      setSQLDatabaseName(arg);
      continue;
    }

    if(strcmp("sql_user", option) == 0) {
      setSQLUserName(arg);
      continue;
    }

    if(strcmp("sql_auth", option) == 0) {
      setSQLAuth(arg);
      continue;
    }

    if(strcmp("use_hyper_tables", option) == 0) {
      lc(arg);
      if(strcmp(arg, "true") || strcmp(arg, "1")) use_hyper_tables = TRUE;
      else if(strcmp(arg, "false") || strcmp(arg, "0")) use_hyper_tables = FALSE;
      else fprintf(stderr, "WARNING! [%s:%d] expected boolean, got: %s\n", filename, l, arg);
      continue;
    }

    if(strcmp("update_interval", option) == 0) {
      double t = parseTimeSpec(arg);
      if(isnan(t)) {
        fprintf(stderr, "WARNING! [%s:%d] update: invalid argument: %s\n", filename, l, arg);
        continue;
      }
      if(t < MINUTE) {
        fprintf(stderr, "WARNING! [%s:%d] update: below minimum value: %s\n", filename, l, arg);
        continue;
      }

      update_interval = (int) round(t);
      continue;
    }

    if(strcmp("snapshot_interval", option) == 0) {
      double t = parseTimeSpec(arg);
      if(isnan(t)) {
        fprintf(stderr, "WARNING! [%s:%d] snapshot: invalid argument: %s\n", filename, l, arg);
        continue;
      }
      if(t <= MINUTE) {
        fprintf(stderr, "WARNING! [%s:%d] snapshot: below minimum value: %s\n", filename, l, arg);
        continue;
      }

      snapshot_interval = (int) round(t);
      continue;
    }

    if(strcmp("max_size", option) == 0) {
      int bytes;
      if(sscanf(arg, "%d", &bytes) < 1) {
        fprintf(stderr, "WARNING! [%s:%d] max_size invalid argument: %s\n", filename, l, arg);
        continue;
      }
      if(bytes < MIN_SIZE) {
        fprintf(stderr, "WARNING! [%s:%d] max_size below limit (%d): %s\n", filename, l, MIN_SIZE, arg);
        continue;
      }
      max_size = bytes;
      continue;
    }

    if(strcmp("max_age", option) == 0) {
      double t = parseTimeSpec(arg);
      if(isnan(t)) {
        fprintf(stderr, "WARNING! [%s:%d] max_age: invalid argument: %s\n", filename, l, arg);
        continue;
      }
      if(t <= MIN_AGE) {
        fprintf(stderr, "WARNING! [%s:%d] max_age: below limit (%d): %s\n", filename, l, MIN_AGE, arg);
        continue;
      }

      max_age = (int) ceil(t);
      continue;
    }

    if(strcmp("exclude", option) == 0) {
      add_rule(&excludes, arg, TRUE);
      continue;
    }

    if(strcmp("include", option) == 0) {
      add_rule(&excludes, arg, FALSE);
      continue;
    }

    if(strcmp("always", option) == 0) {
      add_rule(&force, arg, TRUE);
      continue;
    }

    if(strcmp("sample", option) == 0) {
      char pattern[1024];
      int step;

      if(sscanf(arg, "%d %1023s", &step, pattern) < 2) {
        fprintf(stderr, "WARNING! [%s:%d] sample: too few arguments\n", filename, l);
        continue;
      }

      if(step < 1) {
        fprintf(stderr, "WARNING! [%s:%d] sample: invalid step argument: %d\n", filename, l, step);
        continue;
      }

      add_rule(&samplings, pattern, step);
      continue;
    }
  }

  fclose(f);

  if(update_interval < 0 && snapshot_interval < 0) {
    fprintf(stderr, "ERROR! Both updates and snapshots are disabled. Nothing to do.\n");
    exit(1);
  }

  return 0;
}

/**
 * Returns the number of samples that should be logged into the SQL database for a given SMA-X variable,
 * which may be different from the element count of the variable by a configured downsampling factor.
 *
 * @param u   Pointer to the variable's data structure
 * @return    the number of samples that should be stored in the SQL database, or 0 if the argument was NULL.
 */
int getSampleCount(const Variable *u) {
  int n;

  if(!u) {
    errno = EINVAL;
    return 0;
  }

  n = xGetFieldCount(&u->field);
  if(n <= 0) return 0;
  if(u->sampling < 2) return n;
  return (n + u->sampling - 1) / u->sampling;
}

static logger_properties *add_properties_for(const char *id) {
  ENTRY e, *found = NULL;
  logger_properties *p;
  const pattern_rule *r;

  if(!id) {
    errno = EINVAL;
    return NULL;
  }

  p = (logger_properties *) calloc(1, sizeof(*p));
  if(!p) {
    perror("ERROR! alloc of logger properties");
    exit(errno);
  }

  r = get_last_rule_for(id, samplings);
  if(r) p->sampling = r->ival;
  else p->sampling = 1;

  r = get_last_rule_for(id, force);
  if(r) p->force = r->ival ? TRUE : FALSE;

  if(!p->force) {
    r = get_last_rule_for(id, excludes);
    if(r) p->exclude = r->ival;
  }

  if(++nVars >= capacity) {
    hdestroy_r(&lookup);
    capacity <<= 1;
    if(!hcreate_r(capacity, &lookup)) {
      perror("ERROR! realloc variable properties dictionary");
      exit(errno);
    }
  }

  e.key = strdup(id);
  e.data = p;

  if(!hsearch_r(e, ENTER, &found, &lookup)) {
    perror("WARNING! could not add properties to lookup");
  }

  hsearch_r(e, FIND, &found, &lookup);

  return (logger_properties *) found->data;
}

/**
 * Returns the currently configured logging properties for an SMA-X variable.
 *
 * @param id    The aggregate name/ID of the SMA-X variable
 * @return      Pointer to the logging properties data structure, or NULL if there isn't one
 *              or if the id was NULL (errno will be set to EINVAL in case of the latter).
 */
logger_properties *getLogProperties(const char *id) {
  ENTRY e = {}, *found = NULL;

  if(!id) {
    errno = EINVAL;
    return NULL;
  }

  e.key = (char *) id;
  return hsearch_r(e, FIND, &found, &lookup) ? (logger_properties *) found->data : add_properties_for(id);
}


/**
 * Checks if a given variable is to be logged into the SQL database
 *
 * @param id          The aggregate name/ID of the SMA-X variable
 * @param updateTime  (s) UNIX timestamp when the variable was last updated in the SMA-X database.
 * @return            TRUE (1) if the variable should be logged into the SQL database, or else FALSE (0)
 */
boolean isLogging(const char *id, double updateTime) {
  const logger_properties *p;
  const time_t now = time(NULL);

  if(!id) {
    errno = EINVAL;
    return FALSE;
  }

  if(!capacity) {
    if(!hcreate_r(LOOKUP_INITIAL_CAPACITY, &lookup)) {
      perror("ERROR! alloc variable properties dictionary");
      exit(errno);
    }
    capacity = LOOKUP_INITIAL_CAPACITY;
  }

  p = getLogProperties(id);

  if(p->force) return TRUE;
  if(updateTime + max_age < now) return FALSE;
  return !p->exclude;
}

/**
 * Returns the SMA-X server host name or IP address.
 *
 * @return   the host name or IP address of the SMA-X server
 *
 * @sa setSMAXServerAddress()
 */
const char *getSMAXServerAddress() {
  return smaxServer ? smaxServer : SMAX_DEFAULT_HOSTNAME;
}


/**
 * Sets the SMA-X server address or IP.
 *
 * @param addr      The host name or IP address of the SMA-X server
 * @return          0 if successful, or else -1 if the name is NULL (errno will be set to EINVAL)
 *
 * @sa getSMAXServerAddress()
 */
int setSMAXServerAddress(const char *addr) {
  if(!addr) {
    errno = EINVAL;
    return -1;
  }
  if(smaxServer) free(smaxServer);
  smaxServer = strdup(addr);
  return 0;
}


/**
 * Returns the SQL server host name or IP address.
 *
 * @return   the host name or IP address of the SQL server
 *
 * @sa setSQLServerAddress()
 */
const char *getSQLServerAddress() {
  return sqlServer ? sqlServer : DEFAULT_SQL_SERVER;
}


/**
 * Sets the SQL server address or IP.
 *
 * @param addr      The host name or IP address of the SQL server
 * @return          0 if successful, or else -1 if the name is NULL (errno will be set to EINVAL)
 *
 * @sa getSQLServerAddress()
 * @sa setSQLDatabaseName()
 * @sa setSQLUserName()
 * @sa setSQLAuth()
 */
int setSQLServerAddress(const char *addr) {
  if(!addr) {
    errno = EINVAL;
    return -1;
  }
  if(sqlServer) free(sqlServer);
  sqlServer = strdup(addr);
  return 0;
}

/**
 * Returns the SQL database to use when connecting to the database.
 *
 * @return    The database name to select for logging into the SQL server
 *
 * @sa setSQLDatabaseName()
 */
const char *getSQLDatabaseName() {
  return dbName ? dbName : DEFAULT_SQL_DB;
}

/**
 * Sets the SQL database name to select when connecting to the SQL server.
 *
 * @param name      The SQL database name
 * @return          0 if successful, or else -1 if the name is NULL (errno will be set to EINVAL)
 *
 * @sa getSQLDatabaseName()
 * @sa setSQLUserName()
 * @sa setSQLAuth()
 * @sa setSQLServerAddress()
 */
int setSQLDatabaseName(const char *name) {
  if(!name) {
    errno = EINVAL;
    return -1;
  }
  if(dbName) free(dbName);
  dbName = strdup(name);
  return 0;
}

/**
 * Returns the SQL user name to use when connecting to the database.
 *
 * @return    The username to use for logging into the SQL database
 *
 * @sa setSQLUserName()
 */
const char *getSQLUserName() {
  return dbUser ? dbUser : DEFAULT_SQL_USER;
}

/**
 * Sets the SQL user name to use when connecting to the database.
 *
 * @param name      The SQL database user name
 * @return          0 if successful, or else -1 if the name is NULL (errno will be set to EINVAL)
 *
 * @sa getSQLUserName()
 * @sa setSQLDatabaseName()
 * @sa setSQLAuth()
 * @sa setSQLServerAddress()
 */
int setSQLUserName(const char *name) {
  if(!name) {
    errno = EINVAL;
    return -1;
  }
  if(dbUser) free(dbUser);
  dbUser = strdup(name);
  return 0;
}

/**
 * Returns the SQL database password to use when connecting to the database.
 *
 * @return    The password to use for logging into the SQL database, or NULL to use passwordless login
 *
 * @sa setSQLAuth()
 */
const char *getSQLAuth() {
  return dbAuth;
}

/**
 * Sets the SQL database password to use when connecting to the database.
 *
 * @param passwd    The SQL database password to use
 * @return          0
 *
 * @sa getSQLAuth()
 * @sa setSQLDatabaseName()
 * @sa setSQLUserName()
 * @sa setSQLServerAddress()
 */
int setSQLAuth(const char *passwd) {
  if(dbAuth) free(dbAuth);
  dbAuth = passwd ? strdup(passwd) : NULL;
  return 0;
}

/**
 * Checks whether to use TimescaleDB hypertables. TimescaleDB is available for PostgreSQL only, so the setting
 * will not affect other database backends.
 *
 * @return TRUE (1) if to create hypertables for new variables, if possible, or else FALSE (0).
 *
 * @sa setUseHyperTables()
 */
boolean isUseHyperTables() {
  return use_hyper_tables;
}

/**
 * Sets whether to use TimescaleDB hypertables extension. TimescaleDB is available for PostgreSQL only, so the setting
 * will not affect other database backends.
 *
 * @param value   TRUE (non-zero) to enable TimescaleDB hypertables.
 *
 * @sa isUseHyperTables()
 */
void setUseHyperTables(boolean value) {
  use_hyper_tables = (value != 0);
}

/**
 * Returns the maximum byte size for automatically logged variables, in their binary storage format. For variables
 * that are sampled at some interval
 *
 * @return  (bytes)
 */
int getMaxLogSize() {
  return max_size;
}

/**
 * Returns the currently configured update interval.
 *
 * @return    (s) the currently configured snapshot interval. Values &lt;=0 indicate that snapshots are disabled.
 *
 * @sa getUpdateUniterval()
 */
int getUpdateInterval() {
  return update_interval > 0 ? update_interval : snapshot_interval;
}

/**
 * Returns the currently configured snapshot interval. Snapshots will be taken in the regular update cycle, whenever
 * the time since the last snapshot equals or exceeds the set interval. For example, if the update interval is '2m',
 * and the snapshot interval is '11m', then snapshots will be generated at every 6th update cycle, that is at every
 * 12 minutes.
 *
 * @return    (s) the currently configured snapshot interval. Values &lt;=0 indicate that snapshots are disabled.
 *
 * @sa getUpdateUniterval()
 */
int getSnapshotInterval() {
  return snapshot_interval;
}
