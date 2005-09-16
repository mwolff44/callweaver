/*
 * OpenPBX -- An open source telephony toolkit.
 *
 * Written by Thorsten Lockert <tholo@trollphone.org>
 *
 * Funding provided by Troll Phone Networks AS
 *
 * See http://www.openpbx.org for more information about
 * the OpenPBX project. Please do not directly contact
 * any of the maintainers of this project for assistance;
 * the project provides a web site, mailing lists and IRC
 * channels for your use.
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License Version 2. See the LICENSE file
 * at the top of the source tree.
 */

/*
 * DNS support
 */

#ifndef _OPENPBX_DNS_H
#define _OPENPBX_DNS_H

struct ast_channel;

/*!	\brief	Perform DNS lookup (used by enum and SRV lookups) 
	\param	context
	\param	dname	Domain name to lookup (host, SRV domain, TXT record name)
	\param	class	Record Class (see "man res_search")
	\param	type	Record type (see "man res_search")
	\param	callback Callback function for handling DNS result
*/
extern int ast_search_dns(void *context, const char *dname, int class, int type,
	 int (*callback)(void *context, char *answer, int len, char *fullanswer));

#endif /* _OPENPBX_DNS_H */
