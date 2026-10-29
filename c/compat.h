/*
 * compat.h — Plan 9 surface over Linux libc, for the Linux build.
 *
 * On Plan 9 or plan9port, drop this file and compat.c and include
 * <u.h> and <libc.h> instead: print, fprint, sysfatal, dial, and
 * exits are all real there, with the same shapes used here.
 * emalloc and estrdup are NOT declared there and need small local
 * definitions (see c/README.md). Only two other Linux-isms
 * remain, both marked where they occur: ninepc.c uses dial()
 * (real on Plan 9, shimmed here), and man.c walks directories
 * with opendir/readdir (dirread on Plan 9).
 */
#ifndef COMPAT_H
#define COMPAT_H

typedef unsigned char uchar;
typedef unsigned short ushort;
typedef unsigned int uint;
typedef unsigned long ulong;
typedef unsigned long long uvlong;
typedef unsigned char u8int;
typedef unsigned short u16int;
typedef unsigned int u32int;
typedef unsigned long long u64int;

#define nil ((void*)0)

int print(char *fmt, ...);
int fprint(int fd, char *fmt, ...);
void sysfatal(char *fmt, ...);
void *emalloc(ulong n);
char *estrdup(char *s);
int dial(char *addr, char *local, char *dir, int *cfd);
void exits(char *status);

#endif
