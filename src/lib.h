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

#ifndef __LIB_H__
#define __LIB_H__

long find_motif(const unsigned char *meule_de_foin, unsigned int size_meule,
		const unsigned char *aiguille,      unsigned int size_aiguille);

const char *get_realpath(const char *path);
const char *offset_to_str(long_offset offset);
int is_valid_char(const unsigned char ch);

#endif
