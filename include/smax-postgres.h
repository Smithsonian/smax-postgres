/**
 * @file
 *
 * @date Created  on Mar 16, 2022
 * @author Attila Kovacs
 *
 *  SMA-X to SQL database logger configuration and API.
 */

#ifndef SMAXLOGGER_H_
#define SMAXLOGGER_H_

#include "xchange.h"

#define DEFAULT_SQL_SERVER    "localhost"     ///< The default host name / IP of the SQL server
#define DEFAULT_SQL_DB        "engdb"         ///< The default SQL database name to log to
#define DEFAULT_SQL_USER      "loggerserver"  ///< The default SQL user name for the logger

/**
 * Chunk interval specification for hyper tables (Timescale DB)
 *
 * For best performance it should be set s.t. chunks fill about 25% of the available
 * memory.
 *
 * For ~10k variables logged once per minute (assuming an average 8 bytes per variable
 * plus table timestamp etc overheads), this may lead to ~0.5 GB per day.
 *
 * The default is '3 days'
 *
 */

/// Default configuration file name
#define SMAXPQ_DEFAULT_CONFIG   "/etc/smax-postgress.cfg"

#define TIMESCALE               "3 days"  ///< Default TimescaleDB timescale

#define IDLE_STATE              "IDLE"    ///< systemd state to report when idle.

#define CACHE_SIZE              200000    ///< Maximum number of cached table ids.

#define CONNECT_RETRY_SECONDS   60        ///< Seconds between trying to reconnect to server
#define CONNECT_RETRY_ATTEMPTS  60        ///< Number of retry attempts before giving up....

#define ERROR_EXIT              (-1)      ///< Exit code in case of an error

#define ERROR_RETURN            (-1)      ///< Function return value in case of an error
#define SUCCESS_RETURN          0         ///< Function return value for successful completion

#define MINUTE      60                ///< (s) seconds in a minute
#define HOUR        ( 60 * MINUTE )   ///< (s) seconds in an hour
#define DAY         ( 24 * HOUR )     ///< (s) seconds in a day
#define WEEK        ( 7 * DAY )       ///< (s) seconds in a week
#define YEAR        ( 366 * DAY )     ///< (s) seconds in a leap year (366 days)

#define DEFAULT_MAX_AGE     ( 90 * DAY )  ///< (s) Default value for the max age of variables to log
#define DEFAULT_MAX_SIZE    1024      ///< (bytes) Default maximum binary data size of variables to log

#ifndef TRUE
#  define TRUE              1         ///< Boolean TRUE (1) if not already defined
#endif

#ifndef FALSE
#  define FALSE             0         ///< Boolean FALSE (0) if not already defined
#endif

/// Macro for printing debug messages
#define dprintf             if(debug) printf

/// API major version
#define SMAXPQ_MAJOR_VERSION  0

/// API minor version
#define SMAXPQ_MINOR_VERSION  9

/// Integer sub version of the release
#define SMAXPQ_PATCHLEVEL     0

/// Additional release information in version, e.g. "-1", or "-rc1".
#define SMAXPQ_RELEASE_STRING "-devel"

/// \cond PRIVATE

#ifdef str_2
#  undef str_2
#endif

/// Stringify level 2 macro
#define str_2(s) str_1(s)

#ifdef str_1
#  undef str_1
#endif

/// Stringify level 1 macro
#define str_1(s) #s

/// \endcond

/// The version string for this library
/// \hideinitializer
#define SMAXPQ_VERSION_STRING str_2(SMAXPQ_MAJOR_VERSION) "." str_2(SMAXPQ_MINOR_VERSION) \
                                  "." str_2(SMAXPQ_PATCHLEVEL) SMAXPQ_RELEASE_STRING

extern boolean debug;             ///< whether to show debug messages

/**
 * A set of properties that determine how an SMA-X variable is logged into the PostgreSQL DB.
 */
typedef struct {
  boolean force;                  ///< Whether the variable should be logged no matter what other settings.
  boolean exclude;                ///< Whether to exclude this variable from logging
  int sampling;                   ///< sampling step for array data (sampling every n values only)
} logger_properties;

/**
 * Data for an SMA-X variable that is to be inserted into the PostgreSQL database
 */
typedef struct Variable {
  char *id;                       ///< SMA-X variable id
  XField field;                   ///< SMA-X field data
  time_t updateTime;              ///< (s) UNIX time when variable was last updated in SMA-X
  time_t grabTime;                ///< (s) UNIX time when data was grabbed / or scheduled to be grabbed
  int sampling;                   ///< sampling step for array data (sampling every n values only)
  char *unit;                     ///< Physical unit name (if any)
  struct Variable *next;          ///< Pointer to the next Variable in the linked lisr, or NULL if no more
} Variable;



int initCollector();
int setupDB(const char *name, const char *passwd);
void destroyVariable(Variable *u);
void *SQLThread();

int insertQueue(Variable *u);

int parseConfig(const char *filename);
int isLogging(const char *id, double updateTime);
int getSampleCount(const Variable *u);

const char *getSMAXServerAddress();
int setSMAXServerAddress(const char *addr);

const char *getSQLServerAddress();
int setSQLServerAddress(const char *addr);
const char *getSQLDatabaseName();
int setSQLDatabaseName(const char *name);
const char *getSQLUserName();
int setSQLUserName(const char *name);
const char *getSQLAuth();
int setSQLAuth(const char *passwd);

boolean isUseHyperTables();
void setUseHyperTables(boolean value);

int getUpdateInterval();
int getSnapshotInterval();
int getMaxLogSize();

logger_properties *getLogProperties(const char *id);

int deleteVars(const char *pattern);

#if USE_SYSTEMD
void setSDState(const char *s);
#endif

#endif /* SMAXLOGGER_H_ */
