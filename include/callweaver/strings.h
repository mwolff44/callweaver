/*
 * CallWeaver -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2005, Digium, Inc.
 *
 * Mark Spencer <markster@digium.com>
 *
 * See http://www.callweaver.org for more information about
 * the CallWeaver project. Please do not directly contact
 * any of the maintainers of this project for assistance;
 * the project provides a web site, mailing lists and IRC
 * channels for your use.
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License Version 2. See the LICENSE file
 * at the top of the source tree.
 */

/*! \file
 * \brief String manipulation functions
 */

#ifndef _CALLWEAVER_STRINGS_H
#define _CALLWEAVER_STRINGS_H

#include <string.h>
#include <stdarg.h>
#include <ctype.h>


static inline int cw_strlen_zero(const char *s)
{
	return (!s || (*s == '\0'));
}

/*!
  \brief Gets a pointer to the first non-whitespace character in a string.
  \param str the input string

  \return a pointer to the first non-whitespace character
 */
static inline char *cw_skip_blanks(char *str)
{
	while (*str && isspace(*str))
		str++;
	return str;
}


/*!
  \brief Trims trailing whitespace characters from a string.
  \param str the input string
  \return a pointer to the NULL following the string
 */
static inline char *cw_trim_blanks(char *str)
{
	char *work = str;

	if (work) {
		work += strlen(work) - 1;
		/* It's tempting to only want to erase after we exit this loop, 
		   but since cw_trim_blanks *could* receive a constant string
		   (which we presumably wouldn't have to touch), we shouldn't
		   actually set anything unless we must, and it's easier just
		   to set each position to \0 than to keep track of a variable
		   for it */
		while ((work >= str) && isspace(*work))
			*(work--) = '\0';
	}
	return str;
}

/*!
  \brief Gets a pointer to first whitespace character in a string.
  \param str the input string
  \return a pointer to the first whitespace character
 */
static inline char *cw_skip_nonblanks(char *str)
{
	while (*str && !isspace(*str))
		str++;
	return str;
}
  
/*!
  \brief Strip leading/trailing whitespace from a string.
  \param s The string to be stripped (will be modified).
  \return The stripped string.

  This functions strips all leading and trailing whitespace
  characters from the input string, and returns a pointer to
  the resulting string. The string is modified in place.
*/
static inline char *cw_strip(char *s)
{
	if (s) {
		s = cw_skip_blanks(s);
		cw_trim_blanks(s);
	}
	return s;
}

/*!
  \brief Size-limited null-terminating string copy.
  \param dst The destination buffer.
  \param src The source string
  \param size The size of the destination buffer
  \return Nothing.

  This is similar to \a strncpy, with two important differences:
    - the destination buffer will \b always be null-terminated
    - the destination buffer is not filled with zeros past the copied string length
  These differences make it slightly more efficient, and safer to use since it will
  not leave the destination buffer unterminated. There is no need to pass an artificially
  reduced buffer size to this function (unlike \a strncpy), and the buffer does not need
  to be initialized to zeroes prior to calling this function.
*/
static inline void cw_copy_string(char *dst, const char *src, size_t size)
{
	while (*src) {
		*dst++ = *src++;
		if (--size)
			continue;
		dst--;
		break;
	}
	*dst = '\0';
}

/*! Make sure something is true */
/*!
 * Determine if a string containing a boolean value is "true".
 * This function checks to see whether a string passed to it is an indication of an "true" value.  It checks to see if the string is "yes", "true", "y", "t", "on" or "1".  
 *
 * Returns 0 if val is a NULL pointer, -1 if "true", and 0 otherwise.
 */
extern CW_API_PUBLIC int cw_true(const char *val);

/*! Make sure something is false */
/*!
 * Determine if a string containing a boolean value is "false".
 * This function checks to see whether a string passed to it is an indication of an "false" value.  It checks to see if the string is "no", "false", "n", "f", "off" or "0".  
 *
 * Returns 0 if val is a NULL pointer, -1 if "false", and 0 otherwise.
 */
extern CW_API_PUBLIC int cw_false(const char *val);


#ifndef HAVE_STRCASESTR
extern CW_API_PUBLIC char *strcasestr(const char *, const char *);
#endif

#if !defined(HAVE_STRNDUP) && !defined(__CW_DEBUG_MALLOC)
extern CW_API_PUBLIC char *strndup(const char *, size_t);
#endif

#ifndef HAVE_STRNLEN
extern CW_API_PUBLIC size_t strnlen(const char *, size_t);
#endif

#if !defined(HAVE_VASPRINTF) && !defined(__CW_DEBUG_MALLOC)
extern CW_API_PUBLIC int vasprintf(char **strp, const char *fmt, va_list ap);
#endif

#ifndef HAVE_STRTOQ
extern CW_API_PUBLIC uint64_t strtoq(const char *nptr, char **endptr, int base);
#endif

#ifndef HAVE_STRSEP
extern CW_API_PUBLIC char* strsep(char** str, const char* delims);
#endif

#ifndef HAVE_SETENV
extern CW_API_PUBLIC int setenv(const char *name, const char *value, int overwrite);
#endif

#ifndef HAVE_UNSETENV
extern CW_API_PUBLIC int unsetenv(const char *name);
#endif

#endif /* _CALLWEAVER_STRINGS_H */
