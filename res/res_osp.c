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
 *
 * \brief Provide Open Settlement Protocol capability
 * 
 * \arg See also: \ref chan_sip.c
 */
#include <sys/types.h>
#include <osp.h>
#include <openssl/err.h>
#include <stdio.h>
#include <dirent.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <openssl/bio.h>
#include <openssl/pem.h>

#include "callweaver.h"

CALLWEAVER_FILE_VERSION("$HeadURL$", "$Revision$")

#include "callweaver/file.h"
#include "callweaver/channel.h"
#include "callweaver/logger.h"
#include "callweaver/say.h"
#include "callweaver/module.h"
#include "callweaver/options.h"
#include "callweaver/crypto.h"
#include "callweaver/cli.h"
#include "callweaver/io.h"
#include "callweaver/lock.h"
#include "callweaver/astosp.h"
#include "callweaver/config.h"
#include "callweaver/utils.h"
#include "callweaver/lock.h"
#include "callweaver/causes.h"
#include "callweaver/phone_no_utils.h"
#include "callweaver/pbx.h"

#define MAX_CERTS 10
#define MAX_SERVICEPOINTS 10
#define OSP_MAX 256

#define OSP_DEFAULT_MAX_CONNECTIONS	20
#define OSP_DEFAULT_RETRY_DELAY		0
#define OSP_DEFAULT_RETRY_LIMIT		2
#define OSP_DEFAULT_TIMEOUT			500

static int loadPemCert(unsigned char *FileName, unsigned char *buffer, int *len);
static int loadPemPrivateKey(unsigned char *FileName, unsigned char *buffer, int *len);

CW_MUTEX_DEFINE_STATIC(osplock);

static int initialized = 0;
static int hardware = 0;
static unsigned tokenformat = TOKEN_ALGO_SIGNED;

struct osp_provider {
	char name[OSP_MAX];
	char localpvtkey[OSP_MAX];
	char localcert[OSP_MAX];
	char cacerts[MAX_CERTS][OSP_MAX]; 
	int cacount;
	char servicepoints[MAX_SERVICEPOINTS][OSP_MAX];
	char source[OSP_MAX];
	int spcount;
	int dead;
	int maxconnections;
	int retrydelay;
	int retrylimit;
	int timeout;
	OSPTPROVHANDLE handle;
	struct osp_provider *next;
};
static struct osp_provider *providers;

static int osp_build(struct cw_config *cfg, char *cat)
{
	OSPTCERT TheAuthCert[MAX_CERTS];
	unsigned char Reqbuf[4096],LocalBuf[4096],AuthBuf[MAX_CERTS][4096];
	struct cw_variable *v;
	struct osp_provider *osp;
	int x,length,errorcode=0;
	int mallocd=0,i;
	char *cacerts[MAX_CERTS];
	const char *servicepoints[MAX_SERVICEPOINTS];
	OSPTPRIVATEKEY privatekey;
	OSPTCERT localcert;
	OSPTCERT *authCerts[MAX_CERTS];

	
	
	cw_mutex_lock(&osplock);
	osp = providers;
	while(osp) {
		if (!strcasecmp(osp->name, cat))
			break;
		osp = osp->next;
	}
	cw_mutex_unlock(&osplock);
	if (!osp) {
		mallocd = 1;
		osp = malloc(sizeof(struct osp_provider));
		if (!osp) {
			cw_log(CW_LOG_WARNING, "Out of memory\n");
			return -1;
		}
		memset(osp, 0, sizeof(struct osp_provider));
		osp->handle = -1;
	}
	cw_copy_string(osp->name, cat, sizeof(osp->name));
	snprintf(osp->localpvtkey, sizeof(osp->localpvtkey) ,"%s/%s-privatekey.pem", cw_config[CW_KEY_DIR], cat);
	snprintf(osp->localcert, sizeof(osp->localpvtkey), "%s/%s-localcert.pem", cw_config[CW_KEY_DIR], cat);
	osp->maxconnections=OSP_DEFAULT_MAX_CONNECTIONS;
	osp->retrydelay = OSP_DEFAULT_RETRY_DELAY;
	osp->retrylimit = OSP_DEFAULT_RETRY_LIMIT;
	osp->timeout = OSP_DEFAULT_TIMEOUT;
	osp->source[0] = '\0';
	cw_log(CW_LOG_DEBUG, "Building OSP Provider '%s'\n", cat);
	v = cw_variable_browse(cfg, cat);
	while(v) {
		if (!strcasecmp(v->name, "privatekey")) {
			if (v->value[0] == '/')
				cw_copy_string(osp->localpvtkey, v->value, sizeof(osp->localpvtkey));
			else
				snprintf(osp->localpvtkey, sizeof(osp->localpvtkey), "%s/%s", cw_config[CW_KEY_DIR], v->value);
		} else if (!strcasecmp(v->name, "localcert")) {
			if (v->value[0] == '/')
				cw_copy_string(osp->localcert, v->value, sizeof(osp->localcert));
			else
				snprintf(osp->localcert, sizeof(osp->localcert), "%s/%s", cw_config[CW_KEY_DIR], v->value);
		} else if (!strcasecmp(v->name, "cacert")) {
			if (osp->cacount < MAX_CERTS) {
				if (v->value[0] == '/')
					cw_copy_string(osp->cacerts[osp->cacount], v->value, sizeof(osp->cacerts[0]));
				else
					snprintf(osp->cacerts[osp->cacount], sizeof(osp->cacerts[0]), "%s/%s", cw_config[CW_KEY_DIR], v->value);
				osp->cacount++;
			} else
				cw_log(CW_LOG_WARNING, "Too many CA Certificates at line %d\n", v->lineno);
		} else if (!strcasecmp(v->name, "servicepoint")) {
			if (osp->spcount < MAX_SERVICEPOINTS) {
				cw_copy_string(osp->servicepoints[osp->spcount], v->value, sizeof(osp->servicepoints[0]));
				osp->spcount++;
			} else
				cw_log(CW_LOG_WARNING, "Too many Service points at line %d\n", v->lineno);
		} else if (!strcasecmp(v->name, "maxconnections")) {
			if ((sscanf(v->value, "%d", &x) == 1) && (x > 0) && (x <= 1000)) {
				osp->maxconnections = x;
			} else
				cw_log(CW_LOG_WARNING, "maxconnections should be an integer from 1 to 1000, not '%s' at line %d\n", v->value, v->lineno);
		} else if (!strcasecmp(v->name, "retrydelay")) {
			if ((sscanf(v->value, "%d", &x) == 1) && (x >= 0) && (x <= 10)) {
				osp->retrydelay = x;
			} else
				cw_log(CW_LOG_WARNING, "retrydelay should be an integer from 0 to 10, not '%s' at line %d\n", v->value, v->lineno);
		} else if (!strcasecmp(v->name, "retrylimit")) {
			if ((sscanf(v->value, "%d", &x) == 1) && (x >= 0) && (x <= 100)) {
				osp->retrylimit = x;
			} else
				cw_log(CW_LOG_WARNING, "retrylimit should be an integer from 0 to 100, not '%s' at line %d\n", v->value, v->lineno);
		} else if (!strcasecmp(v->name, "timeout")) {
			if ((sscanf(v->value, "%d", &x) == 1) && (x >= 200) && (x <= 10000)) {
				osp->timeout = x;
			} else
				cw_log(CW_LOG_WARNING, "timeout should be an integer from 200 to 10000, not '%s' at line %d\n", v->value, v->lineno);
		} else if (!strcasecmp(v->name, "source")) {
			cw_copy_string(osp->source, v->value, sizeof(osp->source));
		}
		v = v->next;
	}
	if (osp->cacount < 1) {
		snprintf(osp->cacerts[osp->cacount], sizeof(osp->cacerts[0]), "%s/%s-cacert.pem", cw_config[CW_KEY_DIR], cat);
		osp->cacount++;
	}
	for (x=0;x<osp->cacount;x++)
		cacerts[x] = osp->cacerts[x];
	for (x=0;x<osp->spcount;x++)
		servicepoints[x] = osp->servicepoints[x];
	
	cw_mutex_lock(&osplock);
	osp->dead = 0;
	if (osp->handle > -1) {
		cw_log(CW_LOG_DEBUG, "Deleting old handle for '%s'\n", osp->name);
		OSPPProviderDelete(osp->handle, 0);
	}
		

    length = 0;
	cw_log(CW_LOG_DEBUG, "Loading private key for '%s' (%s)\n", osp->name, osp->localpvtkey);
    errorcode = loadPemPrivateKey(osp->localpvtkey,Reqbuf,&length);
    if (errorcode == 0)
    {
        privatekey.PrivateKeyData = Reqbuf;
        privatekey.PrivateKeyLength = length;
    }
    else
    {
         return -1;
    }

    length = 0;
	cw_log(CW_LOG_DEBUG, "Loading local cert for '%s' (%s)\n", osp->name, osp->localcert);
    errorcode = loadPemCert(osp->localcert,LocalBuf,&length);
    if (errorcode == 0)
    {
        localcert.CertData = LocalBuf;
        localcert.CertDataLength = length;
    }
    else
    {
         return -1;
    }

    for (i=0;i<osp->cacount;i++)
    {
        length = 0;
		cw_log(CW_LOG_DEBUG, "Loading CA cert %d for '%s' (%s)\n", i + 1, osp->name, osp->cacerts[i]);
        errorcode = loadPemCert(osp->cacerts[i],AuthBuf[i],&length);
        if (errorcode == 0)
        {
            TheAuthCert[i].CertData = AuthBuf[i];
            TheAuthCert[i].CertDataLength = length;
            authCerts[i] = &(TheAuthCert[i]);
        }
        else
        {
			return -1;        
		}
    }
	
	cw_log(CW_LOG_DEBUG, "Creating provider handle for '%s'\n", osp->name);
	
	cw_log(CW_LOG_DEBUG, "Service point '%s %d'\n", servicepoints[0], osp->spcount);
	
	if (OSPPProviderNew(osp->spcount, 
					    servicepoints, 
					   NULL, 
					   "localhost", 
					   &privatekey, 
					   &localcert, 
					   osp->cacount, 
					   (const OSPTCERT **)authCerts, 
					   1, 
					   300, 
					   osp->maxconnections, 
					   1, 
					   osp->retrydelay, 
					   osp->retrylimit, 
					   osp->timeout, 
					   "", 
					   "", 
					   &osp->handle)) {
		cw_log(CW_LOG_WARNING, "Unable to initialize provider '%s'\n", cat);
		osp->dead = 1;
	}
	
	if (mallocd) {
		osp->next = providers;
		providers = osp;
	}
	cw_mutex_unlock(&osplock);	
	return 0;
}

static int show_osp(struct cw_dynstr *ds_p, int argc, char *argv[])
{
	struct osp_provider *osp;
	char *search = NULL;
	int x;
	int found = 0;
	char *tokenalgo;

	if ((argc < 2) || (argc > 3))
		return RESULT_SHOWUSAGE;
	if (argc > 2)
		search = argv[2];
	if (!search) {
		switch (tokenformat) {
			case TOKEN_ALGO_BOTH:
				tokenalgo = "Both";
				break;
			case TOKEN_ALGO_UNSIGNED:
				tokenalgo = "Unsigned";
				break;
			case TOKEN_ALGO_SIGNED:
			default:
				tokenalgo = "Signed";
				break;
		}
		cw_dynstr_printf(ds_p, "OSP: %s %s %s\n", initialized ? "Initialized" : "Uninitialized", hardware ? "Accelerated" : "Normal", tokenalgo);
	}

	cw_mutex_lock(&osplock);
	osp = providers;
	while(osp) {
		if (!search || !strcasecmp(osp->name, search)) {
			if (found)
				cw_dynstr_printf(ds_p, "\n");
			cw_dynstr_tprintf(ds_p, 9,
				cw_fmtval(" == OSP Provider '%s' ==\n", osp->name),
				cw_fmtval("Local Private Key: %s\n", osp->localpvtkey),
				cw_fmtval("Local Certificate: %s\n", osp->localcert),
				cw_fmtval("Max Connections:   %d\n", osp->maxconnections),
				cw_fmtval("Retry Delay:       %d seconds\n", osp->retrydelay),
				cw_fmtval("Retry Limit:       %d\n", osp->retrylimit),
				cw_fmtval("Timeout:           %d milliseconds\n", osp->timeout),
				cw_fmtval("Source:            %s\n", (strlen(osp->source) ? osp->source : "<unspecified>")),
				cw_fmtval("OSP Handle:        %d\n", osp->handle)
			);
			for (x=0;x<osp->cacount;x++)
				cw_dynstr_printf(ds_p, "CA Certificate %d:  %s\n", x + 1, osp->cacerts[x]);
			for (x=0;x<osp->spcount;x++)
				cw_dynstr_printf(ds_p, "Service Point %d:   %s\n", x + 1, osp->servicepoints[x]);
			found++;
		}
		osp = osp->next;
	}
	cw_mutex_unlock(&osplock);
	if (!found) {
		if (search) 
			cw_dynstr_printf(ds_p, "Unable to find OSP provider '%s'\n", search);
		else
			cw_dynstr_printf(ds_p, "No OSP providers configured\n");
	}
	return RESULT_SUCCESS;
}


/*----------------------------------------------*
 *               Loads the Certificate          *
 *----------------------------------------------*/
static int loadPemCert(unsigned char *FileName, unsigned char *buffer, int *len)
{
    int length = 0;
    unsigned char *temp;
    BIO *bioIn = NULL;
    X509 *cert=NULL;
    int retVal = OSPC_ERR_NO_ERROR;

    temp = buffer;
    bioIn = BIO_new_file((const char*)FileName,"r");
    if (bioIn == NULL)
    {
		cw_log(CW_LOG_WARNING,"Failed to find the File - %s \n",FileName);
		return -1;
    }
    else
    {
        cert = PEM_read_bio_X509(bioIn,NULL,NULL,NULL);
        if (cert == NULL)
        {
			cw_log(CW_LOG_WARNING,"Failed to parse the Certificate from the File - %s \n",FileName);
			return -1;
        }
        else
        {
            length = i2d_X509(cert,&temp);
            if (cert == 0)
            {
				cw_log(CW_LOG_WARNING,"Failed to parse the Certificate from the File - %s, Length=0 \n",FileName);
				return -1;
            }
            else
			{
               *len = length;
            }
        }
    }

    if (bioIn != NULL)
    {
        BIO_free(bioIn);
    }

    if (cert != NULL)
    {
        X509_free(cert);
    }
    return retVal;
}

/*----------------------------------------------*
 *               Loads the Private Key          *
 *----------------------------------------------*/
static int loadPemPrivateKey(unsigned char *FileName, unsigned char *buffer, int *len)
{
    int length = 0;
    unsigned char *temp;
    BIO *bioIn = NULL;
    RSA *pKey = NULL;
    int retVal = OSPC_ERR_NO_ERROR;

    temp = buffer;

    bioIn = BIO_new_file((const char*)FileName,"r");
    if (bioIn == NULL)
    {
		cw_log(CW_LOG_WARNING,"Failed to find the File - %s \n",FileName);
		return -1;
    }
    else
    {
        pKey = PEM_read_bio_RSAPrivateKey(bioIn,NULL,NULL,NULL);
        if (pKey == NULL)
        {
			cw_log(CW_LOG_WARNING,"Failed to parse the Private Key from the File - %s \n",FileName);
			return -1;
        }
        else
        {
            length = i2d_RSAPrivateKey(pKey,&temp);
            if (length == 0)
            {
				cw_log(CW_LOG_WARNING,"Failed to parse the Private Key from the File - %s, Length=0 \n",FileName);
				return -1;
            }
            else
            {
                *len = length;
            }
        }
    }
    if (bioIn != NULL)
    {
        BIO_free(bioIn);
    }

    if (pKey != NULL)
    {
       RSA_free(pKey);
    }
    return retVal;
}

int cw_osp_validate(char *provider, char *token, int *handle, unsigned int *timelimit, char *callerid, struct in_addr addr, char *extension)
{
	char tmp[256]="", *l, *n;
	char iabuf[INET_ADDRSTRLEN];
	char source[OSP_MAX] = ""; /* Same length as osp->source */
	char *token2;
	int tokenlen;
	struct osp_provider *osp;
	int res = 0;
	unsigned int authorised, dummy;

	if (!provider || !strlen(provider))
		provider = "default";

	token2 = cw_strdupa(token);
	tokenlen = cw_base64decode(token2, token, strlen(token));
	*handle = -1;
	if (!callerid)
		callerid = "";
	cw_copy_string(tmp, callerid, sizeof(tmp));
	cw_callerid_parse(tmp, &n, &l);
	if (!l)
		l = "";
	else {
		cw_shrink_phone_number(l);
		if (!cw_isphonenumber(l))
			l = "";
	}
	callerid = l;
	cw_mutex_lock(&osplock);
	cw_inet_ntoa(iabuf, sizeof(iabuf), addr);
	osp = providers;
	while(osp) {
		if (!strcasecmp(osp->name, provider)) {
			if (OSPPTransactionNew(osp->handle, handle)) {
				cw_log(CW_LOG_WARNING, "Unable to create OSP Transaction handle!\n");
			} else {
				cw_copy_string(source, osp->source, sizeof(source));
				res = 1;
			}
			break;
		}
		osp = osp->next;
	}
	cw_mutex_unlock(&osplock);
	if (res) {
		res = 0;
		dummy = 0;
		if (!OSPPTransactionValidateAuthorisation(*handle, iabuf, source, NULL, NULL, 
			callerid, OSPC_E164, extension, OSPC_E164, 0, "", tokenlen, token2, &authorised, timelimit, &dummy, NULL, tokenformat)) {
			if (authorised) {
				cw_log(CW_LOG_DEBUG, "Validated token for '%s' from '%s@%s'\n", extension, callerid, iabuf);
				res = 1;
			}
		}
	}
	return res;	
}

int cw_osp_lookup(struct cw_channel *chan, char *provider, char *extension, char *callerid, struct cw_osp_result *result)
{
	int cres;
	int res = 0;
	int counts;
	int tokenlen;
	unsigned int dummy=0;
	unsigned int timelimit;
	unsigned int callidlen;
	char callidstr[OSPC_CALLID_MAXSIZE] = "";
	struct osp_provider *osp;
	char source[OSP_MAX] = ""; /* Same length as osp->source */
	char callednum[2048]="";
	char callingnum[2048]="";
	char destination[2048]="";
	char token[2000];
	char tmp[256]="", *l, *n;
	OSPE_DEST_PROT prot;
	OSPE_DEST_OSP_ENABLED ospenabled;
	struct cw_var_t *devinfo;

	result->handle = -1;
	result->numresults = 0;
	result->tech[0] = '\0';
	result->dest[0] = '\0';
	result->token[0] = '\0';

	if (!provider || !strlen(provider))
		provider = "default";

	if (!callerid)
		callerid = "";
	cw_copy_string(tmp, callerid, sizeof(tmp));
	cw_callerid_parse(tmp, &n, &l);
	if (!l)
		l = "";
	else {
		cw_shrink_phone_number(l);
		if (!cw_isphonenumber(l))
			l = "";
	}
	callerid = l;

	if (chan) {
		cres = cw_autoservice_start(chan);
		if (cres < 0)
			return cres;
	}
	cw_mutex_lock(&osplock);
	osp = providers;
	while(osp) {
		if (!strcasecmp(osp->name, provider)) {
			if (OSPPTransactionNew(osp->handle, &result->handle)) {
				cw_log(CW_LOG_WARNING, "Unable to create OSP Transaction handle!\n");
			} else {
				cw_copy_string(source, osp->source, sizeof(source));
				res = 1;
			}
			break;
		}
		osp = osp->next;
	}
	cw_mutex_unlock(&osplock);
	if (res) {
		res = 0;
		/* No more than 10 back */
		counts = 10;
		dummy = 0;
		devinfo = pbx_builtin_getvar_helper(chan, CW_KEYWORD_OSPPEER, "OSPPEER");
		if (!OSPPTransactionRequestAuthorisation(result->handle, source, (devinfo ? devinfo->value : ""),
			  callerid,OSPC_E164, extension, OSPC_E164, NULL, 0, NULL, NULL, &counts, &dummy, NULL)) {
			if (counts) {
				tokenlen = sizeof(token);
				result->numresults = counts - 1;
				callidlen = sizeof(callidstr);
				if (!OSPPTransactionGetFirstDestination(result->handle, 0, NULL, NULL, &timelimit, &callidlen, callidstr, 
					sizeof(callednum), callednum, sizeof(callingnum), callingnum, sizeof(destination), destination, 0, NULL, &tokenlen, token)) {
					cw_log(CW_LOG_DEBUG, "Got destination '%s' and called: '%s' calling: '%s' for '%s' (provider '%s')\n",
						destination, callednum, callingnum, extension, provider);
					/* Only support OSP server with only one duration limit */
					if (cw_channel_cmpwhentohangup (chan, timelimit) < 0) {
						cw_channel_setwhentohangup (chan, timelimit);	
					}
					do {
						if (!OSPPTransactionIsDestOSPEnabled (result->handle, &ospenabled) && (ospenabled == OSPE_OSP_FALSE)) {
							result->token[0] = 0;
						}
						else {
							cw_base64encode(result->token, token, tokenlen, sizeof(result->token) - 1);
						}
						if ((strlen(destination) > 2) && !OSPPTransactionGetDestProtocol(result->handle, &prot)) {
							res = 1;
							/* Strip leading and trailing brackets */
							destination[strlen(destination) - 1] = '\0';
							switch(prot) {
							case OSPE_DEST_PROT_H323_SETUP:
								cw_copy_string(result->tech, "H323", sizeof(result->tech));
								snprintf(result->dest, sizeof(result->dest), "%s@%s", callednum, destination + 1);
								break;
							case OSPE_DEST_PROT_SIP:
								cw_copy_string(result->tech, "SIP", sizeof(result->tech));
								snprintf(result->dest, sizeof(result->dest), "%s@%s", callednum, destination + 1);
								break;
							case OSPE_DEST_PROT_IAX:
								cw_copy_string(result->tech, "IAX", sizeof(result->tech));
								snprintf(result->dest, sizeof(result->dest), "%s@%s", callednum, destination + 1);
								break;
							default:
								cw_log(CW_LOG_DEBUG, "Unknown destination protocol '%d', skipping...\n", prot);
								res = 0;
							}
							if (!res && result->numresults) {
								result->numresults--;
								callidlen = sizeof(callidstr);
								if (OSPPTransactionGetNextDestination(result->handle, OSPC_FAIL_INCOMPATIBLE_DEST, 0, NULL, NULL, &timelimit, &callidlen, callidstr, 
										sizeof(callednum), callednum, sizeof(callingnum), callingnum, sizeof(destination), destination, 0, NULL, &tokenlen, token)) {
										break;
								}
							}
						} else {
							cw_log(CW_LOG_DEBUG, "Missing destination protocol\n");
							break;
						}
					} while(!res && result->numresults);
				}
			}
			
		}
		if (!res) {
			OSPPTransactionDelete(result->handle);
			result->handle = -1;
		}
		if (devinfo)
			cw_object_put(devinfo);
	}
	if (!osp) 
		cw_log(CW_LOG_NOTICE, "OSP Provider '%s' does not exist!\n", provider);
	if (chan) {
		cres = cw_autoservice_stop(chan);
		if (cres < 0)
			return cres;
	}
	return res;
}

int cw_osp_next(struct cw_osp_result *result, int cause)
{
	int res = 0;
	int tokenlen;
	unsigned int dummy=0;
	unsigned int timelimit;
	unsigned int callidlen;
	char callidstr[OSPC_CALLID_MAXSIZE] = "";
	char callednum[2048]="";
	char callingnum[2048]="";
	char destination[2048]="";
	char token[2000];
	OSPE_DEST_PROT prot;
	OSPE_DEST_OSP_ENABLED ospenabled;

	result->tech[0] = '\0';
	result->dest[0] = '\0';
	result->token[0] = '\0';

	if (result->handle > -1) {
		dummy = 0;
		if (result->numresults) {
			tokenlen = sizeof(token);
			while(!res && result->numresults) {
				result->numresults--;
				callidlen = sizeof(callidstr);
				if (!OSPPTransactionGetNextDestination(result->handle, OSPC_FAIL_INCOMPATIBLE_DEST, 0, NULL, NULL, &timelimit, &callidlen, callidstr, 
									sizeof(callednum), callednum, sizeof(callingnum), callingnum, sizeof(destination), destination, 0, NULL, &tokenlen, token)) {
					if (!OSPPTransactionIsDestOSPEnabled (result->handle, &ospenabled) && (ospenabled == OSPE_OSP_FALSE)) {
						result->token[0] = 0;
					}
					else {
						cw_base64encode(result->token, token, tokenlen, sizeof(result->token) - 1);
					}
					if ((strlen(destination) > 2) && !OSPPTransactionGetDestProtocol(result->handle, &prot)) {
						res = 1;
						/* Strip leading and trailing brackets */
						destination[strlen(destination) - 1] = '\0';
						switch(prot) {
						case OSPE_DEST_PROT_H323_SETUP:
							cw_copy_string(result->tech, "H323", sizeof(result->tech));
							snprintf(result->dest, sizeof(result->dest), "%s@%s", callednum, destination + 1);
							break;
						case OSPE_DEST_PROT_SIP:
							cw_copy_string(result->tech, "SIP", sizeof(result->tech));
							snprintf(result->dest, sizeof(result->dest), "%s@%s", callednum, destination + 1);
							break;
						case OSPE_DEST_PROT_IAX:
							cw_copy_string(result->tech, "IAX", sizeof(result->tech));
							snprintf(result->dest, sizeof(result->dest), "%s@%s", callednum, destination + 1);
							break;
						default:
							cw_log(CW_LOG_DEBUG, "Unknown destination protocol '%d', skipping...\n", prot);
							res = 0;
						}
					} else {
						cw_log(CW_LOG_DEBUG, "Missing destination protocol\n");
						break;
					}
				}
			}
			
		}
		if (!res) {
			OSPPTransactionDelete(result->handle);
			result->handle = -1;
		}
		
	}
	return res;
}

static enum OSPEFAILREASON cause2reason(int cause)
{
	switch(cause) {
	case CW_CAUSE_BUSY:
		return OSPC_FAIL_USER_BUSY;
	case CW_CAUSE_CONGESTION:
		return OSPC_FAIL_SWITCHING_EQUIPMENT_CONGESTION;
	case CW_CAUSE_UNALLOCATED:
		return OSPC_FAIL_UNALLOC_NUMBER;
	case CW_CAUSE_NOTDEFINED:
		return OSPC_FAIL_NORMAL_UNSPECIFIED;
	case CW_CAUSE_NOANSWER:
		return OSPC_FAIL_NO_ANSWER_FROM_USER;
	case CW_CAUSE_NORMAL:
	default:
		return OSPC_FAIL_NORMAL_CALL_CLEARING;
	}
}

int cw_osp_terminate(int handle, int cause, time_t start, time_t duration)
{
	unsigned int dummy = 0;
	int res = -1;
	enum OSPEFAILREASON reason;

	time_t endTime = 0;
	time_t alertTime = 0;
	time_t connectTime = 0;
	unsigned isPddInfoPresent = 0;
	unsigned pdd = 0;
	unsigned releaseSource = 0;
	unsigned char *confId = "";
	
	reason = cause2reason(cause);
	if (OSPPTransactionRecordFailure(handle, reason))
		cw_log(CW_LOG_WARNING, "Failed to record call termination for handle %d\n", handle);
	else if (OSPPTransactionReportUsage(handle, duration, start,
			       endTime,alertTime,connectTime,isPddInfoPresent,pdd,releaseSource,confId,
		       	       0, 0, 0, 0, &dummy, NULL))
		cw_log(CW_LOG_WARNING, "Failed to report duration for handle %d\n", handle);
	else {
		cw_log(CW_LOG_DEBUG, "Completed recording handle %d\n", handle);
		OSPPTransactionDelete(handle);
		res = 0;
	}
	return res;
}

static int config_load(void)
{
	struct cw_config *cfg;
	char *cat;
	struct osp_provider *osp, *prev = NULL, *next;
	cw_mutex_lock(&osplock);
	osp = providers;
	while(osp) {
		osp->dead = 1;
		osp = osp->next;
	}
	cw_mutex_unlock(&osplock);
	cfg = cw_config_load("osp.conf");
	if (cfg) {
		if (!initialized) {
			cat = cw_variable_retrieve(cfg, "general", "accelerate");
			if (cat && cw_true(cat))
				if (OSPPInit(1)) {
					cw_log(CW_LOG_WARNING, "Failed to enable hardware accelleration, falling back to software mode\n");
					OSPPInit(0);
				} else
					hardware = 1;
			else
				OSPPInit(0);
			initialized = 1;
		}
		cat = cw_variable_retrieve(cfg, "general", "tokenformat");
		if (cat) {
			if ((sscanf(cat, "%d", &tokenformat) != 1) || (tokenformat < TOKEN_ALGO_SIGNED) || (tokenformat > TOKEN_ALGO_BOTH)) {
				tokenformat = TOKEN_ALGO_SIGNED;
				cw_log(CW_LOG_WARNING, "tokenformat should be an integer from 0 to 2, not '%s'\n", cat);
			}
		}
		cat = cw_category_browse(cfg, NULL);
		while(cat) {
			if (strcasecmp(cat, "general"))
				osp_build(cfg, cat);
			cat = cw_category_browse(cfg, cat);
		}
		cw_config_destroy(cfg);
	} else
		cw_log(CW_LOG_NOTICE, "No OSP configuration found.  OSP support disabled\n");
	cw_mutex_lock(&osplock);
	osp = providers;
	while(osp) {
		next = osp->next;
		if (osp->dead) {
			if (prev)
				prev->next = next;
			else
				providers = next;
			/* XXX Cleanup OSP structure first XXX */
			free(osp);
		} else 
			prev = osp;
		osp = next;
	}
	cw_mutex_unlock(&osplock);
	return 0;
}

static const char show_osp_usage[] =
"Usage: show osp\n"
"       Displays information on Open Settlement Protocol\n";

static struct cw_clicmd cli_show_osp = {
	.cmda = { "show", "osp", NULL },
	.handler = show_osp,
	.summary = "Displays OSP information",
	.usage = show_osp_usage,
};

static int reload_module(void)
{
	config_load();
	cw_log(CW_LOG_NOTICE, "XXX Should reload OSP config XXX\n");
	return 0;
}

static int load_module(void)
{
	/* We should never be unloaded */
	cw_object_get(get_modinfo()->self);

	config_load();
	cw_cli_register(&cli_show_osp);
	return 0;
}

static int unload_module(void)
{
	/* Can't unload this once we're loaded */
	return -1;
}


MODULE_INFO(load_module, reload_module, unload_module, NULL, "Open Settlement Protocol Support")
