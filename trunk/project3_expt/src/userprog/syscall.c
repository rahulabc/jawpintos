#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "threads/malloc.h"
#include "threads/synch.h"
#include "userprog/pagedir.h"
#include "devices/shutdown.h"
#include "filesys/filesys.h"
#include "filesys/file.h"
#include <list.h>
#include "devices/input.h"
#include "userprog/process.h"
#include "vm/page.h"
#include "lib/user/syscall.h"

// #define DEBUG

static void syscall_handler (struct intr_frame *);

/* syscall functions */
static void syscall_halt (struct intr_frame *f, void *cur_sp);
static void syscall_exit (struct intr_frame *f, void *cur_sp);
static void syscall_exec (struct intr_frame *f, void *cur_sp);
static void syscall_wait (struct intr_frame *f, void *cur_sp);
static void syscall_create (struct intr_frame *f, void *cur_sp);
static void syscall_remove (struct intr_frame *f, void *cur_sp);
static void syscall_open (struct intr_frame *f, void *cur_sp);
static void syscall_filesize (struct intr_frame *f, void *cur_sp);
static void syscall_read (struct intr_frame *f, void *cur_sp);
static void syscall_write (struct intr_frame *f, void *cur_sp);
static void syscall_seek (struct intr_frame *f, void *cur_sp);
static void syscall_tell (struct intr_frame *f, void *cur_sp);
static void syscall_close (struct intr_frame *f, void *cur_sp);
static void syscall_mmap (struct intr_frame *f, void *cur_sp);
static void syscall_munmap (struct intr_frame *f, void *cur_sp);

/* pointer validity */
static bool syscall_invalid_ptr (const void *ptr);

/* find file pointer from file descriptor */
static struct file *find_file (int fd);

static struct lock next_fd_lock;
static struct lock create_remove_filesys_lock;
static struct lock read_filesys_lock;
static struct lock write_filesys_lock;
static struct lock next_mapping_id_lock;

void
syscall_init (void) 
{
  lock_init (&next_fd_lock);
  lock_init (&create_remove_filesys_lock);
  lock_init (&write_filesys_lock);
  lock_init (&read_filesys_lock);
  lock_init (&next_mapping_id_lock);
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

void
thread_cleanup_and_exit (int status) 
{
  printf ("%s: exit(%d)\n", thread_name (), status);
  /* close all open file descriptors */
  struct thread *t = thread_current ();
 
  struct list_elem *e;

  /* close all the files opened and
     free spaces allocated for the file list */
  while (!list_empty (&t->file_list))
    {
      e = list_pop_back (&t->file_list);
      struct file_elem *f_elem = list_entry (e, struct file_elem, elem);
      file_close (f_elem->file);
      free (f_elem);
    }

  /* free waited_children_list and children_list */
  while (!list_empty (&t->children_list))
    {
      e = list_pop_back (&t->children_list);
      struct child_elem *c_elem = list_entry (e, struct child_elem, elem);
      // free children from the global exit_list
      free_thread_from_exit_list (c_elem->pid);
      free (c_elem);
    }
  while (!list_empty (&t->waited_children_list))
    {
      e = list_pop_back (&t->waited_children_list);
      struct wait_child_elem *w_elem = list_entry (e, struct wait_child_elem, elem);
      free (w_elem);
    }

  /* Supp Page Table, Swap Table, and Frame Table Free */
  //  spt_free (t->tid);

  add_thread_to_exited_list (t->tid, status);
  
  /* allow file write to executable */
  if (t->exec_file) 
    {
      file_allow_write (t->exec_file);
      file_close (t->exec_file);
    }

  /* release all the locks that have not already been released */
  while (!list_empty (&t->acquired_locks))
    {
      struct list_elem *e = list_front (&t->acquired_locks);
      struct lock *l = list_entry (e, struct lock, elem);
      lock_release (l);
    }
  /* wake parent up if its waiting on it */
  struct thread *parent = get_thread (thread_current ()->parent_id);
  if (parent)  {
    sema_up (&parent->waiting_on_child_exit_sema);
  }

  thread_exit ();
}

void
syscall_thread_exit (struct intr_frame *f, int status)
{
  thread_cleanup_and_exit (status);
  f->eax = status;
}

/* Macro for malloc ing and validating at the same time */
#define MALLOC_AND_VALIDATE(f, var, size)   \
({                                          \
  var = (typeof (var)) malloc (size);       \
  if (var == NULL)                          \
     {                                      \
       syscall_thread_exit (f, -1);         \
       return;                              \
     }                                      \
})

/* Macro for validating and getting data from the stack */
#define VALIDATE_AND_GET_ARG(cur_sp,var,f)  \
({ if (syscall_invalid_ptr (cur_sp))        \
     {                                      \
       syscall_thread_exit (f, -1);         \
       return;                              \
     }                                      \
  var = *(typeof (var)*)cur_sp;             \
})

static void
syscall_handler (struct intr_frame *f) 
{
  int syscall_num;
  VALIDATE_AND_GET_ARG (f->esp, syscall_num, f);
  void *cur_sp = f->esp + sizeof (void *);

#ifdef DEBUG
  printf ("in syscall_handler: %d \n", syscall_num);
#endif
  /* store user program stack pointer to the thread's 
     user_esp before changing to kernel mode */
  struct thread *t = thread_current ();
  t->user_esp = f->esp;

  switch (syscall_num)
    {
      case SYS_HALT:
        syscall_halt (f, cur_sp);
        break;
      case SYS_EXIT:
        syscall_exit (f, cur_sp);
        break;
      case SYS_EXEC:
        syscall_exec (f, cur_sp);
        break;
      case SYS_WAIT:
        syscall_wait (f, cur_sp);
        break;
      case SYS_CREATE:
        syscall_create (f, cur_sp);
        break;
      case SYS_REMOVE:
        syscall_remove (f, cur_sp);
        break;
      case SYS_OPEN:
        syscall_open (f, cur_sp);
        break;
      case SYS_FILESIZE:
        syscall_filesize (f, cur_sp);
        break;
      case SYS_READ:
        syscall_read (f, cur_sp);
        break;
      case SYS_WRITE:
        syscall_write (f, cur_sp);
        break;
      case SYS_SEEK:
        syscall_seek (f, cur_sp);
        break;
      case SYS_TELL:
        syscall_tell (f, cur_sp);
        break;
      case SYS_CLOSE:
        syscall_close (f, cur_sp);
        break;
      case SYS_MMAP:
        syscall_mmap (f, cur_sp);
        break;
      case SYS_MUNMAP:
        syscall_munmap (f, cur_sp);
        break;
      default :
        printf ("Invalid system call! #%d\n", syscall_num);
        syscall_thread_exit (f, -1);
        break;
    }
}

static void 
syscall_halt (struct intr_frame *f UNUSED, void *cur_sp UNUSED)
{
  shutdown_power_off ();
}

static void 
syscall_mmap (struct intr_frame *f, void *cur_sp)
{
  int fd;
  VALIDATE_AND_GET_ARG (cur_sp, fd, f); 
  cur_sp += sizeof (int);
  void *addr;
  VALIDATE_AND_GET_ARG (cur_sp, addr, f);
  if (addr == 0 || pg_ofs (addr) != 0 || fd==0 || fd==1) 
    {
       f->eax = -1;
       return;
    }
  struct file *fil = find_file (fd); 
  if (fil == 0) 
    {
       f->eax = -1;
       return;
    }
  off_t flen = file_length (fil);
  if (flen == 0) 
    {
       f->eax = -1;
       return;
    }
  off_t cur_ofs = 0; 
  while (flen >= PGSIZE) 
    {
      spt_pagedir_update (thread_current(), addr+cur_ofs, 
                          NULL, FRAME_FILE, 
                          0, fil,
                          cur_ofs, PGSIZE, 
                          0, false);
      cur_ofs += PGSIZE;
      flen -= PGSIZE;
    }
  if (flen > 0) 
    {
      spt_pagedir_update (thread_current(), addr+cur_ofs, 
                          NULL, FRAME_FILE, 
                          0, fil,
                          cur_ofs, flen,
                          PGSIZE-flen, false);
    }
  static int next_mapping_id = 0;
  lock_acquire (&next_mapping_id_lock);
  f->eax = ++next_mapping_id;
  lock_release (&next_mapping_id_lock);
  return;
}

static void 
syscall_munmap (struct intr_frame *f UNUSED, void *cur_sp UNUSED)
{
  mapid_t mapping;
  VALIDATE_AND_GET_ARG (cur_sp, mapping, f); 
  return;
}

static void 
syscall_exit (struct intr_frame *f, void *cur_sp)
{
  int status;
  VALIDATE_AND_GET_ARG (cur_sp, status, f); 
  syscall_thread_exit (f, status);
}

static void
syscall_exec (struct intr_frame *f, void *cur_sp)
{
  const char *cmd_line;
  VALIDATE_AND_GET_ARG (cur_sp, cmd_line, f);

  if (syscall_invalid_ptr (cmd_line))
    {
      syscall_thread_exit (f, -1);
      return;
    }
  
  tid_t pid = process_execute (cmd_line);
  
  if (pid == TID_ERROR)
    {
      f->eax = -1;
      return;
    }
  f->eax = pid;
}

static void
syscall_wait (struct intr_frame *f, void *cur_sp)
{
  tid_t pid;
  VALIDATE_AND_GET_ARG (cur_sp, pid, f);

  /* check pid validity */
  struct thread *t = thread_current ();
  
  /* find its child to wait on */  
  struct list_elem *e;
  for (e = list_begin (&t->children_list); 
       e != list_end (&t->children_list);
       e = list_next (e))
    {
      struct child_elem *c_elem = list_entry (e, struct child_elem, elem);
      if (c_elem->pid == pid) /* found child */
        {
          /* if child is already waited on before, return -1 */
          struct list_elem *waited_e;
          for (waited_e = list_begin (&t->waited_children_list); 
               waited_e != list_end (&t->waited_children_list);
               waited_e = list_next (waited_e))
            {
              struct wait_child_elem *waited_c_elem = 
                      list_entry (waited_e, struct wait_child_elem, elem);
              if (waited_c_elem->pid == pid)  /* already waited on */
                {
                  f->eax = -1; /* return -1 */
                  return;
                }
            }
          /* mark child as already waited on wait on it */
          struct wait_child_elem *new_wait_c_elem;
          MALLOC_AND_VALIDATE (f, new_wait_c_elem, sizeof (struct wait_child_elem)); 
          new_wait_c_elem->pid = pid;
          list_push_back (&t->waited_children_list, &new_wait_c_elem->elem);
          f->eax = process_wait (pid); 
          return;
        }
    }
  /* if code gets here, means no child with that pid, so return -1 */
  f->eax = -1;
}

static void 
syscall_create (struct intr_frame *f, void *cur_sp)
{
  const char *file;
  unsigned initial_size;
  VALIDATE_AND_GET_ARG (cur_sp, file, f);
  cur_sp += sizeof (void *);
  VALIDATE_AND_GET_ARG (cur_sp, initial_size, f);

  if (syscall_invalid_ptr (file))
    {
      syscall_thread_exit (f, -1);
      return;
    }
  lock_acquire (&create_remove_filesys_lock);
  f->eax = filesys_create (file, initial_size);
  lock_release (&create_remove_filesys_lock);
}

static void 
syscall_remove (struct intr_frame *f, void *cur_sp)
{
  const char *file;
  VALIDATE_AND_GET_ARG (cur_sp, file, f);
  if (syscall_invalid_ptr (file))
    {
      syscall_thread_exit (f, -1);
      return;
    }
  lock_acquire (&create_remove_filesys_lock);
  f->eax = filesys_remove (file);
  lock_release (&create_remove_filesys_lock);
}


static void
syscall_open (struct intr_frame *f, void *cur_sp)
{
  static int next_fd = 2;
  int fd;
  const char *file_name;
  VALIDATE_AND_GET_ARG (cur_sp, file_name, f);
  if (syscall_invalid_ptr (file_name))
    {
      syscall_thread_exit (f, -1);
      return;
    }

  struct file *file = filesys_open (file_name);
  if (!file)
    fd = -1;
  else
    {
      lock_acquire (&next_fd_lock);
      fd = next_fd++;
      lock_release (&next_fd_lock);
    }
  struct thread *t = thread_current ();
  struct file_elem *f_elem;
  f_elem = (struct file_elem*) malloc (sizeof (struct file_elem));
  if (f_elem == NULL)
    {
      file_close (file);
      syscall_thread_exit (f, -1);
      return;
    }

  f_elem->fd =fd;
  f_elem->file = file;

  list_push_back (&t->file_list, &f_elem->elem);

  f->eax = fd;
}

static void
syscall_filesize (struct intr_frame *f, void *cur_sp)
{
  int fd;
  VALIDATE_AND_GET_ARG (cur_sp, fd, f);

  struct file *file = find_file (fd);
  if (file != NULL)
    {
      lock_acquire (&write_filesys_lock);
      f->eax = file_length (file);
      lock_release (&write_filesys_lock);
    }
  else
    syscall_thread_exit (f, -1);
}

static void
syscall_read (struct intr_frame *f, void *cur_sp)
{
  int fd;
  void * buffer;
  unsigned length;

  VALIDATE_AND_GET_ARG (cur_sp, fd, f);
  cur_sp += sizeof (void *);
  VALIDATE_AND_GET_ARG (cur_sp, buffer, f);
  cur_sp += sizeof (void *);
  VALIDATE_AND_GET_ARG (cur_sp, length, f);

  /* terminate process when the provided arguments
     are invalid for the following reasons:
     1. invalid file descriptors
     2. invalid buffer start address
     3. invalid buffer end address */

  if (fd == STDOUT_FILENO || fd < -1 ||
      syscall_invalid_ptr (buffer) ||
      syscall_invalid_ptr (buffer + length))
    {
      syscall_thread_exit (f, -1);
      return;
    }
  
  /* terminate process if any page that is occupied by the 
     buffer is invalid */
  void *buffer_tmp_ptr = buffer + PGSIZE;
  while (buffer_tmp_ptr < buffer + length)
    {
      if (syscall_invalid_ptr (buffer_tmp_ptr))
	{
	  syscall_thread_exit (f, -1);
	  return;
	}
      buffer_tmp_ptr += PGSIZE;
    }

  ASSERT (fd >= 0);
  
  if (fd == STDIN_FILENO) 
    {
      f->eax = input_getc ();
      return;
    }
  
  struct file *file = find_file (fd);
  if (file != NULL)
    {
      lock_acquire (&read_filesys_lock);
      f->eax = file_read (file, buffer, length);
      lock_release (&read_filesys_lock);
    }
  else
    syscall_thread_exit (f, -1);
}

static void 
syscall_write (struct intr_frame *f, void *cur_sp)
{
  int fd;
  const void * buffer;
  unsigned length;

  VALIDATE_AND_GET_ARG (cur_sp, fd, f);
  cur_sp += sizeof (void *);
  VALIDATE_AND_GET_ARG (cur_sp, buffer, f);
  cur_sp += sizeof (void *);
  VALIDATE_AND_GET_ARG (cur_sp, length, f);

  /* terminate process when the provided arguments
     are invalid for the following reasons:
     1. invalid file descriptors
     2. invalid buffer start address
     3. invalid buffer end address */
  if (fd == STDIN_FILENO || fd < -1 ||
      syscall_invalid_ptr (buffer) ||
      syscall_invalid_ptr (buffer + length))
    {
      syscall_thread_exit (f, -1);
      return;
    }

  /* terminate process if any page that is occupied by the 
     buffer is invalid */
  const void *buffer_tmp_ptr = buffer + PGSIZE;
  while (buffer_tmp_ptr < buffer + length)
    {
      if (syscall_invalid_ptr (buffer_tmp_ptr))
	{
	  syscall_thread_exit (f, -1);
	  return;
	}
      buffer_tmp_ptr += PGSIZE;
    }

  if (fd == STDOUT_FILENO)
    {
      putbuf (buffer, length);
      f->eax = length;
      return;
    }

  struct file *file = find_file (fd);
  if (file != NULL)
    {
      lock_acquire (&read_filesys_lock);
      lock_acquire (&write_filesys_lock);
      f->eax = file_write (file, buffer, length);
      lock_release (&write_filesys_lock);
      lock_release (&read_filesys_lock);
    }
  else
    syscall_thread_exit (f, -1);
}

static void
syscall_seek (struct intr_frame *f, void *cur_sp) 
{
  int fd;
  unsigned position;
  VALIDATE_AND_GET_ARG (cur_sp, fd, f);
  cur_sp += sizeof (void *);
  VALIDATE_AND_GET_ARG (cur_sp, position, f);

  struct file *file = find_file (fd);
  if (file != NULL)
    {
      lock_acquire (&read_filesys_lock);
      lock_acquire (&write_filesys_lock);
      file_seek (file, position);
      lock_release (&write_filesys_lock);
      lock_release (&read_filesys_lock);
    }
  else
    syscall_thread_exit (f, -1);
}

static void 
syscall_tell (struct intr_frame *f, void *cur_sp) 
{
  int fd;
  VALIDATE_AND_GET_ARG (cur_sp, fd, f);

  struct file *file = find_file (fd);
  if (file != NULL)
    {
      lock_acquire (&read_filesys_lock);
      lock_acquire (&write_filesys_lock);
      f->eax = file_tell (file);
      lock_release (&write_filesys_lock);
      lock_release (&read_filesys_lock);
    }
  else
    syscall_thread_exit (f, -1);
}

static void
syscall_close (struct intr_frame *f, void *cur_sp)
{
  // we do not expect same fd's to be called by many threads in this
  // version of pintos, so don't need a lock 
  int fd;
  VALIDATE_AND_GET_ARG (cur_sp, fd, f);

  struct thread *t = thread_current ();
  struct list_elem *e;
  for (e = list_begin (&t->file_list); e != list_end (&t->file_list);
       e = list_next (e))
    {
      struct file_elem *f_elem = list_entry (e, struct file_elem, elem);
      if (f_elem->fd == fd)
        {
          file_close (f_elem->file);
          list_remove (e);
          free (f_elem);
          return;
        }
    }  
  syscall_thread_exit (f, -1); // Need this?
}


static bool 
syscall_invalid_ptr (const void *ptr)
{
  if (!is_user_vaddr (ptr) || ptr == NULL)    
    return true;

  return false;
}


static struct file *
find_file (int fd)
{
  struct thread *t = thread_current ();
  struct list_elem *e;
  for (e = list_begin (&t->file_list);
       e != list_end (&t->file_list);
       e = list_next (e))
    {
      struct file_elem *f_elem = list_entry (e, struct file_elem, elem);
      if (f_elem->fd == fd)
        return f_elem->file;
    }
  return NULL;
}
