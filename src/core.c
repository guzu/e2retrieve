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
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/param.h>
#include <sys/time.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <assert.h>
#include <time.h>
#include <signal.h>
#include <getopt.h>

/*
Equivalent to <sys/mount.h>
#include <linux/fs.h>
*/
#include <sys/mount.h>

#include <linux/major.h>

#include "version.h"

#ifndef MAX
#define MAX(a,b) (((a) > (b)) ? (a) : (b))
#endif

FILE *logfile = NULL;

int user_interrupt = 0;

char *dumpto = NULL;
int stop_after_scan = 0, restart_after_scan = 0;

struct fs_part *ext2_parts = NULL;

time_t reference_date;
int date_mday = REFDATE_DAY;
int date_mon  = REFDATE_MONTH;
int date_year = REFDATE_YEAR;

/* global variables for progress informations */
struct fs_part *currentpart;
off_t totalread;
struct itimerval firsttime = { { 0, 0 }, { 0, 1 } }; /* start 1 usec after */
struct itimerval again     = { { 1, 0 }, { 1, 0 } }; /* restart every second */
struct itimerval stoptimer = { { 0, 0 }, { 0, 0 } };
struct timeval tstart;
struct timezone tzone;
int max_printed = 0;

unsigned int total_element_dumped;

static void save_scan_context(struct fs_part *part, off_t offset);


void usage(void) {
  printf("\n"
	 "Usage: e2retrieve [OPTIONS]... <part 1> [ <part 2> ... <part n> ]\n"
	 "  e2retrieve is an Ext2 data recovering tool.\n"
	 "  e2retrieve will try to recover files and directories of the Ext2 filesystem which\n"
	 "  raw data are in the files or block devices in arguments, and recovered stuff is put\n"
	 "  in the given directory.\n"
	 "\n");
  printf("  Note: the directory where data are going to be written must not exists (e2retrieve\n"
	 "  will create it for you), and parts MUST be ordered.\n"
	 "  Parts can be a melting of regular files and/or block devices.\n"
	 "\n");
  printf(" OPTIONS:\n"
	 "  -t, --dumpto=DIR          directory where to dump\n"
	 "  -1, --stop_after_scan     stop analyse after the scan of all the parts\n"
	 "  -2, --restart_after_scan  reload informations, then analyse and dump\n"
	 "  -r, --resume_scan         resume scan interrupted by user\n"
	 "  -d, --refdate=DDMMYYYY    set the reference date\n"
	 "  -l, --log=FILE            file where to log detailed operations/informations\n"
	 "  -h, --help                display this help\n"
	 "  -v, --version             display version\n"
	 "\n");
}

void parse_cmdline(int argc, char* argv[]) {
  optind = 0;
  while (1) {
    int c, lindex = 0;
    static struct option long_options[] = {
      { "help",               0, NULL, 'h' },
      { "dumpto",             1, NULL, 't' },
      { "refdate",            1, NULL, 'd' },
      { "stop_after_scan",    0, NULL, '1' },
      { "restart_after_scan", 0, NULL, '2' },
      { "resume_scan",        0, NULL, 'r' },
      { "log",                1, NULL, 'l' },
      { "version",            0, NULL, 'v' },
      { NULL, 0, NULL, 0}
    };
 
    c = getopt_long (argc, argv, "12rhl:t:d:v", long_options, &lindex);
    if (c == -1)
      break;
    
    switch (c) {
    case '1':
      stop_after_scan = 1;
      break;
    case '2':
      restart_after_scan = 1;
      break;
    case 'd':
      {
	int n, dd, mm, yy;
		
	n = atoi(optarg);
	
	yy = n % 10000;
	n /= 10000;
	
	mm = n % 100;
	n /= 100;
	
	dd = n % 100;
	
	if( (dd < 1 || dd > 31 ) || (mm < 1 || mm > 12) || yy < 1900) {
	  printf("ERROR: invalid reference date\n");
	  exit(1);
	}
	
	date_mday = dd;
	date_mon = mm;
	date_year = yy;
      }
      break;
    case 'l':
      errno = 0;
      if((logfile = fopen(optarg, "w")) == NULL) {
	printf("ERROR: fopen: %s \n", strerror(errno));
	exit(1);
      }
      break;
    case 't':
      dumpto = strdup(optarg);
      break;
    case 'r':
      fprintf(stderr, "ERROR: Resume not yet implemented\n");
      exit(1);
      break;
    case '?':
    case 'h':
      usage();
      exit(0);
      break;
    case 'v':
      printf("e2retrieve version %s\n", E2RETRIEVE_VERSION);
      exit(0);
      break;
    default:
      exit(1);
    }
  }

  if( ! restart_after_scan && argc - optind <= 0 ) {
    fprintf(stderr, "ERROR: too few arguments.\n");
    exit(1);
  }
}

/*
  The date is before the creation of Linux and the Ext2 filesystem

  Your can change the value  Until your system had a problem of clock, or if you want to change
  the value, 
*/
static time_t mkrefdate(void) {
  struct tm date;

  date.tm_sec   = 0;
  date.tm_min   = 0;
  date.tm_hour  = 0;
  date.tm_mday  = date_mday;
  date.tm_mon   = date_mon;
  date.tm_year  = date_year - 1900;
  date.tm_wday  = 0;
  date.tm_yday  = 0;
  date.tm_isdst = 0;

  return mktime(&date);
}


void user_interrupt_handler(int __unused) {
  user_interrupt = 1;
}

void dump_progress(int __unused) {
  printf("\r%u files and directories dumped (modulo some errors)", total_element_dumped);
  fflush(stdout);

  if(setitimer(ITIMER_REAL, &again, NULL) == -1)
    INTERNAL_ERROR_EXIT("setitimer: ", strerror(errno));
}

void orphans_dump_progress(int __unused) {
  printf("\r%u orphans files dumped (modulo some errors)", total_element_dumped);
  fflush(stdout);

  if(setitimer(ITIMER_REAL, &again, NULL) == -1)
    INTERNAL_ERROR_EXIT("setitimer: ", strerror(errno));
}

void display_progress(int __unused) {
  double percent;
  struct timeval now;
  uint64_t elapsed, estim;
  unsigned int h, m, s;
  int n;

  gettimeofday(&now, &tzone);

  elapsed = (uint64_t)(now.tv_sec - tstart.tv_sec) + (uint64_t)( now.tv_usec - tstart.tv_usec ) / (uint64_t)1000000;
  percent = ((double)totalread / (double)currentpart->max_size) * 100.0;
  estim = (uint64_t) ((double)elapsed / (double)totalread) * (double)currentpart->max_size;

  n = printf("\r %5.2f%% (%u/%u/%u different superblocks, %lu dir. stubs)",
	     percent,
	     ((sb_pool_size == 0) ? 0 : (sb_pool_size - 1)),
	     nb_sb_found,
	     nb_magicnum_found,
	     nb_dirstub_found);

  h = elapsed / (unsigned int)3600;
  m = (elapsed % 3600) / 60;
  s = (elapsed % 3600) % 60;
  n += printf(" %02u:%02u:%02u", h, m, s);

  h = estim / 3600;
  m = (estim % 3600) / 60;
  s = (estim % 3600) % 60;
  n += printf("/%02u:%02u:%02u", h, m, s);

  max_printed = MAX(max_printed, n);

  fflush(stdout);

  if(setitimer(ITIMER_REAL, &again, NULL) == -1)
    INTERNAL_ERROR_EXIT("setitimer: ", strerror(errno));
}


void global_scan(void) {
  unsigned char buff[SCAN_BUFF_SIZE];
  ssize_t n;
  unsigned int motif_len, copied;
  struct fs_part *part, *prev;

  signal(SIGALRM, display_progress);
  signal(SIGINT, user_interrupt_handler);

  printf("Scanning for superblocks and directories stubs:\n");

  motif_len = MAX(magic_motif_len, dir_stub_motif_len);

  if(motif_len <= 0) {
    fprintf(stderr, "ERROR: motif size can't be lower or equal to 0.\n");
    exit(1);
  }

  if( motif_len >= SCAN_BUFF_SIZE ) {
    fprintf(stderr, "ERROR: motif size is greater than buffer size.\n");
    exit(1);
  }

  nb_dirstub_found = 0;
  nb_sb_found = nb_magicnum_found = 0;
  prev = ext2_parts;
  for(part = ext2_parts;
      part && !user_interrupt;
      part = ((part) ? part->next : NULL)) /* because during the loop we could have deleted
					      the last part and modified 'part' */
  {
    off_t offset, start;
    off_t offset1, offset2;
    off_t offset_interrupt = 0;

    printf("(%s) :\n", part->filename);

    currentpart = part;
      
    start  = part->phys_offset;
    offset = part->phys_offset;
    totalread = offset;

    signal(SIGALRM, display_progress);
    if(setitimer(ITIMER_REAL, &firsttime, NULL) == -1)
      INTERNAL_ERROR_EXIT("setitimer: ", strerror(errno));
    gettimeofday(&tstart, &tzone);

    do {
      /*
       * REGULAR READING
       */
      errno = 0;
      copied = 0;

      if( lseek(part->fd, offset, SEEK_SET) == -1 )
	INTERNAL_ERROR_EXIT("lseek: ", strerror(errno));

      while( (n = read(part->fd, buff + copied, SCAN_BUFF_SIZE - copied)) > 0 && !user_interrupt) {
	
	if( superblock_search(part, buff, (unsigned int)n, copied, offset) == -1 )
	  break;

	if( dir_stub_search(part, buff, (unsigned int)n, copied, offset) == -1 )
	  break;
	
	offset += n;
	totalread = offset;
	
	memmove(buff, buff + copied + n - motif_len, motif_len);
	copied = motif_len;      
      }

      if(user_interrupt) {
	offset_interrupt = offset;
	break;
      }

      part->size = offset - part->phys_offset;

      /*
       * "VERITABLE" ERREUR
       */
      if(n < 0 && errno != EIO)
	INTERNAL_ERROR_EXIT("", strerror(errno));
      
      /* error reading is only acceptable on block devices */
      if(n < 0 && errno == EIO && part->type == PART_TYPE_BLOCK) {
	unsigned int sum;
	/*
	  Try to approach nearest as possible the location where data
	  aren't readable.
	  Algorithm is practically the same as above.
	*/
	if( lseek(part->fd, offset, SEEK_SET) == -1 )
	  INTERNAL_ERROR_EXIT("lseek: ", strerror(errno));

	errno = 0;
	sum = copied;
	while( sum < SCAN_BUFF_SIZE
	       && (n = read(part->fd, buff + sum, 1)) > 0)
	  {
	    sum++;
	  }

	offset += sum - copied;
	offset1 = offset;
	part->size = offset - part->phys_offset;

	if(0) {
	  if(start != offset) /* début de partie lisible */
	    printf("\n%s -> %s\n", offset_to_str(start), offset_to_str(offset));
	  else /* début de partie illisible */
	    printf("WARNING (%s:%d): CACA POURRI %s\n", __FILE__, __LINE__, offset_to_str(offset));
	}

	/*
	 * DEBASED READING
	 */

	/* jump over "bad" blocks */
	if(offset < part->max_size) {
	  do {
	    unsigned char ch;
	    
	    lseek(part->fd, offset, SEEK_SET);
	    errno = 0;
	    n = read(part->fd, &ch, 1);
	    offset += DEV_BSIZE;

	    totalread = offset;

	    /*    printf("%u %d %d %d\n", off, n, errno, EIO); */
	  } while(!user_interrupt && n == -1 && errno == EIO && offset < part->max_size);

	  if(user_interrupt) {
	    offset_interrupt = offset;
	    break;
	  }

	  if( n != -1 && ! errno ) {
	    /*start = offset - DEV_BSIZE;*/ /* puisque on a réussi à lire le dernier
					   et qu'on incrémenté une fois de plus */
	    offset -= DEV_BSIZE;
	    LOG(" TRUC 10 %s\n", offset_to_str(start));
	  }
	}
	else {
	  LOG(" TRUC 9 %s\n", offset_to_str(start));
	}

	offset2 = offset;
	
	errno = 0;
	if(offset1 == start && offset2 != part->max_size) {       /* configuration: |--------#########| */
	  part->phys_offset = part->logi_offset = offset2;

	  LOG(" TRUC 11 %s", offset_to_str(start));
	  LOG(" %s\n", offset_to_str(offset2));
	}
	else if(offset1 != start && offset2 != part->max_size && offset1 != offset2) {  /* configuration: |#####-----#######| */
	  struct fs_part *new;

	  /* il faut créer une nouvelle entrée en recopiant l'actuelle et en changeant sa phys_offset */
	  LOG(" TRUC 12 %s", offset_to_str(start));
	  LOG(" %s\n", offset_to_str(offset1));

	  if((new = (struct fs_part *) calloc(1, sizeof(struct fs_part))) == NULL)
	     INTERNAL_ERROR_EXIT("malloc : ", strerror(errno));

	  *new = *part;
	  new->phys_offset = new->logi_offset = offset;
	  new->filename = strdup(part->filename);

	  new->next = part->next;
	  part->next = new;
	  prev = part;
	  part = new;
	}
	else if(offset1 == start && offset2 == part->max_size) {  /* configuration: |-----------------| */
	  struct fs_part *old;
	  /* on peut effacer la partie en cours */
	  LOG(" TRUC 13 %s", offset_to_str(start));
	  LOG(" %s\n", offset_to_str(offset2));

	  old = part;
	  if(part == ext2_parts)
	    ext2_parts = part->next;
	  else
	    part = part->next;

	  free(old->filename);
	  free(old);
	}
	else if(offset1 != start && offset2 == part->max_size) {  /* configuration: |#########--------| */
	  /* nothing to do special to do, part->size is still updated */
	  LOG(" TRUC 14 %s", offset_to_str(start));
	  LOG(" %s", offset_to_str(offset1));
	  LOG(" %s\n", offset_to_str(offset2));
	}
	else {
	  /* unknown case */
	  printf(" TRUC 15 %s !!!!!", offset_to_str(start));
	  printf(" %s", offset_to_str(offset1));
	  printf(" %s", offset_to_str(offset2));
	  printf(" %s\n", offset_to_str(part->max_size));
	  exit(1000);
	}

	start = offset;
      }
    } while( ( n && ! errno ) || errno == EIO );

    /* stop the timer */
    if(setitimer(ITIMER_REAL, &stoptimer, NULL) == -1)
      INTERNAL_ERROR_EXIT("setitimer: ", strerror(errno));    
    signal(SIGALRM, SIG_IGN);

    /* USER INTERRUPTION */
    if(user_interrupt) {
      assert(part != NULL);
      save_scan_context(part, offset_interrupt);
      exit(10);
    }

    /* we're displaying a beautiful 100% */
    n = printf("\r %5.2f%% (%u/%u/%u different superblocks, %lu dir. stubs)",
	       100.0,
	       ((sb_pool_size == 0) ? 0 : (sb_pool_size - 1)),
	       nb_sb_found,
	       nb_magicnum_found,
	       nb_dirstub_found); fflush(stdout);

    /* hidding estimation time */
    for(; n < max_printed; n++)
      printf(" ");
    printf("\n");    
  }
  signal(SIGINT, SIG_DFL);

  printf("Scan finished\n\n");
}

static void save_parts(void) {
  struct fs_part *part;
  char path[MAXPATHLEN];
  int fd;
  
  strcpy(path, dumpto);
  strcat(path, "/parts");

  if((fd = open(path, O_WRONLY | O_TRUNC | O_CREAT, 0666)) == -1)
    INTERNAL_ERROR_EXIT("open: ", strerror(errno));

  for(part = ext2_parts; part; part = part->next) {
    if(write(fd, part, sizeof(struct fs_part)) == -1)
      INTERNAL_ERROR_EXIT("write: ", strerror(errno));

    if(write(fd, part->filename, strlen(part->filename) + 1) == -1)
      INTERNAL_ERROR_EXIT("write: ", strerror(errno));
  }

  close(fd);    
}

static void restore_parts(void) {
  struct fs_part *tmp, **prev;
  char path[MAXPATHLEN], ch;
  int fd, n, i;
  
  strcpy(path, dumpto);
  strcat(path, "/parts");

  if((fd = open(path, O_RDONLY)) == -1)
    INTERNAL_ERROR_EXIT("open: ", strerror(errno));


  prev = &ext2_parts;
  while(1) {
    if( (tmp = (struct fs_part *) malloc(sizeof(struct fs_part))) == NULL )
      INTERNAL_ERROR_EXIT("malloc", strerror(errno));

    if((n = read(fd, tmp, sizeof(struct fs_part))) == -1)
      INTERNAL_ERROR_EXIT("read: ", strerror(errno));

    if(n == 0)
      break;
    else if(n != sizeof(struct fs_part))
      INTERNAL_ERROR_EXIT("can't restore parts.", "");
    
    for(i = 0; i < MAXPATHLEN; i++) {
      if(read(fd, &ch, 1) == -1)
	INTERNAL_ERROR_EXIT("read: ", strerror(errno));

      path[i] = ch;
      if(!ch)
	break;
    }
    if(i >= MAXPATHLEN)
      INTERNAL_ERROR_EXIT("restore_parts: part filename too long", "");

    tmp->fd = open(path, O_RDONLY | O_LARGEFILE);
      
    if(tmp->fd == -1)
      INTERNAL_ERROR_EXIT("open: ", strerror(errno));

    tmp->filename = strdup(path);
    *prev         = tmp;
    prev          = &(tmp->next);
  }
  *prev = NULL;

  close(fd);    
}


static void save_scan_context(struct fs_part *part, off_t offset) {
  char path[MAXPATHLEN];
  int fd;
  
  printf("\n\n!!! Scan interrupted by user !!!\n"
	 "You can restart where it was interrupted by running :\n\n"
	 "   e2retrieve --dumpto=<dir> --resume_scan\n\n");
  
  /* save other things */
  save_parts();
  save_dir_stubs();

  /* save context */
  strcpy(path, dumpto);
  strcat(path, "/interrupt");
  
  if((fd = open(path, O_WRONLY | O_TRUNC | O_CREAT, 0666)) == -1)
    INTERNAL_ERROR_EXIT("open: ", strerror(errno));      
  
  if(write(fd, part->filename, strlen(part->filename) + 1) == -1)
    INTERNAL_ERROR_EXIT("write: ", strerror(errno));
  
  if(write(fd, &offset, sizeof(off_t)) == -1)
    INTERNAL_ERROR_EXIT("write: ", strerror(errno));
  
  close(fd);
}

struct fs_part *search_part_by_filename(const char *filename) {
  struct fs_part *part;
  
  for(part = ext2_parts; part; part = part->next) {
    if(strcmp(part->filename, filename) == 0)
      return part;
  }  

  return NULL;
}

void part_block_bmp_set(struct fs_part *part, BlockNum block, unsigned char val) {
  unsigned char mask = 0xF0;
  
  LOG("part_block_bmp_set : part = %p, block_bmp = %p, block = %lu, nb_block = %lu \n",
      part,
      ((part) ? part->block_bmp : (void*)-1),
      block,
      ((part) ? part->nb_block : (BlockNum)-1));
  assert(part && part->block_bmp && block < part->nb_block);

  if(block % 2 == 0) {
    val = val << 4;
    mask = mask >> 4;
  }
  
  part->block_bmp[block/2] = (part->block_bmp[block/2] & mask) | val;
}

unsigned char part_block_bmp_get(struct fs_part *part, BlockNum block) {
  assert(part && part->block_bmp && block < part->nb_block);

  if(block % 2 == 0)
    return ((part->block_bmp[block/2] & 0xF0) >> 4);
  else
    return (part->block_bmp[block/2] & 0x0F);
}



void do_it(int nbfile, char* files[]) {
  char *old_dumpto;
  struct stat st;
  int i;

  /* tests that first argument doesn't exists */
  if(dumpto == NULL) {
    fprintf(stderr, "ERROR: too few arguments: you must supply a directory name where to dump data.\n");
    exit(1);
  }
  else if(! restart_after_scan && lstat(dumpto, &st) != -1) {
    fprintf(stderr, "ERROR: %s is a file or directory that still exists.\n", dumpto);
    exit(1);
  }

  if(! restart_after_scan) {
    if(mkdir(dumpto, 0777) == -1) {
      fprintf(stderr, "ERROR: %s: %s.\n", dumpto, strerror(errno));
      exit(1);
    }
  }

  old_dumpto = dumpto;
  dumpto = strdup(get_realpath(old_dumpto));
  free(old_dumpto);

  if( ! restart_after_scan) {
    for(i = nbfile-1; i >= 0; i--) {
      struct fs_part *tmp;
      off_t part_size;
      int fd;
      enum fs_part_type type;
      
      if(stat(files[i], &st) == -1)
	INTERNAL_ERROR_EXIT("stat: ", strerror(errno));
      
      if( (tmp = (struct fs_part *) malloc(sizeof(struct fs_part))) == NULL )
	INTERNAL_ERROR_EXIT("malloc", strerror(errno));
      
      if((fd = open(files[i], O_RDONLY | O_LARGEFILE)) == -1)
	INTERNAL_ERROR_EXIT("open: ", strerror(errno));
      
      if(S_ISREG(st.st_mode)) {
	part_size = st.st_size;
	type = PART_TYPE_FILE;
      }
      else if(S_ISBLK(st.st_mode)) {
	unsigned long devsize;
		
	/* BLKGETSIZE64 would be better but I can't find what to do use
	   BLKGETSIZE64. */
	if(ioctl(fd, BLKGETSIZE, &devsize) != 0)
	  INTERNAL_ERROR_EXIT("ioctl(BLKGETSIZE): ", strerror(errno));
	
	part_size = (off_t)devsize * 512;

	/*
	 * Since 2.6 kernel serie, device mapper has changed and we couldn't use the
	 * LVM_BLK_MAJOR macro, but as it isn't used at all, we don't care (for the moment)
	 */
#ifdef LVM_BLK_MAJOR
	if((st.st_rdev >> 8) == LVM_BLK_MAJOR)
	  type = PART_TYPE_LVM;
	else
#endif
	  type = PART_TYPE_BLOCK;	
      }
      else {
	fprintf(stderr, "%s is not a regular file or block device", files[i]);
	exit(1);
      }
      
      tmp->fd           = fd;
      tmp->type         = type;
      tmp->filename     = strdup(get_realpath(files[i]));
      tmp->size         = part_size;  /* ideal case for the moment */
      tmp->max_size     = part_size;
      tmp->aligned      = ( type == PART_TYPE_LVM || type == PART_TYPE_BLOCK );
      tmp->phys_offset  = 0;
      tmp->logi_offset  = 0;

      tmp->first_block  = 0;
      tmp->last_block   = 0;
      tmp->nb_block     = 0;
      tmp->block_bmp    = NULL;

      tmp->next         = ext2_parts;
      ext2_parts = tmp;
    }
  }

  reference_date = mkrefdate();
  printf("Taking the following date as the reference date to search for superblock and files:\n %s\n",
	 ctime(&reference_date));
  IHM_ARGS(display_refdate, ctime(&reference_date));
  
  if(restart_after_scan) {
    printf("Reloading informations...  ");

    restore_parts();
    restore_sb_entrys();
    restore_dir_stubs();
    printf("Done\n");
  }
  else {
    IHM_NOARGS(init_scan);

    global_scan();

    superblock_choose();
    
    /* RECORD SUPERBLOCK, GROUP DESC AND DIRECTORY STUBS AT END OF SCAN */
    /* superblocks are saved at the end of superblock_choose */
    save_parts();
    save_dir_stubs();

    if(stop_after_scan) {
      printf("Process stopped after scan.\n");
      exit(0);
    } 
  }

  superblock_analyse();
  init_inode_data();
  dir_analyse();
  mark_data_blocks();

  /*
  printf("NB INODE: %u %u\n",
	 superblock.s_inodes_count,
	 superblock.s_inodes_count * sizeof(struct ext2_inode));
  */
  scan_for_directory_blocks();

  /* 
     HERE REALLY START DUMP
  */
  total_element_dumped = 0;

  signal(SIGALRM, dump_progress);
  if(setitimer(ITIMER_REAL, &firsttime, NULL) == -1)
    INTERNAL_ERROR_EXIT("setitimer: ", strerror(errno));

  printf("Dumping directory trees...\n");
  dump_trees();

  if(setitimer(ITIMER_REAL, &stoptimer, NULL) == -1)
    INTERNAL_ERROR_EXIT("setitimer: ", strerror(errno));    
  signal(SIGALRM, SIG_IGN);
  printf("\n");

  /*dir_analyse_resting_stubs();*/
  total_element_dumped = 0;
  signal(SIGALRM, orphans_dump_progress);
  if(setitimer(ITIMER_REAL, &firsttime, NULL) == -1)
    INTERNAL_ERROR_EXIT("setitimer: ", strerror(errno));

  inode_search_orphans();

  if(setitimer(ITIMER_REAL, &stoptimer, NULL) == -1)
    INTERNAL_ERROR_EXIT("setitimer: ", strerror(errno));    
  signal(SIGALRM, SIG_IGN);
  printf("\n");
}
