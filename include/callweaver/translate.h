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
 * \brief Translate via the use of pseudo channels
 */

#ifndef _CALLWEAVER_TRANSLATE_H
#define _CALLWEAVER_TRANSLATE_H

#define MAX_FORMAT 32

#if defined(__cplusplus) || defined(c_plusplus)
extern "C" {
#endif

#include "callweaver/object.h"
#include "callweaver/registry.h"
#include "callweaver/module.h"
#include "callweaver/frame.h"

/* Declared by individual translators */
struct opbx_translator_pvt;

typedef struct opbx_translator opbx_translator_t;


extern struct opbx_registry translator_registry;


#define opbx_translator_register(ptr) ({ \
	const typeof(ptr) __ptr = (ptr); \
	opbx_object_init_obj(&__ptr->obj, get_modinfo()->self); \
	__ptr->translator_entry.obj = &__ptr->obj; \
	opbx_registry_add(&translator_registry, &__ptr->translator_entry); \
})
#define opbx_translator_unregister(ptr)	opbx_registry_del(&translator_registry, &(ptr)->translator_entry)


struct opbx_trans_pvt;


extern int opbx_translator_init(void);

/*! Chooses the best translation path */
/*! 
 * Given a list of sources, and a designed destination format, which should
   I choose? Returns 0 on success, -1 if no path could be found.  Modifies
   dests and srcs in place 
   */
extern int opbx_translator_best_choice(int *dsts, int *srcs);

/*!Builds a translator path */
/*! 
 * \param dest destination format
 * \param source source format
 * Build a path (possibly NULL) from source to dest 
 * Returns opbx_trans_pvt on success, NULL on failure
 * */
extern struct opbx_trans_pvt *opbx_translator_build_path(int dest, int dest_rate, int source, int source_rate);

/*! Frees a translator path */
/*!
 * \param tr translator path to get rid of
 * Frees the given translator path structure
 */
extern void opbx_translator_free_path(struct opbx_trans_pvt *tr);

/*! translates one or more frames */
/*! 
 * \param tr translator structure to use for translation
 * \param f frame to translate
 * \param consume Whether or not to free the original frame
 * Apply an input frame into the translator and receive zero or one output frames.  Consume
 * determines whether the original frame should be freed
 * Returns an opbx_frame of the new translation format on success, NULL on failure
 */
extern struct opbx_frame *opbx_translate(struct opbx_trans_pvt *tr, struct opbx_frame *f, int consume);

#if defined(__cplusplus) || defined(c_plusplus)
}
#endif

#endif /* _CALLWEAVER_TRANSLATE_H */
