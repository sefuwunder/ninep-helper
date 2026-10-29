/*
 * route.h — deterministic question routing.
 *
 * route() scores a question against a fixed intent table and
 * returns the intent id and its fixed answer. Live intents
 * (desk-drafts-live, desk-today-live, desk-status-live) carry an
 * empty answer: the caller runs the live command instead. The
 * "man" intent carries the man-search result as its answer.
 * Same question, same answer, every time.
 */
#ifndef ROUTE_H
#define ROUTE_H

#include "compat.h"

char *route(char *question, char **intent);

#endif
