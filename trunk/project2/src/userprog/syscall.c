#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "userprog/pagedir.h"
#include "devices/shutdown.h"
#include "filesys/filesys.h"
#include "filesys/file.h"
#include "threads/malloc.h"
#include <list.h>
#include "devices/input.h"
#include "userprog/process.h"

static void syscall_handler (struct intr_frame *);

/* syscall functions */
static void syscall_halt (struct intr_frame *f, void *cur_sp);
static void syscall_exit (struct intr_frame *f, void *cur_sp);
static void syscall_exec (struct intr_frame *f, void *cur_sp);
static void syscall_wait (struct intr_frame *f, void *cur_sp);
static void syscall_create (struct intr_frame *f, void *cur_sp);
static void syscall_open (struct intr_frame *f, void *cur_sp);
static void syscall_filesize (struct intr_frame *f, void *cur_sp);
static void syscall_read (struct intr_frame *f, void *cur_sp);
static void syscall_write (struct intr_frame *f, void *cur_sp);
static void syscall_seek (struct intr_frame *f, void *cur_sp);
static void syscall_tell (struct intr_frame *f, void *cur_sp);
static void syscall_close (struct intr_frame *f, void *cur_sp);

/* simple exit */
//static void syscall_simple_exit (struct intr_frame *f, int status);

/* pointer validity */
static bool syscall_invalid_ptr (const void *ptr);

/* find file pointer from file descriptor */
static struct file *find_file (int fd);

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

void
syscall_simple_exit (struct intr_frame *f, int status)
{
  printf("%s: exit(%d)\n", thread_name (), status);

  /* close all open file descriptors */
  struct thread *t = thread_current ();
 
  struct list_elem *e;
  // Need a lock ?
  while (!list_empty (&t->file_list))
    {
      e = list_pop_back (&t->file_list);
      struct file_elem *f_elem = list_entry (e, struct file_elem, elem);
      free (f_elem);
    }
  // Need to release the lock?

  thread_exit ();
  f->eax = status;
}

#define VALIDATE_AND_GET_ARG(cur_sp,var,f)   \
({ if (syscall_invalid_ptr (cur_sp))         \
     {					     \
       syscall_simple_exit (f, -1);	     \
       return;				     \
     }					     \
  var = *(typeof (var)*)cur_sp;		     \
})

static void
syscall_handler (struct intr_frame *f) 
{
  int syscall_num;
  VALIDATE_AND_GET_ARG (f->esp, syscall_num, f);
  void *cur_sp = f->esp + sizeof (void *);

  switch (syscall_num)
    {
      case SYS_HALT:
        syscall_halt (f, cur_sp);
        break;
      case SYS_EXIT:
	syscall_exit (f, cur_sp);
        break;
      case SYS_EXEC:
	//	printf("system call SYS_EXEC!\n");
	syscall_exec (f, cur_sp);
        break;
      case SYS_WAIT:
	//        printf("system call SYS_WAIT!\n");
	syscall_wait (f, cur_sp);
        break;
      case SYS_CREATE:
	syscall_create (f, cur_sp);
        break;
      case SYS_REMOVE:
        printf("system call SYS_REMOVE!\n");
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
      default :
        printf ("Invalid system call! #%d\n", syscall_num);
	syscall_simple_exit (f, -1);
        break;
    }
}

static void 
syscall_halt (struct intr_frame *f UNUSED, void *cur_sp UNUSED)
{
  shutdown_power_off ();
}

static void 
syscall_exit (struct intr_frame *f, void *cur_sp)
{
  int status;
  VALIDATE_AND_GET_ARG(cur_sp, status, f); 
  syscall_simple_exit (f, status);
}

static void
syscall_exec (struct intr_frame *f, void *cur_sp)
{
  const char *cmd_line;
  VALIDATE_AND_GET_ARG(cur_sp, cmd_line, f);

  if (syscall_invalid_ptr (cmd_line))
    {
      syscall_simple_exit (f, -1);
      return;
    }
  
  tid_t pid = process_execute (cmd_line);
  
  if (pid == TID_ERROR)
    {
      f->eax = -1;
      return;
    }

  struct thread *t = thread_current ();
  struct child_elem *c_elem = (struct child_elem *) malloc (sizeof 
							    (struct child_elem));
  ASSERT (c_elem);

  c_elem->pid = pid;
  
  // Need a lock
  list_push_back (&t->children_list, &c_elem->elem);
  // Need to release the lock

  f->eax = pid;
}

static void
syscall_wait (struct intr_frame *f, void *cur_sp)
{
  tid_t pid;
  VALIDATE_AND_GET_ARG(cur_sp, pid, f);

  /* check pid validity */
  struct thread *t = thread_current ();
  
  struct list_elem *e;
  // Need to lock
  for (e = list_begin (&t->children_list); 
       e != list_end (&t->children_list);
       e = list_next (e))
    {
      struct child_elem *c_elem = list_entry (e, struct child_elem, elem);
      if (c_elem->pid == pid)
	{
	  f->eax = process_wait (pid); // wrong status
	  return;
	}
    }
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
      syscall_simple_exit (f, -1);
      return;
    }

  f->eax = filesys_create (file, initial_size);
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
      syscall_simple_exit (f, -1);
      return;
    }

  struct file *file = filesys_open (file_name);
  // need a lock
  if (!file)
    fd = -1;
  else
    fd = next_fd++;

  struct thread *t = thread_current ();
  struct file_elem *f_elem = (struct file_elem *) malloc (sizeof (struct file_elem));
  ASSERT (f_elem);

  f_elem->fd =fd;
  f_elem->file = file;

  // Need a lock
  list_push_back (&t->file_list, &f_elem->elem);
  // Need to release the lock

  f->eax = fd;
  // need to release a lock
}

static void
syscall_filesize (struct intr_frame *f, void *cur_sp)
{
  int fd;
  VALIDATE_AND_GET_ARG (cur_sp, fd, f);

  struct file *file = find_file (fd);
  if (file != NULL)
    f->eax = file_length (file);
  else
    syscall_simple_exit (f, -1);
}

static void
syscall_read (struct intr_frame *f, void *cur_sp)
{
  // FIXME :
  /* DOESN'T HAVE FD SPECIFIC READ */
  int fd;
  void * buffer;
  unsigned length;
  VALIDATE_AND_GET_ARG (cur_sp, fd, f);
  cur_sp += sizeof (void *);
  VALIDATE_AND_GET_ARG (cur_sp, buffer, f);
  cur_sp += sizeof (void *);
  VALIDATE_AND_GET_ARG (cur_sp, length, f);

  if (fd == STDOUT_FILENO || fd < -1 ||
      syscall_invalid_ptr (buffer))
    {
      syscall_simple_exit (f, -1);
      return;
    }

  ASSERT (fd >= 0);
  
  if (fd == STDIN_FILENO) 
    {
      //NEED TO FIX INPUT_GETC AND WHAT TO RETURN
      f->eax = input_getc ();
      return;
    }
  
  struct file *file = find_file (fd);
  if (file != NULL)
    f->eax = file_read(file, buffer, length);
  else
    syscall_simple_exit (f, -1);
}

static void 
syscall_write (struct intr_frame *f, void *cur_sp)
{
  // FIXME :
  /* DOESN'T HAVE FD SPECIFIC WRITE */
  int fd;
  const void * buffer;
  unsigned length;
  VALIDATE_AND_GET_ARG (cur_sp, fd, f);
  cur_sp += sizeof (void *);
  VALIDATE_AND_GET_ARG (cur_sp, buffer, f);
  cur_sp += sizeof (void *);
  VALIDATE_AND_GET_ARG (cur_sp, length, f);
  
  if (fd == STDIN_FILENO || fd < -1 ||
      syscall_invalid_ptr (buffer))
    {
      syscall_simple_exit (f, -1);
      return;
    }

  if (fd == STDOUT_FILENO)
    {
      putbuf(buffer, length);
      f->eax = length;
      return;
    }

  struct file *file = find_file (fd);
  if (file != NULL)
    f->eax = file_write (file, buffer, length);
  else
    syscall_simple_exit (f, -1);
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
    file_seek (file, position);
  else
    syscall_simple_exit (f, -1);
}

static void 
syscall_tell (struct intr_frame *f, void *cur_sp) 
{
  int fd;
  VALIDATE_AND_GET_ARG (cur_sp, fd, f);

  struct file *file = find_file (fd);
  if (file != NULL)
    f->eax = file_tell (file);
  else
    syscall_simple_exit (f, -1);
}

static void
syscall_close (struct intr_frame *f, void *cur_sp)
{
  int fd;
  VALIDATE_AND_GET_ARG (cur_sp, fd, f);

  struct thread *t = thread_current ();
  struct list_elem *e;
  // Need a lock 
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
  syscall_simple_exit (f, -1); // Need this?
  // Need to release the lock
}


static bool 
syscall_invalid_ptr (const void *ptr)
{
  if (!is_user_vaddr (ptr) || 
      !pagedir_get_page (thread_current ()->pagedir, ptr) ||
      ptr == NULL)
    return true;
  return false;
}


static struct file *
find_file (int fd)
{
  struct thread *t = thread_current ();
  struct list_elem *e;
  // NEED TO LOCK THIS 
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