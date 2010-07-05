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
#include <sys/stat.h>
#include <sys/param.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <linux/ext2_fs.h>

#define BUFF_SIZE 8192

/* superblock */
struct ext2_super_block superblock;

struct sb_entry {
  struct ext2_super_block  sb;
  off_t                    pos;
  struct fs_part          *part;

  struct sb_entry         *next;
};
unsigned int sb_pool_size = 0;
static struct sb_entry **sb_pool = NULL;

/* groups desc */
unsigned int nb_groups;
struct ext2_group_desc *group_desc;
struct group_info *groups_info;

/* general */
size_t block_size;
unsigned int nb_sb_found, nb_magicnum_found;

static unsigned char bits_per_quartet[] = {
  0,    /* 0000 */
  1,    /* 0001 */
  1,    /* 0010 */
  2,    /* 0011 */
  1,    /* 0100 */
  2,    /* 0101 */
  2,    /* 0110 */
  3,    /* 0111 */
  1,    /* 1000 */
  2,    /* 1001 */
  2,    /* 1010 */
  3,    /* 1011 */
  2,    /* 1100 */
  3,    /* 1101 */
  3,    /* 1110 */
  4     /* 1111 */
};


static void display_superblock(struct ext2_super_block *sb) {
  /* this header can be useful to compare values of two superblocks, using in
     conjunction with 'column -t | less -S' */
  printf("\ns_inodes_count s_blocks_count s_r_blocks_count s_free_blocks_count s_free_inodes_count s_first_data_block s_log_block_size s_log_frag_size s_blocks_per_group s_frags_per_group s_inodes_per_group s_mtime s_wtime s_mnt_count s_max_mnt_count s_magic s_state s_errors s_minor_rev_level s_lastcheck s_checkinterval s_creator_os s_rev_level s_def_resuid s_def_resgid s_first_ino s_inode_size s_block_group_nr s_feature_compat s_feature_incompat s_feature_ro_compat\n");
  printf("\n%u %u %u %u %u %u %u %d %u %u %u %u %u %u %d %u %u %u %u %u %u %u %u %u %u %u %u %u %u %u %u\n",
	 sb->s_inodes_count,
	 sb->s_blocks_count,		
	 sb->s_r_blocks_count,	
	 sb->s_free_blocks_count,	
	 sb->s_free_inodes_count,	
	 sb->s_first_data_block,	
	 sb->s_log_block_size,	
	 sb->s_log_frag_size,	
	 sb->s_blocks_per_group,	
	 sb->s_frags_per_group,	
	 sb->s_inodes_per_group,	
	 sb->s_mtime,		
	 sb->s_wtime,		
	 sb->s_mnt_count,		
	 sb->s_max_mnt_count,	
	 sb->s_magic,		
	 sb->s_state,		
	 sb->s_errors,		
	 sb->s_minor_rev_level, 	
	 sb->s_lastcheck,		
	 sb->s_checkinterval,	
	 sb->s_creator_os,		
	 sb->s_rev_level,		
	 sb->s_def_resuid,		
	 sb->s_def_resgid,
	 sb->s_first_ino,
	 sb->s_inode_size,
	 sb->s_block_group_nr,
	 sb->s_feature_compat,
	 sb->s_feature_incompat,
	 sb->s_feature_ro_compat
	 );
}

/*
  arguments aren't pointers because we are modifying superblocks to easily compare them
  but fields modified could be useful for the futur.
*/
#define ZERO(x) (memset(&(x), 0, sizeof(x)))
static int superblock_compare(struct ext2_super_block sb1, struct ext2_super_block sb2) {

  ZERO(sb1.s_block_group_nr);    ZERO(sb2.s_block_group_nr);
  ZERO(sb1.s_state);             ZERO(sb2.s_state);
  ZERO(sb1.s_padding1);          ZERO(sb2.s_padding1);
  ZERO(sb1.s_reserved);          ZERO(sb2.s_reserved);
  ZERO(sb1.s_free_blocks_count); ZERO(sb2.s_free_blocks_count);
  ZERO(sb1.s_free_inodes_count); ZERO(sb2.s_free_inodes_count);
  ZERO(sb1.s_mtime);	         ZERO(sb2.s_mtime);
  ZERO(sb1.s_wtime);	         ZERO(sb2.s_wtime);
  ZERO(sb1.s_mnt_count);	 ZERO(sb2.s_mnt_count);
  ZERO(sb1.s_max_mnt_count);	 ZERO(sb2.s_max_mnt_count);
  ZERO(sb1.s_errors);	         ZERO(sb2.s_errors);
  ZERO(sb1.s_lastcheck);	 ZERO(sb2.s_lastcheck);
  ZERO(sb1.s_checkinterval);	 ZERO(sb2.s_checkinterval);

  /*
  display_superblock(&sb1);
  display_superblock(&sb2);
  */

  if(memcmp(&sb1, &sb2, sizeof(struct ext2_super_block))) {
    unsigned char *s1 = (unsigned char*)&sb1, *s2 =  (unsigned char*)&sb2;
    size_t i;

    for(i = 0; *(s1+i) == *(s2+i) && i < sizeof(struct ext2_super_block); i++);
    printf("diff %d\n", i);
  }

  return memcmp(&sb1, &sb2, sizeof(struct ext2_super_block));
  return memcmp(&(sb1.s_uuid), &(sb2.s_uuid), 16);
}

static unsigned int superblock_list_length(struct sb_entry *list) {
  struct sb_entry *p;
  unsigned int len = 0;

  for(p = list; p; p = p->next)
    len++;

  return len;
}

static void superblock_add(struct ext2_super_block *new,
			   off_t pos,
			   struct fs_part *part)
{
  struct sb_entry *new_entry;
  unsigned int i;

  errno = 0;
  if((new_entry = (struct sb_entry *) malloc(sizeof(struct sb_entry))) == NULL)
    INTERNAL_ERROR_EXIT("superblock_add: malloc", strerror(errno));

  new_entry->pos   = pos;
  new_entry->sb    = *new;
  new_entry->part  = part;
  new_entry->next  = NULL;

  if(sb_pool == NULL) {
    sb_pool_size = 1;
    sb_pool = (struct sb_entry **) malloc(sizeof(struct sb_entry *));
    sb_pool[0] = new_entry;
  } else {
    for(i = 0; i < sb_pool_size; i++) {
      if(superblock_compare(new_entry->sb, sb_pool[i]->sb) == 0) {	
	struct sb_entry *p;
	
	/* go to the end of the list */
	for(p = sb_pool[i]; p->next; p = p->next);

	/* sometimes it can happen because the magic number is found in the queue
	   that is copied at the beginning of the buffer: the first time the magic number
	   is found at the end of the buffer, and the second time at the beginning...
	 */
	if(p->pos != new_entry->pos)
	  p->next = new_entry;
	else
	  free(new_entry);

	return;
      }
    }

    /* no similar superblock were found */
    sb_pool = (struct sb_entry **) realloc(sb_pool, ++sb_pool_size * sizeof(struct sb_entry *));
    sb_pool[sb_pool_size-1] = new_entry;	
  }
}

static int superblock_advanced_tests(struct fs_part *part, off_t offset) {
  off_t cur_pos = lseek(part->fd, (off_t)0, SEEK_CUR);
  struct ext2_super_block sb;
  int magic_offset;
  ssize_t sb_size;
  
  /* little trick */
  magic_offset = (unsigned char*)&(sb.s_magic) - (unsigned char*)&sb;
  
  errno = 0;
  if( lseek(part->fd, offset - magic_offset, SEEK_SET) == -1 ) {
    printf("WARNING: lseek: %s\n", strerror(errno));
    return -1;
  }
  
  errno = 0;
  if((sb_size = read(part->fd, &sb, sizeof(struct ext2_super_block))) == -1) {
    printf("WARNING: read: %s\n", strerror(errno));
    return -1;
  }
  
  if(sb_size != sizeof(struct ext2_super_block))
    printf("\n(WARNING) superblock tronqué à l'offset %s\n", offset_to_str(offset - magic_offset));
  else {
    /*
      Vérifications diverses
      On pourrait tester plus de champs ou tester que les valeurs de certains champs
      sont bien dans une plage afin de mieux vérifier.
    */
    if(sb.s_magic == EXT2_SUPER_MAGIC)
      nb_magicnum_found++;

    if(sb.s_magic      == EXT2_SUPER_MAGIC &&
       sb.s_creator_os == EXT2_OS_LINUX &&
       sb.s_def_resuid == 0 &&
       sb.s_def_resgid == 0 &&
       sb.s_inode_size == 128 /*&&
       sb.s_lastcheck  >= (__u32)reference_date */)
      {
	/*
	unsigned int group_desc_size;
	unsigned int bsize;
	*/

	superblock_add(&sb, offset - magic_offset, part);
	nb_sb_found++;
	
	/* from super.c */
	/*	bsize = 1 << (sb.s_log_block_size + 10);*/
	/*	nb_groups = ( (sb.s_blocks_count - sb.s_first_data_block) + EXT2_BLOCKS_PER_GROUP(&sb) - 1 ) / EXT2_BLOCKS_PER_GROUP(&sb);*/
	/*
	printf("\rsuperblock trouvé à l'offset % 10ld (bloc % 8ld (%ld)) should be=%d\t blocksize=%d   group=%d/%d(%d)  ??=%d  first inode=%u\n",
	       offset  - magic_offset,
	       (offset - magic_offset) / bsize,
	       (offset - magic_offset) % bsize,
	       sb.s_block_group_nr * sb.s_blocks_per_group * bsize + ((sb.s_block_group_nr) ? sb.s_first_data_block * bsize : 1024),
	       bsize,
	       sb.s_block_group_nr,
	       nb_groups,
	       ( nb_groups + (bsize / sizeof (struct ext2_group_desc)) - 1) / (bsize / sizeof (struct ext2_group_desc)),
	       bsize / sizeof (struct ext2_group_desc),
	       sb.s_first_ino);
	*/
	
	/*
	  {
	  int k;
	  for(k = 0; k < nb_groups; k++) {
	  printf("GROUP %d : % 7d(%d) % 7d(%d) % 7d(%d)\n",
	  k,
	  gr_array[k].bg_block_bitmap, sb.s_blocks_per_group/8/EXT2_BLOCK_SIZE(&sb),
	  gr_array[k].bg_inode_bitmap, sb.s_inodes_per_group/8/EXT2_BLOCK_SIZE(&sb),
	  gr_array[k].bg_inode_table,  sb.s_inodes_per_group*sizeof(struct ext2_inode));
	  }
	  }
	*/
	
	/* taille de bloc */
	/*
	if(block_size != -1 && block_size != bsize)
	  printf("WARNING: block size is different from those found before\n");
	else
	  block_size = bsize;
	*/
	
	/* numero du premier inode */
	/*
	if(first_inode != -1 && first_inode != sb.s_first_ino)
	  printf("WARNING: first inode is different from those found before\n");
	else
	  first_inode = sb.s_first_ino;
	*/

	/* nombre d'inoeud par groupe */
	/*
	if(inodes_per_group != -1 && inodes_per_group != sb.s_inodes_per_group)
	  printf("WARNING: number of inodes per group is different from those found before\n");
	else
	  inodes_per_group = sb.s_inodes_per_group;
	*/
      }
  }

  if( lseek(part->fd, cur_pos, SEEK_SET) == -1 ) {
    printf("ADV 5\n");
    return -1;
  }

  return 0;
}

static void superblock_free_pool(void) {
  struct sb_entry *p;
  unsigned int i;

  for(i = 0; i < sb_pool_size; i++) {
    while(sb_pool[i]) {
      p = sb_pool[i];
      sb_pool[i] = p->next;
      free(p);
    }
  }
}

static unsigned char magic_motif[] = { 0x53, 0xef };
unsigned int magic_motif_len = 2;

int superblock_search(struct fs_part *part,
		      const unsigned char *buffer,
		      unsigned int size,
		      unsigned int head_size,
		      off_t total_bytes)
{
  unsigned int done;
  long pos;
  
  done = 0;
  
  while(done < (size + head_size)
	&&
	(pos = find_motif(buffer + done, ( size + head_size ) - done, magic_motif, magic_motif_len)) >= 0)
    {
      off_t offset = total_bytes + ( done + pos ) - head_size;
      
      if( superblock_advanced_tests(part, offset) == -1 )
	return -1;
      
      done += pos + 1;
    }

  return 0;
}


void part_create_block_bmp(struct fs_part *part) {
  int trunc_begin = 0, trunc_end = 0;
  
  if(part->aligned && part->block_bmp == NULL) {
    unsigned long nb_block = 0;
    off_t size;
    
    size = part->size;
    if(part->logi_offset % block_size) {
      nb_block++;
      size -= block_size - (part->logi_offset % block_size);
      trunc_begin = 1;
    }
    nb_block += (size / block_size);
    if(size % block_size) {
      nb_block++;
      trunc_end = 1;
    }

    part->nb_block = nb_block;
    part->block_bmp = (unsigned char*) calloc(nb_block/2 + ((nb_block%2) ? 1 : 0), sizeof(unsigned char));
    part->first_block = part->logi_offset / block_size;
    part->last_block = (part->logi_offset + size) / block_size
      + (((part->logi_offset + size) % block_size) ? 0 : -1);
    
    if(trunc_begin)
      part_block_bmp_set(part, 0,
			 BLOCK_DUMP_NULL | BLOCK_AV_TRUNC);
    if(trunc_end)
      part_block_bmp_set(part, nb_block-1,
			 BLOCK_DUMP_NULL | BLOCK_AV_TRUNC);
  }
}


void save_sb_entrys(struct sb_entry *sb) {
  char path[MAXPATHLEN];
  struct sb_entry *p;
  int fd;
  
  strcpy(path, dumpto);
  strcat(path, "/superblocks");

  if((fd = open(path, O_WRONLY | O_TRUNC | O_CREAT, 0666)) == -1)
    INTERNAL_ERROR_EXIT("open: ", strerror(errno));

  for(p = sb; p; p = p->next) {
    if(write(fd, p, sizeof(struct sb_entry)) == -1)
      INTERNAL_ERROR_EXIT("write: ", strerror(errno));

    if(write(fd, p->part->filename, strlen(p->part->filename)+1) == -1)
      INTERNAL_ERROR_EXIT("write: ", strerror(errno));

  }

  close(fd);    
}

void restore_sb_entrys(void) {
  struct sb_entry *tmp;
  char path[MAXPATHLEN], ch;
  int fd, i, nb = 0;
  ssize_t n;
  
  strcpy(path, dumpto);
  strcat(path, "/superblocks");

  if((fd = open(path, O_RDONLY)) == -1)
    INTERNAL_ERROR_EXIT("open: ", strerror(errno));

  sb_pool = (struct sb_entry **)malloc(sizeof(struct sb_entry *));
  sb_pool[0] = NULL;
 
  while(1) {
    if( (tmp = (struct sb_entry *) malloc(sizeof(struct sb_entry))) == NULL )
      INTERNAL_ERROR_EXIT("malloc", strerror(errno));

    if((n = read(fd, tmp, sizeof(struct sb_entry))) == -1)
      INTERNAL_ERROR_EXIT("read: ", strerror(errno));

    if(n == 0)
      break;
    else if(n != sizeof(struct sb_entry))
      INTERNAL_ERROR_EXIT("can't restore superblocks.", "");
    
    for(i = 0; i < MAXPATHLEN; i++) {
      if(read(fd, &ch, 1) == -1)
	INTERNAL_ERROR_EXIT("read: ", strerror(errno));

      path[i] = ch;
      if(!ch)
	break;
    }
    if(i >= MAXPATHLEN)
      INTERNAL_ERROR_EXIT("restore_sb_entry: part filename too long", "");

    if((tmp->part = search_part_by_filename(path)) == NULL)      
      INTERNAL_ERROR_EXIT("can't find part from filename:", path);
            
    tmp->next = sb_pool[0];
    sb_pool[0] = tmp;

    nb++;
  }

  if(nb == 0)
    INTERNAL_ERROR_EXIT("restore_sb_entry: no superblock restored", "");
    
  superblock = sb_pool[0]->sb;

  close(fd);    
}


void superblock_choose(void) {
  unsigned int i, max = 0, max_pos = 0;
  int nb_max = 0;
  struct sb_entry *p;
  
  if(sb_pool == NULL) {
    fprintf(stderr, "No superblock found\n");
    exit(1);
  }
  
  printf("Superblocks :\n");
  for(i = 0; i < sb_pool_size; i++) {
    unsigned int len;
    off_t size;
    
    len = superblock_list_length(sb_pool[i]);
    if(len > max) {
      max = len;
      max_pos = i+1;
      nb_max = 1;
    }
    else if(len == max) {
      nb_max++;
    }
    
    size = (off_t)sb_pool[i]->sb.s_blocks_count * (off_t)(1 << (sb_pool[i]->sb.s_log_block_size + 10));
    printf(" #%d (%s Ko) : copy ", i+1, offset_to_str(size / (off_t)1024));
    for(p = sb_pool[i]; p ; p = p->next)
      printf("%u ", p->sb.s_block_group_nr);
    printf("\n");
  }
  printf("Superblock #%u has been choose.\n\n", max_pos);
  
  if(nb_max > 1) {
    /*
      here we could ask user's confirmation or basically choose the biggest
      filesystem
    */
    printf("Two or more different superblocks have been found equal times\n");
    exit(1);
  }

  superblock = sb_pool[max_pos-1]->sb;
  save_sb_entrys(sb_pool[max_pos-1]);
  sb_pool[0] = sb_pool[max_pos-1];
}


void superblock_analyse(void) {
  /*
    We're checking the presence of the filetype flag, because it helps us determine
    the nature of the directory entries when scanning and analysing directories.
    Without it, directories motif would be different.
  */
  if(!superblock.s_feature_incompat & EXT2_FEATURE_INCOMPAT_FILETYPE) {
    fprintf(stderr, "Ext2 filesystem must have been created with the 'filetype' option.\n");
    exit(3);
  }
  
  nb_groups = ( (superblock.s_blocks_count - superblock.s_first_data_block)
		+ EXT2_BLOCKS_PER_GROUP(&superblock) - 1 ) / EXT2_BLOCKS_PER_GROUP(&superblock);
  
  /* from fs/ext2/super.c */
  block_size = 1 << (superblock.s_log_block_size + 10);
  
  /*
    calcul de la troncature pour chaque partie où on a trouvé
    un superblock.
    Chaque bloc de superblock rencontré est marqué comme utilisé.
  */
  {
    struct sb_entry *p;
    off_t should_be;
    unsigned long block;

    for(p = sb_pool[0]; p; p = p->next) {
      if(! p->part->aligned) {
	should_be = (p->sb.s_block_group_nr * p->sb.s_blocks_per_group * block_size)
	  + ((p->sb.s_block_group_nr) ? superblock.s_first_data_block * block_size : 1024);
	
	if(p->part->aligned &&  (should_be - p->pos) != p->part->logi_offset ) {
	  printf("WARNING: offset part determination gives differents values\n");
	  exit(2);	  
	}
	
	if(should_be - p->pos < 0) { /* FIXME: this happen with umounted ext3 partition where
					s_block_group_nr is zero for all the superblocks */
	  printf("WARNING: problem found with superblock block group number\n");
	  continue;
	}

	p->part->logi_offset = should_be - p->pos;
	p->part->aligned = 1;
		
	LOG("%s aligned to logi_offset %s\n", p->part->filename, offset_to_str(p->part->logi_offset));
      }
      
      if( ! p->part->block_bmp )
	part_create_block_bmp(p->part);
      
      /* mark block bitmap */
      block = ( p->pos + p->part->logi_offset ) / block_size;

      part_block_bmp_set(p->part, block - p->part->first_block,
			 BLOCK_AV_NOTFREE | BLOCK_DUMPABLE);
    }
  }
  
  
  /* 
     GROUP DESCRIPTORS

     Group descriptors table is backuped in each group, just after superblock (when it is
     present; see option sparse_super of mke2fs and flag EXT2_FEATURE_RO_COMPAT_SPARSE_SUPER).

     The first full table found will be OK.
     (Enhancement: we could try to recreate the table * by *, if no full table is found).
  */
  {
    struct sb_entry *p;
    unsigned long len, nbblk;
    int ok = 0;

    len = nb_groups * sizeof(struct ext2_group_desc);
    nbblk = len / block_size + ((len % block_size) ? 1 : 0);

    group_desc = (struct ext2_group_desc *) malloc(len);
    errno = 0;
    for(p = sb_pool[0]; p/* && !ok*/; p = p->next) {
      unsigned long int block, n;
      off_t gd_pos;
      ssize_t rd;

      /* mark block bitmap */
      for(n = 1; n <= nbblk; n++) {
	block = ( p->pos + p->part->logi_offset ) / block_size + n;
	part_block_bmp_set(p->part, block - p->part->first_block,
			   BLOCK_AV_NOTFREE | BLOCK_DUMPABLE);
      }

      /*      should_be = (p->sb.s_block_group_nr * p->sb.s_blocks_per_group * block_size)
	      + ((p->sb.s_block_group_nr) ? superblock.s_first_data_block * block_size : 1024);*/
      gd_pos = ((block_size == 1024) ? (p->pos + block_size) : (p->pos + block_size - 1024));

      if(lseek(p->part->fd, gd_pos, SEEK_SET) < 0)
	 INTERNAL_ERROR_EXIT("lseek", strerror(errno));

      errno = 0;
      if((rd = read(p->part->fd, group_desc, len)) == -1)
	printf("WARNING: read: %s", strerror(errno));

      if(rd > 0 && (unsigned long)rd < len)
	printf("WARNING: group descriptors table is truncated.");

      if(rd > 0 && (unsigned long)rd == len)
	 ok = 1;
    }
    
    if(ok)
	superblock_free_pool();
    else
	INTERNAL_ERROR_EXIT("ERROR: Can't found a valid group descriptor table !\n", "");
  }

  /* mark bitmap blocks, inode table and group descriptors table
     as used */
  {
    unsigned int gp, blk;

    printf("Initialize bitmaps from superblock informations : \n"); fflush(stdout);
    LOG("Initialize bitmaps from superblock informations : \n");
    for(gp = 0; gp < nb_groups; gp++) {
      unsigned int n;

      n = (nb_groups * sizeof(struct ext2_group_desc)) / block_size
	  + (((nb_groups * sizeof(struct ext2_group_desc)) % block_size) ? 1 : 0);

      for(blk = group_desc[gp].bg_block_bitmap - n; blk < group_desc[gp].bg_block_bitmap; blk++)
        mark_block(blk, NULL, BLOCK_AV_NOTFREE, BLOCK_DUMPABLE);

      mark_block(group_desc[gp].bg_block_bitmap, NULL, BLOCK_AV_NOTFREE, BLOCK_DUMPABLE);
      mark_block(group_desc[gp].bg_inode_bitmap, NULL, BLOCK_AV_NOTFREE, BLOCK_DUMPABLE);

      n = superblock.s_inodes_per_group * superblock.s_inode_size / block_size
	+ ( ((superblock.s_inodes_per_group * superblock.s_inode_size) % block_size) ? 1 : 0);

      for(blk = group_desc[gp].bg_inode_bitmap + 1; blk < group_desc[gp].bg_inode_bitmap + 1 + n; blk++)
	mark_block(blk, NULL, BLOCK_AV_NOTFREE, BLOCK_DUMPABLE);
    }
    printf("Done\n");
  }


  /* evaluate used blocks */
  if(0) {
    unsigned char *block_bmp;
    unsigned int i, j, used_block = 0;
    off_t pos;

    errno = 0;
    if( (block_bmp = (unsigned char*)malloc(block_size)) == NULL)
      INTERNAL_ERROR_EXIT("memory allocation : ", strerror(errno));

    for(i = 0; i < nb_groups; i++) {
      /* load bitmap block of the group */
      pos = (off_t)group_desc[i].bg_block_bitmap * (off_t)block_size;
      if( block_read_data(pos, block_size, block_bmp) == NULL )
	continue;

      /* count bits at 1 */
      for(j = 0; j < block_size; j++) {
	used_block +=
	  bits_per_quartet[block_bmp[j] & 0x0F] +
	  bits_per_quartet[block_bmp[j] >> 4];
      }
    }
  }

  /* initialise les bitmaps de block en mémoire */
  {
    struct fs_part *p;
    unsigned long i, set, unset, unknown;

    printf("Initialize memory bitmaps from disk bitmaps : \n"); fflush(stdout);
    LOG("Initialize memory bitmaps from disk bitmaps : \n");

    set = unset = unknown = 0;
    for(p = ext2_parts; p; p = p->next) {
      for(i = p->first_block; i <= p->last_block; i++) {
	int ret;
	
	if(i < superblock.s_first_data_block)
	  continue;
	
	if((ret = is_block_allocated(i)) == -1)
	  continue;

	/* 'part' isn't directly used because the affected block can be in
	   another part */
        mark_block(i, NULL, ((ret) ? BLOCK_AV_NOTFREE : BLOCK_AV_FREE), DO_NOT_MARK);
      }
    }
    printf("Done\n");


    /* Verification */
    if(logfile) {
      unsigned int fr, nfr, unk, trc;
      struct fs_part *part; 
      BlockNum blk;

      for(part = ext2_parts; part; part = part->next) {
	fr = nfr = unk = trc = 0;

	for(blk = part->first_block; blk <= part->last_block; blk++) {
	  unsigned char st;
	  
	  st = part_block_bmp_get(part, blk - part->first_block);
	  switch (st & BLOCK_AV_MASK) {
	  case BLOCK_AV_UNKNOWN: unk++; break;
	  case BLOCK_AV_FREE:    fr++;  break;
	  case BLOCK_AV_NOTFREE: nfr++; break;
	  case BLOCK_AV_TRUNC:   trc++; break;
	  }
	}
	LOG(" Verif : %s\n",  part->filename);
	LOG("   total = %lu, free = %u, used = %u, trunc = %u, unknown = %u\n",
	    part->nb_block, fr, nfr, trc, unk);
      }
    }
  }
}
