/**
 * @file
 *
 * @date Created  on Nov 5, 2023
 * @author Attila Kovacs
 *
 *  Logging of SMA-X variables to a historical database, such as an SQL database. Cofiguration
 *  of the logger is possible via a designated configuration file, which can be specified at
 *  runtime via the -c option.
 */

#include <stdio.h>
#include <stdlib.h>
#include <popt.h>
#include <pthread.h>
#include <signal.h>
#include <unistd.h>

#if USE_SYSTEMD
#  include <systemd/sd-daemon.h>
#endif

#include "smax-postgres.h"

boolean debug = FALSE;


static void *CleanupThread(void *arg) {
  (void) arg; // unused
  fprintf(stderr, "Exiting.\n");
  exit(1);
}

static void SignalHandler(int signum) {
  pthread_t tid;
  fprintf(stderr, "Caught signal %d.\n", signum);
  if(pthread_create(&tid, NULL, CleanupThread, NULL) < 0) {
    perror("ERROR! could not launch cleanup thread");
    fprintf(stderr, "Forced exiting.\n");
    _exit(1);
  }
}

#if USE_SYSTEMD

static void exit_notify() {
  sd_notify(0, "STOPPING=1");
}

void setSDState(const char *s) {
  static char sd_state[128];
  snprintf(sd_state, sizeof(sd_state), "STATUS=%s", s);
  sd_notify(0, sd_state);
}

#endif

/**
 * Main entry point of the `smax-postgress` SMA-X to PostgreSQL / TimescaleDB connector application
 *
 * @param argc    Number of command-line arguments passed
 * @param argv    An array of command-line argument strings
 * @return        0 id the program exited normally, or else a non-zero exit code.
 */
int main(int argc, const char *argv[]) {
  char *configFile = SMAXPQ_DEFAULT_CONFIG;
  char *owner = "postgres";
  char *ownerPasswd = NULL;
  boolean bootstrap = FALSE, version = FALSE;
  int c;

  const struct poptOption options[] = {
          {"configfile",  'c', POPT_ARG_STRING, &configFile,   0, "Configuration file to use", NULL},
          {"bootstap",    'b', POPT_ARG_NONE,   &bootstrap,    0, "Bootstraps a clean new database", NULL},
          {"admin",       'a', POPT_ARG_STRING, &owner,        0, "Database admin/creator account for bootstrapping (default: 'postgres')", NULL},
          {"password",    'p', POPT_ARG_STRING, &ownerPasswd,  0, "Database admin/creator password (along with -a option)", NULL},
          {"debug",       'd', POPT_ARG_NONE,   &debug,        0, "Turn on console debug messages", NULL},
          {"version",     'v', POPT_ARG_NONE,   &version,      0, "Print version info only", NULL},
          POPT_AUTOHELP
          {NULL}
  };

  poptContext optCon = poptGetContext("smax-postgres", argc, argv, options, 0);

  while ((c = poptGetNextOpt(optCon)) != -1) poptBadOption(optCon, POPT_BADOPTION_NOALIAS);

  poptFreeContext(optCon);

  if(version) {
    printf("smax-postgress: SMA-X to PostgreSQL / TimescaleDB logger version " SMAXPQ_VERSION_STRING "\n");
    return 0;
  }


# if USE_SYSTEMD
  setSDState("INITIALIZE");
  atexit(exit_notify);
# endif

  if(initCollector() != SUCCESS_RETURN) {
    fprintf(stderr, "ERROR! Could not start SMA-X Collector. Exiting\n");
    exit(ERROR_EXIT);
  }

  if(configFile) if(parseConfig(configFile) != 0) return 1;

  signal(SIGINT, SignalHandler);
  signal(SIGTERM, SignalHandler);
  signal(SIGQUIT, SignalHandler);

  if(bootstrap) if(setupDB(owner, ownerPasswd) != SUCCESS_RETURN) {
    fprintf(stderr, "ERROR! Bootstrapping database. Exiting.\n");
    exit(ERROR_EXIT);
  }

  SQLThread();

  // NOT REACHED
  return 1;
}
