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
#include <utime.h>
#include <string.h>
#include <errno.h>

#include <linux/ext2_fs.h>

#include "ext2_defs.h"

#define BUFF_SIZE 8192

struct file_item {
  unsigned short   state;
  unsigned short   type;
  __u32            inode;
  char            *name;
};

struct dir_item {
    struct dir_stub     stub;

    unsigned int        nb_subdir;
    unsigned int        nb_file;
    char               *name;
    
    struct dir_item   **subdirs;
    struct file_item  **files;
};

/*
struct dir_item_list {
  struct dir_item_list *next;
  struct dir_item *dir;
};
*/

unsigned long nb_dirstub_found;

static unsigned int nb_parent = 0;
static struct dir_item **parents = NULL;

static unsigned long nb_stub;
static struct dir_stub *stubs = NULL;


/* Recursive function */
static struct dir_item *find_parent_item(struct dir_item *parent, unsigned int parent_inode) {
  struct dir_item *p;
  unsigned int i;

  if(parent->stub.inode && parent->stub.inode == parent_inode)
    return parent;

  for(i = 0; i < parent->nb_subdir; i++) {
    p = find_parent_item(parent->subdirs[i], parent_inode);

    if(p)
      return p;
  }

  return NULL;
}


static void add_stub_item(off_t offset,
			  unsigned int inode,
			  unsigned int parent_inode,
			  struct fs_part *part)
{
    errno = 0;
    if(stubs == NULL) {
	stubs = (struct dir_stub*) malloc(sizeof(struct dir_stub));
	nb_stub = 1;
    } else {
	nb_stub++;
	stubs = (struct dir_stub*) realloc(stubs, nb_stub * sizeof(struct dir_stub));
    }

    if(stubs == NULL)
	INTERNAL_ERROR_EXIT("", strerror(errno));

    stubs[nb_stub - 1].offset = offset;
    stubs[nb_stub - 1].inode = inode;
    stubs[nb_stub - 1].parent_inode = parent_inode;
    stubs[nb_stub - 1].part = part;
    stubs[nb_stub - 1].state = UNSURE;
}


struct dir_item *add_dir_item(const struct dir_stub *stub) {
  struct dir_item *p1 = NULL;
  const struct dir_item *p2;
  struct dir_item *parent;
  struct dir_item *new = NULL;
  unsigned int i;

  new = (struct dir_item *) calloc(1, sizeof(struct dir_item));

  if(stub) {
    new->stub = *stub;
    for(i = 0; i < nb_parent && p1 == NULL; i++)
      p1 = find_parent_item(parents[i], stub->parent_inode);
  }

  errno = 0;

  if(stub && (p2 = search_inode_in_trees(stub->inode, &parent))) {
    if(p2->stub.inode != stub->inode) {
      LOG("WARNING: can't add dir item: a file seems to have the same inode number\n");
      return NULL;
    }
    else {
      if(p1) {
	if(p1 == parent) { /* OK */
	  
	}
	else { /* KO */
	  LOG("WARNING: can't add dir item: a directory with same inode number was found but with a different parent directory\n");
	  return NULL;
	}
      }
      else { /* no parent found but an entry found */
	/* this means that the parent shouldn't know its inode number */
	
      }
    }
  }
  else {
    /* if found */
    if(p1) {
      if(p1->subdirs)
	p1->subdirs = (struct dir_item **) realloc(p1->subdirs, ++(p1->nb_subdir) * sizeof(struct dir_item *));
      else
	p1->subdirs = (struct dir_item **) malloc(++(p1->nb_subdir) * sizeof(struct dir_item *));
      
      if(p1->subdirs == NULL)
	INTERNAL_ERROR_EXIT("memory allocation: ", strerror(errno)); 
      
      p1->subdirs[p1->nb_subdir-1] = new;
    } else {
      if(parents)
	parents = (struct dir_item **) realloc(parents, ++nb_parent * sizeof(struct dir_item *));
      else
	parents = (struct dir_item **) malloc(++nb_parent * sizeof(struct dir_item *));
      
      if(parents == NULL)
	INTERNAL_ERROR_EXIT("memory allocation: ", strerror(errno));
      
      parents[nb_parent-1] = new;
    }
  }

  return new;
}

static void remove_dir_item(struct dir_item *dir_item) {
  unsigned int i;

  for(i = 0; i < nb_parent && parents[i] != dir_item; i++);
  
  if(i < nb_parent) {
    /* ugly freeing */

    if(parents[i]->subdirs)
      free(parents[i]->subdirs);
    if(parents[i]->files)
      free(parents[i]->files);

    free(parents[i]);
    parents[i] = NULL;
  }
}

static void add_file_item(struct dir_item *dir, const struct ext2_dir_entry_2 *dir_entry) {
  struct file_item *new;
  
  new = (struct file_item *) calloc(1, sizeof(struct file_item));
  new->inode = dir_entry->inode;
  new->name = strdup(dir_entry->name);
  new->type = dir_entry->file_type;
  
  errno = 0;
  if(dir->files)
    dir->files = (struct file_item **) realloc(dir->files, ++(dir->nb_file) * sizeof(struct file_item *));
  else
    dir->files = (struct file_item **) malloc(++(dir->nb_file) * sizeof(struct file_item *));
  
  if(dir->files == NULL)
    INTERNAL_ERROR_EXIT("memory allocation: ", strerror(errno));
  
  dir->files[dir->nb_file-1] = new;
}

static void add_subdir_item(struct dir_item *dir, const struct ext2_dir_entry_2 *dir_entry) {
  struct dir_item *new;
  
  new = (struct dir_item *) calloc(1, sizeof(struct dir_item));
  new->stub.inode = dir_entry->inode;
  new->name = strdup(dir_entry->name);
  
  errno = 0;
  if(dir->subdirs)
    dir->subdirs = (struct dir_item **) realloc(dir->subdirs, ++(dir->nb_subdir) * sizeof(struct dir_item *));
  else
    dir->subdirs = (struct dir_item **) malloc(++(dir->nb_subdir) * sizeof(struct dir_item *));
  
  if(dir->subdirs == NULL)
    INTERNAL_ERROR_EXIT("memory allocation: ", strerror(errno));
  
  dir->subdirs[dir->nb_subdir-1] = new;
}

/* 
static void display_directory(unsigned int inode_num) {
  struct ext2_inode *inode;
  struct ext2_dir_entry_2 dir_entry;
  off_t offset;

  inode = get_inode(inode_num);

  offset = 12;
  while(offset < inode->i_size
	&&
	inode_read_data(inode, (unsigned char *)&dir_entry, offset, sizeof(struct ext2_dir_entry_2)) != -1)
    {
*/
      /* special case of 'lost+found' directory which has a reserved blocks */
/*      if(dir_entry.rec_len == block_size)
	break;
*/
      /*
      printf("inode %d, rec_len=%d, name_len=%d, type=%d\n",
	     inode_num,
	     dir_entry.rec_len,
	     dir_entry.name_len,
	     dir_entry.file_type);
      */
/*      if(! (dir_entry.name_len == 2 && strncmp("..", dir_entry.name, 2) == 0)) {
	write(1, dir_entry.name, dir_entry.name_len);
	write(1, "\n", 1);
      }
      offset += dir_entry.rec_len;
    }
}
*/

void add_dir_entry(struct dir_item *dir, struct ext2_dir_entry_2 *entry) {
  if(entry->inode) {
    if(entry->file_type == EXT2_FT_DIR && strcmp(entry->name, "..")) {
      unsigned int i;

      /* search for a stub */
      for(i = 0; i < dir->nb_subdir; i++) {
	if(dir->subdirs[i]->stub.inode == entry->inode) {
	  dir->subdirs[i]->name = strdup(entry->name);
	  break;
	}
      }
      
      /* if no stub is found add the new item to subdirs */
      if(i >= dir->nb_subdir)
	add_subdir_item(dir, entry);
    }
    else if(entry->file_type != EXT2_FT_DIR) {
      add_file_item(dir, entry);
    }
  }
}

static void fill_tree(struct dir_item *p) {
  static char blank[512] = "";  /* a depth of 512 is large enough */
  unsigned int i;
  size_t len;

  if(p) {
    off_t pos;
    unsigned int size;
    struct ext2_inode inode;
    struct ext2_dir_entry_2 dir_entry;

    len = strlen(blank);
    blank[len] = ' ';
    blank[len+1] = '\0';
    
    if(p->name && p->stub.inode == 0) /* this is an entry found by reading directory data (no stub found) */
      return;

    if(really_get_inode(p->stub.inode, &inode) == 0)
      return;

    pos = 12;  /* Jump over "." directory entry.
		  We don't jump over ".." because it can tell that the directory is empty.
	       */
    while(pos < inode.i_size) {
      size = 8;
      if(inode_read_data(&inode, (unsigned char *)&dir_entry, pos, &size) == 0) {
	printf("WARNING : can't read directory until the end (inode: %u %u/%u)\n", p->stub.inode, (unsigned int)pos, inode.i_size);
	break;
      }
      
      /* special case of 'lost+found' directory which has reserved blocks */
      if((unsigned int)dir_entry.rec_len == block_size) {
	LOG("lost+found FOUND %u\n", p->stub.inode);
	break;
      }
	    
      size = dir_entry.name_len;
      if(inode_read_data(&inode, (unsigned char *)&(dir_entry.name), pos+8, &size) == 0)
	break;

      dir_entry.name[dir_entry.name_len] = '\0';
      
      add_dir_entry(p, &dir_entry);

      pos += dir_entry.rec_len;
    }

    for(i = 0; i < p->nb_subdir; i++) {
      fill_tree(p->subdirs[i]);
    }

    blank[len] = '\0';
  }
}


static unsigned char curdir_motif[] = {        /* Directory entry length */
  0x01,                   /* Name length */
  0x02,                   /* File type = EXT2_FT_DIR */
  0x2e, 0x00, 0x00, 0x00  /* = ".\0\0" */
};
static unsigned char updir_motif[] = {        /* Directory entry length */
  0x02,                    /* Name length */
  0x02,                    /* File type = EXT2_FT_DIR */
  0x2e, 0x2e, 0x00, 0x00   /* name "..\0" */
};
static size_t curdir_motif_len = sizeof(curdir_motif);
static size_t updir_motif_len  = sizeof(updir_motif);
unsigned int dir_stub_motif_len = sizeof(curdir_motif) + 6 + sizeof(updir_motif);

int dir_stub_search(struct fs_part *part,
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
	(pos = find_motif(buffer + done, ( size + head_size ) - done, curdir_motif, curdir_motif_len)) >= 0)
    {
      off_t offset = total_bytes + ( done + pos ) - head_size;
      
      /* cherchons la référence au répertoire supérieur */
      {
	off_t cur_pos = lseek(part->fd, (off_t)0, SEEK_CUR);
	unsigned char lbuff[255];
	
	if( lseek(part->fd, offset - 6, SEEK_SET) == -1)
	  return -1;

	if( read(part->fd, lbuff, 24) != 24 )
	  return -1;
	
	/*
	  for(j = 0; j < 24; j++)
	  printf("%02x ", lbuff[j]);
	  printf("\n");
	*/
	
	if(memcmp(lbuff + 12 + 6, updir_motif, updir_motif_len) == 0) {
	  /*
	    if(((struct ext2_dir_entry_2 *)(lbuff + 12))->rec_len != 12) {
	    printf("directory which inode number is %d is empty\n", ((struct ext2_dir_entry_2 *)lbuff)->inode);
	    
	    }
	  */
	  
	  if( ((struct ext2_dir_entry_2 *)lbuff)->inode == 0 ||
	      ((struct ext2_dir_entry_2 *)(lbuff + 12))->inode == 0)
	    {
	      fprintf(stderr, "WARNING: found a directory with inode number 0\n");
	      fprintf(stderr, "WARNING: don't know what to do :-)\n");
	      exit(0);
	    }
	  
	  nb_dirstub_found++;
	  add_stub_item(offset - 6,
		       ((struct ext2_dir_entry_2 *)lbuff)->inode,
		       ((struct ext2_dir_entry_2 *)(lbuff + 12))->inode, part);
	}
	
	if( lseek(part->fd, cur_pos, SEEK_SET) == -1 )
	  return -1;
      }
      
      done += pos + 1;
    }
  return 0;
}

int compare_dir_stub(const void *ds1, const void *ds2) {
  if(((const struct dir_stub *)ds1)->inode == ((const struct dir_stub *)ds2)->inode) {
    if(((const struct dir_stub *)ds1)->state == ((const struct dir_stub *)ds2)->state)
      return 0;
      
    if(((const struct dir_stub *)ds1)->state < ((const struct dir_stub *)ds2)->state)
      return -1;
    else
      return 1;
  }

  if(((const struct dir_stub *)ds1)->inode < ((const struct dir_stub *)ds2)->inode)
    return -1;
  else
    return 1;
}


/*
  Since we have all directory stumbs and because during the scan parent can have
  been found before children, children aren't under their parent.
  So we're going to solve this by rearranging the tree.
*/
void rearrange_directories(void) {
  unsigned int i, j;

  for(i = 0; i < nb_parent; i++) {
    for(j = 0; parents[i] && j < nb_parent; j++) {
      if(j != i
	 && parents[j]
	 && parents[i]->stub.inode != parents[i]->stub.parent_inode )
	{
	  struct dir_item *p = find_parent_item(parents[j], parents[i]->stub.parent_inode);
	  
	  if(p) {
	    if(p->subdirs)
	      p->subdirs = (struct dir_item **) realloc(p->subdirs, ++(p->nb_subdir) * sizeof(struct dir_item *));
	    else
	      p->subdirs = (struct dir_item **) malloc(++(p->nb_subdir) * sizeof(struct dir_item *));
	    
	    p->subdirs[p->nb_subdir-1] = parents[i];
	    parents[i] = NULL;
	    
	    i = 0;
	    break;
	  }
	}
    }
  }
}


void dir_analyse(void) {
  unsigned long int i, j;
  unsigned int nb_ok, nb_ko;

  nb_ok = nb_ko = 0;
  for(i = 0; i < nb_stub; i++) {
    if(stubs[i].state != KO) {
      enum inode_bmp_state av;
      struct ext2_inode inode;
      off_t stub_offset;
      int ret;

      LOG("INODE:%u  block:%lu\n", stubs[i].inode, (unsigned long)(stubs[i].offset / block_size));

      /* INODE BITMAP TEST */
      av = is_inode_available(stubs[i].inode);

      if( av == INODE_BMP_0) {
	stubs[i].state = KO;
	nb_ko++;
	LOG("DIR INODE BMP KO\n");
	continue;
      }

      ret = really_get_inode(stubs[i].inode, &inode);

      /* TEST IF INODE IS A DIRECTORY INODE */
      if(ret && (S_ISREG(inode.i_mode) || ! S_ISDIR(inode.i_mode))) {
	stubs[i].state = KO;
	nb_ko++;
	LOG("INODE NOT A DIRECTORY\n");
	continue;
      }

      /* TEST (IF POSSIBLE) THAT THE FIRST BLOCK IS THE BLOCK WHERE THE
	 STUB WAS FOUND */
      stub_offset = stubs[i].offset + stubs[i].part->phys_offset + stubs[i].part->logi_offset;
      if(ret &&
	 stubs[i].part->aligned &&
	 stub_offset != (inode.i_block[0] * block_size))
	{
	  stubs[i].state = KO;
	  nb_ko++;
	  LOG("STUB @ NOT EQUALS\n");
	  continue;
	}

      /* IF EVERY TEST SUCCEDED THEN WE CAN CONSIDER THAT THIS STUB IS OK */
      if(ret &&
	 stubs[i].part->aligned &&
	 stub_offset == (inode.i_block[0] * block_size))
	{
	  stubs[i].state = OK;
	  nb_ok++;
	  continue;
	}

        
      LOG("WEIRD STATE %d %d %s", ret, stubs[i].part->aligned, offset_to_str(stub_offset));
      LOG("%s\n",offset_to_str((off_t)inode.i_block[0] * (off_t)block_size));
      
      /* si on arrive là c'est que
	 - soit l'inoeud n'est pas lisible (on ne peut rien faire),
	 - soit la partie n'est pas alignée, auquelle cas il faudrait essayer de l'aligner
      */
    }
  }

  printf("nb dir stubs ok = %u, ko = %u  / %lu\n", nb_ok, nb_ko, nb_stub);

  qsort(stubs, nb_stub, sizeof(struct dir_stub), compare_dir_stub);

  {
    int nb = 0;

    for(i = 0; i < nb_stub; i++) {
      if(stubs[i].state == OK) {
	add_dir_item(&(stubs[i]));
	nb++;

	for(j = i+1; j < nb_stub && stubs[i].inode == stubs[j].inode; j++)
	  printf("Ignoring directory stub: inode=%u offset=%s part=%s\n",
		 stubs[j].inode, offset_to_str(stubs[j].offset), stubs[j].part->filename);
	i = j - 1;
      }
    }
  }

  rearrange_directories();

  {
    struct fs_part *part;

    for(part = ext2_parts; part; part = part->next) {
      if( ! part->aligned ) {
	/* in this case we could see if we can find the offset from
	   the directory (souche) offset in the part and the first data
	   block of corresponding inode */
	printf("Merde alors\n");
	exit(10);
      }
    }
  }

  /* à partir de maintenant on peut chercher les noms des répertoires */
  /*
    - soit par lecture directe des données du répertoire parent si celui-ci ne dépasse pas
      la taille d'un bloc
    - sinon il faudrait aller chercher dans la table des inoeuds pour trouver tout les blocs
      appartenant au répertoire
    - enfin, en trouvant et connaissant des noms de fichier, on peut trouver des blocs qui
      contiennent des données d'un répertoire (on peut recouper les données en demandant à
      l'utilisateur, et en cherchant dans les blocs de données (alloués ou non) voir si on
      peut réaligner les données.
  */

  for(i = 0; i < nb_parent; i++)
    fill_tree(parents[i]);  
}

/*
  return motif position in the buffer or -1 if nothing is found.
*/
int search_directory_motif(const unsigned char *buff,
			   unsigned int buff_size,
			   unsigned int start)
{
  char name[257]; /* 256 + 1 == sizeof((struct ext2_dir_entry_2).name_len << 8) + 1 */
  int j, k, size, name_start;
  int valid;
  
  k = 0;
  name_start = 0;
  for(j = (int) start; j < (int)buff_size; j++) {
    valid = is_valid_char(buff[j]);
    if( valid ) {
      if(k < 256) {
	if(k == 0)
	  name_start = j;
	
	name[k++] = (char)buff[j];
      }
      else
	k = 0;
    }

    size = 8 + ((k % 4) ? (((k/4))+1)*4 : k);
    
    if(k
       && name_start >= 8
       && buff[name_start-1] > 0
       && buff[name_start-1] < EXT2_FT_MAX
       && buff[name_start-2] == (__u8)(j-name_start)
       && ((int)((const struct ext2_dir_entry_2 *)(&(buff[name_start-8])))->rec_len == size ||
	   (int)((const struct ext2_dir_entry_2 *)(&(buff[name_start-8])))->rec_len == (block_size - name_start + 8)))
      {
	return name_start;
      }
    else if( ! valid)
      k = 0;
  }

  return -1;
}

/*
  Try to identify blocks that can contain directory data.
  This is only done for blocks marked as not free and not dumped
  and blocks with an unknown state.
*/
void scan_for_directory_blocks(void) {
  struct fs_part *part;
  unsigned char *block_data;
  BlockNum blk;
  
  errno = 0;
  if( (block_data = (unsigned char*)malloc(block_size)) == NULL)
    INTERNAL_ERROR_EXIT("memory allocation : ", strerror(errno));

  /* first evaluation */
  printf("Scanning blocks for directory data...\nFirst evaluation :\n");
  for(part = ext2_parts; part; part = part->next) {
    unsigned long int n;

    n = 0;
    for(blk = part->first_block; blk <= part->last_block; blk++) {
      unsigned char st;

      st = part_block_bmp_get(part, blk - part->first_block);

      if((st & BLOCK_AV_MASK) == BLOCK_AV_FREE ||
	 (st & BLOCK_AV_MASK) == BLOCK_AV_TRUNC)
	continue;

      if((st & BLOCK_DUMP_MASK) != BLOCK_DUMP_NULL )
	continue;
      
      n++;
    }
    printf(" %s : %lu/%lu blocks to analyse\n", part->filename, n, part->nb_block);
  }
  printf("End\nScanning...");
  
  for(part = ext2_parts; part; part = part->next) {
    for(blk = part->first_block; blk <= part->last_block; blk++) {
      unsigned char st;
      int next, error, pos;
      unsigned int nb_entry, start_at;
      struct dir_item *dir_item;
      struct dir_stub stub = { 0, 0, 0, 0, NULL };

      st = part_block_bmp_get(part, blk - part->first_block);

      if((st & BLOCK_AV_MASK) == BLOCK_AV_FREE ||
	 (st & BLOCK_AV_MASK) == BLOCK_AV_TRUNC)
	continue;

      if( (st & BLOCK_DUMP_MASK) != BLOCK_DUMP_NULL )
	continue;

      if( block_read_data((off_t)blk * (off_t)block_size, block_size, block_data) == NULL )
	continue;
      
      LOG("Block %u : state = %d\n", blk, st);

      /* recherche un motif qui pourrait faire penser que le bloc contient un répertoire */
      next = -1;
      start_at = 0;
      error = 0;
      nb_entry = 0;
      dir_item = NULL;
      while(start_at < block_size && (pos = search_directory_motif(block_data, block_size, start_at)) != -1) {
	struct ext2_dir_entry_2 dir_entry;
	  
	/* if the totality of the entry can't be get, we're jumping the entry name */
	if(pos < 8) {
	  while(is_valid_char(block_data[pos++]))
	    start_at++;
	  continue;
	}

	if(next != -1) {
	  /* test that entries are well 'linked' */
	  if(next != pos - 8) {
	    error = 1;
	    break;
	  }
	}

	memcpy((unsigned char*)&dir_entry, block_data + pos - 8, 8);
	memcpy((unsigned char*)&dir_entry, block_data + pos - 8, (size_t)dir_entry.name_len + 8);
	dir_entry.name[dir_entry.name_len] = '\0';
	
	if(dir_entry.name[0] == '.' && dir_entry.name[1] == '\0') {
	  stub.inode = dir_entry.inode;
	}
	else {
	  if(dir_item == NULL) {
	    if(stub.inode && strcmp(dir_entry.name, "..") == 0) {
	      stub.parent_inode = dir_entry.inode;
	      LOG("dir added 1: %d %d\n", stub.inode, stub.parent_inode);
	      dir_item = add_dir_item(&stub);
	    }
	    else {
	      LOG("dir added 2\n");
	      dir_item = add_dir_item(NULL);
	    }
	  }
	  
	  add_dir_entry(dir_item, &dir_entry);	  
	  nb_entry++;
	}
	
	start_at = pos - 8 + dir_entry.rec_len;
	next = (int)start_at;
      }

      if(error && dir_item) {
	remove_dir_item(dir_item);
	nb_entry = 0;
      }

      if(nb_entry) {
	LOG("Block %u seems to be a directory content block.\n", blk);
	rearrange_directories();
      }
    }
  }
  printf("\n");
}

void save_dir_stubs(void) {
  char path[MAXPATHLEN];
  unsigned long i;
  int fd;

  strcpy(path, dumpto);
  strcat(path, "/dir_stubs");

  if((fd = open(path, O_WRONLY | O_TRUNC | O_CREAT, 0666)) == -1)
    INTERNAL_ERROR_EXIT("open: ", strerror(errno));

  for(i = 0; i < nb_stub; i++) {
    if(write(fd, &(stubs[i]), sizeof(struct dir_stub)) == -1)
      INTERNAL_ERROR_EXIT("write: ", strerror(errno));

    if(write(fd, stubs[i].part->filename, strlen(stubs[i].part->filename)+1) == -1)
      INTERNAL_ERROR_EXIT("write: ", strerror(errno));

  }

  close(fd);    
}

void restore_dir_stubs(void) {
  struct dir_stub tmp;
  char path[MAXPATHLEN], ch;
  int fd, i;
  ssize_t n;
  
  strcpy(path, dumpto);
  strcat(path, "/dir_stubs");

  if((fd = open(path, O_RDONLY)) == -1)
    INTERNAL_ERROR_EXIT("open: ", strerror(errno));

  while(1) {
    if((n = read(fd, &tmp, sizeof(struct dir_stub))) == -1)
      INTERNAL_ERROR_EXIT("read: ", strerror(errno));

    if(n == 0)
      break;
    else if((size_t)n != sizeof(struct dir_stub))
      INTERNAL_ERROR_EXIT("can't restore directory stub.", "");
    
    for(i = 0; i < MAXPATHLEN; i++) {
      if(read(fd, &ch, 1) == -1)
	INTERNAL_ERROR_EXIT("read: ", strerror(errno));

      path[i] = ch;
      if(!ch)
	break;
    }
    if(i >= MAXPATHLEN)
      INTERNAL_ERROR_EXIT("restore_dir_stubs: part filename too long", "");

    if((tmp.part = search_part_by_filename(path)) == NULL)      
      INTERNAL_ERROR_EXIT("can't find part from filename:", path);
            
    add_stub_item(tmp.offset, tmp.inode,
		 tmp.parent_inode, tmp.part);
  }

  close(fd);    
}

static void dump_directory(struct dir_item *dir, char *path, unsigned int path_len) {
  unsigned int isub, ifile, iname = 0;
  int n;
  struct ext2_inode inode;
      
  if(dir->stub.inode && really_get_inode(dir->stub.inode, &inode)) {
    struct utimbuf utim;

    chmod(path, inode.i_mode);
    chown(path, inode.i_uid, inode.i_gid);
    utim.actime = inode.i_atime;
    utim.modtime = inode.i_mtime;
    utime(path, &utim);
  }
  
  LOG("Dumping directory '%s' inode=%u %d files\n", path, dir->stub.inode, dir->nb_file);
  total_element_dumped++;

  for(isub = 0; isub < dir->nb_subdir; isub++) {
    path[path_len] = '\0';
    
    if(dir->subdirs[isub]->name == NULL || dir->subdirs[isub]->name[0] == '\0')
      n = sprintf(path+path_len, "/noname_%05u", iname++);
    else
      n = sprintf(path+path_len, "/%s", dir->subdirs[isub]->name);

    if(mkdir(path, 0755) == 0)
      dump_directory(dir->subdirs[isub], path, path_len + n);
  }

  for(ifile = 0; ifile < dir->nb_file; ifile++) {
    int state;
    
    path[path_len] = '\0';
    
    if(dir->files[ifile]->name == NULL || dir->files[ifile]->name[0] == '\0')
      n = sprintf(path+path_len, "/noname_%05u", iname++);
    else
      n = sprintf(path+path_len, "/%s", dir->files[ifile]->name);
    
    switch(dir->files[ifile]->type) {
    case EXT2_FT_UNKNOWN:
      break;
    case EXT2_FT_REG_FILE:
      state = inode_dump_regular_file(dir->files[ifile]->inode, path, NULL);
      break;
    case EXT2_FT_SYMLINK:
      state = inode_dump_symlink(dir->files[ifile]->inode, path);
      break;
    case EXT2_FT_CHRDEV:
      state = inode_dump_node(dir->files[ifile]->inode, path, LINUX_S_IFCHR);
      break;
    case EXT2_FT_BLKDEV:
      state = inode_dump_node(dir->files[ifile]->inode, path, LINUX_S_IFBLK);
      break;
    case EXT2_FT_FIFO:
      state = inode_dump_node(dir->files[ifile]->inode, path, LINUX_S_IFIFO);
      break;
    case EXT2_FT_SOCK:
      state = inode_dump_socket(dir->files[ifile]->inode, path);
      break;
    default:
      fprintf(stderr, "unknown dir entry type\n"); /* FIX ME: but we can look at the inode */
      continue;
    }
  }
}

void dump_trees(void) {
  char path[MAXPATHLEN];
  unsigned int i, iname = 0;
  size_t path_len;
  int n;

  strcpy(path, dumpto);
  path_len = strlen(dumpto);
  if(path[path_len-1] == '/')
    path[--path_len] = '\0';

  for(i = 0; i < nb_parent; i++) {
    if(parents[i]) {
      path[path_len] = '\0';

      if(parents[i]->name == NULL || parents[i]->name[0] == '\0')
	n = sprintf(&(path[path_len]), "/noname_%05u", iname++);
      else
	n = sprintf(&(path[path_len]), "/%s", parents[i]->name);

      if(mkdir(path, 0755) == 0)
	dump_directory(parents[i], path, path_len + n);
    }
  }
}


static struct dir_item *search_inode_in_tree(struct dir_item *dir,
					     __u32 inode_num,
					     struct dir_item **parent)
{
  unsigned int isub, ifile;
  struct dir_item *ret;

  for(isub = 0; isub < dir->nb_subdir; isub++) {
    if(dir->subdirs[isub]->stub.inode == inode_num) {
      *parent = dir;
      return dir->subdirs[isub];
    }

    if((ret = search_inode_in_tree(dir->subdirs[isub], inode_num, parent)))
      return ret;
  }

  for(ifile = 0; ifile < dir->nb_file; ifile++) {
    if(dir->files[ifile]->inode == inode_num) {
      *parent = dir;
      return dir;
    }
  }

  return NULL;
}

struct dir_item *search_inode_in_trees(__u32 inode_num, struct dir_item **parent) {
  struct dir_item *ret;
  unsigned int i;

  *parent = NULL;

  for(i = 0; i < nb_parent; i++) {
    if(parents[i]) {
      if(parents[i]->stub.inode)
	return parents[i];

      if((ret = search_inode_in_tree(parents[i], inode_num, parent)))
	return ret;
    }
  }

  return NULL;
}
