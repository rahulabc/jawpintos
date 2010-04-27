#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "userprog/pagedir.h"
#include "devices/shutdown.h"
#include "filesys/filesys.h"

static void syscall_handler (struct intr_frame *);

/* syscall functions */
static void syscall_halt (struct intr_frame *f);
static void syscall_exit (struct intr_frame *f);
static void syscall_create (struct intr_frame *f);
static void syscall_open (struct intr_frame *f);
static void syscall_read (struct intr_frame *f);
static void syscall_write (struct intr_frame *f);

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
        //printf("system call SYS_HALT!\n");
        syscall_halt (f);
        break;
      case SYS_EXIT:
        //      	printf("system call SYS_EXIT!\n");
	syscall_exit (f);
        break;
      case SYS_EXEC:
        printf("system call SYS_EXEC!\n");
        break;
      case SYS_WAIT:
        printf("system call SYS_WAIT!\n");
        break;
      case SYS_CREATE:
	//        printf("system call SYS_CREATE!\n");
	syscall_create (f);
        break;
      case SYS_REMOVE:
        printf("system call SYS_REMOVE!\n");
        break;
      case SYS_OPEN:
	//        printf("system call SYS_OPEN!\n");
	syscall_open (f);
        break;
      case SYS_FILESIZE:
        printf("system call SYS_FILESIZE!\n");
        break;
      case SYS_READ:
	//        printf ("system call SYS_READ!\n");
	syscall_read (f);
        break;
      case SYS_WRITE:
        //	printf ("system call SYS_WRITE!\n");
	syscall_write (f);
        break;
      case SYS_SEEK:
        printf("system call SYS_SEEK!\n");
        break;
      case SYS_TELL:
        printf("system call SYS_TELL!\n");
        break;
      case SYS_CLOSE:
        printf("system call SYS_CLOSE!\n");
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

  f->eax = fd;
  // need to release a lock

  // ADDME:
  /* NEED TO KEEP A LIST OF FILES WITH THEIR FILE DESCRIPTORS */
  /* LIST OF FILE DESCRIPTORS INSIDE A STRUCT TRHEAD? */
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
  f->eax = length; // FIXME:
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
    putbuf(buffer, length);
  f->eax = length; // FIXME:
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
