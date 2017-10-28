#include <stdio.h>
#include <stdlib.h>
#include "tpool.h"
#include <unistd.h>
#include <time.h>


void job(int task) {
  printf("job %d registered\n", task);
  sleep(2);
  printf("job %d complete\n", task);
}

int main() {
  // create a thread pool
  if (tpool_init(job) == 0) {
    fprintf(stderr, "Failed creating pool\n");
    exit(EXIT_FAILURE);
  }

  // create 50 tasks
  for (int i = 0; i < 50; i++) {

    if (tpool_add_task(i) <= 0) {
      fprintf(stderr, "Failed adding task to pool\n");
      sleep(1);
      i--;
    }
  }

  sleep(1);
}

