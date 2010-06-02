#include "filesys/filesys.h"
#include <debug.h>
#include <stdio.h>
#include <string.h>
#include <user/syscall.h>
#include "filesys/file.h"
#include "filesys/free-map.h"
#include "filesys/inode.h"
#include "filesys/directory.h"
#include "filesys/cache.h"
#include "threads/thread.h"

/* Partition that contains the file system. */
struct block *fs_device;

static void do_format (void);

/* Initializes the file system module.
   If FORMAT is true, reformats the file system. */
void
filesys_init (bool format) 
{
  fs_device = block_get_role (BLOCK_FILESYS);
  if (fs_device == NULL)
    PANIC ("No file system device found, can't initialize file system.");

  inode_init ();
  free_map_init ();
  cache_init ();

  if (format) 
    do_format ();

  free_map_open ();

  /* Couldn't add to thread_init because we need inode_init completed
     before we can get access to root directory */
  thread_current ()->cwd_sector = ROOT_DIR_SECTOR;
}

/* Shuts down the file system module, writing any unwritten data
   to disk. */
void
filesys_done (void) 
{
  cache_flush_all ();
  free_map_close ();
}

/* Opens the file with the given NAME.
   Returns the new file if successful or a null pointer
   otherwise.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
struct file *
filesys_open (const char *name)
{
  if (strnlen (name, READDIR_MAX_LEN) == 0)
    return NULL;

  struct dir *parent_dir = dir_reopen(dir_get_parent_dir (name));
  if (parent_dir == NULL)
    return NULL;

  struct inode *inode = NULL;
  
  char leaf_name[NAME_MAX + 1];
  if (!dir_get_leaf_name (name, leaf_name) &&
      strnlen(leaf_name, NAME_MAX) == 0)
    {
      inode = inode_reopen (dir_get_inode (parent_dir));
      dir_close (parent_dir);
      return file_open (inode);
    }

  if (parent_dir != NULL)
    dir_lookup (parent_dir, leaf_name, &inode);
  dir_close (parent_dir);

  return file_open (inode);
}

/* Deletes the file named NAME.
   Returns true if successful, false on failure.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
bool
filesys_remove (const char *name) 
{
  struct dir *dir = dir_open_root ();
  bool success = dir != NULL && dir_remove (dir, name);
  dir_close (dir); 

  return success;
}

/* Creates a file named NAME with the given INITIAL_SIZE.
   Returns true if successful, false otherwise.
   Fails if a file named NAME already exists,
   or if internal memory allocation fails. */
static bool 
_filesys_create (const char *full_path, off_t initial_size, 
		 bool is_dir)
{
  char leaf_name[NAME_MAX + 1];
  if (!dir_get_leaf_name (full_path, leaf_name))
    return false;

  struct dir *parent_dir = dir_get_parent_dir (full_path);

  if (parent_dir == NULL)
    return false;

  block_sector_t inode_sector = 0;
  if (!free_map_allocate_one (&inode_sector))
    {
      dir_close (parent_dir);
      return false;
    }
  bool success = is_dir? dir_create (inode_sector, 0) : 
                         inode_create (inode_sector, initial_size);
  if (!success)
    {
      free_map_release (inode_sector, 1);
      dir_close (parent_dir);
      return false;
    }
  if (!dir_add (parent_dir, leaf_name, inode_sector))
    {
      inode_remove (inode_open (inode_sector));
      free_map_release (inode_sector, 1);
      dir_close (parent_dir);
      return false;
    }
  dir_close (parent_dir);
  return true;
}

bool filesys_create (const char *full_path, off_t initial_size)
{
  return _filesys_create (full_path, initial_size, false);
}

bool filesys_mkdir (const char *full_path)
{
  return _filesys_create (full_path, 0, true);
}

bool filesys_chdir (const char *full_path)
{
  char leaf_name[NAME_MAX + 1];
  if (!dir_get_leaf_name (full_path, leaf_name))
    return false;

  struct dir *parent_dir = dir_get_parent_dir (full_path);
  if (parent_dir == NULL)
    return false;
  struct inode *tmp;
  if (!dir_lookup (parent_dir, leaf_name, &tmp))
    return false;
  if (!inode_is_dir (tmp))
    return false;
  struct dir *actual_dir = dir_open (tmp);
  thread_current()->cwd_sector = inode_get_inumber(dir_get_inode (actual_dir));
  dir_close (actual_dir);
  return true;
}

/* Formats the file system. */
static void
do_format (void)
{
  printf ("Formatting file system...");
  free_map_create ();
  if (!dir_create (ROOT_DIR_SECTOR, 16))
    PANIC ("root directory creation failed");
  free_map_close ();
  printf ("done.\n");
}
