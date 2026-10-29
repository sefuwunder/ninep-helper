/*
 * compat.c — the Linux side of compat.h.
 *
 * print/fprint are a minimal formatter supporting %s, %d, %c, %r
 * (the errno string, as in Plan 9's fmt) and %%. Call sites use
 * only those verbs. dial() speaks Plan 9's net!host!port form over
 * POSIX sockets; it understands tcp only, which is all the helper
 * needs.
 */
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/types.h>
#include "compat.h"

static int
dofmt(int fd, char *fmt, va_list ap)
{
	char buf[64];
	char *p;
	int n = 0;

	for (p = fmt; *p; p++) {
		if (*p != '%') {
			if (write(fd, p, 1) < 0)
				return -1;
			n++;
			continue;
		}
		p++;
		switch (*p) {
		case 's': {
			char *s = va_arg(ap, char*);
			if (s == nil)
				s = "<nil>";
			if (write(fd, s, strlen(s)) < 0)
				return -1;
			n += strlen(s);
			break;
		}
		case 'd': {
			int d = va_arg(ap, int);
			int len = snprintf(buf, sizeof buf, "%d", d);
			if (write(fd, buf, len) < 0)
				return -1;
			n += len;
			break;
		}
		case 'c': {
			char c = (char)va_arg(ap, int);
			if (write(fd, &c, 1) < 0)
				return -1;
			n++;
			break;
		}
		case 'r': {
			char *s = strerror(errno);
			if (write(fd, s, strlen(s)) < 0)
				return -1;
			n += strlen(s);
			break;
		}
		case '%':
			if (write(fd, "%", 1) < 0)
				return -1;
			n++;
			break;
		default:
			/* unknown verb: print it literally */
			if (write(fd, "%", 1) < 0 || write(fd, p, 1) < 0)
				return -1;
			n += 2;
			break;
		}
	}
	return n;
}

int
print(char *fmt, ...)
{
	va_list ap;
	int n;

	va_start(ap, fmt);
	n = dofmt(1, fmt, ap);
	va_end(ap);
	return n;
}

int
fprint(int fd, char *fmt, ...)
{
	va_list ap;
	int n;

	va_start(ap, fmt);
	n = dofmt(fd, fmt, ap);
	va_end(ap);
	return n;
}

void
sysfatal(char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	dofmt(2, fmt, ap);
	va_end(ap);
	if (write(2, "\n", 1) < 0) {
		/* nothing left to report to */
	}
	exits("fatal");
}

void *
emalloc(ulong n)
{
	void *p;

	if (n == 0)
		n = 1;
	p = malloc(n);
	if (p == nil)
		sysfatal("out of memory");
	return p;
}

char *
estrdup(char *s)
{
	char *p;

	p = strdup(s);
	if (p == nil)
		sysfatal("out of memory");
	return p;
}

int
dial(char *addr, char *local, char *dir, int *cfd)
{
	char *net, *host, *port, *bang;
	struct addrinfo hints, *res, *r;
	int fd = -1;

	(void)local;
	(void)dir;
	(void)cfd;
	if (addr == nil)
		return -1;
	bang = strchr(addr, '!');
	if (bang == nil)
		return -1;
	net = emalloc(bang - addr + 1);
	memcpy(net, addr, bang - addr);
	net[bang - addr] = '\0';
	if (strcmp(net, "tcp") != 0) {
		free(net);
		errno = EINVAL;
		return -1;
	}
	free(net);
	host = bang + 1;
	bang = strchr(host, '!');
	if (bang == nil)
		return -1;
	port = bang + 1;
	/* host is not NUL-terminated at bang; copy the piece */
	{
		char *h = emalloc(bang - host + 1);
		memcpy(h, host, bang - host);
		h[bang - host] = '\0';
		memset(&hints, 0, sizeof hints);
		hints.ai_family = AF_UNSPEC;
		hints.ai_socktype = SOCK_STREAM;
		if (getaddrinfo(h, port, &hints, &res) != 0) {
			free(h);
			return -1;
		}
		free(h);
	}
	for (r = res; r != nil; r = r->ai_next) {
		fd = socket(r->ai_family, r->ai_socktype, r->ai_protocol);
		if (fd < 0)
			continue;
		if (connect(fd, r->ai_addr, r->ai_addrlen) == 0)
			break;
		close(fd);
		fd = -1;
	}
	freeaddrinfo(res);
	return fd;
}

void
exits(char *status)
{
	exit(status == nil ? 0 : 1);
}
