#include <pthread.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>

#define TASKS_PER_THREAD 4

int   tpool_init(void (*process_task)(int));
int   tpool_add_task(int newtask);
int   add_task_to_queue(int newtask);
int   handle_flag_and_cond();
int   check_if_full();
int   initialize_queue();
int   initialize_pthreads();
void* thread_loop();
int   dequeue();
void  print_queue_state();

typedef struct worker_queue_struct {
  int front;                 // the front index of the queue
  int back;                  // the back index of the queue
  int* queue_array;          // array of worker threads
  int size;                  // size of the worker thread array
  int flag;                  // flag to show active wokers
  pthread_mutex_t flag_mut;  // lock around the flag
  pthread_mutex_t queue_mut; // lock around the queue
  pthread_cond_t cond;       // condition used to signal threads
} worker_queue_struct;

typedef struct thread_pool_struct{
  int num_threads;             // number of threads in the thread array
  worker_queue_struct * queue; // the struct containing the queueu
  pthread_t* thread_array;     // the array of ready threads
  void (* job) (int);          // the function to be called by all threads
} thread_pool_struct; 

// the overarching thread pool
thread_pool_struct thread_pool; 

// the worker queue to be passed to the thread pool
worker_queue_struct worker_queue; 

int tpool_init(void (*process_task)(int)) {
  /* Initialize the thread pool and threads */

  // get the number of processors
  thread_pool.num_threads = (int)sysconf(_SC_NPROCESSORS_ONLN);

  // create the queue in the thread pool
  if (initialize_queue() <= 0) {
    perror("Could not initialize queue\n");
    return 0;
  }
  // pass in the function to be run by all threads
  thread_pool.job = process_task;

  // create the threads
  if (initialize_pthreads() <= 0) {
    perror("Could not initialize queue\n");
    return 0;
  }
  return 1;
}

int tpool_add_task(int newtask) {
  /* Add a new task to the queue of jobs */


#ifdef DEBUG
  printf("adding job %d\n", newtask);
#endif

  // lock the queue
  pthread_mutex_lock(&(thread_pool.queue->queue_mut));

  // check if there's room in the queue
  if (check_if_full() <= 0) {
    return 0;
  }

  // add the task to the queue and handle the queue's pointers
  if (add_task_to_queue(newtask) <= 0) {
    return 0;
  }

  // increment the flag and signal the condition to the thread
  if (handle_flag_and_cond() <= 0) {
    return 0;
  }

  return 1;
}

int handle_flag_and_cond() {
  // lock the flag mutex and increment the flag
  pthread_mutex_lock(&thread_pool.queue->flag_mut);
  thread_pool.queue->flag += 1;
  pthread_mutex_unlock(&thread_pool.queue->flag_mut);

  // signal the thread that it's ready
  pthread_cond_signal(&(thread_pool.queue->cond));
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
  print_queue_state();
  pthread_mutex_unlock(&(thread_pool.queue->queue_mut));
  return 1;
}

int check_if_full() {
  /* check if queue is full */

  int back  = thread_pool.queue->back;
  int front = thread_pool.queue->front;
  int size  = thread_pool.queue->size;

  if(((back + 1) % size) == front){
#ifdef DEBUG
    perror("Queue is full, could not add task\n");
#endif
    // make sure to unlock the queue before we return error
    pthread_mutex_unlock(&(thread_pool.queue->queue_mut));
    return 0;
  }
  return 1;
}

int initialize_queue() {
  /* Create the queue which will hold the jobs */

  // pass in the address of the global worker_queue 
  // variable to the thread_pool struct's queue
  thread_pool.queue = &worker_queue;

  // lock the mutexes and initialize conditions and flags
  if (pthread_mutex_init(&(thread_pool.queue->queue_mut), NULL) != 0) {
    perror("Could not initialize queue mutex\n");
    return 0;
  }
  if (pthread_mutex_init(&(thread_pool.queue->flag_mut), NULL) != 0) {
    perror("Could not initialize queue mutex\n");
    return 0;
  }
  if (pthread_cond_init(&thread_pool.queue->cond, NULL) != 0) {
    perror("Could not initialize queue mutex\n");
    return 0;
  }

  // initialize the queue values
  // (note: we use a queue sized 4x as large as the number 
  // of threads to avoid idle threads)
  thread_pool.queue->front = 0;
  thread_pool.queue->back  = 0;
  thread_pool.queue->size  = TASKS_PER_THREAD * thread_pool.num_threads;
  thread_pool.queue->flag  = 0;

  // malloc up some space for our queue
  thread_pool.queue->queue_array = (int*) malloc(thread_pool.queue->size * sizeof(int));
  return 1;
}

int initialize_pthreads() {
  /* Initialize the threads themselves */

  pthread_attr_t attr; // passed to pthread_create

  // initialize a new thread
  if(pthread_attr_init(&attr) != 0){
    perror("pthread init failed\n");
    return 0;
  }

  // set thread to start in detached mode
  if(pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED) != 0){
    perror("pthread detach state failed\n");
    return 0;
  }

  // malloc up some memory for the array of threads
  thread_pool.thread_array = (pthread_t *) malloc(thread_pool.num_threads * sizeof(pthread_t));

  for(int i = 0; i < thread_pool.num_threads; i++){
    // create the thread
    if(pthread_create(&thread_pool.thread_array[i], &attr, thread_loop, NULL) != 0){
      perror("pthread creation failed\n");
      return 0;
    }
  }
  return 1;
}

void* thread_loop() {
  // need to pass the job number to the job function
  int job_num;

  while (1) {
    pthread_mutex_lock(&thread_pool.queue->flag_mut);
     
    // wait for flag to increment if it hasn't been already
    while (thread_pool.queue->flag <= 0) {
      print_queue_state();
      pthread_cond_wait(&thread_pool.queue->cond, &thread_pool.queue->flag_mut);
    }

    // decrement flag and unlock its mutex
    thread_pool.queue->flag -= 1; 
    pthread_mutex_unlock(&thread_pool.queue->flag_mut);

    // dequeue the thread
    job_num = dequeue();
    if (job_num != 0) {
      thread_pool.job(job_num);
    }
  }
  return NULL;
}

int dequeue() {
  /* Remove a job from the queue */

  int front = thread_pool.queue->front;
  int queue_size  = thread_pool.queue->size;
  int popped_thread;

  // lock our queue
  pthread_mutex_lock(&(thread_pool.queue->queue_mut));

  // make sure something exists in the queue
  if(thread_pool.queue->front== thread_pool.queue->back){
    perror("Empty queue! Cannot dequeue\n");
    return 0;
  }

  // grab the thread at the top of the queueu
  popped_thread = thread_pool.queue->queue_array[front];

  // increment the front counter
  thread_pool.queue->front = (front+1) % queue_size;

  // unlock our queue
  pthread_mutex_unlock(&(thread_pool.queue->queue_mut));

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
