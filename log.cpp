/* Copyright (c) 2012 Richard Russon (FlatCap)
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation; either version 2 of the License, or (at your option) any later
 * version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU Library General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 59 Temple
 * Place - Suite 330, Boston, MA 02111-1307, USA.
 */


//#ifdef __cplusplus
//#include <cstdarg>
//#include <cstdio>
//#include <cstring>

#include <stdio.h>
#include <stdarg.h>
#include <string.h>

#include "log.h"

//static unsigned int log_level = ~0;
static FILE *file = NULL;

/**
 * _log
 */
__attribute__ ((format (printf, 1, 0)))
static int _log (const char *format, va_list args)
{
	int ret;

	if (!file)
		return 0;

	ret = vfprintf (file, format, args);
	fflush (file);
	return ret;
}


/**
 * log_debug
 */
__attribute__ ((format (printf, 1, 2)))
int log_debug (const char *format, ...)
{
	va_list args;
	int retval;

	if (!file)
		return 0;

	va_start (args, format);
	fprintf (file, "\e[38;5;229m");
	retval = _log (format, args);
	fprintf (file, "\e[0m");
	va_end (args);

	return retval;
}

/**
 * log_error
 */
__attribute__ ((format (printf, 1, 2)))
int log_error (const char *format, ...)
{
	va_list args;
	int retval;

	if (!file)
		return 0;

	va_start (args, format);
	fprintf (file, "\e[31m");
	retval = _log (format, args);
	fprintf (file, "\e[0m");
	va_end (args);

	return retval;
}

/**
 * log_info
 */
__attribute__ ((format (printf, 1, 2)))
int log_info (const char *format, ...)
{
	va_list args;
	int retval;

	if (!file)
		return 0;

	va_start (args, format);
	fprintf (file, "\e[38;5;79m");
	retval = _log (format, args);
	fprintf (file, "\e[0m");
	va_end (args);

	return retval;
}


/**
 * log_init
 */
bool log_init (const char *name)
{
	file = fopen (name, "ae");	// append, close on exec
	//log_info ("log init: %s\n", name);

	if (strncmp (name, "/dev/pts/", 9) == 0) {
		fprintf (file, "\ec");			// reset
		fflush (file);
	}

	return (file != NULL);
}

/**
 * log_close
 */
void log_close (void)
{
	if (!file)
		return;
	fclose (file);
	file = NULL;
}


#if 0
/**
 * log_set_level
 */
unsigned int log_set_level (unsigned int level)
{
	unsigned int old = log_level;
	log_level = level;
	return old;
}

/**
 * log_get_level
 */
unsigned int log_get_level (void)
{
	return log_level;
}

#endif
