/*
 * test.c — tests for the C port.
 *
 * Routing table (same questions as the Bun suite, including the
 * approve-vs-drafts tie-break), man search against a fake
 * $PLAN9/man tree, and — when given a ports file as argv[1] —
 * the live commands against stub 9P trees plus a down-server
 * case. Run via tests/run.sh, which starts the stubs.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <unistd.h>
#include "compat.h"
#include "ninepc.h"
#include "route.h"
#include "man.h"

static int failures;
static int checks;

#define CHECK(cond, msg) do { \
	checks++; \
	if (!(cond)) { \
		failures++; \
		fprint(2, "FAIL: %s\n", msg); \
	} \
} while (0)

static void
testroute(void)
{
	struct {
		char *q;
		char *intent;
	} rows[] = {
		{ "how do tags work in acme", "acme-tags" },
		{ "how do I search in acme", "acme-look" },
		{ "what does Get do in acme", "acme-windows" },
		{ "how does plumbing work", "acme-plumbing" },
		{ "what is factotum", "factotum" },
		{ "what is 9p", "ninep-basics" },
		{ "what is a namespace", "namespace" },
		{ "how does the translator decide", "translator-rules" },
		{ "how do I approve a draft", "translator-approve" },
		{ "how do I start the 9p servers", "desk-start" },
		{ "is the desk secure", "security-loopback" },
		{ "how do I back up the desk", "durability-backup" },
		{ "show me the drafts", "desk-drafts-live" },
		{ "what is due today", "desk-today-live" },
		{ "are the servers up", "desk-status-live" },
		{ "how do I search the man pages", "man-howto" },
		{ "man factotum", "man" },
		{ "what is the weather", "fallback" },
		{ "help", "help" },
	};
	int i;

	for (i = 0; i < (int)(sizeof rows / sizeof rows[0]); i++) {
		char *intent;
		char *ans = route(rows[i].q, &intent);

		{
			char msg[256];

			snprintf(msg, sizeof msg, "route %s -> %s (got %s)",
				rows[i].q, rows[i].intent, intent);
			CHECK(strcmp(intent, rows[i].intent) == 0, msg);
		}
		free(ans);
	}
	{
		char *i1, *i2;
		char *a1 = route("what is factotum", &i1);
		char *a2 = route("what is factotum", &i2);

		CHECK(strcmp(a1, a2) == 0, "same question twice, same answer");
		free(a1);
		free(a2);
	}
	{
		char *intent;
		char *ans = route("how do I approve a draft", &intent);

		CHECK(strcmp(intent, "translator-approve") == 0,
			"approve-a-draft beats show-drafts on a tie");
		free(ans);
	}
}

static void
testman(void)
{
	char dir[] = "/tmp/ninep-helper-man-XXXXXX";
	char path[512];
	FILE *f;
	char *r;

	if (mkdtemp(dir) == nil)
		sysfatal("mkdtemp: %r");
	snprintf(path, sizeof path, "%s/man", dir);
	mkdir(path, 0755);
	snprintf(path, sizeof path, "%s/man/man1", dir);
	mkdir(path, 0755);
	snprintf(path, sizeof path, "%s/man/man1/acme.1", dir);
	f = fopen(path, "w");
	if (f == nil)
		sysfatal("fopen: %r");
	fputs(".TH ACME 1\n.SH NAME\nacme \\- window system\n"
		".SH DESCRIPTION\nAcme is a text editor and window system.\n"
		".PP\nSecond paragraph.\n", f);
	fclose(f);
	setenv("PLAN9", dir, 1);
	r = mansearch("acme");
	CHECK(strstr(r, "window system") != nil, "man page NAME found");
	CHECK(strstr(r, "text editor") != nil, "man page DESCRIPTION found");
	CHECK(strstr(r, ".TH") == nil, "troff .TH stripped");
	CHECK(strstr(r, ".SH") == nil, "troff .SH stripped");
	free(r);
	r = mansearch("factotum");
	CHECK(strstr(r, "Helper note") != nil, "bundled note fallback");
	CHECK(strstr(r, "factotum") != nil, "bundled note topic");
	free(r);
	r = mansearch("nosuchtopic");
	CHECK(strstr(r, "No man page") != nil, "unknown topic reported");
	free(r);
	unsetenv("PLAN9");
	/* best-effort cleanup; the dir is in /tmp either way */
	snprintf(path, sizeof path, "rm -rf %s", dir);
	(void)!system(path);
}

/* read "KEY=value" lines from the ports file into env */
static void
loadports(char *path)
{
	FILE *f = fopen(path, "r");
	char line[256];

	if (f == nil)
		sysfatal("open ports file: %r");
	while (fgets(line, sizeof line, f) != nil) {
		char *eq = strchr(line, '=');

		if (eq == nil)
			continue;
		*eq = '\0';
		{
			char *nl = strchr(eq + 1, '\n');

			if (nl != nil)
				*nl = '\0';
		}
		setenv(line, eq + 1, 1);
	}
	fclose(f);
}

static char *
toaddr(char *envname)
{
	char *a = getenv(envname);
	char *colon, *out;

	if (a == nil)
		return nil;
	colon = strrchr(a, ':');
	if (colon == nil)
		return nil;
	out = emalloc(strlen("tcp!") + (colon - a) + 1 + strlen(colon + 1) + 1);
	snprintf(out, strlen("tcp!") + (colon - a) + 1 + strlen(colon + 1) + 1,
		"tcp!%.*s!%s", (int)(colon - a), a, colon + 1);
	return out;
}

static void
testlive(void)
{
	char *addr;
	Npclient *c;

	addr = toaddr("CADENCE_NINEP");
	c = npconnect(addr);
	CHECK(c != nil, "cadence stub connects");
	if (c != nil) {
		char **names;
		int *isdirs, n, i, found = 0;

		n = nplistdir(c, "/", &names, &isdirs);
		CHECK(n == 2, "cadence root has 2 entries");
		for (i = 0; i < n; i++)
			free(names[i]);
		free(names);
		free(isdirs);
		n = nplistdir(c, "/staging", &names, &isdirs);
		CHECK(n == 1, "staging has 1 draft");
		for (i = 0; i < n; i++) {
			if (strcmp(names[i], "relay-t1") == 0 && isdirs[i])
				found = 1;
			free(names[i]);
		}
		free(names);
		free(isdirs);
		CHECK(found, "staging draft is relay-t1");
		{
			char *t = npreadfile(c, "/today");

			CHECK(t != nil && strstr(t, "Call New Person") != nil,
				"today reads stub today file");
			free(t);
		}
		{
			char *v = npreadfile(c, "/staging/relay-t1/contact");

			CHECK(v != nil && strcmp(v, "New Person") == 0,
				"draft contact field reads");
			free(v);
		}
		npclose(c);
	}
	free(addr);
	/* down server: bind a port, close it, then connect must fail */
	{
		int s = socket(AF_INET, SOCK_STREAM, 0);
		struct sockaddr_in sin;
		socklen_t len = sizeof sin;
		char dead[64];

		sin.sin_family = AF_INET;
		sin.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
		sin.sin_port = 0;
		if (bind(s, (struct sockaddr*)&sin, sizeof sin) == 0 &&
		    getsockname(s, (struct sockaddr*)&sin, &len) == 0) {
			int port = ntohs(sin.sin_port);

			close(s);
			snprintf(dead, sizeof dead, "tcp!127.0.0.1!%d", port);
			c = npconnect(dead);
			CHECK(c == nil, "down server fails to connect");
			if (c != nil)
				npclose(c);
		} else {
			close(s);
			CHECK(0, "could not allocate a dead port");
		}
	}
}

void
main(int argc, char *argv[])
{
	testroute();
	testman();
	if (argc > 1) {
		loadports(argv[1]);
		testlive();
	} else {
		print("live tests skipped (no ports file)\n");
	}
	print("%d checks, %d failures\n", checks, failures);
	exits(failures ? "test failures" : nil);
}
