/*
 * route.c — deterministic question routing.
 *
 * No regular expressions: matching is whole words, word prefixes,
 * phrases, and ordered word sequences, all over a lowercased copy
 * of the question. A pattern's alternatives are written "a|b|c".
 * Score is matched patterns per intent; ties go to the intent
 * listed first. The table order below is load-bearing for two
 * known ties: translator-approve precedes desk-drafts-live, so
 * "how do I approve a draft" does not become "show me drafts".
 */
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include "route.h"
#include "man.h"

enum {
	P_WORD,		/* any alternative as a whole word */
	P_PREFIX,	/* any alternative as a word prefix */
	P_PHRASE,	/* any alternative as a substring */
	P_SEQ,		/* first alternative, then second after it */
	P_SEQ3,		/* first, then second, then third, in order */
};

typedef struct {
	int kind;
	char *a;
	char *b;
	char *c;
} Pat;

typedef struct {
	char *id;
	Pat *pats;
	int npats;
	char *answer;
} Intent;

static int
iswordchar(int ch)
{
	return isalnum(ch) != 0;
}

/* find word w in s as a whole word; returns position or nil */
static char *
findword(char *s, char *w)
{
	int n = strlen(w);
	char *p;

	for (p = s; (p = strstr(p, w)) != nil; p++) {
		if ((p == s || !iswordchar(p[-1])) &&
		    !iswordchar(p[n]))
			return p;
	}
	return nil;
}

/* find a word starting with prefix in s; returns position or nil */
static char *
findprefix(char *s, char *prefix)
{
	int n = strlen(prefix);
	char *p;

	for (p = s; *p; p++) {
		if ((p == s || !iswordchar(p[-1])) &&
		    strncmp(p, prefix, n) == 0)
			return p;
	}
	return nil;
}

/* any alternative in "a|b|c" matches via fn; returns position */
static char *
anyalt(char *s, char *alts, char *(*fn)(char*, char*))
{
	char *copy = estrdup(alts);
	char *save = nil;
	char *tok;
	char *found = nil;

	for (tok = strtok_r(copy, "|", &save); tok != nil;
	     tok = strtok_r(nil, "|", &save)) {
		char *p = fn(s, tok);

		if (p != nil && (found == nil || p < found))
			found = p;
	}
	free(copy);
	return found;
}

/* strstr with the same shape as findword/findprefix */
static char *
substr(char *s, char *w)
{
	return strstr(s, w);
}

static int
matchpat(char *s, Pat *p)
{
	char *first;

	switch (p->kind) {
	case P_WORD:
		return anyalt(s, p->a, findword) != nil;
	case P_PREFIX:
		return anyalt(s, p->a, findprefix) != nil;
	case P_PHRASE:
		return anyalt(s, p->a, substr) != nil;
	case P_SEQ:
		first = anyalt(s, p->a, findword);
		if (first == nil)
			return 0;
		return anyalt(first + 1, p->b, findword) != nil;
	case P_SEQ3: {
		char *second;

		first = anyalt(s, p->a, findword);
		if (first == nil)
			return 0;
		second = anyalt(first + 1, p->b, findword);
		if (second == nil)
			return 0;
		return anyalt(second + 1, p->c, findword) != nil;
	}
	}
	return 0;
}

static Pat p_help[] = {
	{ P_WORD, "help|usage|command|commands", nil, nil },
	{ P_PHRASE, "what can you do", nil, nil },
};
static Pat p_manhowto[] = {
	{ P_PHRASE, "man page", nil, nil },
	{ P_WORD, "manual", nil, nil },
	{ P_PHRASE, "man search", nil, nil },
	{ P_SEQ3, "how", "search|find|read", "man" },
};
static Pat p_tags[] = {
	{ P_WORD, "tag|tags", nil, nil },
	{ P_PHRASE, "middle-click", nil, nil },
	{ P_PHRASE, "middle click", nil, nil },
	{ P_WORD, "chord", nil, nil },
};
static Pat p_look[] = {
	{ P_SEQ, "look|search|find", "acme|window|text", nil },
	{ P_SEQ, "how", "search", nil },
};
static Pat p_windows[] = {
	{ P_SEQ, "window|windows|get|put|undo|del|edit|sam", "acme", nil },
	{ P_SEQ, "acme", "window|windows|file|edit", nil },
};
static Pat p_plumbing[] = {
	{ P_WORD, "plumb|plumber|plumbing", nil, nil },
	{ P_PREFIX, "rout", nil, nil },
};
static Pat p_factotum[] = {
	{ P_WORD, "factotum", nil, nil },
	{ P_SEQ, "key|keys|credential|credentials|secret|secrets",
		"hold|store|confirm|agent", nil },
};
static Pat p_ninep[] = {
	{ P_WORD, "9p|ninep", nil, nil },
	{ P_PHRASE, "file server", nil, nil },
	{ P_WORD, "protocol", nil, nil },
};
static Pat p_namespace[] = {
	{ P_WORD, "namespace", nil, nil },
	{ P_WORD, "mount|mounts", nil, nil },
};
static Pat p_trules[] = {
	{ P_SEQ, "translator", "rule|rules|decide|how|work|draft|drafts", nil },
};
static Pat p_tapprove[] = {
	{ P_SEQ, "approve|reject", "draft|staging|run", nil },
	{ P_SEQ, "draft|staging", "approve|reject", nil },
};
static Pat p_start[] = {
	{ P_SEQ, "start|run|launch|port|ports", "server|servers|desk|translator|9p", nil },
	{ P_SEQ, "how", "start", nil },
};
static Pat p_security[] = {
	{ P_PREFIX, "loopback|auth|secur|expos|forward|port", nil, nil },
};
static Pat p_durability[] = {
	{ P_PREFIX, "backup|restore|durab|crash|surviv|lost", nil, nil },
	{ P_PHRASE, "back up", nil, nil },
};
static Pat p_drafts[] = {
	{ P_SEQ, "show|list|what", "draft|staging|proposed", nil },
	{ P_WORD, "draft|drafts", nil, nil },
};
static Pat p_today[] = {
	{ P_WORD, "today", nil, nil },
	{ P_WORD, "due", nil, nil },
	{ P_SEQ, "what", "due", nil },
};
static Pat p_status[] = {
	{ P_WORD, "status", nil, nil },
	{ P_SEQ, "server|servers|tree|trees", "up|running|answering|alive", nil },
};

static Intent intents[] = {
	{ "help", p_help, 2,
		"I answer fixed questions about the 9P desk — no model involved.\n"
		"Ask me things like:\n"
		"  how do I approve a draft?   what is factotum?   how do tags work?\n"
		"  how does the translator decide?   what is a namespace?\n"
		"Or use the direct forms: man <topic>, status, drafts, today." },
	{ "man-howto", p_manhowto, 4,
		"Man pages are the desk's own documentation — read them first.\n"
		"On a plan9port system: `man <topic>` prints the page; pages live\n"
		"under $PLAN9/man, section 1 for commands and section 4 for file\n"
		"servers. Read NAME, then DESCRIPTION, then stop: most questions are\n"
		"answered in the first two sections. Try: man acme, man plumber,\n"
		"man factotum, or ask me `man <topic>` and I will search for you." },
	{ "acme-tags", p_tags, 4,
		"Acme tags are verbs. Each window has a tag row across the top;\n"
		"middle-click a word to run it. To act on text, select the text first\n"
		"and then middle-click the word — the selection is the argument, the\n"
		"tag word is the verb. Tag words on this desk include Look, Approve,\n"
		"Done, Note, and Fanout. A window's tag is set when the window is\n"
		"created and does not refresh: if a word you expect is missing, close\n"
		"the window and open it fresh rather than hunting for it." },
	{ "acme-look", p_look, 2,
		"Look searches. Middle-click Look with text selected (or with\n"
		"the search text in the tag) and acme jumps to the next match; the\n"
		"right mouse button looks up the word under the cursor, plumbing it\n"
		"to whatever handles that kind of text — a file path opens the file,\n"
		"a thread id opens the thread. If Look finds nothing, check the\n"
		"selection: Look searches for exactly what is selected, nothing more." },
	{ "acme-windows", p_windows, 2,
		"One window per thing: a file, a run directory, a thread, a\n"
		"repo. Get reloads the window from disk, Put writes the body back to\n"
		"the file, Del closes the window, Undo undoes. Edit takes sam\n"
		"commands, so a whole class of edits is one line: for example, an\n"
		"Edit command can change every match in the body at once. Windows\n"
		"are cheap — open one per concern instead of reusing one window for\n"
		"everything." },
	{ "acme-plumbing", p_plumbing, 2,
		"The plumber routes; you do not file things. When acme looks\n"
		"up text, it sends the text to the plumber, whose rules match it by\n"
		"kind — file path, URL, thread id, run id — and open the window it\n"
		"belongs to. On this desk, mail from a contact opens the contact\n"
		"window and an invite becomes a task by routing, not by copying\n"
		"between apps. If a click opens the wrong window, the plumber rule\n"
		"for that kind of text is the place to fix it, not the click." },
	{ "factotum", p_factotum, 2,
		"Factotum holds keys at use. Keys are attribute=value pairs\n"
		"(user, domain, protocol, secret) kept in factotum's memory, never\n"
		"on disk and never shown to any app. It serves files: ctl adds and\n"
		"deletes keys, rpc is a private channel per open, proto lists\n"
		"protocols, needkey prompts you once and caches, confirm gates\n"
		"sensitive uses, log records them. Secstore is the companion: one\n"
		"password unlocks an encrypted vault at login and hands keys to\n"
		"factotum. The rule that matters: approve only a key use you\n"
		"started. A confirmation you did not ask for is declined, always." },
	{ "ninep-basics", p_ninep, 3,
		"9P is the file protocol: everything is a file, served over a\n"
		"connection. A client attaches, walks paths, opens files, and reads\n"
		"and writes them — the same verbs as local files, over the wire.\n"
		"That is why the desk works: cadence, relay, and outpost are trees\n"
		"you walk, not APIs you call. Our servers bind loopback only and\n"
		"v1 has no authentication and no Twstat; the narrow surface is\n"
		"deliberate, not unfinished." },
	{ "namespace", p_namespace, 2,
		"A namespace is what a process sees: its own mount table.\n"
		"The desk's convention mounts cadence at /n/cadence and the comms\n"
		"trees at /n/comms. Namespaces are the access control: a per-client\n"
		"or per-project namespace mounts only that work's trees, and nothing\n"
		"else is visible to it — no per-app permission system to maintain.\n"
		"If a desk sees too much or too little, check what is mounted in\n"
		"it before suspecting the apps." },
	{ "translator-rules", p_trules, 1,
		"The translator only drafts — it never creates a run on its\n"
		"own. Its rules, in order: first-seen thread with at least one\n"
		"message; group threads skipped; threads whose contact emails\n"
		"already exist in cadence skipped (that contact is already in the\n"
		"system); template matched on /onboard/i, else left blank for the\n"
		"approver. Threads with no known email still draft — the approval\n"
		"shelf filters those. Seen-state is per-thread message counts, so a\n"
		"second poll drafts nothing twice, and a crash mid-poll is safe." },
	{ "translator-approve", p_tapprove, 2,
		"A draft is a directory of field files under staging:\n"
		"template, thread, contact, excerpt, created, status, result. To\n"
		"approve, write a playbook id — or empty, to use the draft's\n"
		"template file — to the draft's approve file: the run is created\n"
		"from that template, an event is logged, and status becomes\n"
		"approved with the run id in result. To reject, write anything to\n"
		"reject. v1 note: drafts do not expire on their own, so a stale one\n"
		"is cleared with reject, not by waiting. A rejected draft does not\n"
		"re-draft on the next message; approve it later from the same\n"
		"directory if you change your mind." },
	{ "desk-start", p_start, 2,
		"Start the trees, then the translator:\n"
		"  bun src/ninep-server.ts   # in cadence (9040), relay (9006), outpost (9029)\n"
		"  bun src/translator.ts     # polls comms, writes drafts to staging\n"
		"Relay and outpost serve read-only and need their app databases to\n"
		"already exist, so start those apps first if a database is missing.\n"
		"Cadence creates its data directory on first start. All three bind\n"
		"loopback only; the translator connects to them on 127.0.0.1." },
	{ "security-loopback", p_security, 1,
		"Loopback only, always. The 9P servers bind 127.0.0.1 and v1\n"
		"has no authentication, which is safe exactly because nothing\n"
		"beyond the machine can reach them. Never forward, proxy, or\n"
		"publicly bind a 9P port. If a port answers from another machine,\n"
		"that is a misconfiguration — fix the bind, do not add a password\n"
		"on top of it. Relay and outpost trees are read-only by\n"
		"construction, and cadence's only writable files are step done flags\n"
		"and the staging shelf." },
	{ "durability-backup", p_durability, 2,
		"The desk is durable because it is files first. Backing it\n"
		"up is copying directories, not running an export: cadence's data\n"
		"dir (database plus staging), relay's, outpost's, the translator's\n"
		"state file, and ~/.enclave for at-rest secrets. Do that copy\n"
		"daily, and test a restore by copying a directory back and\n"
		"restarting the servers — the trees rebuild, which is the proof the\n"
		"backup works. Code and config go in git; live data goes in\n"
		"backups; never mixed. If a server dies, restart it: nothing you\n"
		"decided is lost, because decisions live in files and rows, not in\n"
		"the running process." },
	{ "desk-drafts-live", p_drafts, 2, "" },
	{ "desk-today-live", p_today, 3, "" },
	{ "desk-status-live", p_status, 2, "" },
};

static char *
lowerdup(char *s)
{
	char *d = estrdup(s);
	char *p;

	for (p = d; *p; p++)
		*p = tolower((unsigned char)*p);
	return d;
}

static char *
trim(char *s)
{
	char *e;

	while (*s == ' ' || *s == '\t' || *s == '\n')
		s++;
	e = s + strlen(s);
	while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\n'))
		*--e = '\0';
	return s;
}

/*
 * route returns a freshly allocated answer; *intent is set to the
 * intent id, which points at static storage. Live intents return
 * an empty answer: the caller runs the live command.
 */
char *
route(char *question, char **intent)
{
	char *q = lowerdup(trim(question));
	char *p = q;
	int i, j, best = -1, bestscore = 0;

	/* "man <topic>" is a command, not a question */
	while (*p == ' ')
		p++;
	if ((strncmp(p, "man ", 4) == 0 || strncmp(p, "man\t", 4) == 0)) {
		char *topic = trim(p + 4);
		char *ans = mansearch(topic);

		*intent = "man";
		free(q);
		return ans;
	}
	for (i = 0; i < (int)(sizeof intents / sizeof intents[0]); i++) {
		int score = 0;

		for (j = 0; j < intents[i].npats; j++)
			if (matchpat(q, &intents[i].pats[j]))
				score++;
		if (score > bestscore) {
			bestscore = score;
			best = i;
		}
	}
	free(q);
	if (best < 0) {
		*intent = "fallback";
		return estrdup(
			"I do not know that one — I answer fixed questions only.\n"
			"Try: man <topic>, status, drafts, today, or one of these:\n"
			"  how do I approve a draft?   what is factotum?   how do tags work?\n"
			"  how does the translator decide?   what is a namespace?");
	}
	*intent = intents[best].id;
	return estrdup(intents[best].answer);
}
