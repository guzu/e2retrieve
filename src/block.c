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

#include <sys/types.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

#include "e2retrieve.h"
#include "lib.h"


struct fs_part *get_part(long_offset offset) {
  struct fs_part *p;

  p = ext2_parts;

  while(p) {
    /*if(p->aligned && offset > p->logi_offset && offset < (p->logi_offset + (long_offset)p->size))*/

    if(p->aligned && offset > p->phys_offset && offset < (p->phys_offset + (long_offset)p->size))
      return p;
    
    p = p->next;
  }

  return NULL;
}

void mark_block(unsigned int block, struct fs_part *part, int availability, int dump_state) {
  unsigned char val;

  if(part == NULL)	
    part = get_part((long_offset)block * (long_offset)block_size);

  if(part == NULL) /* if the block is truncated */
    part = get_part(((long_offset)(block + 1) * (long_offset)block_size) - (long_offset)1);

  if(part) {
    val = part_block_bmp_get(part, block - part->first_block);

    if(dump_state != -1) {
      if(dump_state == BLOCK_DUMPABLE) {
	availability = BLOCK_AV_NOTFREE;

	if((val & BLOCK_DUMP_MASK) != BLOCK_DUMPABLE)
	  nb_block_marked++;
      }

      val = (val & BLOCK_AV_MASK) | dump_state;
    }

    if(availability != -1)
      val = (val & BLOCK_DUMP_MASK) | availability;

    part_block_bmp_set(part, block - part->first_block, val);
  } 
}

int block_check(unsigned int block) {
  struct fs_part *p = get_part((long_offset)block * (long_offset)block_size);

  /*
    p != NULL permet de savoir si le début est dans une partie ou non
    ensuite on test si le block demandé est tronqué ou pas
  */

  return (p
	  && ((block + 1) * block_size) < (p->logi_offset + p->size));
}

int is_block_allocated(unsigned int block) {
  unsigned char bmp;
  unsigned short mask;
  long_offset offset;

  block -= superblock.s_first_data_block;

  offset = (long_offset)(group_desc[block / superblock.s_blocks_per_group].bg_block_bitmap) * (long_offset)block_size
    + (long_offset)((block % superblock.s_blocks_per_group) / 8);

  if(block_read_data(offset, 1, &bmp) == NULL)
    return -1;
  
  mask = 1 << (block % superblock.s_blocks_per_group) % 8;

  return ((mask & bmp) != 0);
}

void *block_read_data(long_offset offset, unsigned long size, void *data) {
  struct fs_part *p = get_part(offset);
  void *ret = NULL;
  int n;

  if(p == NULL || (offset + (long_offset)size) > (p->logi_offset + (long_offset)p->size)) {
    /*    printf("block_read_data: can't read data at offset %s (%p %s %lu)\n",
	    offset_to_str(offset),
	    p ,
	    offset_to_str(offset),
	    size);*/
    return NULL;
  }

  errno = 0;
  if(lseek(p->fd, p->phys_offset + (offset - p->logi_offset), SEEK_SET) == -1)
    INTERNAL_ERROR_EXIT("lseek: ", strerror(errno));

  if(data)
    ret = data;
  else {
    ret = malloc(size);
    if(ret == NULL)
      INTERNAL_ERROR_EXIT("malloc in block_read_data: ", strerror(errno));
  }
  
  errno = 0;
  if((n = read(p->fd, ret, size)) == -1)
    INTERNAL_ERROR_EXIT("read: ", strerror(errno));

  if( (unsigned long)n  != size) {
    if(data == NULL)
      free(ret);
    return NULL;
  }

  return ret;
}

