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

#include "e2retrieve.h"

#include <sys/types.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <time.h>
#include <utime.h>
#include <libgen.h>
#include <assert.h>
#include <errno.h>

#include "ext2_defs.h"

#ifdef MIN
#undef MIN
#endif
#define MIN(a,b) ( (a)<(b) ? (a) : (b) )


static BlockNum max_indir_1, max_indir_2, max_indir_3;
static unsigned int nb_indir_per_block;
static BlockNum pow_block[3];
unsigned long nb_block_marked;
struct e2f_inode *inode_table;

void inode_display(uint32_t inode_num, struct ext2_inode *i) {
  printf("INODE %d\n"
	 "  mode \t %u\n"
	 "  uid \t %u\n"
	 "  gid \t %u\n"
	 "  size \t %u\n"
	 "  nlinks \t %u\n"
	 "  nblocks \t %u\n"
	 "  flags \t %x\n"
	 "  atime \t %u\n"
	 "  ctime \t %u\n"
	 "  mtime \t %u\n"
	 "  dtime \t %u\n",
	 inode_num, 
	 i->i_mode,
	 i->i_uid,
	 i->i_gid,
	 i->i_size,
	 i->i_links_count,
	 i->i_blocks,
	 i->i_flags,
	 i->i_atime,
	 i->i_ctime,
	 i->i_mtime,
	 i->i_dtime);
}

/***************************************************
 * Description:
 *
 ****************/
static int inode_check_indirection(uint32_t block, int level, BlockNum *nb_blocks) {
  uint32_t *ind_block;
  size_t len, i;

  if(level == 0) {
    *nb_blocks -= 1;
    return (block_check(block)) ? 0 : -1;
  }

  if(block_check(block) == 0) {
    return -1;
  }

  if((ind_block = (uint32_t*) block_read_data((off_t)block * (off_t)block_size, block_size, NULL)) == NULL)
    return -1;
  len = block_size / sizeof(uint32_t);

  for(i = 0; i < len && *nb_blocks; i++) {
    if( inode_check_indirection(ind_block[i], level-1, nb_blocks) == -1 ) {
      free(ind_block);
      return -1;
    }
  }

  free(ind_block);

  return 0;
}


enum inode_bmp_state is_inode_available(uint32_t inode_num) {
  unsigned int group;
  unsigned int bit, pos, shift;
  unsigned char mask, octet;
  
  group = (inode_num - 1) / superblock.s_inodes_per_group;

  bit = (inode_num - 1) % superblock.s_inodes_per_group;
  pos = bit >> 3;
  shift = bit & 0x07;
  
  mask = (unsigned char)1 << shift;
  
  if(block_read_data((off_t)group_desc[group].bg_inode_bitmap * (off_t)block_size + (off_t)pos,
		     1, &octet) == NULL)
    return INODE_BMP_ERR;

  return (octet & mask) ? INODE_BMP_1 : INODE_BMP_0;
}


/***************************************************
 * Description:
 *
 ****************/

int really_get_inode(uint32_t inode_num, struct ext2_inode *inode) {
  unsigned int group;
  off_t offset;
  unsigned char *ret;

  group = (inode_num - 1) / superblock.s_inodes_per_group;

  if( group >= nb_groups )
    INTERNAL_ERROR_EXIT("really_get_inode : group inconsistency\n", "" );

  offset = ((inode_num - 1) % superblock.s_inodes_per_group) * sizeof(struct ext2_inode);
  
  ret = block_read_data((off_t)group_desc[group].bg_inode_table * (off_t)block_size + (off_t)offset,
			sizeof(struct ext2_inode), (unsigned char*)inode);

  if(ret == NULL) {
    /*fprintf(stderr, "really_get_inode: can't read inode %d entry\n", inode_num);*/
    return 0;
  }
  
  return 1;
}

/***************************************************
 * Description:
 *
 ****************/
int inode_check(uint32_t inode_num) {
  struct ext2_inode *inode;
  unsigned int i;
  BlockNum nb_blocks;

  inode = get_inode(inode_num);

  if(inode == NULL)
    return -1;

  /*
    L'inode n'est plus utilis�
  */
  if(inode->i_links_count == 0) {
    /* we don't care about special inodes (as bad block, ACL, boot loader and undelete directory inodes) */
    if(inode_num > 10)
      printf("Unused inode (link count = 0)\n");
     return -1;
  }
  
  
  if(LINUX_S_ISLNK(inode->i_mode) && inode->i_blocks == 0)
    return 0;
  
  /*
    V�rifie la pr�sence de tout les blocs
  */
  nb_blocks = (inode->i_size / block_size) + ((inode->i_size % block_size) ? 1 : 0);

  for(i = 0; i < EXT2_NDIR_BLOCKS && nb_blocks && inode->i_block[i]; i++) {
    if(block_check(inode->i_block[i]) == 0) {
      printf("%u\n", i);
      return -2;
    }
    nb_blocks--;
  }
  
  /* on g�re les blocs d'indirection */
  if(nb_blocks && i >= EXT2_NDIR_BLOCKS && inode->i_block[i]) {
    /*
    printf("size %d, blocks %d, nb_blocks %d (%ld %d)\n", inode->i_size, inode->i_blocks, nb_blocks,
	   (inode->i_size / block_size),
	   ((inode->i_size % block_size) ? 1 : 0));
    */

    assert(inode->i_block[i] != 0);

    if(inode_check_indirection(inode->i_block[EXT2_IND_BLOCK], 1, &nb_blocks) == -1)
      return -3;

    if(nb_blocks)
      if(inode_check_indirection(inode->i_block[EXT2_DIND_BLOCK], 2, &nb_blocks) == -1)
	return -4;

    if(nb_blocks)
      if(inode_check_indirection(inode->i_block[EXT2_TIND_BLOCK], 3, &nb_blocks) == -1)
	return -5;
  }

  /*
    printf("UID : %d\nGID : %d\nmode: %o\nlinks : %d\n",
    inode->i_uid,
    inode->i_gid,
    inode->i_mode,
    inode->i_links_count
    );
  */

  return 0;
}


/***************************************************
 * Description: 
 *
 ****************/
static int inode_get_indir_block(int level,
				 BlockNum indir_block,
				 BlockNum block,
				 int mark,
				 uint32_t *ret_block)
{
  BlockNum indir, rest;
  unsigned char *ret;
  uint32_t val;
  off_t offset;

  indir = block / pow_block[level];
  rest = block % pow_block[level];

  /*printf("inode_get_indir_block: indir_block=%u level=%d %lu %lu\n", indir_block, level, indir, rest);*/

  offset = ((off_t)indir_block * (off_t)block_size) + (off_t)(indir * sizeof(uint32_t));
  ret = block_read_data( offset, sizeof(uint32_t), (unsigned char*)&val);
  if(ret == NULL)
    return 0;

  if(mark)
    mark_block(indir_block, NULL, DO_NOT_MARK, BLOCK_DUMPABLE);

  if(level == 0) {
    *ret_block = val;

    if(mark)
      mark_block(*ret_block, NULL, DO_NOT_MARK, BLOCK_DUMPABLE);

    return 1;
  }

  return inode_get_indir_block(level - 1, val, rest, mark, ret_block);
}


/***************************************************
 * Description: 
 *
 ****************/
static int inode_get_block(const struct ext2_inode *inode,
			   BlockNum block,
			   int mark,
			   uint32_t *block_ret)
{
  static unsigned int *block_buff = NULL;
  int ok;

  *block_ret = 0;

  if(block_buff == NULL) {
    errno = 0;
    if((block_buff = (unsigned int*) malloc(block_size)) == NULL)
      INTERNAL_ERROR_EXIT("malloc in inode_get_block", strerror(errno));
  }

  /* NOT OK by default */
  ok = 0;

  /* direct */
  if(block < EXT2_IND_BLOCK) {
    *block_ret = inode->i_block[block];
    ok = 1;
    goto out;
  }

  block -= EXT2_IND_BLOCK;

  if(block < max_indir_1)      /* simple indirection */
    ok = inode_get_indir_block(0, inode->i_block[EXT2_IND_BLOCK], block, mark, block_ret);
  else if(block < max_indir_2) /* double indirection */
    ok = inode_get_indir_block(1, inode->i_block[EXT2_DIND_BLOCK], block - max_indir_1, mark, block_ret);
  else if(block < max_indir_3) /* triple indirection */
    ok = inode_get_indir_block(2, inode->i_block[EXT2_TIND_BLOCK], block - max_indir_2, mark, block_ret);
  else
    ok = 0;

 out:
  if(ok && mark)
    mark_block(*block_ret, NULL, DO_NOT_MARK, BLOCK_DUMPABLE);

  return (ok == 0);
}


/***************************************************
 * Description:
 *
 ****************/
int inode_read_data(const struct ext2_inode *inode,
		    unsigned char *buff,
		    off_t offset,
		    unsigned int *size)
{
  unsigned int chunk_size;
  off_t offset_in_block;
  unsigned char *ret;
  BlockNum block;
  int err;

  /*
    printf("inode_read_data offset=%d size=%d\n", offset, size);
  */
  if(offset >= inode->i_size)
    return 0;

  if(offset + *size > inode->i_size)
    *size = inode->i_size - offset; /* readjust size if needed */

  while(*size > 0) {
    if(offset % block_size > 0) {
      chunk_size = ((offset / block_size) + 1) * block_size;

      if(*size < chunk_size)
	chunk_size = *size;
    }
    else {
      if(*size > block_size)
	chunk_size = block_size;
      else
	chunk_size = *size;
    }

    err = inode_get_block(inode, offset / block_size, 0, &block);
    if(err)
      return 0;
      
    if(block == 0) { /* hole */
      memset(buff, 0, chunk_size);
    }
    else {
      offset_in_block = offset % block_size;
      ret = block_read_data((off_t)block * (off_t)block_size + (off_t)offset_in_block, chunk_size, buff);
      if(ret == NULL)
	return 0;
    }

    *size -= chunk_size;
    offset += chunk_size;
    buff += chunk_size;
  }

  return 1; /* OK */
}

const char *get_trunc_filename(const char *path, unsigned int partnum) {
  static char *buff = NULL;
  static unsigned int buff_len;

  char format[sizeof(TRUNC_FILE_SUFFIX) + sizeof("%04d")];
  char suffix[sizeof(TRUNC_FILE_SUFFIX) + 5];
  size_t l1;
  int l2;
  
  strcpy((char*)format, (char*)TRUNC_FILE_SUFFIX);
  strcat(format, "%06u");
  
  l1 = strlen(path);
  l2 = sprintf(suffix, format, partnum);
  
  errno = 0;
  if(buff == NULL) {
    buff_len = l1 + l2 + 1;
    if((buff = (char*) malloc(buff_len)) == NULL)
      INTERNAL_ERROR_EXIT("memory allocation: ", strerror(errno));
  }
  else if((l1 + l2 + 1) > buff_len)  {
    buff_len = l1 + l2 + 1;    
    if((buff = (char*) realloc(buff, buff_len)) == NULL)
      INTERNAL_ERROR_EXIT("memory allocation: ", strerror(errno));    
  }

  strcpy(buff, path);
  strcpy(buff+l1, suffix);
  
  return buff;
}

int inode_dump_regular_file(uint32_t inode_num, const char *path, const struct ext2_inode *p_inode) {
  static unsigned char *buff = NULL;

  unsigned int partnum = 0;
  struct ext2_inode inode;
  BlockNum block;
  size_t size;
  off_t pos;
  const char *new_path;
  struct utimbuf utim;
  int fd, err, error;

  if(buff == NULL) {
    errno = 0;
    if((buff = (unsigned char *)malloc(block_size)) == NULL)
      INTERNAL_ERROR_EXIT("memory allocation: ", strerror(errno));
  }

  /* if the inode is not available, at least we will get the name */
  if((fd = open(path, O_CREAT | O_WRONLY | O_LARGEFILE, 0666)) == -1) {
    perror("open");
    return 0;
  }

  if(p_inode)
    inode = *p_inode;
  else {
    if(really_get_inode(inode_num, &inode) == 0) {
      close(fd);
      return 0;
    }
  }

  LOG("Dumping file(%u) '%s'\n", inode_num, path);
  total_element_dumped++;

  if( ! LINUX_S_ISREG(inode.i_mode) ) {
    printf("WARNING: can't dump regular file, difference found between directory type info and inode type\n");
    return 0;
  }

  fchmod(fd, inode.i_mode);
  fchown(fd, inode.i_uid, inode.i_gid);
  utim.actime = inode.i_atime;
  utim.modtime = inode.i_mtime;
  utime(path, &utim);

  pos = 0;
  error = 0;
  while(pos < (off_t)inode.i_size) {
    err = inode_get_block(&inode, pos / (off_t)block_size, 0, &block);
    /* printf(" % 5lu %s pos = %lu  block = %lu\n", nloop++, path, (unsigned long)pos, block); fflush(stdout);*/

    if(err) {
      printf("WARNING: error while dumping '%s' %lu/%u : can't get block\n", path, (unsigned long)pos, inode.i_size);
      error = 1;
      pos += block_size;
      continue;
    }

    /* get block data */
    if(block == 0) { /* hole */
      memset(buff, 0, block_size);      
    }
    else {
      if(block_read_data((off_t)block * (off_t)block_size, block_size, buff) == 0) {
	printf("WARNING: error while dumping '%s' %lu/%u : can't read block\n", path, (unsigned long)pos, inode.i_size);
	error = 1;
	pos += block_size;
	continue;
      }
    }

    /* determine if we need to open a new */
    if(error) {
      close(fd);

      if(partnum == 0) {
	new_path = get_trunc_filename(path, partnum);

	errno = 0;
	if(rename(path, new_path) == -1)
	  INTERNAL_ERROR_EXIT("", strerror(errno));
      }

      partnum++;      
      new_path = get_trunc_filename(path, partnum);
      errno = 0;
      if((fd = open(new_path, O_CREAT | O_WRONLY | O_LARGEFILE, 0666)) == -1) {
	printf("WARNING: error while dumping file: open: %s\n", strerror(errno));
	return 0;
      }

      error = 0;
    }

    /* write data */
    if((inode.i_size - pos) < block_size)
      size = inode.i_size - pos;
    else
      size = block_size;
    
    errno = 0;
    if(write(fd, buff, size) == -1) {
      printf("WARNING: problem occured while dumping file: open: %s\n", strerror(errno));
      close(fd);
      return 0;
    }

    pos += block_size;
  }

  close(fd);

  if(error && partnum == 0) {
    new_path = get_trunc_filename(path, partnum);
    
    errno = 0;
    if(rename(path, new_path) == -1)
      INTERNAL_ERROR_EXIT("", strerror(errno));
  }

  return 1;
}


int inode_dump_symlink(uint32_t inode_num, const char *path) {
  static char *symlink_target = NULL;
  static unsigned int len;

  struct ext2_inode inode;
  struct utimbuf utim;

  if(really_get_inode(inode_num, &inode) == 0)
    return 0;

  LOG("Dumping symlink(%u) '%s'\n", inode_num, path);
  total_element_dumped++;

  if( ! LINUX_S_ISLNK(inode.i_mode) ) {
    printf("WARNING: can't dump symlink, difference found between directory type info and inode type\n");
    return 0;
  }

  /* allocate or reallocate the buffer for the name */
  errno = 0;
  if(symlink_target == NULL) {
    len = inode.i_size + 1;
    if((symlink_target = (char *)malloc(len)) == NULL)
      INTERNAL_ERROR_EXIT("memory allocation: ", strerror(errno));
  }
  else if(inode.i_size + 1 > len) {
    len = inode.i_size + 1;
    if((symlink_target = (char *)realloc(symlink_target, len)) == NULL)
      INTERNAL_ERROR_EXIT("memory allocation: ", strerror(errno));
  }

  /* get target filename */
  if((size_t)inode.i_size <= sizeof(inode.i_block))
    strncpy(symlink_target, (char*) inode.i_block, inode.i_size);
  else if(inode.i_size < block_size) {
    if(block_read_data((off_t)inode.i_block[0] * (off_t)block_size, inode.i_size, (unsigned char*)symlink_target) == 0) {
      printf("WARNING: can't read symlink data block\n");
      return 0;
    }  
  }
  else {
    fprintf(stderr, "symlink size too big\n");
    return 0;
  }

  symlink_target[inode.i_size] = '\0';
    
  /* create symlink */
  errno = 0;
  if(symlink(symlink_target, path) == -1)
    perror("symlink");

  lchown(path, inode.i_uid, inode.i_gid);
  utim.actime = inode.i_atime;
  utim.modtime = inode.i_mtime;
  utime(path, &utim);

  return 1;
}

int inode_dump_node(uint32_t inode_num, const char *path, __u16 type) {
  struct ext2_inode inode;
  struct utimbuf utim;

  if(really_get_inode(inode_num, &inode) == 0)
    return 0;

  LOG("Dumping node(%u) '%s'\n", inode_num, path);
  total_element_dumped++;

  if((inode.i_mode & LINUX_S_IFMT) != type) {
    printf("WARNING: can't dump node: difference found between directory type info and inode type\n");
    return 0;
  } 

  errno = 0;
  if(mknod(path, inode.i_mode, inode.i_block[0]) == -1)
    printf("WARNING: can't create special file: %s\n", strerror(errno));

  lchown(path, inode.i_uid, inode.i_gid);
  utim.actime = inode.i_atime;
  utim.modtime = inode.i_mtime;
  utime(path, &utim);

  return 1;
}


int inode_dump_socket(uint32_t inode_num, const char *path) {
  static char *buff = NULL;
  static unsigned int buff_size = 128;
  struct ext2_inode inode;
  struct sockaddr_un sock;
  struct utimbuf utim;
  char *cwd, *bname, *dname, *path_copy;
  int fd, ret = 0;

  if(really_get_inode(inode_num, &inode) == 0)
    return 0;

  LOG("Dumping socket(%u) '%s'\n", inode_num, path);
  total_element_dumped++;

  if( ! LINUX_S_ISSOCK(inode.i_mode)) {
    printf("WARNING: can't dump socket: difference found between directory type info and inode type\n");
    return 0;
  } 

  errno = 0;
  if((fd = socket(AF_UNIX, SOCK_DGRAM, 0)) == -1) {
    printf("WARNING: can't dump socket: socket error: %s\n", strerror(errno));
    return 0;
  }

  /* as sock.sun_path is relatively short we must go to detination directory */
  if(buff == NULL)
    if((buff = malloc(buff_size)) == NULL)
      INTERNAL_ERROR_EXIT("memory allocation: ", strerror(errno));

  while((cwd = getcwd(buff, buff_size)) == NULL && errno == ERANGE) {
    buff_size *= 2;
    if((buff = realloc(buff, buff_size)) == NULL)
      INTERNAL_ERROR_EXIT("memory allocation: ", strerror(errno));
  }
  if(errno && errno != ERANGE)
    return 0;

  path_copy = strdup(path);
  dname = strdup(dirname(path_copy));
  strcpy(path_copy, path);
  bname = strdup(basename(path_copy));

  if(chdir(dname)) {
    printf("WARNING: can't dump socket: chdir error: %s\n", strerror(errno));
    goto error;
  }
  
  sock.sun_family = AF_UNIX;
  strncpy(sock.sun_path, bname, 107 /* 108 - 1 as defined in un.h */ );

  if(bind(fd, (struct sockaddr*)&sock, sizeof(sock)) == -1) {
    printf("WARNING: can't dump socket: bind error: %s\n", strerror(errno));
    goto error;
  }

  lchown(path, inode.i_uid, inode.i_gid);
  utim.actime = inode.i_atime;
  utim.modtime = inode.i_mtime;
  utime(path, &utim);

  ret = 1;

 error:
  close(fd);

  chdir(cwd);
  free(cwd);

  free(path_copy);
  free(dname);
  free(bname);

  return ret;
}


void inode_search_orphans(void) {
  struct ext2_inode inode;
  char path[MAXPATHLEN];
  unsigned long len;
  unsigned int iname;
  uint32_t ino;

  printf("Searching and dumping orphans...\n");

  strcpy(path, dumpto);
  strcat(path, "/");
  strcat(path, "orphans");
  len = strlen(path);

  if(mkdir(path, 0755) == -1) {
    printf("ERROR: can't make orphans directory\n");
    return;
  }

  /*
    only cares about files, because directory inodes have been already
    read by mark_data_blocks
  */
  iname = 0;
  for(ino = 1; ino <= superblock.s_inodes_count; ino++) {
    struct dir_item *parent;

    if(really_get_inode(ino, &inode) == 0)
      continue;

    if(inode.i_links_count != 0 && search_inode_in_trees(ino, &parent) == 0) {
      path[len] ='\0';
      sprintf(&(path[len]), "/inode_%u", ino);

      LOG("Inode %u %u %s\n", ino, (inode.i_mode & LINUX_S_IFMT), path);

      if(LINUX_S_ISREG(inode.i_mode))
	inode_dump_regular_file(ino, path, &inode);
      else
	LOG(" can't dump this type : %o\n", (inode.i_mode & LINUX_S_IFMT));
    }
  }
}

/*
  This is a simplified inode_dump_regular_file version.
*/
static void inode_mark_data_blocks(uint32_t inode_num, struct ext2_inode *inode) {
  struct ext2_inode l_inode;
  BlockNum block;
  off_t pos;
  int err;

  if(inode == NULL) {
    if(really_get_inode(inode_num, &l_inode) == 0)
      return;
    inode = &l_inode;
  }

  pos = 0;
  while(pos < (off_t)inode->i_size) {
    err = inode_get_block(inode, pos / (off_t)block_size, 1, &block);

    pos += block_size;
  }
}

/*
  This is quite similar to the inode_dump_orphans but just to mark data
  blocks.
*/
void mark_data_blocks(void) {
  struct ext2_inode inode;
  unsigned int iname;
  uint32_t inum;

  printf("Marking blocks...\n");

  /* then first look at files */
  iname = 0;
  nb_block_marked = 0;
  for(inum = 1; inum <= superblock.s_inodes_count; inum++) {
    if(really_get_inode(inum, &inode) == 0)
      continue;

    if(inode.i_links_count != 0) {
      if(LINUX_S_ISLNK(inode.i_mode) && (size_t)inode.i_size > sizeof(inode.i_block))
	inode_mark_data_blocks(inum, &inode);
      else if(LINUX_S_ISREG(inode.i_mode))
	inode_mark_data_blocks(inum, &inode);
      else if(LINUX_S_ISDIR(inode.i_mode)) {
	static unsigned char *block_data;
	int err;

	errno = 0;
	if(block_data == NULL)
	  if( (block_data = (unsigned char*)malloc(block_size)) == NULL)
	    INTERNAL_ERROR_EXIT("memory allocation : ", strerror(errno));
	
	/* if first block is available, this means that we must have found
	   the stub before, so we can mark blocks as DUMPABLE */
	if( block_read_data((off_t)inode.i_block[0] * (off_t)block_size, block_size, block_data))
	  inode_mark_data_blocks(inum, &inode);
	else {
	  /* we are collecting blocks from this directory, block reassembling
	     will be easier :) */
	  struct dir_item *dir_item;
	  __u32 pos, start, nblock, block;
	  struct ext2_dir_entry_2 dir_entry;
  
	  dir_item = add_dir_item(NULL);	  
	  start = pos = 0;

	  /* there shouldn't be some holes in directories so inode.i_blocks 
	     can be directly used */
	  for(nblock = 1; nblock < inode.i_size / block_size; nblock++) {
	    err = inode_get_block(&inode, nblock, 0, &block);

	    if(!err) {
	      int ret;

	      if( block_read_data((off_t)block * (off_t)block_size, block_size, block_data) == NULL ) {
		start = pos = 0;
		continue;
	      }

	      if(pos) { /* there is remaining data from the block before */
		if(pos < block_size) {
		  unsigned int n;

		  n = block_size - pos;
		  if(n < 6) {
		    memcpy(((unsigned char*)&dir_entry) + n, block_data, (size_t)6 - n);
		    memcpy(((unsigned char*)&dir_entry) + 6, block_data + (6 - n), (size_t)dir_entry.rec_len - 6);
		  }
		  else {
		    memcpy(((unsigned char*)&dir_entry) + n, block_data, (size_t)dir_entry.rec_len - n);
		  }
		  dir_entry.name[dir_entry.name_len] = '\0';
		  start = dir_entry.rec_len - n;
		    
		  add_dir_entry(dir_item, &dir_entry);
		}
		else {
		  LOG("Abnormal 'pos' value\n");
		  start = pos = 0;
		}
	      }

	      /* tant qu'il est impossible d'avoir les infos avant le nom
		 on avance */
	      do {

		ret = search_directory_motif(block_data, block_size, start);

		if(ret != -1 && ret < 8)
		  while(is_valid_char(block_data[start]))
		    start++;

	      } while(ret != -1 && ret < 8 && start < block_size);

	      if(ret == -1)
		pos = start = 0;
	      else {
		pos = (__u32)ret - 8;
		while(pos < block_size) {
		  if(block_size - pos < 6) { /* if it's not possible to have 'rec_len' */
		    memcpy(((unsigned char*)&dir_entry) + 6, block_data + pos + 6, block_size - (pos + 6));
		    break;
		  }
		  
		  memcpy((unsigned char*)&dir_entry, block_data + pos, 8);
		  
		  if(pos + dir_entry.rec_len > block_size) {
		    memcpy((unsigned char*)&dir_entry, block_data + pos, block_size - pos);
		    break;
		  }
		  
		  memcpy((unsigned char*)&dir_entry, block_data + pos, (size_t)dir_entry.name_len + 8);
		  dir_entry.name[dir_entry.name_len] = '\0';
		  
		  add_dir_entry(dir_item, &dir_entry);
		  
		  pos += dir_entry.rec_len;
		}
	      }
	    }
	    else {
	      start = pos = 0;
	    }
	  }
	  rearrange_directories();
	}
      }
      else if(!LINUX_S_ISLNK(inode.i_mode)
	      && !LINUX_S_ISCHR(inode.i_mode)
	      && !LINUX_S_ISBLK(inode.i_mode)
	      && !LINUX_S_ISFIFO(inode.i_mode)
	      && !LINUX_S_ISSOCK(inode.i_mode))
	{
	  LOG("Marking blocks : unknown inode type\n");
	}
    }
  }

  printf("%lu blocks marked\n", nb_block_marked);
}

void init_inode_data(void) {
  nb_indir_per_block = block_size / sizeof(uint32_t);

  max_indir_1 = nb_indir_per_block;
  max_indir_2 = nb_indir_per_block * nb_indir_per_block;
  max_indir_3 = nb_indir_per_block * nb_indir_per_block * nb_indir_per_block;

  pow_block[0] = 1;
  pow_block[1] = max_indir_1;
  pow_block[2] = max_indir_2;
}
