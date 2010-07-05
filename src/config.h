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

/*
  The reference date will be used when scanning raw data.
  This can improve the scan to determine if the data analysed can be
  a good guess for what we are searching (superblock, inode table).
  Typically, in the superblock the date of creation
  The date doesn't need to be exact, but must be below the date you
  think you created the filesystem. Just changing the year is sufficent.

  BE CAREFULL, the filesystem can have been created before you adjusted the
  system date... So this is THIS date that must taken into account.

  1990 is before the first release of the Linux OS, so this can be a good
  reference date...
*/
#define REFDATE_YEAR      1990  /* must be greater than 1900 */
#define REFDATE_MONTH     01    /* 01 - 12 */
#define REFDATE_DAY       01    /* 01 - 31 */


/*
  This suffix is completed by a 4 digit number
*/
#define TRUNC_FILE_SUFFIX ".part"


/*
  If the drive is configured with hdparm to prefetch data increasing SCAN_BUFF_SIZE
  will not change the general throughput performance.
  After some tests, 8192 seems a godd value.
*/
#define SCAN_BUFF_SIZE 8192
