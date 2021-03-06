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
 * \brief Configuration File Parser
 */

#ifndef _CALLWEAVER_CONFIG_H
#define _CALLWEAVER_CONFIG_H

#if defined(__cplusplus) || defined(c_plusplus)
extern "C" {
#endif

#include <stdarg.h>

#include "callweaver/object.h"
#include "callweaver/registry.h"
#include "callweaver/module.h"


struct cw_config;

struct cw_category;

struct cw_variable {
	char *value;
	int lineno;
	struct cw_variable *next;
	char name[0];
};

typedef struct cw_config *config_load_func(const char *database, const char *table, const char *configfile, struct cw_config *config);
typedef struct cw_variable *realtime_var_get(const char *database, const char *table, va_list ap);
typedef struct cw_config *realtime_multi_get(const char *database, const char *table, va_list ap);
typedef int realtime_update(const char *database, const char *table, const char *keyfield, const char *entity, va_list ap);

struct cw_config_engine {
	struct cw_object obj;
	struct cw_registry_entry *reg_entry;
	const char *name;
	config_load_func *load_func;
	realtime_var_get *realtime_func;
	realtime_multi_get *realtime_multi_func;
	realtime_update *update_func;
	struct cw_config_engine *next;
};


extern CW_API_PUBLIC struct cw_registry config_engine_registry;


#define cw_config_engine_register(ptr) ({ \
	const typeof(ptr) __ptr = (ptr); \
	/* We know 0 refs means not initialized because we know how objs work \
	 * internally and we know that registration only happens while the \
	 * module lock is held. \
	 */ \
	if (!cw_object_refs(__ptr)) \
		cw_object_init_obj(&__ptr->obj, CW_OBJECT_CURRENT_MODULE, 0); \
	__ptr->reg_entry = cw_registry_add(&config_engine_registry, 0, &__ptr->obj); \
	0; \
})
#define cw_config_engine_unregister(ptr) ({ \
	const typeof(ptr) __ptr = (ptr); \
	if (__ptr->reg_entry) \
		cw_registry_del(&config_engine_registry, __ptr->reg_entry); \
	0; \
})


/*! \brief Load a config file 
 * \param configfile path of file to open.  If no preceding '/' character, path is considered relative to CW_CONFIG_DIR
 * Create a config structure from a given configuration file.
 *
 * Returns NULL on error, or an cw_config data structure on success
 */
extern CW_API_PUBLIC struct cw_config *cw_config_load(const char *filename);

/*! \brief Destroys a config 
 * \param config pointer to config data structure
 * Free memory associated with a given config
 *
 */
extern CW_API_PUBLIC void cw_config_destroy(struct cw_config *config);

/*! \brief Goes through categories 
 * \param config Which config structure you wish to "browse"
 * \param prev A pointer to a previous category.
 * This funtion is kind of non-intuitive in it's use.  To begin, one passes NULL as the second arguement.  It will return a pointer to the string of the first category in the file.  From here on after, one must then pass the previous usage's return value as the second pointer, and it will return a pointer to the category name afterwards.
 *
 * Returns a category on success, or NULL on failure/no-more-categories
 */
extern CW_API_PUBLIC char *cw_category_browse(struct cw_config *config, const char *prev);

/*! \brief Goes through variables
 * Somewhat similar in intent as the cw_category_browse.
 * List variables of config file category
 *
 * Returns cw_variable list on success, or NULL on failure
 */
extern CW_API_PUBLIC struct cw_variable *cw_variable_browse(const struct cw_config *config, const char *category);

/*! \brief Gets a variable 
 * \param config which (opened) config to use
 * \param category category under which the variable lies
 * \param value which variable you wish to get the data for
 * Goes through a given config file in the given category and searches for the given variable
 *
 * Returns the variable value on success, or NULL if unable to find it.
 */
extern CW_API_PUBLIC char *cw_variable_retrieve(const struct cw_config *config, const char *category, const char *variable);

/*! \brief Retrieve a category if it exists
 * \param config which config to use
 * \param category_name name of the category you're looking for
 * This will search through the categories within a given config file for a match.
 *
 * Returns pointer to category if found, NULL if not.
 */
extern CW_API_PUBLIC struct cw_category *cw_category_get(const struct cw_config *config, const char *category_name);

/*! \brief Check for category duplicates 
 * \param config which config to use
 * \param category_name name of the category you're looking for
 * This will search through the categories within a given config file for a match.
 *
 * Return non-zero if found
 */
extern CW_API_PUBLIC int cw_category_exist(const struct cw_config *config, const char *category_name);

/*! \brief Retrieve realtime configuration 
 * \param family which family/config to lookup
 * \param keyfield which field to use as the key
 * \param lookup which value to look for in the key field to match the entry.
 * This will use builtin configuration backends to look up a particular 
 * entity in realtime and return a variable list of its parameters.  Note
 * that unlike the variables in cw_config, the resulting list of variables
 * MUST be fred with cw_free_runtime() as there is no container.
 */
extern CW_API_PUBLIC struct cw_variable *cw_load_realtime(const char *family, ...);

/*! \brief Retrieve realtime configuration 
 * \param family which family/config to lookup
 * \param keyfield which field to use as the key
 * \param lookup which value to look for in the key field to match the entry.
 * This will use builtin configuration backends to look up a particular 
 * entity in realtime and return a variable list of its parameters. Unlike
 * the cw_load_realtime, this function can return more than one entry and
 * is thus stored inside a taditional cw_config structure rather than 
 * just returning a linked list of variables.
 */
extern CW_API_PUBLIC struct cw_config *cw_load_realtime_multientry(const char *family, ...);

/*! \brief Update realtime configuration 
 * \param family which family/config to be updated
 * \param keyfield which field to use as the key
 * \param lookup which value to look for in the key field to match the entry.
 * \param variable which variable should be updated in the config, NULL to end list
 * \param value the value to be assigned to that variable in the given entity.
 * This function is used to update a parameter in realtime configuration space.
 *
 */
extern CW_API_PUBLIC int cw_update_realtime(const char *family, const char *keyfield, const char *lookup, ...);

/*! \brief Check if realtime engine is configured for family 
 * returns 1 if family is configured in realtime and engine exists
 * \param family which family/config to be checked
*/
extern CW_API_PUBLIC int cw_check_realtime(const char *family);

/*! \brief Free variable list 
 * \param var the linked list of variables to free
 * This function frees a list of variables.
 */
extern CW_API_PUBLIC void cw_variables_destroy(struct cw_variable *var);

int register_config_cli(void);
void read_config_maps(void);

extern CW_API_PUBLIC struct cw_config *cw_config_new(void);
extern CW_API_PUBLIC struct cw_category *cw_config_get_current_category(const struct cw_config *cfg);
extern CW_API_PUBLIC void cw_config_set_current_category(struct cw_config *cfg, const struct cw_category *cat);

extern CW_API_PUBLIC struct cw_category *cw_category_new(const char *name);
extern CW_API_PUBLIC void cw_category_append(struct cw_config *config, struct cw_category *cat);
extern CW_API_PUBLIC int cw_category_delete(struct cw_config *cfg, char *category);
extern CW_API_PUBLIC void cw_category_destroy(struct cw_category *cat);
extern CW_API_PUBLIC struct cw_variable *cw_category_detach_variables(struct cw_category *cat);
extern CW_API_PUBLIC void cw_category_rename(struct cw_category *cat, const char *name);

extern CW_API_PUBLIC struct cw_variable *cw_variable_new(const char *name, const char *value);
extern CW_API_PUBLIC void cw_variable_append(struct cw_category *category, struct cw_variable *variable);
extern CW_API_PUBLIC int cw_variable_delete(struct cw_config *cfg, char *category, char *variable, char *value);

extern CW_API_PUBLIC struct cw_config *cw_config_internal_load(const char *configfile, struct cw_config *cfg);

#if defined(__cplusplus) || defined(c_plusplus)
}
#endif

#endif /* _CALLWEAVER_CONFIG_H */

