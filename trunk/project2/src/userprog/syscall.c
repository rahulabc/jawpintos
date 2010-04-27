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
static void syscall_halt (void);
static int syscall_read (int fd, void *buffer, unsigned length);
static int syscall_write (int fd, const void *buffer, unsigned length);
static void syscall_exit (int status);

/* pointer validity */
static bool syscall_invalid_ptr (const void *buffer);

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static void
syscall_handler (struct intr_frame *f UNUSED) 
{
  int syscall_num = *((int *)f->esp);
  //hex_dump ((int) f->esp, f->esp, 16, true); // REMOVEME:
  switch (syscall_num)
    {
      int fd, status;
      void *buffer;
      unsigned length;

      case SYS_HALT:
        //printf("system call SYS_HALT!\n");
        syscall_halt ();
        break;
      case SYS_EXIT:
        //      	printf("system call SYS_EXIT!\n");
        status = *(int *)(f->esp + sizeof (int));
        syscall_exit (status);
        f->eax = status;
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
        fd = *(int *) (f->esp + sizeof (int));
        buffer = *(void **) (f->esp + 2 * sizeof (int));
        length = *(int *) (f->esp + 2 * sizeof (int) + sizeof (void *));
        f->eax = syscall_read (fd, buffer, length);
        break;
      case SYS_WRITE:
        //	printf ("system call SYS_WRITE!\n");
        fd = *(int *) (f->esp + sizeof (int));
        buffer = *(void **) (f->esp + 2 * sizeof (int));
        length = *(int *) (f->esp + 2 * sizeof (int) + sizeof (void *));
        f->eax = syscall_write (fd, buffer, length);
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
syscall_halt (void)
{
  shutdown_power_off ();
}

static int 
syscall_read (int fd, void *buffer, unsigned length)
{
  if (syscall_invalid_ptr (buffer))
    syscall_exit (-1);
  ASSERT (fd != 1 && fd >= 0);
  return length;
}

static int 
syscall_write (int fd, const void *buffer, unsigned length)
{
  if (syscall_invalid_ptr (buffer))
    syscall_exit (-1);

  ASSERT (fd > 0);
  if (fd == 1)
    putbuf(buffer, length);
  return length;
}

static void 
syscall_exit (int status)
{
  printf("%s: exit(%d)\n", thread_name (), status);
  thread_exit ();
}

static bool 
syscall_invalid_ptr (const void *buffer)
{
  if (!is_user_vaddr (buffer))
    return true;
  if (!pagedir_get_page (thread_current ()->pagedir, buffer))
    return true;
  return false;
}
