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

#include <sys/param.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "e2retrieve.h"

const char *offset_to_str(long_offset offset) {
  static char str[21];
  int i, m, n = 0;
  char tmp;

  if(offset == 0) {
    strcpy(str, "0");
    return str;
  }

  if(offset < 0) {
    strcpy(str, "neg");
    return str;
  }

  while(offset > 0) {
    str[n++] = '0' + (offset % 10);
    offset /= 10;
  }
  str[n] = '\0';

  n--;
  m = n / 2;
  for(i = 0; i <= m; i++) {
    tmp = str[i];
    str[i] = str[n - i];
    str[n - i] = tmp;
  }

  return str;
}

long find_motif(const unsigned char *meule_de_foin, unsigned int size_meule,
		const unsigned char *aiguille,      unsigned int size_aiguille)
{
  unsigned long i;

  for(i = 0; i < size_meule - size_aiguille; i++) {
    if(memcmp(meule_de_foin + i, aiguille, size_aiguille) == 0)
      return i;
  }

  return -1;
}


const char *get_realpath(const char *path) {
  static char rpath[MAXPATHLEN];

  errno = 0;
  if(realpath(path, rpath) != rpath)
    INTERNAL_ERROR_EXIT("realpath: ", strerror(errno));

  return rpath;
}

int is_valid_char(const unsigned char ch) {
  static char authorized_char_french[] = "חיטךכאגשûןמפ";
  char *authorized_char_set = authorized_char_french;

  if(ch >= 32 && ch < 127 && ch != '/')
    return 1;

  if(strchr(authorized_char_set, ch) == NULL)
    return 0;
  else
    return 1;
}
