/* Sam Messina
 * Thread Pool 
 * CS 407 - Lab 4
 */

#include <pthread.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <time.h>

// 4 tasks per thread to allow tasks to queue up, 
// avoiding idle threads
#define TASKS_PER_THREAD 4

/* MAIN FUNCTIONS */
int   tpool_init(void (*process_task)(int));
int   tpool_add_task(int newtask);

/* INTERNAL FUNCTIONS */
int   add_task_to_queue(int newtask);
int   handle_flag_and_cond();
int   check_if_full();
int   initialize_queue();
int   initialize_pthreads();
void* thread_loop();
int   dequeue();

/* DEBUG FUNCTIONS */
void  print_queue_state();

typedef struct worker_queue_struct {
  /* The overarching thread pool */

  int front;                   // the front index of the queue
  int back;                    // the back index of the queue
  int* queue_array;            // array of worker threads
  int size;                    // size of the worker thread array
  pthread_mutex_t queue_m;     // lock around the queue itself 

  int q_full_flag;             // flag to show queue is full
  pthread_mutex_t q_flag_m;    // lock around the q_full_flag
  pthread_cond_t q_cond;       // signals queue has space

  int proc_rdy_flag;           // flag to show task is ready
  pthread_mutex_t proc_flag_m; // lock around the proc_rdy_flag
  pthread_cond_t task_cond;    // signals task is ready
} worker_queue_struct;

typedef struct thread_pool_struct{
  /* The worker queue to be passed to the thread pool */

  int num_threads;             // number of threads in the thread array
  worker_queue_struct * queue; // the struct containing the queueu
  pthread_t* thread_array;     // the array of ready threads
  void (* job) (int);          // the function to be called by all threads
} thread_pool_struct; 

// instantiate our thread pool and queue
thread_pool_struct  thread_pool; 
worker_queue_struct worker_queue; 

int tpool_init(void (*process_task)(int)) {
  /* Initialize the thread pool and threads */

  // the number of processors will correspond to 
  // our number of threads
  thread_pool.num_threads = (int)sysconf(_SC_NPROCESSORS_ONLN);

  if (initialize_queue() <= 0) {
    perror("Could not initialize queue\n");
    return 0;
  }

  // pass in the function to be run by all threads
  thread_pool.job = process_task;

  if (initialize_pthreads() <= 0) {
    perror("Could not initialize threads\n");
    return 0;
  }

  return 1;
}

int tpool_add_task(int newtask) {
  /* Add a new task to the queue of jobs */

  #ifdef DEBUG
  printf("adding job %d\n", newtask);
  #endif

  pthread_mutex_lock(&(thread_pool.queue->queue_m));

  // check if there's room in the queue and
  // add the task to the queue and handle the queue's pointers
  if ((check_if_full() <= 0) ||
      (add_task_to_queue(newtask) <= 0)) { 
    // make sure to unlock the queue before we return error
    pthread_mutex_unlock(&(thread_pool.queue->queue_m));
    return 0; }

  pthread_mutex_unlock(&(thread_pool.queue->queue_m));

  // increment the flag and signal the condition to the thread
  if (handle_flag_and_cond() <= 0) { return 0; }

  return 1;
}

int handle_flag_and_cond() {
  /* Increment flag and signal condition */

  pthread_mutex_lock(&thread_pool.queue->proc_flag_m);
  thread_pool.queue->proc_rdy_flag += 1;
  pthread_mutex_unlock(&thread_pool.queue->proc_flag_m);

  // signal the thread that it's ready
  pthread_cond_signal(&(thread_pool.queue->task_cond));
  #ifdef DEBUG
  printf("thread signaled\n");
  #endif
  return 1;
}

int add_task_to_queue(int newtask) {
  /* Add the task to the queue and increment the back pointer */

  int back  = thread_pool.queue->back;
  int size  = thread_pool.queue->size;

  thread_pool.queue->queue_array[back] = newtask;
  back = (back + 1) % size;
  thread_pool.queue->back = back;

  if (((back + 1) % size) == thread_pool.queue->front) {
    #ifdef DEBUG
    printf("inc q flag\n");
    #endif
    pthread_mutex_lock(&thread_pool.queue->q_flag_m);
    thread_pool.queue->q_full_flag += 1;
    pthread_mutex_unlock(&thread_pool.queue->q_flag_m);
  }

  print_queue_state();
  return 1;
}

int check_if_full() {
  /* Check if queue is full */

  pthread_mutex_lock(&thread_pool.queue->q_flag_m);

  // check if the queue has been flagged as full
  while (thread_pool.queue->q_full_flag > 0 ){
    // if it's full, unlock the queue and wait for a dequeue operation
    pthread_mutex_unlock(&(thread_pool.queue->queue_m));
    pthread_cond_wait(&thread_pool.queue->q_cond, &thread_pool.queue->q_flag_m);

    // relock the queue to move forward properly in tpool_add_task()
    pthread_mutex_lock(&(thread_pool.queue->queue_m));
  }

  pthread_mutex_unlock(&thread_pool.queue->q_flag_m);
  return 1;
}

int initialize_queue() {
  /* Create the queue which will hold the jobs */

  // pass in the address of the global worker_queue 
  // variable to the thread_pool struct's queue
  thread_pool.queue = &worker_queue;

  // lock the mutexes and initialize conditions and flags
  if (pthread_mutex_init(&(thread_pool.queue->queue_m), NULL) != 0) {
    perror("Could not initialize queue mutex\n");
    return 0; }
  if (pthread_mutex_init(&(thread_pool.queue->proc_flag_m), NULL) != 0) {
    perror("Could not initialize queue mutex\n");
    return 0; }
  if (pthread_cond_init(&thread_pool.queue->task_cond, NULL) != 0) {
    perror("Could not initialize queue mutex\n");
    return 0; }

  if (pthread_mutex_init(&(thread_pool.queue->q_flag_m), NULL) != 0) {
    perror("Could not initialize queue mutex\n");
    return 0; }
  if (pthread_cond_init(&thread_pool.queue->q_cond, NULL) != 0) {
    perror("Could not initialize queue mutex\n");
    return 0; }

  thread_pool.queue->front         = 0;
  thread_pool.queue->back          = 0;
  thread_pool.queue->proc_rdy_flag = 0;
  thread_pool.queue->q_full_flag   = 0;

  // We use a queue sized 4x as large as the number 
  // of threads to avoid idle threads
  thread_pool.queue->size  = TASKS_PER_THREAD * thread_pool.num_threads;

  // malloc up some space for the queue
  thread_pool.queue->queue_array = (int*) malloc(thread_pool.queue->size * sizeof(int));
  return 1;
}

int initialize_pthreads() {
  /* Initialize the threads themselves */

  pthread_attr_t attr; // passed to pthread_create

  if(pthread_attr_init(&attr) != 0){
    perror("pthread init failed\n");
    return 0;
  }

  if(pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED) != 0){
    perror("pthread detach state failed\n");
    return 0;
  }

  thread_pool.thread_array = (pthread_t *) malloc(thread_pool.num_threads * sizeof(pthread_t));

  for(int i = 0; i < thread_pool.num_threads; i++){
    if(pthread_create(&thread_pool.thread_array[i], &attr, thread_loop, NULL) != 0){
      perror("pthread creation failed\n");
      return 0;
    }
  }
  return 1;
}

void* thread_loop() {
  /* Infinite loop to pop off ready jobs and assign them a thread */

  // Pass the job number to the job function.
  // This will correspond to FDs in our implementation.
  int job_num;

  while (1) {
    pthread_mutex_lock(&thread_pool.queue->proc_flag_m);

    while (thread_pool.queue->proc_rdy_flag <= 0) {
      print_queue_state();
      pthread_cond_wait(&thread_pool.queue->task_cond, &thread_pool.queue->proc_flag_m);
    }

    thread_pool.queue->proc_rdy_flag -= 1; 
    pthread_mutex_unlock(&thread_pool.queue->proc_flag_m);

    job_num = dequeue();
    if (job_num != 0) {
      thread_pool.job(job_num);
    }
  }
  return NULL;
}

int dequeue() {
  /* Remove a job from the queue */

  int front       = thread_pool.queue->front;
  int queue_size  = thread_pool.queue->size;
  int popped_thread;

  pthread_mutex_lock(&(thread_pool.queue->queue_m));

  // make sure something exists in the queue
  if(thread_pool.queue->front == thread_pool.queue->back){
    #ifdef DEBUG
    perror("Empty queue! Cannot dequeue\n");
    #endif
    return 0;
  }

  // grab the thread at the top of the queue
  popped_thread = thread_pool.queue->queue_array[front];

  thread_pool.queue->front = (front+1) % queue_size;
  pthread_mutex_unlock(&(thread_pool.queue->queue_m));

  pthread_mutex_lock(&thread_pool.queue->q_flag_m);
  if (thread_pool.queue->q_full_flag > 0 ){
    #ifdef DEBUG
    printf("dec q full flag\n");
    #endif
    thread_pool.queue->q_full_flag -= 1;
    pthread_cond_signal(&(thread_pool.queue->q_cond));
  }
  pthread_mutex_unlock(&thread_pool.queue->q_flag_m);

  // return the thread num we popped off
  return popped_thread;
}

void print_queue_state() {
  /* Print some debug statements about the queue */
  #ifdef DEBUG
  printf("front: %d\n", thread_pool.queue->front);
  printf("back:  %d\n",  thread_pool.queue->back);
  printf("queue: ");
  for(int i = 0; i < thread_pool.queue->size; i++)
    printf(" %d, ", thread_pool.queue->queue_array[i]);
  printf("\n");
  #endif
}
