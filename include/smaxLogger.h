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
#define TIMESCALE               "3 days"

#define IDLE_STATE              "IDLE"    ///< systemd state to report when idle.

#define CACHE_SIZE              200000    ///< Maximum number of cached table ids.

#define CONNECT_RETRY_SECONDS   60        ///< Seconds between trying to reconnect to server
#define CONNECT_RETRY_ATTEMPTS  60        ///< Number of retry attempts before giving up....

#define ERROR_EXIT              (-1)

#define ERROR_RETURN            (-1)
#define SUCCESS_RETURN          0

#define MINUTE      60                ///< (s) seconds in a minute
#define HOUR        ( 60 * MINUTE )   ///< (s) seconds in an hour
#define DAY         ( 24 * HOUR )     ///< (s) seconds in a day
#define WEEK        ( 7 * DAY )       ///< (s) seconds in a week
#define YEAR        ( 366 * DAY )     ///< (s) seconds in a leap year (366 days)

#define DEFAULT_MAX_AGE     ( 90 * DAY )  ///< (s) Default value for the max age of variables to log
#define DEFAULT_MAX_SIZE    1024      ///< (bytes) Default maximum binary data size of variables to log

#ifndef TRUE
#  define TRUE              1
#endif

#ifndef FALSE
#  define FALSE             0
#endif


#define dprintf             if(debug) printf

extern boolean debug;             ///< whether to show debug messages

typedef struct {
  boolean force;                  /// Whether the variable should be logged no matter what other settings.
  boolean exclude;                /// Whether to exclude this variable from logging
  int sampling;                   /// sampling step for array data (sampling every n values only)
} logger_properties;

typedef struct Variable {
  char *id;                       ///< SMA-X variable id
  XField field;                   ///< SMA-X field data
  time_t updateTime;              ///< (s) UNIX time when variable was last updated in SMA-X
  time_t grabTime;                ///< (s) UNIX time when data was grabbed / or scheduled to be grabbed
  int sampling;                   ///< sampling step for array data (sampling every n values only)
  char *unit;                     ///< Physical unit name (if any)
  struct Variable *next;
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
