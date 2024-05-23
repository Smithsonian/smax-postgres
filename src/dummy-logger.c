/**
 * @file
 *
 * @date Created  on Mar 17, 2022
 * @author Attila Kovacs
 *
 *  Dummy logger program for testing smaxLogger collector modules. This is for testing/debugging only.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>

#include "smaxLogger.h"

int main(int argc, const char *argv[]) {
  if(initSMAXGrabber() != SUCCESS_RETURN) {
    fprintf(stderr, "ERROR! Could not start SMA-X Collector. Exiting.\n");
    exit(ERROR_EXIT);
  }
  for(;;) pause();
  // NOT REACHED
}


void insertQueue(Variable *u) {
  printf(" + %s\n", u->id);
  destroyVariable(u);
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

  free(u);
}


