/*
 * CallWeaver -- An open source telephony toolkit.
 *
 * Copyright (C) 2003-2005, Digium, Inc.
 *
 * Brian K. West <brian@bkw.org>
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
 * \brief ODBC CDR Backend
 * 
 * \author Brian K. West <brian at bkw.org>
 *
 * See also:
 * \arg http://www.unixodbc.org
 * \arg \ref Config_cdr
 */
#include <sys/types.h>
#include <stdio.h>
#include <string.h>

#include <stdlib.h>
#include <unistd.h>
#include <time.h>

#ifndef __CYGWIN__
#include <sql.h>
#include <sqlext.h>
#include <sqltypes.h>
#else
#include <windows.h>
#include <w32api/sql.h>
#include <w32api/sqlext.h>
#include <w32api/sqltypes.h>
#endif

#include "callweaver.h"

CALLWEAVER_FILE_VERSION("$HeadURL$", "$Revision$")

#include "callweaver/config.h"
#include "callweaver/options.h"
#include "callweaver/channel.h"
#include "callweaver/cdr.h"
#include "callweaver/module.h"
#include "callweaver/logger.h"

#define DATE_FORMAT "%Y-%m-%d %T"

static const char desc[] = "ODBC CDR Backend";
static const char name[] = "ODBC";
static const char config[] = "cdr_odbc.conf";
static char *dsn = NULL, *username = NULL, *password = NULL, *table = NULL;
static int loguniqueid = 0;
static int usegmtime = 0;
static int dispositionstring = 0;
static int connected = 0;

CW_MUTEX_DEFINE_STATIC(odbc_lock);

static int odbc_do_query(void);
static int odbc_init(void);

static SQLHENV	ODBC_env = SQL_NULL_HANDLE;	/* global ODBC Environment */
static SQLHDBC	ODBC_con;			/* global ODBC Connection Handle */
static SQLHSTMT	ODBC_stmt;			/* global ODBC Statement Handle */

static int odbc_log(struct cw_cdr *batch)
{
	unsigned char sqlcmd[2048];
	unsigned char ODBC_msg[200], ODBC_stat[10];
	char timestr[128];
	struct tm tm;
	struct cw_cdr *cdrset, *cdr;
	SQLINTEGER ODBC_err;
	short int ODBC_mlen;
	int ODBC_res;
	int res = 0;

	cw_mutex_lock(&odbc_lock);

	while ((cdrset = batch)) {
		batch = batch->batch_next;

		while ((cdr = cdrset)) {
			cdrset = cdrset->next;

			if (usegmtime)
				gmtime_r(&cdr->start.tv_sec,&tm);
			else
				localtime_r(&cdr->start.tv_sec,&tm);

			strftime(timestr, sizeof(timestr), DATE_FORMAT, &tm);

			if (loguniqueid) {
				snprintf((char *) sqlcmd,sizeof(sqlcmd),"INSERT INTO %s "
				"(calldate,clid,src,dst,dcontext,channel,dstchannel,lastapp,"
				"lastdata,duration,billsec,disposition,amaflags,accountcode,uniqueid,userfield) "
				"VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)", table);
			} else {
				snprintf((char *) sqlcmd,sizeof(sqlcmd),"INSERT INTO %s "
				"(calldate,clid,src,dst,dcontext,channel,dstchannel,lastapp,lastdata,"
				"duration,billsec,disposition,amaflags,accountcode) "
				"VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?)", table);
			}

			if (!connected) {
				res = odbc_init();
				if (res < 0) {
					connected = 0;
					cw_mutex_unlock(&odbc_lock);
					return 0;
				}
			}

			ODBC_res = SQLAllocHandle(SQL_HANDLE_STMT, ODBC_con, &ODBC_stmt);

			if ((ODBC_res != SQL_SUCCESS) && (ODBC_res != SQL_SUCCESS_WITH_INFO)) {
				if (option_verbose > 10)
					cw_verbose( VERBOSE_PREFIX_4 "cdr_odbc: Failure in AllocStatement %d\n", ODBC_res);
				SQLGetDiagRec(SQL_HANDLE_DBC, ODBC_con, 1, ODBC_stat, &ODBC_err, ODBC_msg, 100, &ODBC_mlen);
				SQLFreeHandle(SQL_HANDLE_STMT, ODBC_stmt);
				connected = 0;
				cw_mutex_unlock(&odbc_lock);
				return 0;
			}

			/* We really should only have to do this once.  But for some
			   strange reason if I don't it blows holes in memory like
			   like a shotgun.  So we just do this so its safe. */

			ODBC_res = SQLPrepare(ODBC_stmt, sqlcmd, SQL_NTS);

			if ((ODBC_res != SQL_SUCCESS) && (ODBC_res != SQL_SUCCESS_WITH_INFO)) {
				if (option_verbose > 10)
					cw_verbose( VERBOSE_PREFIX_4 "cdr_odbc: Error in PREPARE %d\n", ODBC_res);
				SQLGetDiagRec(SQL_HANDLE_DBC, ODBC_con, 1, ODBC_stat, &ODBC_err, ODBC_msg, 100, &ODBC_mlen);
				SQLFreeHandle(SQL_HANDLE_STMT, ODBC_stmt);
				connected = 0;
				cw_mutex_unlock(&odbc_lock);
				return 0;
			}

			SQLBindParameter(ODBC_stmt, 1, SQL_PARAM_INPUT, SQL_C_CHAR, SQL_CHAR, sizeof(timestr), 0, &timestr, 0, NULL);
			SQLBindParameter(ODBC_stmt, 2, SQL_PARAM_INPUT, SQL_C_CHAR, SQL_CHAR, sizeof(cdr->clid), 0, cdr->clid, 0, NULL);
			SQLBindParameter(ODBC_stmt, 3, SQL_PARAM_INPUT, SQL_C_CHAR, SQL_CHAR, sizeof(cdr->src), 0, cdr->src, 0, NULL);
			SQLBindParameter(ODBC_stmt, 4, SQL_PARAM_INPUT, SQL_C_CHAR, SQL_CHAR, sizeof(cdr->dst), 0, cdr->dst, 0, NULL);
			SQLBindParameter(ODBC_stmt, 5, SQL_PARAM_INPUT, SQL_C_CHAR, SQL_CHAR, sizeof(cdr->dcontext), 0, cdr->dcontext, 0, NULL);
			SQLBindParameter(ODBC_stmt, 6, SQL_PARAM_INPUT, SQL_C_CHAR, SQL_CHAR, sizeof(cdr->channel), 0, cdr->channel, 0, NULL);
			SQLBindParameter(ODBC_stmt, 7, SQL_PARAM_INPUT, SQL_C_CHAR, SQL_CHAR, sizeof(cdr->dstchannel), 0, cdr->dstchannel, 0, NULL);
			SQLBindParameter(ODBC_stmt, 8, SQL_PARAM_INPUT, SQL_C_CHAR, SQL_CHAR, sizeof(cdr->lastapp), 0, cdr->lastapp, 0, NULL);
			SQLBindParameter(ODBC_stmt, 9, SQL_PARAM_INPUT, SQL_C_CHAR, SQL_CHAR, sizeof(cdr->lastdata), 0, cdr->lastdata, 0, NULL);
			SQLBindParameter(ODBC_stmt, 10, SQL_PARAM_INPUT, SQL_C_SLONG, SQL_INTEGER, 0, 0, &cdr->duration, 0, NULL);
			SQLBindParameter(ODBC_stmt, 11, SQL_PARAM_INPUT, SQL_C_SLONG, SQL_INTEGER, 0, 0, &cdr->billsec, 0, NULL);
			if (dispositionstring) {
				const char *s = cw_cdr_disp2str(cdr->disposition);
				SQLBindParameter(ODBC_stmt, 12, SQL_PARAM_INPUT, SQL_C_CHAR, SQL_CHAR, strlen(s) + 1, 0, (char *)s, 0, NULL);
			} else
				SQLBindParameter(ODBC_stmt, 12, SQL_PARAM_INPUT, SQL_C_SLONG, SQL_INTEGER, 0, 0, &cdr->disposition, 0, NULL);
			SQLBindParameter(ODBC_stmt, 13, SQL_PARAM_INPUT, SQL_C_SLONG, SQL_INTEGER, 0, 0, &cdr->amaflags, 0, NULL);
			SQLBindParameter(ODBC_stmt, 14, SQL_PARAM_INPUT, SQL_C_CHAR, SQL_CHAR, sizeof(cdr->accountcode), 0, cdr->accountcode, 0, NULL);

			if (loguniqueid) {
				SQLBindParameter(ODBC_stmt, 15, SQL_PARAM_INPUT, SQL_C_CHAR, SQL_CHAR, sizeof(cdr->uniqueid), 0, cdr->uniqueid, 0, NULL);
				SQLBindParameter(ODBC_stmt, 16, SQL_PARAM_INPUT, SQL_C_CHAR, SQL_CHAR, sizeof(cdr->userfield), 0, cdr->userfield, 0, NULL);
			}

			if (connected) {
				res = odbc_do_query();
				if (res < 0) {
					if (option_verbose > 10)
						cw_verbose( VERBOSE_PREFIX_4 "cdr_odbc: Query FAILED Call not logged!\n");
					res = odbc_init();
					if (option_verbose > 10)
						cw_verbose( VERBOSE_PREFIX_4 "cdr_odbc: Reconnecting to dsn %s\n", dsn);
					if (res < 0) {
						if (option_verbose > 10)
							cw_verbose( VERBOSE_PREFIX_4 "cdr_odbc: %s has gone away!\n", dsn);
						connected = 0;
					} else {
						if (option_verbose > 10)
							cw_verbose( VERBOSE_PREFIX_4 "cdr_odbc: Trying Query again!\n");
						res = odbc_do_query();
						if (res < 0) {
							if (option_verbose > 10)
								cw_verbose( VERBOSE_PREFIX_4 "cdr_odbc: Query FAILED Call not logged!\n");
						}
					}
				}
			} else {
				if (option_verbose > 10)
					cw_verbose( VERBOSE_PREFIX_4 "cdr_odbc: Query FAILED Call not logged!\n");
			}
			SQLFreeHandle(SQL_HANDLE_STMT, ODBC_stmt);
		}
	}

	cw_mutex_unlock(&odbc_lock);
	return 0;
}


static void release(void)
{
	cw_mutex_lock(&odbc_lock);
	if (connected) {
		if (option_verbose > 10)
			cw_verbose( VERBOSE_PREFIX_4 "cdr_odbc: Disconnecting from %s\n", dsn);
		SQLFreeHandle(SQL_HANDLE_STMT, ODBC_stmt);
		SQLDisconnect(ODBC_con);
		SQLFreeHandle(SQL_HANDLE_DBC, ODBC_con);
		SQLFreeHandle(SQL_HANDLE_ENV, ODBC_env);
		connected = 0;
	}

	free(dsn);
	free(username);
	free(password);
	free(table);

	cw_mutex_unlock(&odbc_lock);
}


static struct cw_cdrbe cdrbe = {
	.name = name,
	.description = desc,
	.handler = odbc_log,
};


static int unload_module(void)
{
	cw_cdrbe_unregister(&cdrbe);
	return 0;
}

static int load_module(void)
{
	int res = 0;
	struct cw_config *cfg;
	struct cw_variable *var;
	const char *tmp;

	cw_cdrbe_register(&cdrbe);

	cw_mutex_lock(&odbc_lock);

	cfg = cw_config_load(config);
	if (!cfg) {
		cw_log(CW_LOG_WARNING, "cdr_odbc: Unable to load config for ODBC CDR's: %s\n", config);
		goto out;
	}
	
	var = cw_variable_browse(cfg, "global");
	if (!var) {
		/* nothing configured */
		goto out;
	}

	tmp = cw_variable_retrieve(cfg,"global","dsn");
	if (tmp == NULL) {
		cw_log(CW_LOG_WARNING,"cdr_odbc: dsn not specified.  Assuming callweaverdb\n");
		tmp = "callweaverdb";
	}
	dsn = strdup(tmp);
	if (dsn == NULL) {
		cw_log(CW_LOG_ERROR,"cdr_odbc: Out of memory error.\n");
		res = -1;
		goto out;
	}

	tmp = cw_variable_retrieve(cfg,"global","dispositionstring");
	if (tmp) {
		dispositionstring = cw_true(tmp);
	} else {
		dispositionstring = 0;
	}
		
	tmp = cw_variable_retrieve(cfg,"global","username");
	if (tmp) {
		username = strdup(tmp);
		if (username == NULL) {
			cw_log(CW_LOG_ERROR,"cdr_odbc: Out of memory error.\n");
			res = -1;
			goto out;
		}
	}

	tmp = cw_variable_retrieve(cfg,"global","password");
	if (tmp) {
		password = strdup(tmp);
		if (password == NULL) {
			cw_log(CW_LOG_ERROR,"cdr_odbc: Out of memory error.\n");
			res = -1;
			goto out;
		}
	}

	tmp = cw_variable_retrieve(cfg,"global","loguniqueid");
	if (tmp) {
		loguniqueid = cw_true(tmp);
		if (loguniqueid) {
			cw_log(CW_LOG_DEBUG,"cdr_odbc: Logging uniqueid\n");
		} else {
			cw_log(CW_LOG_DEBUG,"cdr_odbc: Not logging uniqueid\n");
		}
	} else {
		cw_log(CW_LOG_DEBUG,"cdr_odbc: Not logging uniqueid\n");
		loguniqueid = 0;
	}

	tmp = cw_variable_retrieve(cfg,"global","usegmtime");
	if (tmp) {
		usegmtime = cw_true(tmp);
		if (usegmtime) {
			cw_log(CW_LOG_DEBUG,"cdr_odbc: Logging in GMT\n");
		} else {
			cw_log(CW_LOG_DEBUG,"cdr_odbc: Not logging in GMT\n");
		}
	} else {
		cw_log(CW_LOG_DEBUG,"cdr_odbc: Not logging in GMT\n");
		usegmtime = 0;
	}

	tmp = cw_variable_retrieve(cfg,"global","table");
	if (tmp == NULL) {
		cw_log(CW_LOG_WARNING,"cdr_odbc: table not specified.  Assuming cdr\n");
		tmp = "cdr";
	}
	table = strdup(tmp);
	if (table == NULL) {
		cw_log(CW_LOG_ERROR,"cdr_odbc: Out of memory error.\n");
		res = -1;
		goto out;
	}

	cw_config_destroy(cfg);
	if (option_verbose > 2) {
		cw_verbose( VERBOSE_PREFIX_3 "cdr_odbc: dsn is %s\n",dsn);
		if (username)
		{
			cw_verbose( VERBOSE_PREFIX_3 "cdr_odbc: username is %s\n",username);
			cw_verbose( VERBOSE_PREFIX_3 "cdr_odbc: password is [secret]\n");
		}
		else
			cw_verbose( VERBOSE_PREFIX_3 "cdr_odbc: retreiving username and password from odbc config\n");
		cw_verbose( VERBOSE_PREFIX_3 "cdr_odbc: table is %s\n",table);
	}
	
	res = odbc_init();
	if (res < 0) {
		cw_log(CW_LOG_ERROR, "cdr_odbc: Unable to connect to datasource: %s\n", dsn);
		if (option_verbose > 2) {
			cw_verbose( VERBOSE_PREFIX_3 "cdr_odbc: Unable to connect to datasource: %s\n", dsn);
		}
	}
out:
	cw_mutex_unlock(&odbc_lock);
	return res;
}

static int odbc_do_query(void)
{
	SQLINTEGER ODBC_err;
	int ODBC_res;
	short int ODBC_mlen;
	unsigned char ODBC_msg[200], ODBC_stat[10];
	
	ODBC_res = SQLExecute(ODBC_stmt);
	
	if ((ODBC_res != SQL_SUCCESS) && (ODBC_res != SQL_SUCCESS_WITH_INFO)) {
		if (option_verbose > 10)
			cw_verbose( VERBOSE_PREFIX_4 "cdr_odbc: Error in Query %d\n", ODBC_res);
		SQLGetDiagRec(SQL_HANDLE_DBC, ODBC_con, 1, ODBC_stat, &ODBC_err, ODBC_msg, 100, &ODBC_mlen);
		SQLFreeHandle(SQL_HANDLE_STMT, ODBC_stmt);
		connected = 0;
		return -1;
	} else {
		if (option_verbose > 10)
			cw_verbose( VERBOSE_PREFIX_4 "cdr_odbc: Query Successful!\n");
		connected = 1;
	}
	return 0;
}

static int odbc_init(void)
{
	SQLINTEGER ODBC_err;
	short int ODBC_mlen;
	int ODBC_res;
	unsigned char ODBC_msg[200], ODBC_stat[10];

	if (ODBC_env == SQL_NULL_HANDLE || connected == 0) {
		ODBC_res = SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &ODBC_env);
		if ((ODBC_res != SQL_SUCCESS) && (ODBC_res != SQL_SUCCESS_WITH_INFO)) {
			if (option_verbose > 10)
				cw_verbose( VERBOSE_PREFIX_4 "cdr_odbc: Error AllocHandle\n");
			connected = 0;
			return -1;
		}

		ODBC_res = SQLSetEnvAttr(ODBC_env, SQL_ATTR_ODBC_VERSION, (void*)SQL_OV_ODBC3, 0);

		if ((ODBC_res != SQL_SUCCESS) && (ODBC_res != SQL_SUCCESS_WITH_INFO)) {
			if (option_verbose > 10)
				cw_verbose( VERBOSE_PREFIX_4 "cdr_odbc: Error SetEnv\n");
			SQLFreeHandle(SQL_HANDLE_ENV, ODBC_env);
			connected = 0;
			return -1;
		}

		ODBC_res = SQLAllocHandle(SQL_HANDLE_DBC, ODBC_env, &ODBC_con);

		if ((ODBC_res != SQL_SUCCESS) && (ODBC_res != SQL_SUCCESS_WITH_INFO)) {
			if (option_verbose > 10)
				cw_verbose( VERBOSE_PREFIX_4 "cdr_odbc: Error AllocHDB %d\n", ODBC_res);
			SQLFreeHandle(SQL_HANDLE_ENV, ODBC_env);
			connected = 0;
			return -1;
		}
		SQLSetConnectAttr(ODBC_con, SQL_LOGIN_TIMEOUT, (SQLPOINTER *)10, 0);	
	}

	/* Note that the username and password could be NULL here, but that is allowed in ODBC.
           In this case, the default username and password will be used from odbc.conf */
	ODBC_res = SQLConnect(ODBC_con, (SQLCHAR*)dsn, SQL_NTS, (SQLCHAR*)username, SQL_NTS, (SQLCHAR*)password, SQL_NTS);

	if ((ODBC_res != SQL_SUCCESS) && (ODBC_res != SQL_SUCCESS_WITH_INFO)) {
		if (option_verbose > 10)
			cw_verbose( VERBOSE_PREFIX_4 "cdr_odbc: Error SQLConnect %d\n", ODBC_res);
		SQLGetDiagRec(SQL_HANDLE_DBC, ODBC_con, 1, ODBC_stat, &ODBC_err, ODBC_msg, 100, &ODBC_mlen);
		SQLFreeHandle(SQL_HANDLE_ENV, ODBC_env);
		connected = 0;
		return -1;
	} else {
		if (option_verbose > 10)
			cw_verbose( VERBOSE_PREFIX_4 "cdr_odbc: Connected to %s\n", dsn);
		connected = 1;
	}
	return 0;
}


MODULE_INFO(load_module, NULL, unload_module, release, desc)
