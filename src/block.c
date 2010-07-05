/*
 * Copyright (C) 2003  Emmanuel VARAGNAT <coredump@free.fr>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

#include "e2retrieve.h"

#include <sys/types.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

struct fs_part *get_part_from_block(uint32_t block) {
  struct fs_part *p;

  p = ext2_parts;

  for(p = ext2_parts; p; p = p->next) {
    if(block >= p->first_block && block <= p->last_block)
      return p;
  }

  return NULL;
}

/*@null@*/
static 
struct fs_part *get_part_from_offset(off_t offset) {
  struct fs_part *p;

  p = ext2_parts;

  while(p) {
#ifdef POURMONPROBLEME
    /* FIX my problem */
    if((p->aligned) && (offset > p->phys_offset) && (offset < (p->phys_offset + (off_t)p->size)))
#else
    if((p->aligned) && (offset > p->logi_offset) && (offset < (p->logi_offset + (off_t)p->size)))
#endif
      return p;
    
    p = p->next;
  }

  return NULL;
}

void mark_block(uint32_t block,
		struct fs_part *part,
		unsigned char availability,
		unsigned char dump_state)
{
  unsigned char val;

  if(part == NULL)	
    part = get_part_from_block(block);

  if(part) {
    val = part_block_bmp_get(part, block - part->first_block);

    if(dump_state != DO_NOT_MARK) {
      if(dump_state == BLOCK_DUMPABLE) {
	availability = BLOCK_AV_NOTFREE;

	if((val & BLOCK_DUMP_MASK) != BLOCK_DUMPABLE)
	  nb_block_marked++;
      }

      val = (val & BLOCK_AV_MASK) | dump_state;
    }

    if(availability != DO_NOT_MARK)
      val = (val & BLOCK_DUMP_MASK) | availability;

    part_block_bmp_set(part, block - part->first_block, val);
  } 
}

int block_check(uint32_t block) {
  struct fs_part *p = get_part_from_block(block);

  /*
    p != NULL permet de savoir si le début est dans une partie ou non
    ensuite on test si le block demandé est tronqué ou pas
  */

  return (int)(p
	       && ((block + 1) * block_size) < (p->logi_offset + p->size));
}

int is_block_allocated(uint32_t block) {
  unsigned char bmp;
  unsigned short mask;
  off_t offset;

  block -= superblock.s_first_data_block;

  offset = (off_t)(group_desc[block / superblock.s_blocks_per_group].bg_block_bitmap) * (off_t)block_size
    + (off_t)((block % superblock.s_blocks_per_group) / 8);

  if(block_read_data(offset, 1, &bmp) == NULL)
    return -1;
  
  mask = (unsigned short)(1 << (block % superblock.s_blocks_per_group) % 8);

  return (int)((mask & bmp) != (unsigned char)0);
}


unsigned char *block_read_data(off_t offset, size_t size, unsigned char *data) {
  struct fs_part *p = get_part_from_offset(offset);
  unsigned char *ret = NULL;
  ssize_t n;

  if(p == NULL || (offset + (off_t)size) > (p->logi_offset + (off_t)p->size)) {
    /*    printf("block_read_data: can't read data at offset %s (%p %s %lu)\n",
	    offset_to_str(offset),
	    p ,
	    offset_to_str(offset),
	    size);*/
    return NULL;
  }

  /*
  printf("block_read_data : offset=%lld size=%d\n", offset, size);
  printf("block_read_data : lseek to %lld\n", p->phys_offset + (offset - p->logi_offset));
  */

  errno = 0;
  if(lseek(p->fd, p->phys_offset + (offset - p->logi_offset), SEEK_SET) == -1)
    INTERNAL_ERROR_EXIT("lseek: ", strerror(errno));

  if(data != NULL)
    ret = data;
  else {
    ret = malloc(size);
    if(ret == NULL)
      INTERNAL_ERROR_EXIT("malloc in block_read_data: ", strerror(errno));
  }
  
  errno = 0;
  if((n = read(p->fd, ret, size)) == -1)
    INTERNAL_ERROR_EXIT("read: ", strerror(errno));

  if( (size_t)n != size) {
    if(data == NULL)
      free(ret);
    return NULL;
  }

  return ret;
}

