#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include "tpool.h"

void job(int task) {
  // a 1 second job to test the thread pool
  printf("job %d registered\n", task);
  sleep(1);
  printf("\t\t\tjob %d complete\n", task);
}

int main() {
  // create a thread pool
  if (tpool_init(job) == 0) {
    fprintf(stderr, "Failed creating pool\n");
    exit(EXIT_FAILURE);
  }

  // create 50 tasks
  for (int i = 1; i <= 50; i++) {
    if (tpool_add_task(i) <= 0) { printf("Error Adding Task\n");return 0; }
  }
  // wait for any errant tasks
  sleep(5);
}

