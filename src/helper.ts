// helper.ts — deterministic 9P desk helper agent.
//
// No model, no network search, no guessing: questions are routed by
// keyword scoring over fixed patterns, and answers are fixed text —
// except status/drafts/today, which read the live 9P trees. The same
// question always gives the same answer.
//
// Three things it helps you employ, per its brief:
//   1. Man pages search — find and read the manual for a topic.
//   2. Acme usage and integration — tags, windows, plumbing, and how
//      the desk trees fit into acme.
//   3. Using translators — the comms -> cadence translator: drafts,
//      staging, approve/reject, and its rules.
//
// Usage:
//   bun src/helper.ts <question...>   ask in plain words
//   bun src/helper.ts man <topic>     search the man pages for a topic
//   bun src/helper.ts status          which 9P servers are answering
//   bun src/helper.ts drafts          current staging drafts
//   bun src/helper.ts today           today's steps from cadence
// The question form also works from an acme tag: the arguments are
// the question, so a tag word can run the helper on selected text.

import { existsSync, readdirSync, readFileSync } from "node:fs";
import { join } from "node:path";
import { NinePClient, parseNinePAddr } from "./ninep.ts";

export interface RouteResult {
  intent: string;
  answer: string;
}

interface Intent {
  id: string;
  patterns: RegExp[];
  answer: string;
}

const INTENTS: Intent[] = [
  {
    id: "help",
    patterns: [/\b(help|usage|what can you do|commands?)\b/],
    answer: `I answer fixed questions about the 9P desk — no model involved.
Ask me things like:
  how do I approve a draft?   what is factotum?   how do tags work?
  how does the translator decide?   what is a namespace?
Or use the direct forms: man <topic>, status, drafts, today.`,
  },
  {
    id: "man-howto",
    patterns: [/\b(man page|manual|how .* (search|find|read).* man|man search)\b/],
    answer: `Man pages are the desk's own documentation — read them first.
On a plan9port system: \`man <topic>\` prints the page; pages live
under $PLAN9/man, section 1 for commands and section 4 for file
servers. Read NAME, then DESCRIPTION, then stop: most questions are
answered in the first two sections. Try: man acme, man plumber,
man factotum, or ask me \`man <topic>\` and I will search for you.`,
  },
  {
    id: "acme-tags",
    patterns: [/\btags?\b/, /\bmiddle[- ]?click\b/, /\bchord\b/],
    answer: `Acme tags are verbs. Each window has a tag row across the top;
middle-click a word to run it. To act on text, select the text first
and then middle-click the word — the selection is the argument, the
tag word is the verb. Tag words on this desk include Look, Approve,
Done, Note, and Fanout. A window's tag is set when the window is
created and does not refresh: if a word you expect is missing, close
the window and open it fresh rather than hunting for it.`,
  },
  {
    id: "acme-look",
    patterns: [/\b(look|search|find).*(acme|window|text)\b/, /\bhow .* search\b/],
    answer: `Look searches. Middle-click Look with text selected (or with
the search text in the tag) and acme jumps to the next match; the
right mouse button looks up the word under the cursor, plumbing it
to whatever handles that kind of text — a file path opens the file,
a thread id opens the thread. If Look finds nothing, check the
selection: Look searches for exactly what is selected, nothing more.`,
  },
  {
    id: "acme-windows",
    patterns: [/\b(windows?|get|put|undo|del|edit|sam)\b.*\bacme\b/, /\bacme\b.*\b(windows?|file|edit)\b/],
    answer: `One window per thing: a file, a run directory, a thread, a
repo. Get reloads the window from disk, Put writes the body back to
the file, Del closes the window, Undo undoes. Edit takes sam
commands, so a whole class of edits is one line: for example, an
Edit command can change every match in the body at once. Windows
are cheap — open one per concern instead of reusing one window for
everything.`,
  },
  {
    id: "acme-plumbing",
    patterns: [/\b(plumb|plumber|plumbing|rout\w*)\b/],
    answer: `The plumber routes; you do not file things. When acme looks
up text, it sends the text to the plumber, whose rules match it by
kind — file path, URL, thread id, run id — and open the window it
belongs to. On this desk, mail from a contact opens the contact
window and an invite becomes a task by routing, not by copying
between apps. If a click opens the wrong window, the plumber rule
for that kind of text is the place to fix it, not the click.`,
  },
  {
    id: "factotum",
    patterns: [/\bfactotum\b/, /\b(key|credential|secret)s?\b.*\b(hold|store|confirm|agent)\b/],
    answer: `Factotum holds keys at use. Keys are attribute=value pairs
(user, domain, protocol, secret) kept in factotum's memory, never
on disk and never shown to any app. It serves files: ctl adds and
deletes keys, rpc is a private channel per open, proto lists
protocols, needkey prompts you once and caches, confirm gates
sensitive uses, log records them. Secstore is the companion: one
password unlocks an encrypted vault at login and hands keys to
factotum. The rule that matters: approve only a key use you
started. A confirmation you did not ask for is declined, always.`,
  },
  {
    id: "ninep-basics",
    patterns: [/\b(9p|ninep|file server|protocol)\b/],
    answer: `9P is the file protocol: everything is a file, served over a
connection. A client attaches, walks paths, opens files, and reads
and writes them — the same verbs as local files, over the wire.
That is why the desk works: cadence, relay, and outpost are trees
you walk, not APIs you call. Our servers bind loopback only and
v1 has no authentication and no Twstat; the narrow surface is
deliberate, not unfinished.`,
  },
  {
    id: "namespace",
    patterns: [/\bnamespace\b/, /\bmounts?\b/],
    answer: `A namespace is what a process sees: its own mount table.
The desk's convention mounts cadence at /n/cadence and the comms
trees at /n/comms. Namespaces are the access control: a per-client
or per-project namespace mounts only that work's trees, and nothing
else is visible to it — no per-app permission system to maintain.
If a desk sees too much or too little, check what is mounted in
it before suspecting the apps.`,
  },
  {
    id: "translator-rules",
    patterns: [/\btranslator\b.*\b(rule|decide|how|work|drafts?)\b/],
    answer: `The translator only drafts — it never creates a run on its
own. Its rules, in order: first-seen thread with at least one
message; group threads skipped; threads whose contact emails
already exist in cadence skipped (that contact is already in the
system); template matched on /onboard/i, else left blank for the
approver. Threads with no known email still draft — the approval
shelf filters those. Seen-state is per-thread message counts, so a
second poll drafts nothing twice, and a crash mid-poll is safe.`,
  },
  {
    id: "translator-approve",
    patterns: [/\b(approve|reject)\b.*\b(draft|staging|run)\b/, /\b(draft|staging)\b.*\b(approve|reject)\b/],
    answer: `A draft is a directory of field files under staging:
template, thread, contact, excerpt, created, status, result. To
approve, write a playbook id — or empty, to use the draft's
template file — to the draft's approve file: the run is created
from that template, an event is logged, and status becomes
approved with the run id in result. To reject, write anything to
reject. v1 note: drafts do not expire on their own, so a stale one
is cleared with reject, not by waiting. A rejected draft does not
re-draft on the next message; approve it later from the same
directory if you change your mind.`,
  },
  {
    id: "desk-start",
    patterns: [/\b(start|run|launch|ports?)\b.*\b(servers?|desk|translator|9p)\b/, /\bhow .* start\b/],
    answer: `Start the trees, then the translator:
  bun src/ninep-server.ts   # in cadence (9040), relay (9006), outpost (9029)
  bun src/translator.ts     # polls comms, writes drafts to staging
Relay and outpost serve read-only and need their app databases to
already exist, so start those apps first if a database is missing.
Cadence creates its data directory on first start. All three bind
loopback only; the translator connects to them on 127.0.0.1.`,
  },
  {
    id: "security-loopback",
    patterns: [/\b(loopback|auth|secur|expos|forward|port)/],
    answer: `Loopback only, always. The 9P servers bind 127.0.0.1 and v1
has no authentication, which is safe exactly because nothing
beyond the machine can reach them. Never forward, proxy, or
publicly bind a 9P port. If a port answers from another machine,
that is a misconfiguration — fix the bind, do not add a password
on top of it. Relay and outpost trees are read-only by
construction, and cadence's only writable files are step done flags
and the staging shelf.`,
  },
  {
    id: "durability-backup",
    patterns: [/\b(backups?|back\s+up|restore|durab|crash|surviv|lost)/],
    answer: `The desk is durable because it is files first. Backing it
up is copying directories, not running an export: cadence's data
dir (database plus staging), relay's, outpost's, the translator's
state file, and ~/.enclave for at-rest secrets. Do that copy
daily, and test a restore by copying a directory back and
restarting the servers — the trees rebuild, which is the proof the
backup works. Code and config go in git; live data goes in
backups; never mixed. If a server dies, restart it: nothing you
decided is lost, because decisions live in files and rows, not in
the running process.`,
  },
  {
    id: "desk-drafts-live",
    patterns: [/\b(show|list|what).*\b(draft|staging|proposed)\b/, /\bdrafts?\b/],
    answer: "",
  },
  {
    id: "desk-today-live",
    patterns: [/\btoday\b/, /\b(due|due today|what.*due)\b/],
    answer: "",
  },
  {
    id: "desk-status-live",
    patterns: [/\bstatus\b/, /\b(servers?|trees?).*\b(up|running|answering|alive)\b/],
    answer: "",
  },
];

export function route(question: string): RouteResult {
  const q = question.trim();
  const manMatch = q.match(/^man\s+(.+)$/i);
  if (manMatch) {
    const topic = manMatch[1].trim();
    return { intent: "man", answer: searchMan(topic).text };
  }
  let best: Intent | null = null;
  let bestScore = 0;
  for (const intent of INTENTS) {
    let score = 0;
    for (const p of intent.patterns) if (p.test(q.toLowerCase())) score++;
    if (score > bestScore) {
      bestScore = score;
      best = intent;
    }
  }
  if (!best || bestScore === 0) {
    return {
      intent: "fallback",
      answer: `I do not know that one — I answer fixed questions only.
Try: man <topic>, status, drafts, today, or one of these:
  how do I approve a draft?   what is factotum?   how do tags work?
  how does the translator decide?   what is a namespace?`,
    };
  }
  return { intent: best.id, answer: best.answer };
}

// ---------- man page search ----------

function stripTroff(text: string): string {
  const lines: string[] = [];
  for (const line of text.split("\n")) {
    if (line.startsWith(".")) continue; // troff requests (.TH, .SH, .PP, ...)
    lines.push(line);
  }
  return lines
    .join("\n")
    .replace(/\\f\[[A-Za-z]+\]/g, "")
    .replace(/\\f[A-Z]/g, "")
    .replace(/\\-/g, "-")
    .replace(/\\&/g, "")
    .replace(/\\\\/g, "\\")
    .replace(/[ \t]+/g, " ")
    .replace(/\n{3,}/g, "\n\n")
    .trim();
}

function extractSection(text: string, title: string): string | null {
  const lines = text.split("\n");
  let start = -1;
  for (let i = 0; i < lines.length; i++) {
    if (new RegExp(`^\\.SH\\s+${title}\\s*$`).test(lines[i])) { start = i + 1; break; }
  }
  if (start < 0) return null;
  const out: string[] = [];
  for (let i = start; i < lines.length; i++) {
    if (lines[i].startsWith(".SH")) break;
    out.push(lines[i]);
  }
  const cleaned = stripTroff(out.join("\n"));
  return cleaned || null;
}

const BUNDLED_NOTES: Record<string, string> = {
  "9p": `Helper note (not man text): 9P is the file protocol — attach,
walk, open, read, write. Everything is a file, served over a
connection. Our desk servers bind loopback only, v1 has no
authentication and no Twstat.`,
  acme: `Helper note (not man text): acme is the window system where
each window is a file with a tag row of verbs. Middle-click a tag
word to run it; selection is the argument. Get reloads, Put writes,
Del closes, Edit takes sam commands. The plumber routes looked-up
text to the window it belongs to.`,
  plumber: `Helper note (not man text): the plumber routes looked-up
text by kind — file path, URL, thread id, run id — to the window
or app that handles it. Rules match patterns; fix a wrong routing
in the rule, not in the click.`,
  factotum: `Helper note (not man text): factotum holds keys at use in
memory (attribute=value pairs), served as files: ctl, rpc, proto,
needkey, confirm, log. Secstore unlocks with one password at login
and hands keys to factotum. Approve only a key use you started.`,
  namespace: `Helper note (not man text): a namespace is a process's
mount table. The desk mounts cadence at /n/cadence and comms at
/n/comms; scoping is by mount, not by per-app permissions.`,
  translator: `Helper note (not man text): the translator drafts runs
from comms threads into cadence staging. First-seen threads only;
groups and known contacts skipped; template matched on /onboard/i.
Approve by writing a playbook id (or empty) to approve; reject by
writing to reject. Drafts do not expire in v1.`,
};

export function searchMan(topic: string, plan9dir?: string): { found: boolean; text: string } {
  const dir = plan9dir ?? process.env.PLAN9;
  if (dir) {
    const manRoot = join(dir, "man");
    if (existsSync(manRoot)) {
      for (const section of readdirSync(manRoot)) {
        if (!section.startsWith("man")) continue;
        const sectionDir = join(manRoot, section);
        let files: string[] = [];
        try { files = readdirSync(sectionDir); } catch { continue; }
        for (const f of files) {
          if (f !== topic && !f.startsWith(topic + ".")) continue;
          const raw = readFileSync(join(sectionDir, f), "utf8");
          const name = extractSection(raw, "NAME");
          const desc = extractSection(raw, "DESCRIPTION");
          const body = [name, desc].filter(Boolean).join("\n\n");
          if (body) return { found: true, text: `${topic}(${section.replace("man", "")}) — from $PLAN9/man:\n${body}` };
        }
      }
    }
  }
  const note = BUNDLED_NOTES[topic.toLowerCase()];
  if (note) return { found: true, text: note };
  return {
    found: false,
    text: `No man page or helper note for "${topic}". Try: 9p, acme, plumber, factotum, namespace, translator — or on a plan9port system, set PLAN9 and run: man ${topic}`,
  };
}

// ---------- live 9P commands ----------

function addr(env: string, fallback: string): string {
  return process.env[env] || fallback;
}

async function tryClient(a: string): Promise<NinePClient | null> {
  const { host, port } = parseNinePAddr(a);
  const c = new NinePClient();
  try {
    await c.connect(host, port);
    return c;
  } catch {
    return null;
  }
}

export async function deskStatus(): Promise<string> {
  const servers: [string, string][] = [
    ["cadence", addr("CADENCE_NINEP", "127.0.0.1:9040")],
    ["relay", addr("RELAY_NINEP", "127.0.0.1:9006")],
    ["outpost", addr("OUTPOST_NINEP", "127.0.0.1:9029")],
  ];
  const lines: string[] = [];
  for (const [name, a] of servers) {
    const c = await tryClient(a);
    if (!c) {
      lines.push(`${name}: not answering on ${a}`);
      continue;
    }
    try {
      const entries = await c.listDir("/");
      lines.push(`${name}: up on ${a} (${entries.length} top-level entries)`);
    } catch (e: any) {
      lines.push(`${name}: error on ${a}: ${e.message}`);
    } finally {
      c.close();
    }
  }
  return lines.join("\n");
}

export async function listDrafts(): Promise<string> {
  const a = addr("CADENCE_NINEP", "127.0.0.1:9040");
  const c = await tryClient(a);
  if (!c) return `cadence 9P not answering on ${a} — start it first.`;
  try {
    const drafts = (await c.listDir("/staging")).filter((e) => e.isDir);
    if (!drafts.length) return "No drafts in staging.";
    const lines: string[] = [];
    for (const d of drafts) {
      const read = async (f: string) => {
        try { return (await c.readFile(`/staging/${d.name}/${f}`)).trim(); } catch { return ""; }
      };
      lines.push(`${d.name}: [${await read("status") || "proposed"}] ${await read("contact")} — ${await read("thread")} (template: ${await read("template") || "(blank)"})`);
    }
    return lines.join("\n");
  } finally {
    c.close();
  }
}

export async function readToday(): Promise<string> {
  const a = addr("CADENCE_NINEP", "127.0.0.1:9040");
  const c = await tryClient(a);
  if (!c) return `cadence 9P not answering on ${a} — start it first.`;
  try {
    return await c.readFile("/today");
  } catch (e: any) {
    return `could not read today: ${e.message}`;
  } finally {
    c.close();
  }
}

// ---------- CLI ----------

const USAGE = `ninep-helper — deterministic 9P desk helper (no model).
  bun src/helper.ts <question...>   ask in plain words
  bun src/helper.ts man <topic>     search the man pages
  bun src/helper.ts status          which 9P servers are answering
  bun src/helper.ts drafts          current staging drafts
  bun src/helper.ts today           today's steps from cadence`;

if (import.meta.main) {
  const args = process.argv.slice(2);
  const cmd = args[0];
  if (!args.length) {
    console.log(USAGE);
  } else if (cmd === "man" && args[1]) {
    console.log(searchMan(args.slice(1).join(" ")).text);
  } else if (cmd === "status") {
    console.log(await deskStatus());
  } else if (cmd === "drafts") {
    console.log(await listDrafts());
  } else if (cmd === "today") {
    console.log(await readToday());
  } else {
    const question = (cmd === "ask" ? args.slice(1) : args).join(" ");
    const r = route(question);
    if (r.intent === "desk-drafts-live") console.log(await listDrafts());
    else if (r.intent === "desk-today-live") console.log(await readToday());
    else if (r.intent === "desk-status-live") console.log(await deskStatus());
    else console.log(r.answer);
  }
}
