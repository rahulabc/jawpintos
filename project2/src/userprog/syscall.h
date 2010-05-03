#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

#include <list.h>
#include "threads/interrupt.h"
#include "threads/thread.h"

/* File list element */
struct file_elem
  {
    int fd;                  /* file descriptor */
    struct file *file;
    struct list_elem elem;
  };

void syscall_init (void);

/* function for exiting a thread after cleaning up any held resources */
void thread_cleanup_and_exit (int status);

/* same as above, but sets the status in the intr_frame as well */
void syscall_thread_exit (struct intr_frame *f, int status);

#endif /* userprog/syscall.h */
