/*
 * CallWeaver -- An open source telephony toolkit.
 *
 * Copyright (C) 2009 - 2010, Eris Associates Limited, UK
 *
 * Mike Jagdis <mjagdis@eris-associates.co.uk>
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

/*! \brief a dynamic string suite */

#ifndef _CALLWEAVER_DYNSTR_H
#define _CALLWEAVER_DYNSTR_H

#include <stdarg.h>
#include <stdlib.h>

#include "callweaver/dynarray.h"
#include "callweaver/preprocessor.h"


/* A dynamic string is derived from a dynamic array of chars */
typedef CW_DYNARRAY(char) cw_dynstr_t;

#define CW_DYNSTR_DEFAULT_CHUNK CW_DYNARRAY_DEFAULT_CHUNK


/* \brief Static initializer for a dynamic string. */
#define CW_DYNSTR_INIT	{ \
	.used = 0, \
	.size = 0, \
	.chunk = CW_DYNARRAY_DEFAULT_CHUNK, \
	.error = 0, \
	.data = (char *)"", \
}


/*! \brief Initialize a new dynamic string.
 *
 *	\param ds_p	dynamic string to initialize
 *	\param len	initial length
 *	\param chunk	allocations are rounded up to a multiple of this
 *			(this MUST be a power of 2)
 */
static inline void cw_dynstr_init(cw_dynstr_t *ds_p, size_t len, size_t chunk)
{
	cw_dynarray_init(ds_p, len, chunk);

	/* N.B. We don't care about errors if the malloc failed because any
	 * initial length is just a hint how much might be needed. If this
	 * malloc doesn't happen we might still manage to malloc later when
	 * the space is actually needed.
	 */
	ds_p->error = 0;

	/* Dynamic strings NEVER have a NULL data pointer */
	if (!ds_p->size)
		ds_p->data = (char *)"";
}


/* \brief Reset a dynamic string to contain nothing but do NOT release
 * the memory associated with it.
 *
 *	\param ds_p	dynamic string to reset
 */
static inline void cw_dynstr_reset(cw_dynstr_t *ds_p)
	__attribute__ ((nonnull (1)));

static inline void cw_dynstr_reset(cw_dynstr_t *ds_p)
{
	if (ds_p->size)
		ds_p->data[0] = '\0';
	cw_dynarray_reset(ds_p);
}


/* \brief Return the amount of unused but allocated space in a dynamic
 * string.
 *
 *	\param ds_p	dynamic string to query
 */
static inline size_t cw_dynstr_space(cw_dynstr_t *ds_p)
	__attribute__ ((nonnull (1)));

static inline size_t cw_dynstr_space(cw_dynstr_t *ds_p)
{
	return ds_p->size - ds_p->used;
}


/* \brief Return the length of a dynamic string.
 *
 *	\param ds_p	dynamic string to query
 */
static inline size_t cw_dynstr_end(cw_dynstr_t *ds_p)
	__attribute__ ((nonnull (1)));

static inline size_t cw_dynstr_end(cw_dynstr_t *ds_p)
{
	return ds_p->used;
}


/* \brief Truncate a dynamic string to the given length.
 *
 * The given dynamic string is truncated to the length given and null
 * terminated.
 *
 * \note The given length MUST be less than the current size of the dynamic
 * string and SHOULD be less than the current length of the dynamic string.
 * If the given length is greater than the current length but less than the
 * current size data between the current length and the given length is
 * undefined although the user of the dynamic string is allowed to assume that
 * any data written to that region will not have changed absent any explicit
 * dynamic string operations affecting that region.
 *
 *	\param ds_p	dynamic string to truncate
 *	\param count	length to truncate to
 */
static inline void cw_dynstr_truncate(cw_dynstr_t *ds_p, size_t count)
	__attribute__ ((nonnull (1)));

static inline void cw_dynstr_truncate(cw_dynstr_t *ds_p, size_t count)
{
	ds_p->used = count;
	if (ds_p->size)
		ds_p->data[count] = '\0';
}


/* \brief Steal the data from a dynstr.
 *
 * The data owned by the dynstr is returned and the dynstr is reset to be empty.
 * The returned data is in the form of a malloc'd, null terminated string
 * and must be freed after use.
 *
 * \note Calling cw_dynstr_steal() on a dynstr that is marked as errored will
 * return a string with arbitrary contents.
 *
 * \note Calling cw_dynstr_steal() on an empty dynstr will return NULL rather
 * than a zero length string.
 *
 * In all cases the dynstr is reset to be empty and not in an error state.
 *
 *	\param ds_p	dynamic string to steal data from
 *
 *	\returns the previous contents of the dynstr as a malloc'd, null
 *	terminated string or NULL if the dynstr was empty
 */
static inline char *cw_dynstr_steal(cw_dynstr_t *ds_p)
	__attribute__ ((nonnull (1)));

static inline char *cw_dynstr_steal(cw_dynstr_t *ds_p)
{
	char *data = NULL;

	if (ds_p->size) {
		data = ds_p->data;
		ds_p->size = ds_p->used = 0;
		ds_p->data = (char *)"";
	}

	ds_p->error = 0;

	return data;
}


/* \brief Make sure a dynamic string has at least the given amount of free space
 * already allocated.
 *
 *	\param ds_p	dynamic string to check
 *	\param len	minimum free space required
 */
static inline void cw_dynstr_need(cw_dynstr_t *ds_p, size_t len)
	__attribute__ ((nonnull (1)));

static inline void cw_dynstr_need(cw_dynstr_t *ds_p, size_t len)
{
	cw_dynarray_need(ds_p, len);
}


/* \brief Reset a dynamic string to contain nothing and release all memory
 * that has been allocated to it.
 *
 *	\param ds_p	dynamic string to free
 */
static inline void cw_dynstr_free(cw_dynstr_t *ds_p)
	__attribute__ ((nonnull (1)));

static inline void cw_dynstr_free(cw_dynstr_t *ds_p)
{
	cw_dynarray_free(ds_p);

	/* Dynamic strings NEVER have a NULL data pointer */
	ds_p->data = (char *)"";
}


extern CW_API_PUBLIC int cw_dynstr_vprintf(cw_dynstr_t *ds_p, const char *fmt, va_list ap)
	__attribute__ ((__nonnull__ (1,2)));
extern CW_API_PUBLIC int cw_dynstr_printf(cw_dynstr_t *ds_p, const char *fmt, ...)
	__attribute__ ((__nonnull__ (1,2), __format__ (printf, 2,3)));


/* If you are looking at this trying to fix a weird compile error
 * check the count is a constant integer corresponding to the
 * number of cw_fmtval() arguments, that there are _only_
 * cw_fmtval() arguments after the count (there can be no
 * expressions wrapping cw_fmtval to select one or the other
 * for instance) and that you are not missing a comma after a
 * cw_fmtval().
 * In particular note:
 *
 *    this is legal
 *        if (...)
 *            cw_dynstr_tprintf(...,
 *                cw_fmtval(...),
 *                ...
 *            );
 *        else
 *            cw_dynstr_tprintf(...,
 *                cw_fmtval(...),
 *                ...
 *            );
 *
 *    but this is not
 *        cw_dynstr_tprintf(...,
 *            (... ? cw_fmtval(...) : cw_fmtval(...)),
 *            ...
 *        );
 *
 *    although this is
 *        cw_dynstr_tprintf(...,
 *            cw_fmtval(..., (... ? a : b)),
 *            ...
 *        );
 */

#ifndef CW_DEBUG_COMPILE

/* These are deliberately empty. They only exist to allow compile time
 * syntax checking of _almost_ the actual code rather than the preprocessor
 * expansion. They will be optimized out.
 * Note that we only get _almost_ there. Specifically there is no way to
 * stop the preprocessor eating line breaks so you way get told arg 3
 * doesn't match the format string, but not which cw_fmtval in the
 * list it is talking about. If you can't spot it try compiling
 * with CW_DEBUG_COMPILE defined. This breaks expansion completely
 * so you get accurate line numbers for errors and warnings but then
 * the compiled code will have references to non-existent functions.
 */
static __inline__ int cw_dynstr_tprintf(cw_dynstr_t *ds_p, size_t count, ...)
	__attribute__ ((always_inline, const, unused, no_instrument_function, nonnull (1)));
static __inline__ int cw_dynstr_tprintf(cw_dynstr_t *ds_p __attribute__((unused)), size_t count __attribute__((unused)), ...)
{
	return 0;
}
static __inline__ char *cw_fmtval(const char *fmt, ...)
	__attribute__ ((always_inline, const, unused, no_instrument_function, nonnull (1), format (printf, 1,2)));
static __inline__ char *cw_fmtval(const char *fmt __attribute__((unused)), ...)
{
	return NULL;
}

#  define CW_TPRINTF_DEBRACKET_cw_fmtval(fmt, ...)	fmt, ## __VA_ARGS__
#  define CW_TPRINTF_DO(op, ...)			op(__VA_ARGS__)
#  define CW_TPRINTF_FMT(n, a)				CW_TPRINTF_DO(CW_TPRINTF_FMT_I, n, CW_CPP_CAT(CW_TPRINTF_DEBRACKET_, a))
#  define CW_TPRINTF_FMT_I(n, fmt, ...)			fmt
#  define CW_TPRINTF_ARGS(n, a)				CW_TPRINTF_DO(CW_TPRINTF_ARGS_I, n, CW_CPP_CAT(CW_TPRINTF_DEBRACKET_, a))
#  define CW_TPRINTF_ARGS_I(n, fmt, ...)		, ## __VA_ARGS__

#  define cw_dynstr_tprintf(ds_p, count, ...) ({ \
	(void)cw_dynstr_tprintf(ds_p, count, \
		__VA_ARGS__ \
	); \
	cw_dynstr_printf(ds_p, \
		CW_CPP_CAT(CW_CPP_ITERATE_, count)(0, CW_TPRINTF_FMT, __VA_ARGS__) \
		CW_CPP_CAT(CW_CPP_ITERATE_, count)(0, CW_TPRINTF_ARGS, __VA_ARGS__) \
	); \
   })

#else

extern int cw_dynstr_tprintf(cw_dynstr_t *ds_p, size_t count, ...)
	__attribute__ ((__nonnull__ (1)));

extern char *cw_fmtval(const char *fmt, ...)
	__attribute__ ((__nonnull__ (1), __format__ (printf, 1,2)));

#endif

#endif /* _CALLWEAVER_DYNSTR_H */
