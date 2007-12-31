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
 * \brief Scheduler Routines (derived from cheops)
 */

#ifndef _CALLWEAVER_SCHED_H
#define _CALLWEAVER_SCHED_H

#if defined(__cplusplus) || defined(c_plusplus)
extern "C" {
#endif

/*! Max num of schedule structs */
/*!
 * The max number of schedule structs to keep around
 * for use.  Undefine to disable schedule structure
 * caching. (Only disable this on very low memory 
 * machines)
 */
#define SCHED_MAX_CACHE 128

struct sched_context;

/*! New schedule context */
/* !
 * Create a scheduling context
 * Returns a malloc'd sched_context structure, NULL on failure
 */
extern struct sched_context *sched_context_create(void);

/*! destroys a schedule context */
/*!
 * \param c Context to free
 * Destroys (free's) the given sched_context structure
 * Returns 0 on success, -1 on failure
 */
void sched_context_destroy(struct sched_context *c);

/*! callback for a cheops scheduler */
/*! 
 * A cheops scheduler callback takes a pointer with callback data and
 * returns a 0 if it should not be run again, or non-zero if it should be
 * rescheduled to run again
 */
typedef int (*opbx_sched_cb)(void *data);
#define OPBX_SCHED_CB(a) ((opbx_sched_cb)(a))

/*!Adds a scheduled event */
/*! 
 * \param con Schduler context to add
 * \param when how many milliseconds to wait for event to occur
 * \param callback function to call when the amount of time expires
 * \param data data to pass to the callback
 * Schedule an event to take place at some point in the future.  callback 
 * will be called with data as the argument, when milliseconds into the
 * future (approximately)
 * If callback returns 0, no further events will be re-scheduled
 * Returns a schedule item ID on success, -1 on failure
 */
extern int opbx_sched_add(struct sched_context *con, int when, opbx_sched_cb callback, void *data);

/*!Adds a scheduled event */
/*! 
 * \param con Schduler context to add
 * \param when how many milliseconds to wait for event to occur
 * \param callback function to call when the amount of time expires
 * \param data data to pass to the callback
 * \param variable If true, the result value of callback function will be 
 *       used for rescheduling
 * Schedule an event to take place at some point in the future.  callback 
 * will be called with data as the argument, when milliseconds into the
 * future (approximately)
 * If callback returns 0, no further events will be re-scheduled
 * Returns a schedule item ID on success, -1 on failure
 */
extern int opbx_sched_add_variable(struct sched_context *con, int when, opbx_sched_cb callback, void *data, int variable);

/*! Deletes a scheduled event */
/*!
 * \param con scheduling context to delete item from
 * \param id ID of the scheduled item to delete
 * Remove this event from being run.  A procedure should not remove its
 * own event, but return 0 instead.
 * Returns 0 on success, -1 on failure
 */
extern int opbx_sched_del(struct sched_context *con, int id);

/*!Returns the number of seconds before an event takes place */
/*!
 * \param con Context to use
 * \param id Id to dump
 */
extern long opbx_sched_when(struct sched_context *con,int id);


#if defined(__cplusplus) || defined(c_plusplus)
}
#endif

#endif /* _CALLWEAVER_SCHED_H */
