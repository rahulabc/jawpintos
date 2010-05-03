#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

#include <list.h>
#include "threads/interrupt.h"
#include "threads/thread.h"

/* File list element */
struct file_elem
  {
    int fd;
    struct file *file;
    struct list_elem elem;
  };

void syscall_init (void);
/* simple exit */
void syscall_simple_exit (struct intr_frame *f, int status);

#endif /* userprog/syscall.h */
