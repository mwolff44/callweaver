/*
 * CallWeaver -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2005, Digium, Inc.
 *
 * Kevin P. Fleming <kpfleming@digium.com>
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
 * \brief Network socket handling
 * 
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <signal.h>
#include <errno.h>
#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netdb.h>
#include <net/if.h>
#include <netinet/in_systm.h>
#include <netinet/ip.h>
#include <sys/ioctl.h>

#if defined(__OpenBSD__) || defined(__NetBSD__) || defined(__FreeBSD__)
#include <fcntl.h>
#include <net/route.h>
#endif

#if defined (SOLARIS)
#include <sys/sockio.h>
#endif

#include "callweaver.h"

CALLWEAVER_FILE_VERSION("$HeadURL$", "$Revision$")

#include "callweaver/netsock.h"
#include "callweaver/logger.h"
#include "callweaver/channel.h"
#include "callweaver/options.h"
#include "callweaver/utils.h"
#include "callweaver/lock.h"
#include "callweaver/srv.h"

struct cw_netsock {
	ASTOBJ_COMPONENTS(struct cw_netsock)
	struct sockaddr_in bindaddr;
	int sockfd;
	struct cw_io_rec ioref;
	cw_io_context_t ioc;
	void *data;
};

struct cw_netsock_list {
	ASTOBJ_CONTAINER_COMPONENTS(struct cw_netsock);
	cw_io_context_t ioc;
};

static void cw_netsock_destroy(struct cw_netsock *netsock)
{
	cw_io_remove(netsock->ioc, &netsock->ioref);
	close(netsock->sockfd);
	free(netsock);
}

struct cw_netsock_list *cw_netsock_list_alloc(void)
{
	struct cw_netsock_list *res;

	res = calloc(1, sizeof(*res));

	return res;
}

int cw_netsock_init(struct cw_netsock_list *list)
{
	memset(list, 0, sizeof(*list));
	ASTOBJ_CONTAINER_INIT(list);

	return 0;
}

int cw_netsock_release(struct cw_netsock_list *list)
{
	ASTOBJ_CONTAINER_DESTROYALL(list, cw_netsock_destroy);
	ASTOBJ_CONTAINER_DESTROY(list);

	return 0;
}

struct cw_netsock *cw_netsock_find(struct cw_netsock_list *list,
				     struct sockaddr_in *sa)
{
	struct cw_netsock *sock = NULL;

	ASTOBJ_CONTAINER_TRAVERSE(list, !sock, {
		ASTOBJ_RDLOCK(iterator);
		if (!inaddrcmp(&iterator->bindaddr, sa))
			sock = iterator;
		ASTOBJ_UNLOCK(iterator);
	});

	return sock;
}

struct cw_netsock *cw_netsock_bindaddr(struct cw_netsock_list *list, cw_io_context_t ioc, struct sockaddr_in *bindaddr, int tos, cw_io_cb callback, void *data)
{
	int netsocket = -1;
	char iabuf[INET_ADDRSTRLEN];
	
	struct cw_netsock *ns;
	
	/* Make a UDP socket */
	netsocket = socket_cloexec(AF_INET, SOCK_DGRAM, IPPROTO_IP);
	
	if (netsocket < 0) {
		cw_log(CW_LOG_ERROR, "Unable to create network socket: %s\n", strerror(errno));
		return NULL;
	}
	if (bind(netsocket,(struct sockaddr *)bindaddr, sizeof(struct sockaddr_in))) {
		cw_log(CW_LOG_ERROR, "Unable to bind to %s port %d: %s\n", cw_inet_ntoa(iabuf, sizeof(iabuf), bindaddr->sin_addr), ntohs(bindaddr->sin_port), strerror(errno));
		close(netsocket);
		return NULL;
	}
	if (option_verbose > 1)
		cw_verbose(VERBOSE_PREFIX_2 "Using TOS bits %d\n", tos);

	if (setsockopt(netsocket, IPPROTO_IP, IP_TOS, &tos, sizeof(tos))) 
		cw_log(CW_LOG_WARNING, "Unable to set TOS to %d\n", tos);

	cw_enable_packet_fragmentation(netsocket);

	if ((ns = malloc(sizeof(struct cw_netsock)))) {
		/* Establish I/O callback for socket read */
		cw_io_init(&ns->ioref, callback, ns);
		if (!cw_io_add(ioc, &ns->ioref, netsocket, CW_IO_IN)) {
			ASTOBJ_INIT(ns);
			ns->ioc = ioc;
			ns->sockfd = netsocket;
			ns->data = data;
			memcpy(&ns->bindaddr, bindaddr, sizeof(ns->bindaddr));
			ASTOBJ_CONTAINER_LINK(list, ns);
			return ns;
		}

		free(ns);
	}

	cw_log(CW_LOG_ERROR, "Out of memory\n");
	close(netsocket);
	return NULL;
}

struct cw_netsock *cw_netsock_bind(struct cw_netsock_list *list, cw_io_context_t ioc, const char *bindinfo, int defaultport, int tos, cw_io_cb callback, void *data)
{
	struct sockaddr_in sin;
	char *tmp;
	char *host;
	char *port;
	int portno;

	memset(&sin, 0, sizeof(sin));
	sin.sin_family = AF_INET;
	sin.sin_port = htons(defaultport);
	tmp = cw_strdupa(bindinfo);

	host = strsep(&tmp, ":");
	port = tmp;

	if (port && ((portno = atoi(port)) > 0))
		sin.sin_port = htons(portno);

	inet_aton(host, &sin.sin_addr);

	return cw_netsock_bindaddr(list, ioc, &sin, tos, callback, data);
}

int cw_netsock_sockfd(const struct cw_netsock *ns)
{
	return ns ? ns-> sockfd : -1;
}

const struct sockaddr_in *cw_netsock_boundaddr(const struct cw_netsock *ns)
{
	return &(ns->bindaddr);
}

void *cw_netsock_data(const struct cw_netsock *ns)
{
	return ns->data;
}
