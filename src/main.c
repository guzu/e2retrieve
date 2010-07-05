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

#include <stdlib.h>
#include <getopt.h>

int main(int argc, char *argv[]) {
  int r, w;

  parse_cmdline(argc, argv);

  /* put part files at the beginning of argv */
  w = 0;
  for(r = optind; r < argc; r++)
    argv[w++] = argv[r];

  do_it(w, argv);

  return 0;
}

