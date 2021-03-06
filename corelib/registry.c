/*
 * CallWeaver -- An open source telephony toolkit.
 *
 * Copyright (C) 2007-2008, Eris Associates Ltd, UK
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
 *
 * \brief Registry API
 * 
 */
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <signal.h>

#include "callweaver.h"

CALLWEAVER_FILE_VERSION("$HeadURL$", "$Revision$")

#include "callweaver/lock.h"
#include "callweaver/options.h"
#include "callweaver/object.h"
#include "callweaver/registry.h"
#include "callweaver/logger.h"
#include "callweaver/utils.h"


#define registry_begin(registry) atomic_inc(&(registry)->inuse);

#define registry_end(registry) do { \
	struct cw_registry *reg = (registry); \
	struct cw_list *del = reg->del; \
	if (atomic_dec_and_test(&reg->inuse)) \
		registry_purge(reg, del); \
} while (0)


static void registry_purge(struct cw_registry *registry, struct cw_list *list)
{
	pthread_mutex_lock(&registry->lock);
	if ((registry->del == list))
		registry->del = NULL;
	else
		list = NULL;
	pthread_mutex_unlock(&registry->lock);

	while (list) {
		struct cw_registry_entry *entry = container_of(list, struct cw_registry_entry, list);
		list = list->del;
		cw_object_put_obj(entry->obj);
		free(entry);
	}
}

struct cw_registry_entry *cw_registry_add(struct cw_registry *registry, unsigned int hash, struct cw_object *obj)
{
	struct cw_registry_entry *entry = malloc(sizeof(*entry));

	if (entry) {
		cw_list_init(&entry->list);
		entry->obj = cw_object_get_obj(obj);
		entry->hash = hash;

		pthread_mutex_lock(&registry->lock);
		cw_list_add(&registry->list[hash % registry->size], &entry->list);
		registry->entries++;
		pthread_mutex_unlock(&registry->lock);

		if (registry->onchange)
			registry->onchange();
	} else {
		cw_log(CW_LOG_ERROR, "Out of memory\n");
	}

	return entry;
}


int cw_registry_del(struct cw_registry *registry, struct cw_registry_entry *entry)
{
	registry_begin(registry);

	pthread_mutex_lock(&registry->lock);
	cw_list_del(&registry->del, &entry->list);
	registry->entries--;
	pthread_mutex_unlock(&registry->lock);

	if (registry->onchange)
		registry->onchange();

	registry_end(registry);

	return 0;
}


int cw_registry_replace(struct cw_registry *registry, unsigned int hash, const void *pattern, struct cw_object *obj)
{
	struct cw_registry_entry *entry;
	struct cw_list *list;
	int ret = -1;

	entry = NULL;

	registry_begin(registry);

	if (obj && !(entry = cw_registry_add(registry, hash, obj)))
		goto out;

	if (pattern && registry->match) {
		cw_list_for_each(list, (entry ? &entry->list : &registry->list[hash % registry->size])) {
			struct cw_registry_entry *entry2 = container_of(list, struct cw_registry_entry, list);
			if (entry2->hash == hash && registry->match(entry2->obj, pattern)) {
				cw_registry_del(registry, entry2);
				break;
			}
		}
	}

	ret = 0;

out:
	registry_end(registry);

	return ret;
}


int cw_registry_iterate(struct cw_registry *registry, int (*func)(struct cw_object *, void *), void *data)
{
	struct cw_list *list;
	int i, ret = 0;

	registry_begin(registry);

	for (i = 0; i < registry->size; i++) {
		cw_list_for_each(list, &registry->list[i]) {
			struct cw_registry_entry *entry = container_of(list, struct cw_registry_entry, list);
			if ((ret = func(entry->obj, data)))
				goto scan_complete;
		}
	}
scan_complete:

	registry_end(registry);

	return ret;
}


int cw_registry_iterate_rev(struct cw_registry *registry, int (*func)(struct cw_object *, void *), void *data)
{
	struct cw_list *list;
	int i, ret = 0;

	registry_begin(registry);

	for (i = 0; i < registry->size; i++) {
		cw_list_for_each_rev(list, &registry->list[i]) {
			struct cw_registry_entry *entry = container_of(list, struct cw_registry_entry, list);
			if ((ret = func(entry->obj, data)))
				goto scan_complete;
		}
	}
scan_complete:

	registry_end(registry);

	return ret;
}


int cw_registry_iterate_ordered(struct cw_registry *registry, int (*func)(struct cw_object *, void *), void *data)
{
	struct cw_object **objs;
	struct cw_list *list;
	int size, n, i, ret = -1;

	if ((objs = malloc((size = registry->entries + 1) * sizeof(objs[0])))) {
		ret = 0;

		registry_begin(registry);

		for (n = 0, i = 0; i < registry->size; i++) {
			cw_list_for_each(list, &registry->list[i]) {
				struct cw_registry_entry *entry = container_of(list, struct cw_registry_entry, list);
				objs[n++] = cw_object_dup_obj(entry->obj);
				if (unlikely(n == size && !(objs = realloc(objs, (size += 4) * sizeof(objs[0]))))) {
					cw_log(CW_LOG_ERROR, "Out of memory!\n");
					ret = -1;
					goto skip_action;
				}
			}
		}

		registry_end(registry);

		qsort(objs, n, sizeof(objs[0]), registry->qsort_compare);

		for (i = 0; i < n; i++) {
			if ((ret = func(objs[i], data)))
				break;
		}

skip_action:
		for (i = 0; i < n; i++)
			cw_object_put_obj(objs[i]);

		free(objs);
	} else
		cw_log(CW_LOG_ERROR, "Out of memory!\n");

	return ret;
}


struct cw_object *cw_registry_find(struct cw_registry *registry, int have_hash, unsigned int hash, const void *pattern)
{
	struct cw_object *obj = NULL;
	struct cw_list *list;
	int i;

	registry_begin(registry);

	i = (have_hash ? hash % registry->size : 0);
	do {
		cw_list_for_each(list, &registry->list[i]) {
			struct cw_registry_entry *entry = container_of(list, struct cw_registry_entry, list);
			if ((!have_hash || entry->hash == hash) && registry->match && registry->match(entry->obj, pattern)) {
				obj = cw_object_dup_obj(entry->obj);
				break;
			}
		}
		i++;
	} while (!have_hash && !obj && i < registry->size);

	registry_end(registry);

	return obj;
}


int cw_registry_init(struct cw_registry *registry, size_t estsize)
{
	int i;

	if ((registry->list = malloc(sizeof(*registry->list) * estsize))) {
		registry->size = estsize;
		registry->del = NULL;
		registry->entries = 0;
		pthread_mutex_init(&registry->lock, &global_mutexattr_simple);
		atomic_set(&registry->inuse, 0);
		for (i = 0; i < estsize; i++)
			cw_list_init(&registry->list[i]);
		return 0;
	}

	cw_log(CW_LOG_ERROR, "Out of memory");
	return -1;
}


void cw_registry_flush(struct cw_registry *registry)
{
	struct cw_list *list;
	int i;

	registry_begin(registry);

	for (i = 0; i < registry->size; i++) {
		cw_list_for_each(list, &registry->list[i]) {
			cw_list_del(&registry->del, list);
		}
	}

	registry_end(registry);
}


void cw_registry_destroy(struct cw_registry *registry)
{
	cw_registry_flush(registry);
	pthread_mutex_destroy(&registry->lock);
	free(registry->list);
}
