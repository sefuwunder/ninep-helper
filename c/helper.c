/*
 * helper.c — the ninep-helper command.
 *
 *   helper <question...>   ask in plain words
 *   helper man <topic>     search the man pages
 *   helper status          which 9P servers are answering
 *   helper drafts          current staging drafts
 *   helper today           today's steps from cadence
 *
 * The question form also works from an acme tag: the arguments
 * are the question, so a tag word can run the helper on selected
 * text. Live commands speak 9P to the desk servers; a server that
 * is down is reported, never fatal.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "compat.h"
#include "ninepc.h"
#include "route.h"

static char *usage =
	"ninep-helper — deterministic 9P desk helper (no model).\n"
	"  helper <question...>   ask in plain words\n"
	"  helper man <topic>     search the man pages\n"
	"  helper status          which 9P servers are answering\n"
	"  helper drafts          current staging drafts\n"
	"  helper today           today's steps from cadence\n";

/* env addr "host:port" -> dial addr "tcp!host!port" */
static char *
dialaddr(char *envname, char *fallback)
{
	char *a = getenv(envname);
	char *colon, *out;
	int n;

	if (a == nil)
		a = fallback;
	colon = strrchr(a, ':');
	if (colon == nil)
		sysfatal("bad address %s", a);
	n = strlen("tcp!") + (colon - a) + 1 + strlen(colon + 1) + 1;
	out = emalloc(n);
	snprintf(out, n, "tcp!%.*s!%s", (int)(colon - a), a, colon + 1);
	return out;
}

static Npclient *
dialserver(char *envname, char *fallback)
{
	char *addr = dialaddr(envname, fallback);
	Npclient *c = npconnect(addr);

	free(addr);
	return c;
}

static void
cmdstatus(void)
{
	struct {
		char *name;
		char *env;
		char *fallback;
	} servers[] = {
		{ "cadence", "CADENCE_NINEP", "127.0.0.1:9040" },
		{ "relay", "RELAY_NINEP", "127.0.0.1:9006" },
		{ "outpost", "OUTPOST_NINEP", "127.0.0.1:9029" },
	};
	int i;

	for (i = 0; i < 3; i++) {
		Npclient *c = dialserver(servers[i].env, servers[i].fallback);
		char *a = getenv(servers[i].env);

		if (a == nil)
			a = servers[i].fallback;
		if (c == nil) {
			print("%s: not answering on %s\n", servers[i].name, a);
			continue;
		}
		{
			char **names;
			int *isdirs, n, j;

			n = nplistdir(c, "/", &names, &isdirs);
			if (n < 0) {
				print("%s: error on %s: %s\n", servers[i].name, a, nperrstr());
			} else {
				print("%s: up on %s (%d top-level entries)\n",
					servers[i].name, a, n);
				for (j = 0; j < n; j++)
					free(names[j]);
				free(names);
				free(isdirs);
			}
		}
		npclose(c);
	}
}

static char *
trimdup(char *s)
{
	char *d, *e;

	while (*s == ' ' || *s == '\t' || *s == '\n')
		s++;
	d = estrdup(s);
	e = d + strlen(d);
	while (e > d && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\n'))
		*--e = '\0';
	return d;
}

static char *
field(Npclient *c, char *draft, char *name)
{
	char path[512];
	char *v;

	snprintf(path, sizeof path, "/staging/%s/%s", draft, name);
	v = npreadfile(c, path);
	if (v == nil)
		return estrdup("");
	{
		char *t = trimdup(v);

		free(v);
		return t;
	}
}

static void
cmddrafts(void)
{
	Npclient *c = dialserver("CADENCE_NINEP", "127.0.0.1:9040");
	char **names;
	int *isdirs, n, i;

	if (c == nil) {
		print("cadence 9P not answering — start it first.\n");
		return;
	}
	n = nplistdir(c, "/staging", &names, &isdirs);
	if (n < 0) {
		print("could not list staging: %s\n", nperrstr());
		npclose(c);
		return;
	}
	if (n == 0) {
		print("No drafts in staging.\n");
	} else {
		for (i = 0; i < n; i++) {
			char *status, *contact, *thread, *template;

			if (!isdirs[i]) {
				free(names[i]);
				continue;
			}
			status = field(c, names[i], "status");
			contact = field(c, names[i], "contact");
			thread = field(c, names[i], "thread");
			template = field(c, names[i], "template");
			print("%s: [%s] %s — %s (template: %s)\n",
				names[i],
				status[0] ? status : "proposed",
				contact, thread,
				template[0] ? template : "(blank)");
			free(status);
			free(contact);
			free(thread);
			free(template);
			free(names[i]);
		}
	}
	free(names);
	free(isdirs);
	npclose(c);
}

static void
cmdtoday(void)
{
	Npclient *c = dialserver("CADENCE_NINEP", "127.0.0.1:9040");
	char *t;

	if (c == nil) {
		print("cadence 9P not answering — start it first.\n");
		return;
	}
	t = npreadfile(c, "/today");
	if (t == nil)
		print("could not read today: %s\n", nperrstr());
	else {
		print("%s", t);
		free(t);
	}
	npclose(c);
}

static char *
joinargs(int argc, char *argv[], int from)
{
	int i, n = 0;
	char *out, *p;

	for (i = from; i < argc; i++)
		n += strlen(argv[i]) + 1;
	out = emalloc(n + 1);
	p = out;
	for (i = from; i < argc; i++) {
		int len = strlen(argv[i]);

		memcpy(p, argv[i], len);
		p += len;
		if (i + 1 < argc)
			*p++ = ' ';
	}
	*p = '\0';
	return out;
}

void
main(int argc, char *argv[])
{
	if (argc < 2) {
		print("%s", usage);
		exits(nil);
	}
	if (strcmp(argv[1], "status") == 0) {
		cmdstatus();
	} else if (strcmp(argv[1], "drafts") == 0) {
		cmddrafts();
	} else if (strcmp(argv[1], "today") == 0) {
		cmdtoday();
	} else {
		char *question;
		char *intent;
		char *answer;
		int from = 1;

		if (strcmp(argv[1], "ask") == 0)
			from = 2;
		if (strcmp(argv[1], "man") == 0) {
			question = joinargs(argc, argv, 1);
		} else {
			question = joinargs(argc, argv, from);
		}
		answer = route(question, &intent);
		free(question);
		if (strcmp(intent, "desk-drafts-live") == 0)
			cmddrafts();
		else if (strcmp(intent, "desk-today-live") == 0)
			cmdtoday();
		else if (strcmp(intent, "desk-status-live") == 0)
			cmdstatus();
		else
			print("%s\n", answer);
		free(answer);
	}
	exits(nil);
}
