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
CFLAGS_DEVEL=-O1 -g -Wall -W -Wstrict-prototypes -Wshadow -Wuninitialized \
	-Wpointer-arith -Wcast-qual -Wcast-align -Wnested-externs
#CFLAGS_DEVEL=-O1 -g -pedantic -Wall -W -Wstrict-prototypes -Wshadow -Wuninitialized \
#	-Wpointer-arith -Wcast-qual -Wcast-align -Wconversion -Wnested-externs
CFLAGS_DIST=-O2 -Wall


################################################################################
################################################################################
#  END OF CUSTOMISATION
################################################################################
################################################################################

OBJS_COMMON=src/core.o \
	src/lib.o \
	src/superblock.o \
	src/directory.o \
	src/inode.o \
	src/block.o

OBJS_TXT=src/main.o
OBJS_GTK=src/ge2r.o


#CFLAGS_COMMON=-D_FILE_OFFSET_BITS=64 -D_LARGEFILE_SOURCE -D_LARGEFILE64_SOURCE -I $(shell pwd)/include
CFLAGS_COMMON=-D_FILE_OFFSET_BITS=64 -D_LARGEFILE_SOURCE -D_LARGEFILE64_SOURCE `gtk-config --cflags`

ifeq ($(MAKECMDGOALS),devel)
CFLAGS=$(CFLAGS_COMMON) $(CFLAGS_DEVEL)
# __DEBUG_E2RETRIEVE__ isn't used for the moment
#CFLAGS=$(CFLAGS) -D__DEBUG_E2RETRIEVE__
else
CFLAGS=$(CFLAGS_COMMON) $(CFLAGS_DIST)
endif


PROJECT_NAME = e2retrieve
IHM_NAME = ge2r
CURRENTDIR := $(notdir $(shell pwd) )
DATE := $(shell date '+%Y%m%d')
ARCHIVE_NAME := $(CURRENTDIR)_$(DATE).tar.gz

all: show_warning $(PROJECT_NAME)

devel: $(PROJECT_NAME) $(IHM_NAME)

$(PROJECT_NAME): $(OBJS_COMMON) $(OBJS_TXT)
#	$(CC) -Wl,-static -o $@ $(OBJS_COMMON) $(OBJS_TXT)
	$(CC) -o $@ $(OBJS_COMMON) $(OBJS_TXT)
ifneq ($(MAKECMDGOALS),devel)
	$(STRIP) $@
endif

$(IHM_NAME): $(OBJS_COMMON) $(OBJS_GTK)
	$(CC)  `gtk-config --libs` -o $@ $(OBJS_COMMON) $(OBJS_GTK)

show_warning:
	@echo
	@echo "INFO:"
	@echo "INFO: You can read and edit the config.h file before compiling e2retrieve."
	@echo "INFO:"
	@echo

version:
	@echo "Making version.h file..."
	echo "#define E2RETRIEVE_VERSION \"$(DATE)\"" > src/version.h
	@echo

tgz: version clean dist-clean
	chmod ugo+x utils/mke2loop.sh
	cd .. ; tar -c -z --owner=root --group=root -f $(ARCHIVE_NAME) $(CURRENTDIR) ; cd -
	@echo
	@echo " ==> ../$(ARCHIVE_NAME) created"

clean:
	rm -f $(PROJECT_NAME) $(IHM_NAME) src/*.o

dist-clean:
	rm -f src/*~ core
	find . -type f -exec chmod 0644 {} \;
	find . -type d -exec chmod 0755 {} \;

read-test: read-test.o
	$(CC) -o $@ read-test.o

