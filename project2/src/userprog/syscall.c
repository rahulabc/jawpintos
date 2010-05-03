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

/* simple exit */
//static void syscall_simple_exit (struct intr_frame *f, int status);

/* pointer validity */
static bool syscall_invalid_ptr (const void *ptr);

/* find file pointer from file descriptor */
static struct file *find_file (int fd);

struct lock next_fd_lock;
void
syscall_init (void) 
{
  lock_init (&next_fd_lock);
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
      file_close (f_elem->file);
      free (f_elem);
    }
  // Need to release the lock?
  // free waited_children_list and children_list
   while (!list_empty (&t->children_list))
    {
      e = list_pop_back (&t->children_list);
      struct child_elem *c_elem = list_entry (e, struct child_elem, elem);
      // free children from the global exit_list
      free_thread_from_exit_list(c_elem->pid);
      free (c_elem);
    }
  while (!list_empty (&t->waited_children_list))
    {
      e = list_pop_back (&t->waited_children_list);
      struct child_elem *c_elem = list_entry (e, struct child_elem, elem);
      free (c_elem);
    }
  add_thread_to_exited_list (t->tid, status);
  
  // allow file write to executable...
  if (t->exec_file) 
    {
      file_allow_write ( t->exec_file );
      file_close ( t->exec_file );
    }
  thread_exit ();
  f->eax = status;
}

#define MALLOC_AND_VALIDATE(f, var, size)   \
({                                      \
  var = (typeof(var)) malloc (size);    \
  if (var == NULL)                      \
     {                                  \
       syscall_simple_exit (f, -1);     \
       return;                          \
     }                                  \
})

#define VALIDATE_AND_GET_ARG(cur_sp,var,f)   \
({ if (syscall_invalid_ptr (cur_sp))         \
     {               \
       syscall_simple_exit (f, -1);      \
       return;             \
     }               \
  var = *(typeof (var)*)cur_sp;        \
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
  //  printf("system call SYS_EXEC!\n");
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
          /* if child is already waited on before, return -1 */
          struct list_elem *waited_e;
          for (waited_e = list_begin (&t->waited_children_list); 
               waited_e != list_end (&t->waited_children_list);
               waited_e = list_next (waited_e))
            {
              struct child_elem *waited_c_elem = list_entry (waited_e, struct child_elem, elem);
              if (waited_c_elem->pid == pid) 
                {
                  f->eax = -1;
                  return;
                }
            }
          /* mark child as already waited on */
          struct child_elem *new_c_elem;
          MALLOC_AND_VALIDATE(f, new_c_elem, sizeof (struct child_elem)); 
          new_c_elem->pid = pid;
          list_push_back (&t->waited_children_list, &new_c_elem->elem);
          f->eax = process_wait (pid); // wrong status
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
      syscall_simple_exit (f, -1);
      return;
    }

  f->eax = filesys_create (file, initial_size);
}

static void 
syscall_remove (struct intr_frame *f, void *cur_sp)
{
  const char *file;
  VALIDATE_AND_GET_ARG (cur_sp, file, f);
  if (syscall_invalid_ptr (file))
    {
      syscall_simple_exit (f, -1);
      return;
    }
  f->eax = filesys_remove (file);
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
  if (!file)
    fd = -1;
  else
    {
      // need a lock, for global next_fd
      lock_acquire (&next_fd_lock);
      fd = next_fd++;
      lock_release (&next_fd_lock);
    }
  struct thread *t = thread_current ();
  struct file_elem *f_elem;
  MALLOC_AND_VALIDATE(f, f_elem, sizeof (struct file_elem)); 

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
    {
      return true;
    }
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
