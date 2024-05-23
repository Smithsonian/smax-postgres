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

#include "smaxLogger.h"

boolean debug = FALSE;


static void *CleanupThread(void *arg) {
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


int main(int argc, char *argv[]) {
  char *configFile = NULL;
  char *owner = "postgres";
  char *ownerPasswd = NULL;
  boolean bootstrap = FALSE;
  int c;

  const struct poptOption options[] = {
          {"configfile",  'c', POPT_ARG_STRING, &configFile,   0, "Configuration file to use"},
          {"bootstap",    'b', POPT_ARG_NONE,   &bootstrap,    0, "Bootstraps a clean new database"},
          {"admin",       'a', POPT_ARG_STRING, &owner,        0, "Database admin/creator account for bootstrapping (default: 'postgres')"},
          {"password",    'p', POPT_ARG_STRING, &ownerPasswd,  0, "Database admin/creator password (along with -a option)"},
          {"debug",       'd', POPT_ARG_NONE,   &debug,        0, "Turn on console debug messages"},
          POPT_AUTOHELP
          {NULL}
  };

  poptContext optCon = poptGetContext("smax-logger", argc, argv, options, 0);

  while ((c = poptGetNextOpt(optCon)) != -1) poptBadOption(optCon, POPT_BADOPTION_NOALIAS);

  poptFreeContext(optCon);

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
