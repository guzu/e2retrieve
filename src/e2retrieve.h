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

#include <stdio.h>
#include <linux/ext2_fs.h>

#include "config.h"

#define INTERNAL_ERROR_EXIT(string,errmsg) \
 { fprintf(stderr, "ERROR(%s:%u): %s%s\n", __FILE__, __LINE__, string, errmsg); exit(2); }

extern FILE *log;

typedef __off64_t long_offset;

/*********************************
 *
 *  #ifndef __STRICT_ANSI__
 *  #define LOG(str...) { if( log ) fprintf(log, str); }
 *  #else
 *  void LOG(const char *, ...);
 *  #endif
 *
 *********************************/
void LOG(const char *, ...);


/****************************
 * main.c
 ****************************/
enum fs_part_type {
  PART_TYPE_FILE,
  PART_TYPE_BLOCK,
  PART_TYPE_LVM
};

#define BLOCK_AV_MASK       0x3

#define BLOCK_AV_UNKNOWN    0x0
#define BLOCK_AV_FREE       0x1
#define BLOCK_AV_NOTFREE    0x2
#define BLOCK_AV_TRUNC      0x3

#define BLOCK_DUMP_MASK     0xC

#define BLOCK_DUMP_NULL     0x0
#define BLOCK_DUMPABLE      0x4
#define BLOCK_DUMPED        0x8

struct fs_part {
  struct fs_part    *next;
  char              *filename;
  int                fd;
  enum fs_part_type  type;
  long_offset        size;
  long_offset        max_size;
  long_offset        phys_offset;
  long_offset        logi_offset;
  unsigned int       aligned;

  unsigned long      nb_block;
  unsigned long      first_block, last_block;
  unsigned char     *block_bmp; /* each block information is represented by a quartet:
				   - two bits for the type of data (BLOCK_TYPE_* ),
				   - two bits for the availability (BLOCK_AV_*).
				*/
};
extern struct fs_part *ext2_parts;
extern char *dumpto;
extern time_t reference_date;
void part_block_bmp_set(struct fs_part *part, unsigned long block, unsigned char val);
unsigned char part_block_bmp_get(struct fs_part *part, unsigned long block);
struct fs_part *search_part_by_filename(const char *filename);



/****************************
 * superblock.c
 ****************************/
extern struct ext2_super_block superblock;
extern unsigned int nb_groups;
extern struct ext2_group_desc *group_desc; /* stocke les derniers group descriptors trouvés */
extern unsigned int block_size;

/*
extern struct group_info {
  unsigned char *block_bitmap;
  unsigned char *inode_bitmap;
} *groups_info;
*/

extern unsigned int magic_motif_len;
extern unsigned int nb_sb_found;
extern unsigned int nb_magicnum_found;
extern unsigned int sb_pool_size;

void superblock_scan(void);
int superblock_search(struct fs_part *part,
		      const unsigned char *buffer,
		      long_offset size,
		      unsigned int queue_size,
		      long_offset total_bytes);
void superblock_choose(void);
void superblock_analyse(void);
void part_create_block_bmp(struct fs_part *part);
void restore_sb_entrys(void);



/****************************
 * directory.c
 ****************************/
extern unsigned long nb_dirstub_found;
extern unsigned int dir_stub_motif_len;
void dir_scan(void);
int dir_stub_search(struct fs_part *part,
		    const unsigned char *buffer,
		    long_offset size,
		    unsigned int head_size,
		    long_offset total_bytes);
void dir_analyse(void);
void restore_dir_stubs(void);
void save_dir_stubs(void);
void dump_trees(void);
const struct dir_item *search_inode_in_trees(__u32 inode_num);
void scan_for_directory_blocks(void);



/****************************
 * inode.c
 ****************************/
enum inode_bmp_state {
  INODE_BMP_ERR,
  INODE_BMP_0,
  INODE_BMP_1
};

#define INO_STATUS_NONE    0
#define INO_STATUS_DUMPED  1
#define INO_STATUS_COMPLET 2
#define INO_STATUS_BMP_OK  4
#define INO_STATUS_BMP_SET 8

struct e2f_inode {
  unsigned short status;
  struct ext2_inode *e2i; /* as some fields are not used by e2retrieve we could
			     reduce memory usage by redefining a structure */
};
extern struct e2f_inode *inode_table;

#define	SET_INO_STATUS(i,f)   (inode_table[i-1].status |= f)
#define	UNSET_INO_STATUS(i,f) (inode_table[i-1].status &= ~(f))
#define get_inode(i)          (inode_table[i-1].e2i)

void init_inode_data(void);
void inode_display(int inode_num, struct ext2_inode *i);
int really_get_inode(unsigned int inode_num, struct ext2_inode *inode);
enum inode_bmp_state is_inode_available(unsigned int inode_num);
int inode_check(int inode_num);
int inode_read_data(const struct ext2_inode *inode,
		    unsigned char *buff,
		    long_offset offset,
		    unsigned int *size);
unsigned short inode_dump_regular_file(__u32 inode, const char *path, const struct ext2_inode *);
unsigned short inode_dump_symlink(__u32 inode_num, const char *path);
unsigned short inode_dump_node(__u32 inode_num, const char *path, __u16 type);
unsigned short inode_dump_socket(__u32 inode_num, const char *path);
void inode_search_orphans(void);
void mark_data_blocks(void);



/****************************
 * block.c
 ****************************/
struct fs_part *get_part(long_offset offset);
void mark_block(unsigned int block, struct fs_part *part, int availability, int dump_state);
int block_check(unsigned int block);
int is_block_allocated(unsigned int block);
void *block_read_data(long_offset offset, unsigned long size, void *data);

