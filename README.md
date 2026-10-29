# ninep-helper — deterministic 9P desk helper agent

No model, no network search, no guessing. Questions are routed by
keyword scoring over fixed patterns, and answers are fixed text —
except `status`, `drafts`, and `today`, which read the live 9P
trees. The same question always gives the same answer.

It helps you employ three Plan 9 principles:

1. **Man pages search** — find and read the manual for a topic.
2. **Acme usage and integration** — tags, windows, plumbing, and how
   the desk trees fit into acme.
3. **Using translators** — the comms → cadence translator: drafts,
   staging, approve/reject, and its rules.

## Usage

```
bun src/helper.ts <question...>   ask in plain words
bun src/helper.ts man <topic>     search the man pages for a topic
bun src/helper.ts status          which 9P servers are answering
bun src/helper.ts drafts          current staging drafts
bun src/helper.ts today           today's steps from cadence
```

The question form also works from an acme tag: the arguments are
the question, so a tag word can run the helper on selected text.

Example questions:

```
how do tags work in acme
how do I approve a draft
how does the translator decide
what is factotum
what is a namespace
how do I back up the desk
show me the drafts
are the servers up
```

## Man page search

`man <topic>` searches in this order:

1. `$PLAN9/man` — the plan9port man tree, if PLAN9 is set. The NAME
   and DESCRIPTION sections are extracted and troff markup stripped.
2. Bundled helper notes for the core topics (9p, acme, plumber,
   factotum, namespace, translator), clearly labeled as helper notes,
   not man text.
3. Otherwise it says so and lists the topics it knows.

## Live commands

`status`, `drafts`, and `today` speak 9P to the desk servers using
the vendored `src/ninep.ts` (same library, same sha256, as cadence,
relay, outpost, and translator). Addresses come from `CADENCE_NINEP`,
`RELAY_NINEP`, and `OUTPOST_NINEP`, defaulting to 127.0.0.1:9040,
:9006, and :9029. A server that is down is reported, never thrown.

## Tests

`bun test` — 28 tests: routing table (including the approve-vs-drafts
tie-break), man search against a fake $PLAN9/man tree, and the live
commands against stub 9P trees plus a down-server case. No sibling
checkout or running desk is needed.

## C port (Plan 9 style)

`c/` holds the same helper rewritten in Plan 9 style C — `u8int` /
`u32int`, `print` / `fprint` / `sysfatal`, `emalloc`, `dial`, `void
main` with `exits(nil)`, no regex. It includes its own minimal
9P2000 client (`ninepc.c`: version, attach, walk, open, read,
clunk). Build with `make` (gcc, zero warnings); `make test` runs
the C suite (35 checks) against Bun stub trees. See `c/README.md`
for the plan9port notes.
