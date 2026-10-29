# ninep-helper in C — Plan 9 style

The same deterministic helper as `../src/helper.ts`, rewritten in
Plan 9 style C: `u8int`/`u32int` types, `print`/`fprint`/`sysfatal`,
`emalloc`, `dial`, `void main` with `exits(nil)`, short lowercase
names, `/* */` comments, no regex (matching is whole words, word
prefixes, phrases, and ordered word sequences).

## Layout

```
compat.h/c   Plan 9 surface over Linux libc (see below)
ninepc.h/c   minimal 9P2000 client: version, attach, walk, open,
             read, clunk — no auth, no writes, no stat
route.h/c    intent table + matcher + knowledge base
man.h/c      man page search ($PLAN9/man, then bundled notes)
helper.c     the command: questions + status/drafts/today
test.c       test main (routing, man search, live commands)
tests/       Bun stub 9P trees + run.sh harness
```

## Build (Linux, verified here)

```
make          # gcc, zero warnings
make test     # starts Bun stub trees, runs helper_test: 35 checks
./helper how do tags work
./helper man factotum
./helper status
```

Live commands speak 9P to the desk servers via `ninepc.c`, using
`CADENCE_NINEP`, `RELAY_NINEP`, `OUTPOST_NINEP` (defaults
127.0.0.1:9040, :9006, :9029). The test harness proves the client
against the same `ninep.ts` server the real desk servers run.

## Plan 9 / plan9port notes

Verified 2026-10-29 against plan9port at `~/workspace/plan9port`
(system gcc, `-I$PLAN9/include`, link `$PLAN9/lib/lib9.a`,
`-Wno-main`): the helper builds with zero warnings, and the
resulting binary matches the Linux binary exactly on routing
answers, man search against a real `$PLAN9/man` tree (including
the unknown-topic miss), and the live `today` / `drafts` /
`status` commands plus the down-server case against the Bun
stub trees.

`compat.h`/`compat.c` exist only for the Linux build. On
plan9port, drop them and include `<u.h>` and `<libc.h>` instead.
Real there, with the shapes used here: `print`, `fprint`,
`sysfatal`, `exits`, `dial` (mapped to `p9dial`), `open`
(`p9open`), `close` (`p9close`), `getenv` (`p9getenv`), `dirread`,
`Dir`, `nil`, `OREAD`, `read`/`write`, `strtok`, `strdup`
(`p9strdup`), `snprint`, and the `u8int`/`u16int`/`u32int`/`u64int`
types. Two names are NOT declared and need small local
definitions: `emalloc` and `estrdup` (only `emalloc9p` exists, in
`9p.h`, and caller-provided `emalloc` in `acme.h`/`diskfs.h`).
Pair them with plan9port's allocator: `emalloc` on `p9malloc`,
since `free` maps to `p9free` — do not mix plain `malloc` with
`p9free`.

The remaining source deltas from the Linux tree, all verified in
the scratch build:

- `ninepc.c`: drop the system includes and the `errno`/`EINTR`
  retries (plan9port has no `errno`); rename the file's static
  `readn` to `readfull` (`libc.h` already declares `readn`, with
  a different signature); drop the local `DMDIR` enum member
  (`libc.h` `#define`s it to the same value).
- `route.c`: no `<ctype.h>` in plan9port (`tolower` is commented
  out of `libc.h`) — an ASCII-only `tolower` macro is
  behavior-identical here; `strtok_r` does not exist, plain
  `strtok` is safe (no nested tokenization).
- `man.c`: no `<dirent.h>` or `<stdio.h>` — the walk becomes
  `open` + `dirread` (`dirread` skips `.` and `..` itself,
  returns up to 10 entries per call, each batch freed with
  `free`), and `readall` becomes `open` + `read` with a growing
  buffer (`p9realloc`). `snprintf` maps to `snprint`, which
  handles the formats used here, including `%.*s` (proven by the
  live test, where the server address is built that way).
- `test.c` is NOT ported: it needs `mkdtemp`/`mkdir`/`fopen`/
  `setenv` and BSD sockets, none of which plan9port's libc
  provides. Its down-server trick (bind a port, close it) has a
  native equivalent in `announce` + `close`. The helper binary
  itself is verified by direct comparison instead, as above.
