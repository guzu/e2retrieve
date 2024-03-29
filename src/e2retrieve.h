/*
 * Copyright (C) 2003  Emmanuel VARAGNAT <e2retrieve@guzu.net>
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

#ifndef __E2RETRIEVE_H__
#define __E2RETRIEVE_H__

/* For LCLINT */
#ifdef __LCLINT__
#define __signed__ signed
#endif

#include <sys/types.h>
#include <stdio.h>
#include <stdint.h>
#include <ext2fs/ext2fs.h>

//#include "ext2_fs.h"
#include "config.h"

#define INTERNAL_ERROR_EXIT(string,errmsg) \
 do { \
   fprintf(stderr, "ERROR(%s:%u): %s%s\n", \
           __FILE__, \
	   __LINE__, \
	   string,   \
	   errmsg);  \
   exit(2);          \
 } while(0)

/*@null@*/ extern FILE *logfile;

/*typedef __off64_t         long_offset;*/
typedef uint32_t BlockNum;

struct {
  void (*init_scan)(void);
  void (*display_refdate)(const char*);
} ihm_funcs;

#define IHM_ARGS(f, ...) ((ihm_funcs.f) ? (ihm_funcs.f( __VA_ARGS__ )) : (void)0)
#define IHM_NOARGS(f) ((ihm_funcs.f) ? (ihm_funcs.f()) : (void)0)

/*********************************
 *
 *  #ifndef __STRICT_ANSI__
 *  #define LOG(str...) { if( logfile ) fprintf(logfile, str); }
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

#define DO_NOT_MARK         (unsigned char)0xFF

#define BLOCK_AV_MASK       (unsigned char)0x3

#define BLOCK_AV_UNKNOWN    (unsigned char)0x0
#define BLOCK_AV_FREE       (unsigned char)0x1
#define BLOCK_AV_NOTFREE    (unsigned char)0x2
#define BLOCK_AV_TRUNC      (unsigned char)0x3

#define BLOCK_DUMP_MASK     (unsigned char)0xC

#define BLOCK_DUMP_NULL     (unsigned char)0x0
#define BLOCK_DUMPABLE      (unsigned char)0x4
#define BLOCK_DUMPED        (unsigned char)0x8

struct fs_part {
  struct fs_part    *next;
  char              *filename;
  int                fd;
  enum fs_part_type  type;
  off_t              size;
  off_t              max_size;
  off_t              phys_offset;
  off_t              logi_offset;
  unsigned int       aligned;

  unsigned long      nb_block;
  BlockNum           first_block, last_block;
  unsigned char     *block_bmp; /* each block information is represented by a quartet:
				   - two bits for the type of data (BLOCK_TYPE_* ),
				   - two bits for the availability (BLOCK_AV_*).
				*/
};
/*@null@*/ extern struct fs_part *ext2_parts;
/*@null@*/ extern char *dumpto;
extern time_t reference_date;
extern unsigned int total_element_dumped;

void part_block_bmp_set(struct fs_part *part, BlockNum block, unsigned char val);
unsigned char part_block_bmp_get(struct fs_part *part, BlockNum block);
struct fs_part *search_part_by_filename(const char *filename);



/****************************
 * superblock.c
 ****************************/
extern struct ext2_super_block superblock;
extern unsigned int nb_groups;
extern struct ext2_group_desc *group_desc; /* stocke les derniers group descriptors trouv�s */
extern size_t block_size;

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
		      unsigned int size,
		      unsigned int queue_size,
		      off_t total_bytes);
void superblock_choose(void);
void superblock_analyse(void);
void part_create_block_bmp(struct fs_part *part);
void restore_sb_entrys(void);



/****************************
 * directory.c
 ****************************/
enum dir_stub_state {  /* order is important to sort stubs */
  OK, UNSURE, KO
};

struct dir_stub {
  off_t                offset;  /* offset in the part */
  uint32_t	       inode;
  unsigned int         parent_inode;
  enum dir_stub_state  state;

  struct fs_part      *part;
};

extern unsigned long nb_dirstub_found;
extern unsigned int dir_stub_motif_len;
void dir_scan(void);
int dir_stub_search(struct fs_part *part,
		    const unsigned char *buffer,
		    unsigned int size,
		    unsigned int head_size,
		    off_t total_bytes);
int search_directory_motif(const unsigned char *buff,
			   unsigned int buff_size,
			   unsigned int start);
void dir_analyse(void);
struct dir_item *add_dir_item(const struct dir_stub *stub);
void add_dir_entry(struct dir_item *dir, struct ext2_dir_entry_2 *entry);
void restore_dir_stubs(void);
void save_dir_stubs(void);
void dump_trees(void);
struct dir_item *search_inode_in_trees(uint32_t inode_num, struct dir_item **parent);
void scan_for_directory_blocks(void);
void rearrange_directories(void);



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
extern unsigned long nb_block_marked;

#define	SET_INO_STATUS(i,f)   (inode_table[i-1].status |= f)
#define	UNSET_INO_STATUS(i,f) (inode_table[i-1].status &= ~(f))
#define get_inode(i)          (inode_table[i-1].e2i)

void init_inode_data(void);
void inode_display(uint32_t inode_num, struct ext2_inode *i);
int really_get_inode(uint32_t inode_num, struct ext2_inode *inode);
enum inode_bmp_state is_inode_available(uint32_t inode_num);
int inode_check(uint32_t inode_num);
int inode_read_data(const struct ext2_inode *inode,
		    unsigned char *buff,
		    off_t offset,
		    unsigned int *size);
int inode_dump_regular_file(uint32_t inode, const char *path, const struct ext2_inode *);
int inode_dump_symlink(uint32_t inode_num, const char *path);
int inode_dump_node(uint32_t inode_num, const char *path, __u16 type);
int inode_dump_socket(uint32_t inode_num, const char *path);
void inode_search_orphans(void);
void mark_data_blocks(void);



/****************************
 * block.c
 ****************************/
/*@null@*/
struct fs_part *get_part_from_block(BlockNum block);
void mark_block(BlockNum block, struct fs_part *part, unsigned char availability, unsigned char dump_state);
int block_check(BlockNum block);
int is_block_allocated(BlockNum block);
/*@null@*/
unsigned char *block_read_data(off_t offset, size_t size, /*@null@*/ /*@out@*/ unsigned char *data);



/****************************
 * lib.c
 ****************************/
long find_motif(const unsigned char *meule_de_foin, size_t size_meule,
		const unsigned char *aiguille,      size_t size_aiguille);

const char *get_realpath(const char *path);
const char *offset_to_str(off_t offset);
int is_valid_char(const unsigned char ch);
/*void LOG(const char *str, ...);*/



/****************************
 * core.c
 ****************************/
extern FILE *logfile;
extern int stop_after_scan, restart_after_scan;
extern int date_mday, date_mon, date_year;

void usage(void);
void parse_cmdline(int argc, char* argv[]);
void do_it(int nbfile, char* files[]);

#endif
