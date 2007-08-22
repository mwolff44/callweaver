/*
 * CallWeaver -- An open source telephony toolkit.
 *
 * Copyright (C) 2007, Eris Associates Ltd.
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

/*! \file
 * \brief Clean up on exit function handling
 */

#ifndef _CALLWEAVER_ATEXIT_H
#define _CALLWEAVER_ATEXIT_H

#include "callweaver/object.h"
#include "callweaver/registry.h"


/*! \brief structure associated with registering an exit handler */
struct opbx_atexit {
	struct opbx_object obj;
	struct opbx_registry_entry atexit_entry;
	void (*function)(void);
	char *name;
};


extern struct opbx_registry atexit_registry;


#define opbx_atexit_register(ptr) ({ \
	const typeof(ptr) __ptr = (ptr); \
	opbx_object_init_obj(&__ptr->obj, NULL); \
	/* atexits don't pin the module when registered, but they do pin it \
	 * just before being run or unregistered so the normal puts only \
	 * release the module once we're done. \
	 */ \
	__ptr->obj.module = get_modinfo()->self; \
	__ptr->atexit_entry.obj = &__ptr->obj; \
	opbx_registry_add(&atexit_registry, &__ptr->atexit_entry); \
})
#define opbx_atexit_unregister(ptr)	({ \
	const typeof(ptr) __ptr = (ptr); \
	opbx_module_get(__ptr->obj.module); \
	opbx_registry_del(&atexit_registry, &__ptr->atexit_entry); \
})


#endif /* _CALLWEAVER_ATEXIT_H */