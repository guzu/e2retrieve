#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <stdio.h>
#include <fcntl.h>
#include <errno.h>

#include <linux/fs.h>
#include <linux/hdreg.h>

/*  #define O_LARGEFILE 0100000 */

int main(int argc, char *argv[]) {
  struct stat st;
  int fd, n;
  char ch;
  off_t off, start;
  unsigned long blksize, devsize, sectsize;
  struct hd_geometry geom;

  if((fd = open(argv[1], O_RDONLY | O_LARGEFILE)) == -1) {
    perror("open");
    exit(1);
  }

  if(fstat(fd, &st) == -1) {
    perror("fstat");
    exit(1);
  }

if(0) {
  if(ioctl(fd, BLKGETSIZE, &devsize) != 0) {
    perror("ioctl(BLKGETSIZE)");
    exit(1);
  }

  if(ioctl(fd, HDIO_GETGEO, &geom) != 0) {
    perror("ioctl(HDIO_GETGEO)");
    exit(1);
  }

  printf("geom: %d %d %d %ld\n",
	 geom.heads,
	 geom.sectors,
	 geom.cylinders,
	 geom.start);
  /*
  if(ioctl(fd, BLKSSZGET, &sectsize) != 0) {
    perror("ioctl(BLKSSZGET)");
    exit(1);
  }
  */

  /*
  if(ioctl(fd, BLKBSZGET, &blksize) != 0) {
    perror("ioctl(BLKBSZGET)");
    exit(1);
  }

  printf("dev: %lu  sect: %lu  blk:%lu\n", devsize, sectsize, blksize);
  nb_block = devsize >> ((blksize / sectsize) - 1); */
}
  start = off = 0;

  do {
    unsigned char buff[512];

    //
    // LECTURE NORMALE
    //
    off = start;
    lseek(fd, off, SEEK_SET);
    errno = 0;
    while (read(fd, &buff, 512) == 512) {
      printf("\r%lld -> %lld", start, off);
      off += 512;
    }

    if(start != off)
      printf("\r%lld -> %lld\n", start, off);
    else
      printf("CACA POURRI %lld\n", off);

    //
    // "VERITABLE" ERREUR
    //
    if(errno != EIO)
      break;

    //
    // LECTURE DEGRADEE
    //
    do {
      lseek(fd, off, SEEK_SET);
      errno = 0;
      n = read(fd, &ch, 1);
      off += 512;
      /*    printf("%u %d %d %d\n", off, n, errno, EIO); */
    } while(n == -1 && errno == EIO);

    start = off - 512;

  } while(!errno || errno == EIO);

  if(errno && errno != EIO)
      perror("ERROR:");

  /*
  printf("%u %u\n", off - 512, (off - 512) / 1000000);
  */

  return 0;
}

