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

#define REFDATE_DAY       01    /* 01 - 31 */
#define REFDATE_MONTH     01    /* 01 - 12 */
#define REFDATE_YEAR      1990  /* greater than 1900 */

/* this suffix is completed by a 4 digit number */
#define TRUNC_FILE_SUFFIX ".part"

/* if the drive is configured with hdparm to prefetch data
   increasing SCAN_BUFF_SIZE will not change the general throughput */
#define SCAN_BUFF_SIZE 8192
