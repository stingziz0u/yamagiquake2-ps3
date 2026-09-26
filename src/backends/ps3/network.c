/*
 * Copyright (C) 1997-2001 Id Software, Inc.
 * Copyright (C) 2026 the Quake2PS3 contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 *
 * =======================================================================
 *
 * Network layer for the PS3: loopback only. Single player talks to the
 * local server through these two queues, exactly like the unix build does
 * when no sockets are open. Real sockets (LAN/online play) are out of
 * scope for now; every IP address is rejected cleanly.
 *
 * The loopback code is taken verbatim from src/backends/unix/network.c.
 *
 * =======================================================================
 */

#include <string.h>

#include "../../common/header/common.h"

netadr_t net_local_adr;   /* zeroed = NA_LOOPBACK */

#define MAX_LOOPBACK 4

typedef struct
{
	byte data[MAX_MSGLEN];
	int datalen;
} loopmsg_t;

typedef struct
{
	loopmsg_t msgs[MAX_LOOPBACK];
	int get, send;
} loopback_t;

static loopback_t loopbacks[2];

void
NET_Init(void)
{
	Com_Printf("NET_Init: loopback only\n");
}

void
NET_Shutdown(void)
{
}

void
NET_Config(qboolean multiplayer)
{
	(void)multiplayer;
}

void
NET_Sleep(int msec)
{
	(void)msec;
}

qboolean
NET_CompareAdr(netadr_t a, netadr_t b)
{
	if (a.type != b.type)
	{
		return false;
	}

	if (a.type == NA_LOOPBACK)
	{
		return true;
	}

	if (a.type == NA_IP)
	{
		return (a.ip[0] == b.ip[0]) && (a.ip[1] == b.ip[1]) &&
			(a.ip[2] == b.ip[2]) && (a.ip[3] == b.ip[3]) && (a.port == b.port);
	}

	return false;
}

qboolean
NET_CompareBaseAdr(netadr_t a, netadr_t b)
{
	if (a.type != b.type)
	{
		return false;
	}

	if (a.type == NA_LOOPBACK)
	{
		return true;
	}

	if (a.type == NA_IP)
	{
		return (a.ip[0] == b.ip[0]) && (a.ip[1] == b.ip[1]) &&
			(a.ip[2] == b.ip[2]) && (a.ip[3] == b.ip[3]);
	}

	return false;
}

char *
NET_AdrToString(netadr_t a)
{
	static char s[64];

	if (a.type == NA_LOOPBACK)
	{
		Com_sprintf(s, sizeof(s), "loopback");
	}
	else
	{
		/* port is kept in network byte order = big-endian = native here */
		Com_sprintf(s, sizeof(s), "%i.%i.%i.%i:%i", a.ip[0], a.ip[1],
				a.ip[2], a.ip[3], (int)(unsigned short)a.port);
	}

	return s;
}

qboolean
NET_StringToAdr(const char *s, netadr_t *a)
{
	memset(a, 0, sizeof(*a));

	if (!strcmp(s, "localhost") || !strcmp(s, "loopback"))
	{
		a->type = NA_LOOPBACK;
		return true;
	}

	/* No sockets: nothing else is reachable. */
	return false;
}

qboolean
NET_IsLocalAddress(netadr_t adr)
{
	return NET_CompareAdr(adr, net_local_adr);
}

static qboolean
NET_GetLoopPacket(netsrc_t sock, netadr_t *net_from, sizebuf_t *net_message)
{
	int i;
	loopback_t *loop;

	loop = &loopbacks[sock];

	if (loop->send - loop->get > MAX_LOOPBACK)
	{
		loop->get = loop->send - MAX_LOOPBACK;
	}

	if (loop->get >= loop->send)
	{
		return false;
	}

	i = loop->get & (MAX_LOOPBACK - 1);
	loop->get++;

	memcpy(net_message->data, loop->msgs[i].data, loop->msgs[i].datalen);
	net_message->cursize = loop->msgs[i].datalen;
	*net_from = net_local_adr;

	return true;
}

static void
NET_SendLoopPacket(netsrc_t sock, int length, void *data, netadr_t to)
{
	int i;
	loopback_t *loop;

	(void)to;

	loop = &loopbacks[sock ^ 1];

	i = loop->send & (MAX_LOOPBACK - 1);
	loop->send++;

	memcpy(loop->msgs[i].data, data, length);
	loop->msgs[i].datalen = length;
}

qboolean
NET_GetPacket(netsrc_t sock, netadr_t *net_from, sizebuf_t *net_message)
{
	return NET_GetLoopPacket(sock, net_from, net_message);
}

void
NET_SendPacket(netsrc_t sock, int length, void *data, netadr_t to)
{
	if (to.type == NA_LOOPBACK)
	{
		NET_SendLoopPacket(sock, length, data, to);
	}

	/* anything else goes nowhere */
}
