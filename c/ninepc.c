/*
 * ninepc.c — minimal 9P2000 client.
 *
 * Message framing is 4-byte little-endian size, 1-byte type,
 * 2-byte tag. Calls are synchronous: one outstanding call at a
 * time, so tags just count up (version uses NOTAG). Reads loop
 * until a zero-length read: the server caps each reply at
 * msize-11 and honors offsets for files and directories alike.
 */
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "ninepc.h"

enum {
	Tversion = 100, Rversion = 101,
	Tattach = 104, Rattach = 105, Rerror = 107,
	Twalk = 110, Rwalk = 111,
	Topen = 112, Ropen = 113,
	Tread = 116, Rread = 117,
	Tclunk = 120, Rclunk = 121,
	NOTAG = 0xFFFF,
	NOFID = 0xFFFFFFFF,
	DMDIR = 0x80000000,
};

static char nperr[256];

char *
nperrstr(void)
{
	return nperr;
}

static void
seterr(char *s)
{
	strncpy(nperr, s, sizeof nperr - 1);
	nperr[sizeof nperr - 1] = '\0';
}

/* --- little-endian helpers --- */

typedef struct {
	uchar *p;
	uchar *end;
} Buf;

static void
put16(Buf *b, u16int v)
{
	b->p[0] = v & 0xFF;
	b->p[1] = (v >> 8) & 0xFF;
	b->p += 2;
}

static void
put32(Buf *b, u32int v)
{
	b->p[0] = v & 0xFF;
	b->p[1] = (v >> 8) & 0xFF;
	b->p[2] = (v >> 16) & 0xFF;
	b->p[3] = (v >> 24) & 0xFF;
	b->p += 4;
}

static void
put64(Buf *b, u64int v)
{
	int i;

	for (i = 0; i < 8; i++)
		b->p[i] = (v >> (8 * i)) & 0xFF;
	b->p += 8;
}

static void
putstr(Buf *b, char *s)
{
	int n = strlen(s);

	put16(b, n);
	memcpy(b->p, s, n);
	b->p += n;
}

static int
get16(Buf *b, u16int *v)
{
	if (b->p + 2 > b->end)
		return -1;
	*v = b->p[0] | (b->p[1] << 8);
	b->p += 2;
	return 0;
}

static int
get32(Buf *b, u32int *v)
{
	if (b->p + 4 > b->end)
		return -1;
	*v = b->p[0] | (b->p[1] << 8) | (b->p[2] << 16) | ((u32int)b->p[3] << 24);
	b->p += 4;
	return 0;
}

static int
getstr(Buf *b, char *dst, int n)
{
	u16int len;

	if (get16(b, &len) < 0 || b->p + len > b->end)
		return -1;
	if (len >= n)
		return -1;
	memcpy(dst, b->p, len);
	dst[len] = '\0';
	b->p += len;
	return 0;
}

static int
skipn(Buf *b, int n)
{
	if (b->p + n > b->end)
		return -1;
	b->p += n;
	return 0;
}

static int
skipstr(Buf *b)
{
	u16int len;

	if (get16(b, &len) < 0 || b->p + len > b->end)
		return -1;
	b->p += len;
	return 0;
}

/* --- transport --- */

static int
writen(int fd, uchar *p, int n)
{
	while (n > 0) {
		int w = write(fd, p, n);

		if (w < 0) {
			if (errno == EINTR)
				continue;
			return -1;
		}
		p += w;
		n -= w;
	}
	return 0;
}

static int
readn(int fd, uchar *p, int n)
{
	int got = 0;

	while (got < n) {
		int r = read(fd, p + got, n - got);

		if (r < 0) {
			if (errno == EINTR)
				continue;
			return -1;
		}
		if (r == 0)
			break;
		got += r;
	}
	return got;
}

/*
 * docall sends one message and reads its reply. On Rerror it sets
 * nperr from the server's string and returns -1. The reply body
 * (after size/type/tag) is left in rbuf for the caller to parse.
 */
static int
docall(Npclient *c, int type, uchar *body, int nbody, uchar *rbuf, int *rn)
{
	uchar hdr[7];
	u32int size;
	u16int tag;
	int n;

	if (type == Tversion)
		tag = NOTAG;
	else
		tag = c->tag++;
	size = 7 + nbody;
	hdr[0] = size & 0xFF;
	hdr[1] = (size >> 8) & 0xFF;
	hdr[2] = (size >> 16) & 0xFF;
	hdr[3] = (size >> 24) & 0xFF;
	hdr[4] = type;
	hdr[5] = tag & 0xFF;
	hdr[6] = (tag >> 8) & 0xFF;
	if (writen(c->fd, hdr, 7) < 0 || writen(c->fd, body, nbody) < 0) {
		seterr("write failed");
		return -1;
	}
	if (readn(c->fd, hdr, 7) != 7) {
		seterr("short reply");
		return -1;
	}
	size = hdr[0] | (hdr[1] << 8) | (hdr[2] << 16) | ((u32int)hdr[3] << 24);
	if (size < 7 || size > 1024 * 1024) {
		seterr("bad reply size");
		return -1;
	}
	n = size - 7;
	if (readn(c->fd, rbuf, n) != n) {
		seterr("short reply body");
		return -1;
	}
	if (hdr[4] == Rerror) {
		Buf b = { rbuf, rbuf + n };
		char err[200];

		if (getstr(&b, err, sizeof err) < 0)
			seterr("server error");
		else
			seterr(err);
		return -1;
	}
	*rn = n;
	return hdr[4];
}

Npclient *
npconnect(char *addr)
{
	Npclient *c;
	uchar body[64], rbuf[128];
	Buf b;
	int rn, rtype;
	u32int msize;
	char vers[32];

	c = emalloc(sizeof *c);
	c->fd = dial(addr, nil, nil, nil);
	if (c->fd < 0) {
		seterr("dial failed");
		free(c);
		return nil;
	}
	c->tag = 1;
	c->msize = 8192;
	c->nextfid = 1;
	/* version */
	b.p = body;
	b.end = body + sizeof body;
	put32(&b, 8192);
	putstr(&b, "9P2000");
	rtype = docall(c, Tversion, body, b.p - body, rbuf, &rn);
	if (rtype != Rversion) {
		if (rtype >= 0)
			seterr("unexpected reply to version");
		close(c->fd);
		free(c);
		return nil;
	}
	{
		Buf r = { rbuf, rbuf + rn };

		if (get32(&r, &msize) < 0 || getstr(&r, vers, sizeof vers) < 0) {
			seterr("bad version reply");
			close(c->fd);
			free(c);
			return nil;
		}
		c->msize = msize;
	}
	/* attach fid 1 as root, so failures surface at connect time */
	{
		b.p = body;
		b.end = body + sizeof body;
		put32(&b, 1);
		put32(&b, NOFID);
		putstr(&b, "user");
		putstr(&b, "");
		rtype = docall(c, Tattach, body, b.p - body, rbuf, &rn);
		if (rtype != Rattach) {
			if (rtype >= 0)
				seterr("unexpected reply to attach");
			close(c->fd);
			free(c);
			return nil;
		}
		c->nextfid = 2;	/* fid 1 is root; fresh fids from 2 */
	}
	return c;
}

void
npclose(Npclient *c)
{
	if (c == nil)
		return;
	close(c->fd);
	free(c);
}

/* walk root fid 1 to path, returning a fresh opened fid, or -1.
 * A zero-element walk clones the root, so "/" gets its own fid
 * too and clunking it never kills the root. */
static int
walkopen(Npclient *c, char *path)
{
	uchar body[1024], rbuf[1024];
	Buf b;
	int rn, rtype, i;
	u32int newfid;
	char *s, *e;
	int nw = 0;
	char *names[64];

	/* split path on '/', skipping empties */
	for (s = path; *s; ) {
		while (*s == '/')
			s++;
		if (*s == '\0')
			break;
		e = s;
		while (*e && *e != '/')
			e++;
		if (nw >= 64) {
			seterr("path too deep");
			return -1;
		}
		names[nw] = emalloc(e - s + 1);
		memcpy(names[nw], s, e - s);
		names[nw][e - s] = '\0';
		nw++;
		s = e;
	}
	newfid = c->nextfid++;
	b.p = body;
	b.end = body + sizeof body;
	put32(&b, 1);	/* root fid */
	put32(&b, newfid);
	put16(&b, nw);
	for (i = 0; i < nw; i++)
		putstr(&b, names[i]);
	rtype = docall(c, Twalk, body, b.p - body, rbuf, &rn);
	for (i = 0; i < nw; i++)
		free(names[i]);
	if (rtype != Rwalk) {
		if (rtype >= 0)
			seterr("unexpected reply to walk");
		return -1;
	}
	{
		Buf r = { rbuf, rbuf + rn };
		u16int nqid;

		if (get16(&r, &nqid) < 0 || nqid != nw) {
			seterr("short walk");
			return -1;
		}
	}
	/* open, mode OREAD */
	b.p = body;
	b.end = body + sizeof body;
	put32(&b, newfid);
	body[b.p - body] = 0;
	b.p++;
	rtype = docall(c, Topen, body, b.p - body, rbuf, &rn);
	if (rtype != Ropen) {
		if (rtype >= 0)
			seterr("unexpected reply to open");
		return -1;
	}
	return newfid;
}

static void
clunk(Npclient *c, u32int fid)
{
	uchar body[16], rbuf[64];
	Buf b;
	int rn;

	b.p = body;
	b.end = body + sizeof body;
	put32(&b, fid);
	docall(c, Tclunk, body, b.p - body, rbuf, &rn);
}

/* read fid fully, looping to offset EOF; returns buffer + length */
static int
readall(Npclient *c, u32int fid, uchar **out, int *outlen)
{
	uchar body[32], rbuf[8192 + 64];
	Buf b;
	int rn, rtype;
	u64int off = 0;
	int cap = 8192, len = 0;
	uchar *buf = emalloc(cap);

	for (;;) {
		b.p = body;
		b.end = body + sizeof body;
		put32(&b, fid);
		put64(&b, off);
		put32(&b, 8192);
		rtype = docall(c, Tread, body, b.p - body, rbuf, &rn);
		if (rtype != Rread) {
			if (rtype >= 0)
				seterr("unexpected reply to read");
			free(buf);
			return -1;
		}
		{
			Buf r = { rbuf, rbuf + rn };
			u32int n;

			if (get32(&r, &n) < 0 || r.p + n > r.end) {
				seterr("bad read reply");
				free(buf);
				return -1;
			}
			if (n == 0)
				break;
			if ((u32int)(cap - len) < n) {
				cap = (len + n) * 2;
				buf = realloc(buf, cap);
				if (buf == nil)
					sysfatal("out of memory");
			}
			memcpy(buf + len, r.p, n);
			len += n;
			off += n;
		}
	}
	*out = buf;
	*outlen = len;
	return 0;
}

char *
npreadfile(Npclient *c, char *path)
{
	int fid = walkopen(c, path);
	uchar *buf;
	int len;
	char *s;

	if (fid < 0)
		return nil;
	if (readall(c, fid, &buf, &len) < 0) {
		clunk(c, fid);
		return nil;
	}
	clunk(c, fid);
	s = emalloc(len + 1);
	memcpy(s, buf, len);
	s[len] = '\0';
	free(buf);
	return s;
}

int
nplistdir(Npclient *c, char *path, char ***names, int **isdirs)
{
	int fid = walkopen(c, path);
	uchar *buf;
	int len, count = 0, cap = 16;
	char **ns;
	int *ds;
	Buf r;

	if (fid < 0)
		return -1;
	if (readall(c, fid, &buf, &len) < 0) {
		clunk(c, fid);
		return -1;
	}
	clunk(c, fid);
	ns = emalloc(cap * sizeof *ns);
	ds = emalloc(cap * sizeof *ds);
	r.p = buf;
	r.end = buf + len;
	while (r.p < r.end) {
		u16int size;
		u32int mode;
		char name[256];
		uchar *start;

		if (get16(&r, &size) < 0)
			break;
		start = r.p;
		/* type(2) dev(4) qid(13) */
		if (skipn(&r, 2 + 4 + 13) < 0)
			break;
		if (get32(&r, &mode) < 0)
			break;
		/* atime(4) mtime(4) length(8) */
		if (skipn(&r, 4 + 4 + 8) < 0)
			break;
		if (getstr(&r, name, sizeof name) < 0)
			break;
		/* uid gid muid */
		if (skipstr(&r) < 0 || skipstr(&r) < 0 || skipstr(&r) < 0)
			break;
		r.p = start + size;
		if (count >= cap) {
			cap *= 2;
			ns = realloc(ns, cap * sizeof *ns);
			ds = realloc(ds, cap * sizeof *ds);
			if (ns == nil || ds == nil)
				sysfatal("out of memory");
		}
		ns[count] = estrdup(name);
		ds[count] = (mode & DMDIR) != 0;
		count++;
	}
	free(buf);
	*names = ns;
	*isdirs = ds;
	return count;
}
