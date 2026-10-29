/*
 * ninepc.h — minimal 9P2000 client, enough for the helper.
 *
 * One connection serves many reads: each read attaches a fresh root
 * fid, walks it, opens, reads to EOF, and clunks. Only the verbs
 * the helper needs exist here: version, attach, walk, open, read,
 * clunk. No auth, no writes, no stat.
 */
#ifndef NINEPC_H
#define NINEPC_H

#include "compat.h"

typedef struct Npclient Npclient;
struct Npclient {
	int fd;
	int tag;
	u32int msize;
	u32int nextfid;
};

char *nperrstr(void);
Npclient *npconnect(char *addr);	/* addr: tcp!host!port */
void npclose(Npclient *c);
char *npreadfile(Npclient *c, char *path);	/* nil on error; caller frees */
int nplistdir(Npclient *c, char *path, char ***names, int **isdirs);
/* count or -1; caller frees names, each name, and isdirs */

#endif
