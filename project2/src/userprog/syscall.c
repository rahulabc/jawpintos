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

static void syscall_handler (struct intr_frame *);

/* syscall functions */
static void syscall_halt (struct intr_frame *f);
static void syscall_exit (struct intr_frame *f);
static void syscall_create (struct intr_frame *f);
static void syscall_open (struct intr_frame *f);
static void syscall_filesize (struct intr_frame *f);
static void syscall_read (struct intr_frame *f);
static void syscall_write (struct intr_frame *f);
static void syscall_seek (struct intr_frame *f);
static void syscall_tell (struct intr_frame *f);
static void syscall_close (struct intr_frame *f);

/* simple exit */
static void syscall_simple_exit (struct intr_frame *f, int status);

/* pointer validity */
static bool syscall_invalid_ptr (const void *ptr);

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static void
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

static void
syscall_handler (struct intr_frame *f) 
{
  if (syscall_invalid_ptr (f->esp))
    {
      syscall_simple_exit (f, -1);
      return;
    }
      
  int syscall_num = *((int *)f->esp);

  switch (syscall_num)
    {
      case SYS_HALT:
        syscall_halt (f);
        break;
      case SYS_EXIT:
	syscall_exit (f);
        break;
      case SYS_EXEC:
        printf("system call SYS_EXEC!\n");
        break;
      case SYS_WAIT:
        printf("system call SYS_WAIT!\n");
        break;
      case SYS_CREATE:
	syscall_create (f);
        break;
      case SYS_REMOVE:
        printf("system call SYS_REMOVE!\n");
        break;
      case SYS_OPEN:
	syscall_open (f);
        break;
      case SYS_FILESIZE:
	syscall_filesize (f);
        break;
      case SYS_READ:
	syscall_read (f);
        break;
      case SYS_WRITE:
	syscall_write (f);
        break;
      case SYS_SEEK:
        syscall_seek (f);
        break;
      case SYS_TELL:
        syscall_tell (f);
        break;
      case SYS_CLOSE:
	syscall_close (f);
        break;
      default :
        printf ("Invalid system call! #%d\n", syscall_num);
        thread_exit ();
        break;
    }
}

static void 
syscall_halt (struct intr_frame *f UNUSED)
{
  shutdown_power_off ();
}

static void 
syscall_exit (struct intr_frame *f)
{
  int status;
  if (syscall_invalid_ptr (f->esp + sizeof (int)))
    status = -1;
  else 
    status = *(int *) (f->esp + sizeof (int));
  
  syscall_simple_exit (f, status);
}

static void 
syscall_create (struct intr_frame *f)
{
  if (syscall_invalid_ptr (f->esp + sizeof (int)) ||
      syscall_invalid_ptr (f->esp + sizeof (int) +
			   sizeof (char *)))
    {
      syscall_simple_exit (f, -1);
      return;
    }

  const char *file = *(char **) (f->esp + sizeof (int));
  if (syscall_invalid_ptr (file))
    {
      syscall_simple_exit (f, -1);
      return;
    }
  unsigned initial_size = * (int *) (f->esp + sizeof (int) +
				     sizeof (char *));

  f->eax = filesys_create (file, initial_size);
}

static void
syscall_open (struct intr_frame *f)
{
  static int next_fd = 2;
  int fd;

  if (syscall_invalid_ptr (f->esp + sizeof (int)))
    {
      syscall_simple_exit (f, -1);
      return;
    }

  const char *file_name = * (char **) (f->esp + sizeof (int));
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
syscall_filesize (struct intr_frame *f)
{
  if (syscall_invalid_ptr (f->esp + sizeof (int)))
    {
      syscall_simple_exit (f, -1);
      return;
    }
  
  int fd = *(int *) (f->esp + sizeof (int));
  struct thread *t = thread_current ();
  struct list_elem *e;
  for (e = list_begin (&t->file_list); e != list_end (&t->file_list);
       e = list_next (e)) 
    {
      struct file_elem *f_elem = list_entry (e, struct file_elem, elem);
      if (f_elem->fd == fd) 
	{
	  f->eax = file_length (f_elem->file);
	  return;
	}
    }
  syscall_simple_exit (f, -1);
}

static void
syscall_read (struct intr_frame *f)
{
  // FIXME :
  /* DOESN'T HAVE FD SPECIFIC READ */
  if (syscall_invalid_ptr (f->esp + sizeof (int)) ||
      syscall_invalid_ptr (f->esp + 2 * sizeof (int)) ||
      syscall_invalid_ptr (f->esp + 2 * sizeof (int) +
			   sizeof (void *)))
    {
      syscall_simple_exit (f, -1);
      return;
    }

  int fd = *(int *) (f->esp + sizeof (int));
  void *buffer = *(void **) (f->esp + 2 * sizeof (int));
  unsigned length = *(int *) (f->esp + 2 * sizeof (int) + 
			      sizeof (void *));

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
  
  struct thread *t = thread_current ();
  struct list_elem *e;

  //NEED TO LOCK THIS

  for (e = list_begin (&t->file_list); e != list_end (&t->file_list);
       e = list_next (e)) 
    {
      struct file_elem *f_elem = list_entry (e, struct file_elem, elem);
      if (f_elem->fd == fd) 
	{
	  f->eax = file_read (f_elem->file, buffer, length);
	  return;
	}
    }
  syscall_simple_exit (f, -1);
}

static void 
syscall_write (struct intr_frame *f)
{
  // FIXME :
  /* DOESN'T HAVE FD SPECIFIC WRITE */
  if (syscall_invalid_ptr (f->esp + sizeof (int)) ||
      syscall_invalid_ptr (f->esp + 2 * sizeof (int)) ||
      syscall_invalid_ptr (f->esp + 2 * sizeof (int) +
			   sizeof (void *)))
    {
      syscall_simple_exit (f, -1);
      return;
    }

  int fd = *(int *) (f->esp + sizeof (int));
  const void *buffer = *(void **) (f->esp + 2 * sizeof (int));
  unsigned length = *(int *) (f->esp + 2 * sizeof (int) + 
			      sizeof (void *));
  
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

  struct thread *t = thread_current ();
  struct list_elem *e;
  for (e = list_begin (&t->file_list); e != list_end (&t->file_list);
       e = list_next (e)) 
    {
      struct file_elem *f_elem = list_entry (e, struct file_elem, elem);
      if (f_elem->fd == fd) 
	{
	  f->eax = file_write (f_elem->file, buffer, length);
	  return;
	}
    }
  syscall_simple_exit (f, -1);
}

static void
syscall_seek (struct intr_frame *f) 
{
  if (syscall_invalid_ptr (f->esp + sizeof (int)) ||
      syscall_invalid_ptr (f->esp + 2 * sizeof (int)))
    {
      syscall_simple_exit (f, -1);
      return;
    }

  int fd = *(int *) (f->esp + sizeof (int));
  unsigned position = *(int *) (f->esp + 2 * sizeof (int));

  struct thread *t = thread_current ();

  struct list_elem *e;
  // Need a lock
  for (e = list_begin (&t->file_list); e != list_end (&t->file_list);
       e = list_next (e))
    {
      struct file_elem *f_elem = list_entry (e, struct file_elem, elem);
      if (f_elem->fd == fd)
        {
          file_seek (f_elem->file, position);
          return;
        }
    }
  syscall_simple_exit (f, -1);
}

static void 
syscall_tell (struct intr_frame *f) 
{
  if (syscall_invalid_ptr (f->esp + sizeof (int)))
    {
      syscall_simple_exit (f, -1);
      return;
    }

  int fd = *(int *) (f->esp + sizeof (int));
  
  struct thread *t = thread_current ();

  struct list_elem *e;
  // Need a lock
  for (e = list_begin (&t->file_list); e != list_end (&t->file_list);
       e = list_next (e))
    {
      struct file_elem *f_elem = list_entry (e, struct file_elem, elem);
      if (f_elem->fd == fd)
        {
          f->eax = file_tell (f_elem->file);
	  return;
        }
    }
  syscall_simple_exit (f, -1);
}

static void
syscall_close (struct intr_frame *f)
{
  if (syscall_invalid_ptr (f->esp + sizeof (int)))
    {
      syscall_simple_exit (f, -1);
      return;
    }

  int fd = *(int *) (f->esp + sizeof (int));
  
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
