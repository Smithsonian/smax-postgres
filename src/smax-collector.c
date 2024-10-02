/**
 * @file
 *
 * @date Created  on Mar 16, 2022
 * @author Attila Kovacs
 *
 *  smaxLogger connector module that mines SMA-X for inserting select variables into a
 *  time-series database at regular intervals.
 */

/// For clock_gettime()
#define _POSIX_C_SOURCE 200809

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <errno.h>
#include <pthread.h>

#if USE_SYSTEMD
#  include <systemd/sd-daemon.h>
#endif

#include "smax-postgres.h"
#include "redisx.h"
#include "smax.h"

#ifndef SYSERR_RTN
#  define SYSERR_RTN            0x40        ///< Exit code indicating not to restart in case of error
#endif

#define CONNECT_RETRY_INTERVAL  3           ///< (sec) Time between reconnection attempts when initializing.
#define CONNECT_ATTEMPTS        20          ///< Total number of reconnection attempts when initializing.
#define REDIS_SCAN_COUNT        100         ///< Work-load count to use for Redis SCAN-type commands.

#define UPDATE_TIMEOUT          10000       ///< (ms) Timeout value for gathering queued SMA-X variables

/**
 * An SMA-X variable update link, including metadata (for timestamp information).
 */
typedef struct Update {
  Variable *var;        ///< The variable data that will be queued for inserting into engdb
  XMeta meta;           ///< container in which to pull SMA-X metadata for this variable.
  struct Update *next;  ///< Link to the next update in a list.
} Update;


/**
 * A group of SMA-X variables handled by this connector
 */
typedef struct {
  char *pattern;        ///< The keyword pattern of the Redis hash tables containing data for this group
  double lastUpdate;    ///< (s) Server timestamp when data was last grabbed for this group.
} VarGroup;


static VarGroup allVars = { "*", 0.0 };            ///< All SMA-X variables

static VarGroup *varGroups[] = { &allVars, NULL };

static pthread_t grabberPID;

static void *GrabberThread(void *arg);

/**
 * Initializes the SMA-X collector. It connects to SMA-X and starts a grabber
 * thread in the background, which will be pushing data to the time-series database at regular intervals.
 *
 * @return      X_SUCCESS (0) if successful or else an error code (<0).
 */
int initCollector() {
  static boolean warned;

  int i, status;

  //xDebug = TRUE;
  //smaxSetVerbose(TRUE);
  smaxSetPipelined(TRUE);
  smaxSetResilient(TRUE);
  smaxSetResilientExit(FALSE);

  status = smaxConnect();

  // Keep trying to connect to SMA-X for up to 1 minute before giving up...
  for(i=0; i < CONNECT_ATTEMPTS && status == X_NO_SERVICE; i++) {
    status = smaxConnect(getSMAXServerAddress());
    if(status == X_NO_SERVICE) {
      if(!warned) {
        fprintf(stderr, "Failed to connect to SMA-X. Will keep trying silently...\n");
        warned = TRUE;
      }
      sleep(CONNECT_RETRY_INTERVAL);
    }
  }

  if(status) return ERROR_RETURN;

  if(warned) fprintf(stderr, "INFO! Connected to SMA-X.\n");
  dprintf("initSMAX(): Connected to SMA-X.\n");

  if(pthread_create(&grabberPID, NULL, GrabberThread, NULL) < 0) {
    perror("ERROR! initSMAXGrabber()");
    return ERROR_RETURN;
  }

  return SUCCESS_RETURN;
}


/**
 * Destroys a variable, freeing up the memory it occupies.
 *
 * \param u     Pointer to the variable.
 */
void destroyVariable(Variable *u) {
  XField *f;

  if(!u) return;

  f = &u->field;
  if(f->name) free(f->name);
  if(f->value) free(f->value);
  if(u->id) free(u->id);

  free(u);
}


/**
 * Returns the decimal time between to precision timestamps.
 *
 * @param start     starting timestamp
 * @param end       ending timestamp
 * @return          (s) Time between the two timestamps.
 */
static double GetDiffTime(const struct timespec *start, const struct timespec *end) {
  if(!start || !end) {
    errno = EINVAL;
    return NAN;
  }

  return (end->tv_sec - start->tv_sec) + 1e-9 * (end->tv_nsec - start->tv_nsec);
}


/**
 * Destroys an update structure, freeing up all its resources.
 *
 * @param u     Pointer to the update structure to destroy.
 */
static void DestroyUpdate(Update *u) {
  if(!u) return;
  if(u->var) destroyVariable(u->var);
  free(u);
  return;
}


/**
 * Destroys an array of redis entries, freeing up all resources used by it.
 *
 * @param entries       Pointer to the entries array
 * @param n             The number of elements in the array
 */
static void DestroyEntries(RedisEntry *entries, int n) {
  if(!entries) return;

  while(--n >= 0) {
    RedisEntry *e = &entries[n];
    if(e->key) free(e->key);
    if(e->value) free(e->value);
  }
  free(entries);
}


/**
 * Submits an individual variable for inserting into the time-series database.
 *
 * @param u     The variable update.
 * @return      TRUE (1) if successfully queued a DB update for the variable, or else FALSE (0; errno may be
 *              set to indicate the type of error -- if any).
 */
static boolean SubmitUpdate(Update *u) {
  const XMeta *m;
  const logger_properties *p;
  Variable *v;
  XField *f;
  boolean force = FALSE;

  if(!u || !u->var) {
    errno = EINVAL;
    return FALSE;
  }

  v = u->var;
  f = &v->field;

  if(!f->value) {
    errno = EINVAL;
    return FALSE;
  }

  m = &u->meta;
  if(m->storeType == X_STRUCT) return FALSE;

  p = getLogProperties(v->id);
  if(p) {
    v->sampling = p->sampling;
    force = p->force;
  }

  f->isSerialized = TRUE;
  f->type = m->storeType;
  f->ndim = m->storeDim;
  memcpy(f->sizes, m->storeSizes, sizeof(f->sizes));

  // Write raw types as string
  if(f->type == X_RAW) {
    f->type = X_STRING;
    f->ndim = 1;
    f->sizes[0] = 1;
  }

# if FIX_PYSMAX_STRING_DIM
  if(f->type == X_STRING && f->ndim <= 1 && f->sizes[0] == strlen(f->value)) {
    dprintf("!FIX! %s: string count %d -> 1\n", v->id, f->sizes[0]);
    f->ndim = 1;
    f->sizes[0] = 1;
  }
# endif

  if(!force) {
    if(getSampleCount(v) * xElementSizeOf(v->field.type) > getMaxLogSize()) return FALSE;
  }

  dprintf("UPDATE %s: force %d, sampling = %d, size = %d, time = %ld\n", v->id, force, p->sampling, getSampleCount(v) * xElementSizeOf(v->field.type), v->grabTime);

  // Convert from serialized to binary
  smax2xField(f);

  insertQueue(u->var);

  // De-reference from update structure, so we don't destroy.
  u->var = NULL;

  return TRUE;
}


/**
 * Sumbits a list of variables for insertion into the time-series database. The variables are placed on a queue
 * and will be pushed to the time-series database as soon as possible.
 *
 * @param list          The list of variables to update
 * @param grabTime      Time when data was grabbed (or queued).
 * @return  the number of variables submitted.
 */
static int SubmitList(Update *list, const time_t grabTime) {
  Update *u;
  int n = 0;

  if(!list) {
    errno = EINVAL;
    return -1;
  }

  for(u = list; u != NULL; u = u->next) {
    Variable *v = u->var;

    v->grabTime = grabTime;
    v->updateTime = u->meta.timestamp.tv_sec;

    if(SubmitUpdate(u)) n++;
  }

  dprintf("Submitted updates for %d variables.\n", n);

  return n;
}

static void DestroyList(Update *list) {
  Update *u;

  for(u = list; u != NULL; ) {
    Update *next = u->next;
    DestroyUpdate(u);
    u = next;
  }
}


/**
 * Queues an SMA-X variable for an asynchronous time-series database update
 *
 * @param id            The full ID of the SMA-X variable.
 * @param units         Alphabetically sorted table of physical units, or NULL.
 * @param nu            Number of physical units in table.
 * @return              X_SUCCESS (0) if the variable is a valid leaf variable (not structure!)
 *                      and was queued for update, or else an error code (<0).
 */
static Update *QueueForUpdate(const char *id, const RedisEntry *units, int nu, const time_t grabTime) {
  char *table, *key;
  int status = X_SUCCESS;
  Update *u;
  Variable *v;

  if(!id) {
    errno = EINVAL;
    return NULL;
  }

  u = (Update *) calloc(1, sizeof(*u));
  if(!u) {
    // Out of memory
    perror("QueueForUpdate(): alloc update");
    exit(ERROR_EXIT);
  }

  u->var = (Variable *) calloc(1, sizeof(*u->var));
  if(!u->var) {
    // Out of memory
    perror("QueueForUpdate(): alloc variable");
    exit(ERROR_EXIT);
  }

  v = u->var;
  v->id = strdup(id);
  v->grabTime = grabTime;

  if(units) {
    RedisEntry *unit = bsearch(id, units, nu, sizeof(RedisEntry), (__compar_fn_t) strcmp);
    if(unit) {
      // Move reference to variable.
      v->unit = unit->value;
      unit->value = NULL;
    }
  }

  table = strdup(id);

  if(xSplitID(table, &key) != X_SUCCESS) {
    fprintf(stderr, "WARNING! not a table:key id: %s\n", v->id);
    status = X_NAME_INVALID;
  }

  if(!status) status = smaxQueue(table, key, X_RAW, 1, &v->field.value, &u->meta);
  free(table);

  if(status) {
    dprintf("! SMA-X: smaxQueue(): %s\n", smaxErrorDescription(status));
    DestroyUpdate(u);
    return NULL;
  }

  return u;
}


/**
 * Updates the time-series data only for the select variables that have changed since (and including) the
 * specified cutoff time.
 *
 * @param pattern       the SMA-X table name pattern of the group of variables to check
 * @param from          (s) Decimal SMA-X server time after which to select updates.
 * @return              X_SUCCESS (0) if successful, or else an error code (<0)
 */
static int UpdateChanged(const char *pattern, double from, const time_t grabTime) {
  int i, n, nu = 0, status;
  RedisEntry *entries;
  RedisEntry *units;
  struct timespec start, end;
  Update *list = NULL;
  XSyncPoint *s;

  if(!pattern) {
    errno = EINVAL;
    return -1;
  }

  clock_gettime(CLOCK_REALTIME, &start);

  dprintf("Checking for changes in '%s' (dt = %.3f s)\n", pattern, from - (start.tv_sec + 1e-9 * start.tv_nsec));

  entries = redisxScanTable(smaxGetRedis(), "<timestamps>", pattern, &n, &status);
  if(status) {
    dprintf("! SMA-X: redisxScanTable(): %s\n", smaxErrorDescription(status));
    DestroyEntries(entries, n);
    return status;
  }

  if(n == 0) {
    dprintf("! No matching timestamps for '%s'\n", pattern);
    if(entries) free(entries);
    return 0;
  }

  units = redisxScanTable(smaxGetRedis(), "<units>", pattern, &nu, &status);

  dprintf("Got %d timestamps for '%s' to check...\n", n, pattern);

  for(i=0; i<n; i++) {
    const RedisEntry *e = &entries[i];
    if(*e->key == '_') continue;
    if(*e->key == '<') continue;

    char *tail;
    double t = strtod(e->value, &tail);
    if(tail == e->value || errno == ERANGE) {
      dprintf("! Bad timestamp: [%s]\n", e->value);
      continue;
    }

    if(t >= from) if(isLogging(e->key, t)) {
      Update *u = QueueForUpdate(e->key, units, nu, grabTime);
      if(!u) continue;

      u->next = list;
      list = u;
    }
  }

  DestroyEntries(entries, n);
  DestroyEntries(units, nu);

  if(!list) {
    dprintf("! No changes found.\n");
    return 0;
  }

  s = smaxCreateSyncPoint();
  if(!s) {
    perror("UpdateChanged(): create sync point.\n");
    exit(ERROR_EXIT);
  }

  status = smaxSync(s, UPDATE_TIMEOUT);

  if(!status) n = SubmitList(list, grabTime);
  else dprintf("! SMA-X: smaxSync() failed: %s\n", smaxErrorDescription(status));

  smaxDestroySyncPoint(s);
  DestroyList(list);

  clock_gettime(CLOCK_REALTIME, &end);
  printf(" -- Update for '%s' (%d): %.3f seconds (%s)\n", pattern, n, GetDiffTime(&start, &end), smaxErrorDescription(status));

  return status;
}


/**
 * Snapshorts all SMA-X variables with the matching keyword pattern
 *
 * @param pattern       the Redis keyword pattern selecting what data is mined from SMA-X
 * @param grabTime      (s) UNIX time when data was to be grabbed.
 * @param useGrabTime   Whether to use the grabbing time as the timestamp.
 * @return              X_SUCCESS (0) if the update was successful, or else an appropriate error (<0)
 */
static int Snapshot(const char *pattern, time_t grabTime) {
  int status;

  if(!pattern) {
    errno = EINVAL;
    return -1;
  }

# if USE_SYSTEMD
  setSDState("SNAPSHOT");
# endif

  status = UpdateChanged(pattern, 0.0, grabTime);

# if USE_SYSTEMD
  setSDState(IDLE_STATE);
# endif

  return status;
}


/**
 * Sleeps to the next round multiple of the number of seconds specified.
 *
 * @param seconds       (s) The maximum sleep time
 * @return              the integer UNIX time of the targeted wakeup.
 */
static time_t SleepToRound(int seconds) {
  struct timespec now, dt, rem = {0};
  time_t target;

  clock_gettime(CLOCK_REALTIME, &now);

  dt.tv_sec = seconds - (now.tv_sec % seconds);
  if(now.tv_nsec) {
    dt.tv_sec--;
    dt.tv_nsec = 1000000000 - now.tv_nsec;
  }
  else dt.tv_nsec = 0;

  target = now.tv_sec + dt.tv_sec;

  dprintf("sleeping for %ld.%09ld s\n", dt.tv_sec, dt.tv_nsec);

  while(nanosleep(&dt, &rem)) {
    perror("WARNING! nanosleep");
    if(errno != EINTR) exit(SYSERR_RTN);
    dt = rem;
  }

  return target;
}


/**
 * Returns the SMA-X server time as decimal seconds since 1970.
 *
 * @return  (s) The decimal SMA-X server time (UNIX time), as queried by TIME on Redis
 */
static double GetServerTime() {
  struct timespec ts;
  double t;

  smaxGetServerTime(&ts);
  t = ts.tv_sec + 1e-9 * ts.tv_nsec;

  dprintf("SMA-X Time: %.6f\n", t);
  return t;
}


/**
 * Grabs data from a group of SMA-X variables and queues them for insertion into the time-series database.
 *
 * @param group             The variable group to update
 * @param grabTime          (s) UNIX time when data was to be grabbed.
 * @param isSnapshot        Whether we want a complete snapshot. If FALSE, the function may still do a snapshot
 *                          if there has not been a successful snapshot taken already. Otherwise it will just
 *                          perform an incremental update of the variable that have changed since last time.
 * @return X_SUCCESS (0) or else en error code (<0)
 */
static int Grab(VarGroup *group, const time_t grabTime, boolean isSnapshot) {
  double t = GetServerTime();
  int status;

  if(!group) {
    errno = EINVAL;
    return -1;
  }

  if(group->lastUpdate <= 0 || isSnapshot) {
    status = Snapshot(group->pattern, grabTime);
    if(status) printf("WARNING! Snapshot for '%s' failed: %s\n", group->pattern, smaxErrorDescription(status));
  }
  else {
# if USE_SYSTEMD
  setSDState("UPDATE");
# endif

  status = UpdateChanged(group->pattern, group->lastUpdate, grabTime);

# if USE_SYSTEMD
  setSDState(IDLE_STATE);
# endif

    if(status) printf("WARNING! Incremental update for '%s' failed: %s\n", group->pattern, smaxErrorDescription(status));
  }

  if(!status) group->lastUpdate = t;

  return status;
}


/**
 * Service thead that continuously collects data from SMA-X and queues them for insertion ino the time-series database.
 * It does a differential update every UPDATE_INTERVAL or a full snapshot every SNAPSHOT_INTERVAL.
 *
 * @param arg       Unused.
 */
static void *GrabberThread(void *arg) {
  (void) arg; // unused

  pthread_detach(pthread_self());

  if(REDIS_SCAN_COUNT > 0) redisxSetScanCount(smaxGetRedis(), REDIS_SCAN_COUNT);

  for(;;) {
    time_t target = SleepToRound(getUpdateInterval());
    boolean isSnapshot = FALSE;
    int i;

    if(getSnapshotInterval() > 0) isSnapshot = (target % getSnapshotInterval() < getUpdateInterval());

    for(i=0; varGroups[i] != NULL; i++) Grab(varGroups[i], target, isSnapshot);
  }

  return NULL; // NOT REACHED
}



