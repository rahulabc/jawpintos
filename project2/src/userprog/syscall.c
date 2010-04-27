#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "userprog/pagedir.h"
#include "devices/shutdown.h"

static void syscall_handler (struct intr_frame *);

/* syscall functions */
static void syscall_halt (struct intr_frame *f);
static void syscall_exit (struct intr_frame *f);
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
        printf("system call SYS_CREATE!\n");
        break;
      case SYS_REMOVE:
        printf("system call SYS_REMOVE!\n");
        break;
      case SYS_OPEN:
        printf("system call SYS_OPEN!\n");
        break;
      case SYS_FILESIZE:
        printf("system call SYS_FILESIZE!\n");
        break;
      case SYS_READ:
        printf ("system call SYS_READ!\n");
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
syscall_read (struct intr_frame *f)
{
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

  ASSERT (fd != 1 && fd >= 0);
  f->eax = length; // FIXME:
}

static void 
syscall_write (struct intr_frame *f)
{
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
  
  ASSERT (fd > 0);

  if (fd == 1)
    putbuf(buffer, length);
  f->eax = length; // FIXME:
}


static bool 
syscall_invalid_ptr (const void *ptr)
{
  if (!is_user_vaddr (ptr))
    return true;
  if (!pagedir_get_page (thread_current ()->pagedir, ptr))
    return true;
  return false;
}
