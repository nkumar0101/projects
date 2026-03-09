#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include "threads/thread.h"
#include <stdint.h>

// At most 8MB can be allocated to the stack
// These defines will be used in Project 2: Multithreading
#define MAX_STACK_PAGES (1 << 11)
#define MAX_THREADS 127

/* PIDs and TIDs are the same type. PID should be
   the TID of the main thread of the process */
typedef tid_t pid_t;

/* Thread functions (Project 2: Multithreading) */
typedef void (*pthread_fun)(void*);
typedef void (*stub_fun)(pthread_fun, void*);

/* File stuff */
typedef int file_descriptor;

typedef struct file_elem {
  struct list_elem elem; // prev and next pointers
  file_descriptor fd;    // file descriptor of this element
  struct file* fp;       // pointer to FILE struct for this fd

  bool is_dir;
  struct dir* dp;
} file_elem_t;

/* Process control stuff */
typedef struct child_elem {
  struct list_elem elem;   // pointers to prev and next
  struct wait_data* wdata; // pointer to a wait_data struct
  pid_t child_pid;         // pid of process that wdata is waiting for
  bool being_waited;       // true iff parent called wait on this child
} child_elem_t;

// goes on kernel heap
typedef struct wait_data {
  int exit_code;         // exit code of child process
  int ref_count;         // keep track of how many processes need this data
                         // initially 2 (?), free if 0
  struct semaphore sema; // allows for parent to wait for child
  struct lock ref_lock;  // lock for ref_count
} wait_data_t;

// passed into thread_create in process_execute
typedef struct start_process_arg {
  struct semaphore sema;   // let parent know success status after loading
  char* cmd_line_input;    // input to be parsed in start_process
  bool load_success;       // true iff child loaded executable successfully
  struct wait_data* wdata; // child stores this wdata into its PCB
  struct dir* cwd;
} start_process_arg_t;

/* The process control block for a given process. Since
   there can be multiple threads per process, we need a separate
   PCB from the TCB. All TCBs in a process will have a pointer
   to the PCB, and the PCB will have a pointer to the main thread
   of the process, which is `special`. */
struct process {
  /* Owned by process.c. */
  uint32_t* pagedir;          /* Page directory. */
  char process_name[16];      /* Name of the main thread */
  struct thread* main_thread; /* Pointer to main thread */

  /* New fields */
  struct list file_desc_table; // linked list of file_elem
  int next_fd;                 // next file descriptor number to use

  struct list children;    // linked list of child processes
  struct wait_data* wdata; // write exit code here for parent process

  struct dir* cwd;
};

void userprog_init(void);

pid_t process_execute(const char* cmd_line_input);
int process_wait(pid_t);
void process_exit(int status);
void process_activate(void);

bool is_main_thread(struct thread*, struct process*);
pid_t get_pid(struct process*);

tid_t pthread_execute(stub_fun, pthread_fun, void*);
tid_t pthread_join(tid_t);
void pthread_exit(void);
void pthread_exit_main(void);

#endif /* userprog/process.h */
