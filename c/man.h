/*
 * man.h — man page search.
 *
 * mansearch() returns a freshly allocated answer, always: a real
 * page from $PLAN9/man when one is found, else a bundled helper
 * note for a known topic (labeled as not man text), else a plain
 * statement that neither exists.
 */
#ifndef MAN_H
#define MAN_H

#include "compat.h"

char *mansearch(char *topic);

#endif
