#ifndef SM_TPOOL
#define SM_TPOOL

extern int tpool_init(void (*process_task)(int));

extern int tpool_add_task(int newtask);

#endif
