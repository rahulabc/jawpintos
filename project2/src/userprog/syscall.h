#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

#include <list.h>

/* File list element */
struct file_elem
  {
    int fd;
    struct file *file;
    struct list_elem elem;
  };

void syscall_init (void);

#endif /* userprog/syscall.h */
