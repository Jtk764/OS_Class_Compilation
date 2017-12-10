#include "filesys/inode.h"
#include <list.h>
#include <debug.h>
#include <round.h>
#include <string.h>
#include "filesys/filesys.h"
#include "filesys/free-map.h"
#include "threads/malloc.h"

/* Identifies an inode. */
#define INODE_MAGIC 0x494e4f44

/*
4 Direct Blocks, each points to 512 bytes
9 Indirect Blocks, each points to 128 pointers
                    each points to 512 bytes
1 Double Indirect Block, points to 128 pointers, 
                          each points to 128 pointers
                            each points to 512 bytes

4*512 + 9*128*512 +   128*128*512 = 8980480 bytes
                                = 8770 kilobytes
                              = 8MB
*/

/* On-disk inode.
   Must be exactly BLOCK_SECTOR_SIZE bytes long. */
struct inode_disk
  {
//    block_sector_t start;               /* First data sector. */
    off_t length;                       /* File size in bytes. */
    unsigned magic;                     /* Magic number. */
    uint32_t unused[125 + 1 - 14 - 3];               /* Not used. */

    uint32_t dir_index;
    uint32_t indir_index;
    uint32_t dindir_index;
    block_sector_t blocks[14];
  };

/* Returns the number of sectors to allocate for an inode SIZE
   bytes long. */
static inline size_t
bytes_to_sectors (off_t size)
{
  return DIV_ROUND_UP (size, BLOCK_SECTOR_SIZE);
}

/* In-memory inode. */
struct inode
  {
    struct list_elem elem;              /* Element in inode list. */
    block_sector_t sector;              /* Sector number of disk location. */
    int open_cnt;                       /* Number of openers. */
    bool removed;                       /* True if deleted, false otherwise. */
    int deny_write_cnt;                 /* 0: writes ok, >0: deny writes. */
    struct inode_disk data;             /* Inode content. */

    off_t length;
    off_t readTil;
    uint32_t dir_index;
    uint32_t indir_index;
    uint32_t dindir_index;
    block_sector_t blocks[14];
    
  };

/* Returns the block device sector that contains byte offset POS
   within INODE.
   Returns -1 if INODE does not contain data for a byte at offset
   POS. */
static block_sector_t
byte_to_sector (const struct inode *inode, off_t pos)
{
  uint32_t ind;
  uint32_t b[128]
  ASSERT (inode != NULL);
  if(pos < inode->length){
    if (pos < (4*BLOCK_SECTOR_SIZE)){
      //Direct block
      ind = pos/BLOCK_SECTOR_SIZE;
      return inode->blocks[ind]
    }

    else if(pos < (4 + 9*128)*BLOCK_SECTOR_SIZE){
      //indirect block
      pos -= 4*BLOCK_SECTOR_SIZE;
      //get the indirect block and put into b
      ind = 4 + pos/(128*BLOCK_SECTOR_SIZE);
      block_read(fs_device, inode->blocks[ind], &b);
      //get the indirect pointer 
      pos %= 128*BLOCK_SECTOR_SIZE;
      return b[pos/BLOCK_SECTOR_SIZE];
    }

    else {
      //double indirect block
      //put the first level of indirect pointers into b
      block_read(fs_device, inode->blocks[11], &b);

      pos -= ((4 + 9*128) * BLOCK_SECTOR_SIZE);
      //put the second level on indirect pointers into b
      ind = pos / (128 * BLOCK_SECTOR_SIZE);
      block_read(fs_device, inode->blocks[ind], &b);
      //return pointer to sectors
      pos %= (128 * BLOCK_SECTOR_SIZE);
      return b[pos/BLOCK_SECTOR_SIZE];

    }
  }
  
  else
    return -1;
}

/* List of open inodes, so that opening a single inode twice
   returns the same `struct inode'. */
static struct list open_inodes;

/* Initializes the inode module. */
void
inode_init (void)
{
  list_init (&open_inodes);
}

/* Initializes an inode with LENGTH bytes of data and
   writes the new inode to sector SECTOR on the file system
   device.
   Returns true if successful.
   Returns false if memory or disk allocation fails. */
bool
inode_create (block_sector_t sector, off_t length)
{
  struct inode_disk *disk_inode = NULL;
  bool success = false;
  struct inode *inode = NULL;

  ASSERT (length >= 0);

  /* If this assertion fails, the inode structure is not exactly
     one sector in size, and you should fix that. */
  ASSERT (sizeof *disk_inode == BLOCK_SECTOR_SIZE);

  disk_inode = calloc (1, sizeof *disk_inode);
  if (disk_inode != NULL)
    {
//      size_t sectors = bytes_to_sectors (length);
      disk_inode->length = length;
      disk_inode->magic = INODE_MAGIC;

/*
      if (free_map_allocate (sectors, &disk_inode->start))
        {
          block_write (fs_device, sector, disk_inode);
          if (sectors > 0)
            {
              static char zeros[BLOCK_SECTOR_SIZE];
              size_t i;

              for (i = 0; i < sectors; i++)
                block_write (fs_device, disk_inode->start + i, zeros);
            }
          success = true;
        }
*/
      //use this inode initialized to zero to hold what disk inode should hold
      //populate it, then copy it to the disk structure, then write the disk inode to disk
      inode = calloc(1, sizeof *inode);
      formatInode(&inode, length);
      disk_inode->dir_index = inode->dir_index;
      disk_inode->indir_index = inode->indir_index;
      disk_inode->dindir_index = inode->dindir_index;
      memcpy(&inode_disk->blocks, &inode->blocks, 14*sizeof(block_sector_t));
      block_write(fs_device, sector, disk_inode);
      success = true;
      free (disk_inode);
      free(inode);
    }
  return success;
}


//populates an inode of size length, adding zeroed out sectors until it is the write size
off_t formatInode(struct inode *inode, off_t length){
  static char zeros[BLOCK_SECTOR_SIZE];
  //check how many sectors are in inode compared to how many should be
  size_t sectors = bytes_to_sectors(length) - bytes_to_sectors(inode->length);

  //if no sectors to add, return length of inode (also length param)
  if(sectors == 0) return length;

  //fill first four direct blocks
  while(inode->dir_index < 4 && sectors != 0){
    free_map_allocate(1, &inode->blocks[inode->dir_index]);
    block_write(fs_device, inode->blocks[inode->dir_index], zeros);
    inode->dir_index++;
    sectors--;
  }

  //If more sectors, fill next 9 indirect blocks
  while(inode->dir_index < 13 && sectors != 0){
    block_sector_t levelOne[128];

    //read the first level of pointers, or allocate first level if doesnt exist
    if(inode->indir_index == 0) free_map_allocate(1, &inode->blocks[inode->dir_index]);
    else block_read(fs_device, inode->blocks[inode->dir_index], &levelOne);

    while(inode->indir_index < 128 && sectors != 0){
      free_map_allocate(1, &levelOne[inode->indir_index]);
      block_write(fs_device, levelOne[inode->indir_index], zeros);
      inode->indir_index++;
      sectors--;
    }

    //filled up last block of 128 pointers, or done growing file
    //write pointers back to inode block
    block_write(fs_device, inode->blocks[inode->dir_index], &levelOne)
    //if last block was full, move to next indirect block
    if(inode->indir_index == 128){
      inode->indir_index = 0;
      inode->dir_index++;
    }
  }

  //If still more sectors, start filling up the double indirect block
  if(inode->dir_index == 13 && sectors != 0){
    block_sector_t levelOne[128];
    block_sector_t levelTwo[128];

    //read or allocate first level of pointers
    if (inode->dindir_index == 0 && inode->dindir_index == 0) free_map_allocate(1, &inode->blocks[inode->dir_index]);
    else block_read(fs_device, inode->blocks[inode->dir_index], &levelOne);

    // start filling level one
    while (inode->indir_index < 128 && sectors != 0)
    {
      // read or allocate second level 
      if (inode->dindir_index == 0)
        free_map_allocate(1, &levelOne[inode->indir_index]);
      else
        block_read(fs_device, levelOne[inode->dindir_index], &levelTwo);

      // expand
      while (inode->dindir_index < 128 && sectors != 0)
      {
        free_map_allocate(1, &levelTwo[inode->dindir_index]);
        block_write(fs_device, levelTwo[inode->dindir_index], zeros);
        inode->dindir_index++;
        sectors--;
      }

      // write back level two block
      block_write(fs_device, levelOne[inode->indir_index], &levelTwo);

      // go to new level two block
      if (inode->dindir_index == 128)
      {
        inode->dindir_index = 0;
        inode->indir_index++;
      }
    }

    // write back level one block
    block_write(fs_device, inode->blocks[inode->direct_index], &level_one);
  }

}

/* Reads an inode from SECTOR
   and returns a `struct inode' that contains it.
   Returns a null pointer if memory allocation fails. */
struct inode *
inode_open (block_sector_t sector)
{
  struct list_elem *e;
  struct inode *inode;

  /* Check whether this inode is already open. */
  for (e = list_begin (&open_inodes); e != list_end (&open_inodes);
       e = list_next (e))
    {
      inode = list_entry (e, struct inode, elem);
      if (inode->sector == sector)
        {
          inode_reopen (inode);
          return inode;
        }
    }

  /* Allocate memory. */
  inode = malloc (sizeof *inode);
  if (inode == NULL)
    return NULL;

  /* Initialize. */
  list_push_front (&open_inodes, &inode->elem);
  inode->sector = sector;
  inode->open_cnt = 1;
  inode->deny_write_cnt = 0;
  inode->removed = false;
  block_read (fs_device, inode->sector, &inode->data);

  //inode contains disk data
  inode->length = inode->data->length;
  inode->dir_index = inode->data->dir_index;
  inode->indir_index = inode->data->indir_index;
  inode->dindir_index = inode->data->dindir_index;
  memcpy(&inode->blocks, &inode->data->blocks, 14 * sizeof(block_sector_t));

  return inode;
}

/* Reopens and returns INODE. */
struct inode *
inode_reopen (struct inode *inode)
{
  if (inode != NULL)
    inode->open_cnt++;
  return inode;
}

/* Returns INODE's inode number. */
block_sector_t
inode_get_inumber (const struct inode *inode)
{
  return inode->sector;
}

/* Closes INODE and writes it to disk.
   If this was the last reference to INODE, frees its memory.
   If INODE was also a removed inode, frees its blocks. */
void
inode_close (struct inode *inode)
{
  size_t index = 0;
  size_t sectors;
  block_sector_t levelOne[128];
  block_sector_t levelTwo[128];
  /* Ignore null pointer. */
  if (inode == NULL)
    return;

  /* Release resources if this was the last opener. */
  if (--inode->open_cnt == 0)
    {
      /* Remove from inode list and release lock. */
      list_remove (&inode->elem);

      /* Deallocate blocks if removed. */
      if (inode->removed)
        {
          free_map_release (inode->sector, 1);
          sectors = bytes_to_sectors(inode->length);

          if(sectors == 0) return;

          //free direct pointers on bitmap
          while(index < 4 && sectors != 0){
            free_map_release(inode->blocks[index], 1);
            sectors--;
            index++;
          }
          //free indirect pointers on bitmap
          while(inode->dir_index >= 4 && index<13 && sectors != 0){
            size_t blockSize;
            if(sectors >= 128) blockSize = 128;
            else blockSize = sectors;

            //read level one
            block_read(fs_device, inode->blocks[index], &levelOne);

            size_t i = 0;
            while(i < blockSize){
              free_map_release(levelOne[i]);
              sectors--;
              i++;
            }

            free_map_release(inode->blocks[index], 1);
            index++;
          }

          //free double indirect pointers on bitmap
          if(inode->dir_index == 13){
            block_read(fs_device, inode->blocks[13], &levelOne);
            size_t indBlockSize = DIV_ROUND_UP(sectors, 128);
            size_t i = 0;
            

            while(i<indBlockSize){
              size_t blockSize;
              size_t j = 0;
              if(sectors >= 128) blockSize = 128;
              else blockSize = sectors;

              while(j < blockSize){
                free_map_release(levelTwo[i], 1);
                sectors--;
                j++;
              }
              free_map_release(levelOne[i], 1);
              i++;
            }

            free_map_release(inode->blocks[13], 1);

          }

          /*
          free_map_release (inode->data.start,
                            bytes_to_sectors (inode->data.length));
          */
        }
        else{
          inode->data->length = inode->length;
          inode->data->dir_index = inode->dir_index;
          inode->data->indir_index = inode->indir_index;
          inode->data->dindir_index = inode->dindir_index;
          memcpy(&inode->data->blocks, &inode->blocks, 14*sizeof(BLOCK_SECTOR_SIZE));
          block_write(fs_device, inode->sector, &inode->data); 
        }
        free (inode);
    }
}

/* Marks INODE to be deleted when it is closed by the last caller who
   has it open. */
void
inode_remove (struct inode *inode)
{
  ASSERT (inode != NULL);
  inode->removed = true;
}

/* Reads SIZE bytes from INODE into BUFFER, starting at position OFFSET.
   Returns the number of bytes actually read, which may be less
   than SIZE if an error occurs or end of file is reached. */
off_t
inode_read_at (struct inode *inode, void *buffer_, off_t size, off_t offset)
{
  uint8_t *buffer = buffer_;
  off_t bytes_read = 0;
  uint8_t *bounce = NULL;

  if(offset > inode->readTil) return 0;

  while (size > 0)
    {
      /* Disk sector to read, starting byte offset within sector. */
      block_sector_t sector_idx = byte_to_sector (inode, offset);
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode_length (inode) - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually copy out of this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;

      if (sector_ofs == 0 && chunk_size == BLOCK_SECTOR_SIZE)
        {
          /* Read full sector directly into caller's buffer. */
          block_read (fs_device, sector_idx, buffer + bytes_read);
        }
      else
        {
          /* Read sector into bounce buffer, then partially copy
             into caller's buffer. */
          if (bounce == NULL)
            {
              bounce = malloc (BLOCK_SECTOR_SIZE);
              if (bounce == NULL)
                break;
            }
          block_read (fs_device, sector_idx, bounce);
          memcpy (buffer + bytes_read, bounce + sector_ofs, chunk_size);
        }

      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_read += chunk_size;
    }
  free (bounce);

  return bytes_read;
}

/* Writes SIZE bytes from BUFFER into INODE, starting at OFFSET.
   Returns the number of bytes actually written, which may be
   less than SIZE if end of file is reached or an error occurs.
   (Normally a write at end of file would extend the inode, but
   growth is not yet implemented.) */
off_t
inode_write_at (struct inode *inode, const void *buffer_, off_t size,
                off_t offset)
{
  const uint8_t *buffer = buffer_;
  off_t bytes_written = 0;
  uint8_t *bounce = NULL;

  if (inode->deny_write_cnt)
    return 0;

  if(offset + size > inode_length(inode)){
    inode->length = formatInode(inode, offset+size);
  }

  while (size > 0)
    {
      /* Sector to write, starting byte offset within sector. */
      block_sector_t sector_idx = byte_to_sector (inode, offset);
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode_length (inode) - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually write into this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;

      if (sector_ofs == 0 && chunk_size == BLOCK_SECTOR_SIZE)
        {
          /* Write full sector directly to disk. */
          block_write (fs_device, sector_idx, buffer + bytes_written);
        }
      else
        {
          /* We need a bounce buffer. */
          if (bounce == NULL)
            {
              bounce = malloc (BLOCK_SECTOR_SIZE);
              if (bounce == NULL)
                break;
            }

          /* If the sector contains data before or after the chunk
             we're writing, then we need to read in the sector
             first.  Otherwise we start with a sector of all zeros. */
          if (sector_ofs > 0 || chunk_size < sector_left)
            block_read (fs_device, sector_idx, bounce);
          else
            memset (bounce, 0, BLOCK_SECTOR_SIZE);
          memcpy (bounce + sector_ofs, buffer + bytes_written, chunk_size);
          block_write (fs_device, sector_idx, bounce);
        }

      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_written += chunk_size;
    }
  free (bounce);
  inode->readTil = inode_length(inode);

  return bytes_written;
}

/* Disables writes to INODE.
   May be called at most once per inode opener. */
void
inode_deny_write (struct inode *inode)
{
  inode->deny_write_cnt++;
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
}

/* Re-enables writes to INODE.
   Must be called once by each inode opener who has called
   inode_deny_write() on the inode, before closing the inode. */
void
inode_allow_write (struct inode *inode)
{
  ASSERT (inode->deny_write_cnt > 0);
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
  inode->deny_write_cnt--;
}

/* Returns the length, in bytes, of INODE's data. */
off_t
inode_length (const struct inode *inode)
{
  return inode->data.length;
}
