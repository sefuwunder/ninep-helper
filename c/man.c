/*
 * man.c — man page search.
 *
 * Order: $PLAN9/man first (section dirs named man1, man4, ...; a
 * file named <topic> or <topic>.*), then bundled helper notes,
 * then an honest miss. Troff is stripped the same way the Bun
 * helper does it: request lines dropped, font escapes removed.
 * Directory walking here uses opendir/readdir — the Linux side.
 * On Plan 9 that walk becomes dirread; the rest is unchanged.
 */
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "man.h"

typedef struct {
	char *topic;
	char *note;
} Note;

static Note notes[] = {
	{ "9p",
		"Helper note (not man text): 9P is the file protocol — attach,\n"
		"walk, open, read, write. Everything is a file, served over a\n"
		"connection. Our desk servers bind loopback only, v1 has no\n"
		"authentication and no Twstat." },
	{ "acme",
		"Helper note (not man text): acme is the window system where\n"
		"each window is a file with a tag row of verbs. Middle-click a tag\n"
		"word to run it; selection is the argument. Get reloads, Put writes,\n"
		"Del closes, Edit takes sam commands. The plumber routes looked-up\n"
		"text to the window it belongs to." },
	{ "plumber",
		"Helper note (not man text): the plumber routes looked-up\n"
		"text by kind — file path, URL, thread id, run id — to the window\n"
		"or app that handles it. Rules match patterns; fix a wrong routing\n"
		"in the rule, not in the click." },
	{ "factotum",
		"Helper note (not man text): factotum holds keys at use in\n"
		"memory (attribute=value pairs), served as files: ctl, rpc, proto,\n"
		"needkey, confirm, log. Secstore unlocks with one password at login\n"
		"and hands keys to factotum. Approve only a key use you started." },
	{ "namespace",
		"Helper note (not man text): a namespace is a process's\n"
		"mount table. The desk mounts cadence at /n/cadence and comms at\n"
		"/n/comms; scoping is by mount, not by per-app permissions." },
	{ "translator",
		"Helper note (not man text): the translator drafts runs\n"
		"from comms threads into cadence staging. First-seen threads only;\n"
		"groups and known contacts skipped; template matched on /onboard/i.\n"
		"Approve by writing a playbook id (or empty) to approve; reject by\n"
		"writing to reject. Drafts do not expire in v1." },
};

/* read a whole file; nil on failure, caller frees */
static char *
readall(char *path)
{
	FILE *f = fopen(path, "r");
	long n;
	char *buf;

	if (f == nil)
		return nil;
	fseek(f, 0, SEEK_END);
	n = ftell(f);
	fseek(f, 0, SEEK_SET);
	if (n < 0)
		n = 0;
	buf = emalloc(n + 1);
	if (n > 0 && fread(buf, 1, n, f) != (size_t)n) {
		fclose(f);
		free(buf);
		return nil;
	}
	buf[n] = '\0';
	fclose(f);
	return buf;
}

/* strip troff: drop request lines, remove font escapes, collapse */
static char *
striptroff(char *s)
{
	char *out = emalloc(strlen(s) + 1);
	char *o = out;
	char *p = s;
	int lastnl = 1;	/* at line start, for blank-run collapse */

	while (*p) {
		/* line start: request line? */
		if (lastnl && *p == '.') {
			while (*p && *p != '\n')
				p++;
			continue;
		}
		if (*p == '\n') {
			/* collapse runs of blank lines to one */
			if (o > out && o[-1] == '\n') {
				p++;
				continue;
			}
			*o++ = *p++;
			lastnl = 1;
			continue;
		}
		lastnl = 0;
		if (p[0] == '\\' && p[1] == 'f' && p[2] == '[') {
			p += 3;
			while (*p && *p != ']')
				p++;
			if (*p == ']')
				p++;
			continue;
		}
		if (p[0] == '\\' && p[1] == 'f' && p[2]) {
			p += 3;
			continue;
		}
		if (p[0] == '\\' && p[1] == '-') {
			*o++ = '-';
			p += 2;
			continue;
		}
		if (p[0] == '\\' && p[1] == '&') {
			p += 2;
			continue;
		}
		if (p[0] == '\\' && p[1] == '\\') {
			*o++ = '\\';
			p += 2;
			continue;
		}
		if (*p == ' ' || *p == '\t') {
			/* collapse horizontal space */
			if (o > out && o[-1] != ' ' && o[-1] != '\n')
				*o++ = ' ';
			p++;
			continue;
		}
		*o++ = *p++;
	}
	*o = '\0';
	return out;
}

/* extract the .SH TITLE section; nil if absent or empty */
static char *
section(char *text, char *title)
{
	char *p, *start, *end, *raw, *clean;
	char hdr[64];

	snprintf(hdr, sizeof hdr, ".SH %s", title);
	for (p = text; p != nil; ) {
		char *nl = strchr(p, '\n');
		int linelen = nl ? (int)(nl - p) : (int)strlen(p);

		if (linelen == (int)strlen(hdr) && strncmp(p, hdr, linelen) == 0)
			break;
		p = nl ? nl + 1 : nil;
	}
	if (p == nil)
		return nil;
	start = strchr(p, '\n');
	if (start == nil)
		return nil;
	start++;
	end = start;
	while (*end) {
		if (strncmp(end, ".SH", 3) == 0 &&
		    (end[3] == ' ' || end[3] == '\t' || end[3] == '\n'))
			break;
		end = strchr(end, '\n');
		if (end == nil) {
			end = start + strlen(start);
			break;
		}
		end++;
	}
	raw = emalloc(end - start + 1);
	memcpy(raw, start, end - start);
	raw[end - start] = '\0';
	clean = striptroff(raw);
	free(raw);
	/* trim leading/trailing whitespace */
	{
		char *s = clean;
		char *e;

		while (*s == ' ' || *s == '\n' || *s == '\t')
			s++;
		e = s + strlen(s);
		while (e > s && (e[-1] == ' ' || e[-1] == '\n' || e[-1] == '\t'))
			*--e = '\0';
		if (*s == '\0') {
			free(clean);
			return nil;
		}
		if (s != clean)
			memmove(clean, s, strlen(s) + 1);
	}
	return clean;
}

static char *
lowerdup(char *s)
{
	char *d = estrdup(s);
	char *p;

	for (p = d; *p; p++)
		if (*p >= 'A' && *p <= 'Z')
			*p += 'a' - 'A';
	return d;
}

/* join dir/file with one slash */
static char *
joinpath(char *dir, char *file)
{
	int n = strlen(dir) + 1 + strlen(file) + 1;
	char *p = emalloc(n);

	snprintf(p, n, "%s/%s", dir, file);
	return p;
}

char *
mansearch(char *topic)
{
	char *plan9 = getenv("PLAN9");
	char *lower;
	int i;

	if (plan9 != nil) {
		char *manroot = joinpath(plan9, "man");
		DIR *d = opendir(manroot);

		if (d != nil) {
			struct dirent *sec;

			while ((sec = readdir(d)) != nil) {
				DIR *sd;
				struct dirent *f;
				char *secdir;

				if (strncmp(sec->d_name, "man", 3) != 0)
					continue;
				secdir = joinpath(manroot, sec->d_name);
				sd = opendir(secdir);
				if (sd == nil) {
					free(secdir);
					continue;
				}
				while ((f = readdir(sd)) != nil) {
					char *path, *raw, *name, *desc, *out;
					int n;

					if (strcmp(f->d_name, topic) != 0 &&
					    strncmp(f->d_name, topic, strlen(topic)) != 0)
						continue;
					if (strcmp(f->d_name, topic) != 0 &&
					    f->d_name[strlen(topic)] != '.')
						continue;
					path = joinpath(secdir, f->d_name);
					raw = readall(path);
					free(path);
					if (raw == nil)
						continue;
					name = section(raw, "NAME");
					desc = section(raw, "DESCRIPTION");
					free(raw);
					if (name == nil && desc == nil)
						continue;
					n = strlen(topic) + 64 +
					    (name ? strlen(name) : 0) +
					    (desc ? strlen(desc) : 0) + 4;
					out = emalloc(n);
					snprintf(out, n, "%s(%s) — from $PLAN9/man:\n%s%s%s",
						topic, sec->d_name + 3,
						name ? name : "",
						(name && desc) ? "\n\n" : "",
						desc ? desc : "");
					free(name);
					free(desc);
					closedir(sd);
					closedir(d);
					free(secdir);
					free(manroot);
					return out;
				}
				closedir(sd);
				free(secdir);
			}
			closedir(d);
		}
		free(manroot);
	}
	lower = lowerdup(topic);
	for (i = 0; i < (int)(sizeof notes / sizeof notes[0]); i++) {
		if (strcmp(lower, notes[i].topic) == 0) {
			free(lower);
			return estrdup(notes[i].note);
		}
	}
	free(lower);
	{
		int n = strlen(topic) + 160;
		char *out = emalloc(n);

		snprintf(out, n,
			"No man page or helper note for \"%s\". Try: 9p, acme, "
			"plumber, factotum, namespace, translator — or on a "
			"plan9port system, set PLAN9 and run: man %s",
			topic, topic);
		return out;
	}
}
