#include "filesys/directory.h"
#include <stdio.h>
#include <string.h>
#include <list.h>
#include "filesys/filesys.h"
#include "filesys/inode.h"
#include <user/syscall.h>
#include "threads/malloc.h"
#include "threads/thread.h"

/* A directory. */
struct dir 
  {
    struct inode *inode;                /* Backing store. */
    off_t pos;                          /* Current position. */
  };

/* A single directory entry. */
struct dir_entry 
  {
    block_sector_t inode_sector;        /* Sector number of header. */
    char name[NAME_MAX + 1];            /* Null terminated file name. */
    bool in_use;                        /* In use or free? */
  };

/* Creates a directory with space for ENTRY_CNT entries in the
   given SECTOR.  Returns true if successful, false on failure. */
bool
dir_create (block_sector_t sector, size_t entry_cnt)
{
  if(inode_create (sector, entry_cnt * sizeof (struct dir_entry)))
    {
      struct inode *inode = inode_open (sector);
      inode_set_is_dir (inode);
      return true;
    }
  return false;
}

/* Opens and returns the directory for the given INODE, of which
   it takes ownership.  Returns a null pointer on failure. */
struct dir *
dir_open (struct inode *inode) 
{
  struct dir *dir = calloc (1, sizeof *dir);
  if (inode != NULL && dir != NULL)
    {
      dir->inode = inode;
      dir->pos = 0;
      inode_set_is_dir (inode);
      if (inode_get_inumber (inode) == ROOT_DIR_SECTOR)
        inode_set_parent_dir_sector (inode, ROOT_DIR_SECTOR);
      return dir;
    }
  else
    {
      inode_close (inode);
      free (dir);
      return NULL; 
    }
}

/* Opens the root directory and returns a directory for it.
   Return true if successful, false on failure. */
struct dir *
dir_open_root (void)
{
  return dir_open (inode_open (ROOT_DIR_SECTOR));
}

/* Opens and returns a new directory for the same inode as DIR.
   Returns a null pointer on failure. */
struct dir *
dir_reopen (struct dir *dir) 
{
  if (dir == NULL)
    return NULL;
  return dir_open (inode_reopen (dir->inode));
}

/* Destroys DIR and frees associated resources. */
void
dir_close (struct dir *dir) 
{
  if (dir != NULL)
    {
      inode_close (dir->inode);
      free (dir);
    }
}

/* Returns the inode encapsulated by DIR. */
struct inode *
dir_get_inode (struct dir *dir) 
{
  return dir->inode;
}

/* Searches DIR for a file with the given NAME.
   If successful, returns true, sets *EP to the directory entry
   if EP is non-null, and sets *OFSP to the byte offset of the
   directory entry if OFSP is non-null.
   otherwise, returns false and ignores EP and OFSP. */
static bool
lookup (const struct dir *dir, const char *name,
        struct dir_entry *ep, off_t *ofsp) 
{
  struct dir_entry e;
  size_t ofs;
  
  ASSERT (dir != NULL);
  ASSERT (name != NULL);

  for (ofs = 0; inode_read_at (dir->inode, &e, sizeof e, ofs) == sizeof e;
       ofs += sizeof e) 
    if (e.in_use && !strcmp (name, e.name)) 
      {
        if (ep != NULL)
          *ep = e;
        if (ofsp != NULL)
          *ofsp = ofs;
        return true;
      }
  return false;
}

/* Searches DIR for a file with the given NAME
   and returns true if one exists, false otherwise.
   On success, sets *INODE to an inode for the file, otherwise to
   a null pointer.  The caller must close *INODE. */
bool
dir_lookup (const struct dir *dir, const char *name,
            struct inode **inode) 
{
  struct dir_entry e;

  ASSERT (dir != NULL);
  ASSERT (name != NULL);

  if (lookup (dir, name, &e, NULL))
    *inode = inode_open (e.inode_sector);
  else
    *inode = NULL;

  return *inode != NULL;
}

/* Adds a file named NAME to DIR, which must not already contain a
   file by that name.  The file's inode is in sector
   INODE_SECTOR.
   Returns true if successful, false on failure.
   Fails if NAME is invalid (i.e. too long) or a disk or memory
   error occurs. */
bool
dir_add (struct dir *dir, const char *name, 
         block_sector_t inode_sector)
{
  struct dir_entry e;
  off_t ofs;
  bool success = false;

  ASSERT (dir != NULL);
  ASSERT (name != NULL);

  /* Check NAME for validity. */
  if (*name == '\0' || strlen (name) > NAME_MAX)
    return false;

  /* Check that NAME is not in use. */
  if (lookup (dir, name, NULL, NULL))
    goto done;

  /* Set OFS to offset of free slot.
     If there are no free slots, then it will be set to the
     current end-of-file.
     
     inode_read_at() will only return a short read at end of file.
     Otherwise, we'd need to verify that we didn't get a short
     read due to something intermittent such as low memory. */
  for (ofs = 0; inode_read_at (dir->inode, &e, sizeof e, ofs) == sizeof e;
       ofs += sizeof e) 
    if (!e.in_use)
      break;

  /* Write slot. */
  e.in_use = true;
  strlcpy (e.name, name, sizeof e.name);
  e.inode_sector = inode_sector;

  /* update parent sector */
  struct inode *child = inode_open (inode_sector);
  inode_set_parent_dir_sector (child, inode_get_inumber (dir->inode));
  inode_close (child);
 
  success = inode_write_at (dir->inode, &e, sizeof e, ofs) == sizeof e;

 done:
  return success;
}

/* Removes any entry for NAME in DIR.
   Returns true if successful, false on failure,
   which occurs only if there is no file with the given NAME. */
bool
dir_remove (struct dir *dir, const char *name) 
{
  struct dir_entry e;
  struct inode *inode = NULL;
  bool success = false;
  off_t ofs;

  ASSERT (dir != NULL);
  ASSERT (name != NULL);

  /* Find directory entry. */
  if (!lookup (dir, name, &e, &ofs))
    goto done;

  /* Open inode. */
  inode = inode_open (e.inode_sector);
  if (inode == NULL)
    goto done;

  /* Erase directory entry. */
  e.in_use = false;
  if (inode_write_at (dir->inode, &e, sizeof e, ofs) != sizeof e) 
    goto done;

  /* Remove inode. */
  inode_remove (inode);
  success = true;

 done:
  inode_close (inode);
  return success;
}

/* Reads the next directory entry in DIR and stores the name in
   NAME.  Returns true if successful, false if the directory
   contains no more entries. */
bool
dir_readdir (struct dir *dir, char name[NAME_MAX + 1])
{
  struct dir_entry e;

  while (inode_read_at (dir->inode, &e, sizeof e, dir->pos) == sizeof e) 
    {
      dir->pos += sizeof e;
      if (e.in_use)
        {
          strlcpy (name, e.name, NAME_MAX + 1);
          return true;
        } 
    }
  return false;
}

static size_t
_strip_leading_spaces (const char *full_path, char *tokens, size_t len)
{
  // strip first spaces and copy rest into tokens...
  size_t i = 0;
  for (i = 0; i < len; ++i) 
    {
      if (full_path[i] != ' ')
        break;
    } 
  size_t new_len = len - i;
  strlcpy (tokens, &full_path[i], new_len+1);
  return new_len;
}

bool 
dir_get_leaf_name (const char *full_path, char *leaf_name)
{
  size_t len = strnlen (full_path, READDIR_MAX_LEN); 
  char *tokens = (char *) malloc (len * sizeof (char) + 1);
  size_t new_len = _strip_leading_spaces (full_path, tokens, len);
  if (new_len == 0)
    {
      leaf_name[0] = '\0';
      free (tokens);
      return false;
    }
  // tokenize on /
  char *token, *save_ptr, *last_token=NULL;
  for (token = strtok_r (tokens, "/", &save_ptr); token != NULL;
       token = strtok_r (NULL, "/", &save_ptr))
    {
      if (strcmp (token, ".") == 0)
        continue;
      last_token = token;
    }
  if (last_token == NULL)
    {
      free (tokens);
      leaf_name[0] = '\0';
      return false;
    }  
  strlcpy (leaf_name, last_token, strnlen (last_token, NAME_MAX)+1);
  if (strnlen (last_token, READDIR_MAX_LEN) > NAME_MAX)
    {
      free (tokens);
      return false;
    }
  free (tokens);
  return true;
}

struct dir * 
dir_get_parent_dir (const char *full_path)
{
  size_t len = strnlen (full_path, READDIR_MAX_LEN); 
  char *tokens = (char *) malloc (len * sizeof (char) + 1);
  size_t new_len = _strip_leading_spaces (full_path, tokens, len);

  struct dir *curr_dir = NULL;

  if (tokens[0] == '/')
    curr_dir = dir_open_root ();
  else 
    {
      curr_dir = dir_open(inode_open (thread_current ()->cwd_sector));
    }

  if (new_len == 0)
    {
      free (tokens);    
      return curr_dir;
    }
  
  // tokenize on /
  struct dir *prev_dir = curr_dir;
  if (inode_get_inumber (curr_dir->inode) != ROOT_DIR_SECTOR)
    prev_dir = dir_open(inode_open (inode_get_parent_dir_sector 
                                              (curr_dir->inode)));
  char *token, *save_ptr;
  bool cleanup_and_exit = false;

  for (token = strtok_r (tokens, "/", &save_ptr); token != NULL;
       token = strtok_r (NULL, "/", &save_ptr))
    {
      if (strcmp (token, ".") == 0) 
        continue;
      struct inode *entry;
      if (!dir_lookup (curr_dir, token, &entry))
        {
	  token = strtok_r (NULL, "/", &save_ptr);
	  if (token != NULL)
	    cleanup_and_exit = true;
	  else
	    prev_dir = curr_dir;
          break;
        }
      if (!inode_is_dir (entry))
        {
          token = strtok_r (NULL, "/", &save_ptr);
          if (token != NULL)
            cleanup_and_exit = true;
	  else
	    prev_dir = curr_dir;
          break;
        }
      else 
        {
          if (prev_dir != curr_dir)
            dir_close (prev_dir);
          prev_dir = curr_dir;
          curr_dir = dir_open (entry);
          ASSERT( curr_dir != NULL);
        }
  }
  if (cleanup_and_exit) 
    {
       if (prev_dir != curr_dir)  
         dir_close (prev_dir);
       dir_close (curr_dir);
       free (tokens);
       return NULL;
    }
  if (prev_dir != curr_dir)
    dir_close (curr_dir);
  free (tokens);
  return prev_dir; 
}

bool 
dir_is_empty (struct inode *inode)
{
  struct dir *dir = dir_open (inode);
  
  struct dir_entry e;
  size_t ofs;

  for (ofs = 0; inode_read_at (dir->inode, &e, sizeof e, ofs) == sizeof e;
       ofs += sizeof e)
    if (e.in_use)
      return false;
  return true;
}

void 
dir_set_pos (struct dir *dir, off_t pos)
{
  dir->pos = pos;
}

off_t 
dir_get_pos (struct dir *dir)
{
  return dir->pos;
}


