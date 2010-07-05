CC=gcc
STRIP=strip

############################################################
# if you want to make a floppy rescue disk based on uClibc
#
#CC=/usr/i386-linux-uclibc/bin/i386-uclibc-gcc
#STRIP=/usr/i386-linux-uclibc/bin/i386-uclibc-strip
############################################################

# -O1 optimization option helps in doing a few more checks ( and is necessary
# for the -Wuninitialized option ) but does not break the debugging (according
# to the GCC manual page).
CFLAGS_DEVEL=-O1 -g -pedantic -Wall -W -Wstrict-prototypes -Wshadow -Wuninitialized
CFLAGS_DIST=-O2 -Wall


################################################################################
################################################################################
#  END OF CUSTOMISATION
################################################################################
################################################################################

OBJS=src/main.o \
	 src/lib.o \
	 src/superblock.o \
	 src/directory.o \
	 src/inode.o \
	 src/block.o

#CFLAGS_COMMON=-D_FILE_OFFSET_BITS=64 -D_LARGEFILE_SOURCE -D_LARGEFILE64_SOURCE -I $(shell pwd)/include
CFLAGS_COMMON=-D_FILE_OFFSET_BITS=64 -D_LARGEFILE_SOURCE -D_LARGEFILE64_SOURCE

ifeq ($(MAKECMDGOALS),devel)
CFLAGS=$(CFLAGS_COMMON) $(CFLAGS_DEVEL)
else
CFLAGS=$(CFLAGS_COMMON) $(CFLAGS_DIST)
endif

# __DEBUG_E2RETRIEVE__ isn't used for the moment
#CFLAGS=-Wall -g -D__DEBUG_E2RETRIEVE__

# -fpack-struct  could be useful for some structures, but (from 'info gcc') :
#
# `-fpack-struct'
#     Pack all structure members together without holes.  Usually you
#     would not want to use this option, since it makes the code
#     suboptimal, and the offsets of structure members won't agree with
#     system libraries.
#

CURRENTDIR := $(notdir $(shell pwd) )
DATE := $(shell date '+%Y%m%d')
ARCHIVE_NAME := $(CURRENTDIR)_$(DATE).tar.gz

all: e2retrieve

devel: e2retrieve

e2retrieve: $(OBJS) 
#	$(CC) -Wl,-static -o $@ $(OBJS)
	$(CC) -o $@ $(OBJS)
ifneq ($(MAKECMDGOALS),devel)
	$(STRIP) $@
endif

version:
	@echo "Making version.h file..."
	echo "#define E2RETRIEVE_VERSION \"$(DATE)\"" > src/version.h
	@echo

tgz: version clean
	rm -f src/*~ core
	find . -type f -exec chmod 0644 {} \;
	find . -type d -exec chmod 0755 {} \;
	chmod ugo+x utils/mke2loop.sh
	cd .. ; tar -c -z --owner=root --group=root -f $(ARCHIVE_NAME) $(CURRENTDIR) ; cd -
	@echo
	@echo " ==> ../$(ARCHIVE_NAME) created"

clean:
	rm -f e2retrieve src/*.o

read-test: read-test.o
	$(CC) -o $@ read-test.o

