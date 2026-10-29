// stubs.ts — stub 9P trees for the C helper's live tests.
//
// Serves the same small trees the Bun suite uses: a cadence tree
// with a today file and one staging draft, plus bare relay and
// outpost roots. Writes CADENCE_NINEP / RELAY_NINEP / OUTPOST_NINEP
// lines (host:port) to the ports file given as argv[2], then waits
// until killed. Run via tests/run.sh, not by hand.
import { writeFileSync } from "node:fs";
import { serveNineP, type DirNode, type FileNode, type Node } from "../../src/ninep.ts";

function textFile(name: string, path: string, value: string): FileNode {
  return {
    kind: "file", name, path,
    len: () => new TextEncoder().encode(value).length,
    read: (off: number, count: number) => new TextEncoder().encode(value).slice(off, off + count),
    mtime: () => Date.now(),
  };
}

function dirNode(name: string, path: string, kids: Record<string, Node>): DirNode {
  return {
    kind: "dir", name, path,
    list: () => Object.values(kids).map((k) => ({ name: k.name, isDir: k.kind === "dir" })),
    get: (n: string) => kids[n] || null,
    mtime: () => Date.now(),
  };
}

const draft = dirNode("relay-t1", "/staging/relay-t1", {
  template: textFile("template", "/staging/relay-t1/template", "onboard"),
  thread: textFile("thread", "/staging/relay-t1/thread", "t1"),
  contact: textFile("contact", "/staging/relay-t1/contact", "New Person"),
  excerpt: textFile("excerpt", "/staging/relay-t1/excerpt", "we want to start"),
  status: textFile("status", "/staging/relay-t1/status", "proposed"),
});
const cadence = dirNode("", "/", {
  today: textFile("today", "/today", "1. Call New Person\n"),
  staging: dirNode("staging", "/staging", { "relay-t1": draft }),
});
const relay = dirNode("", "/", { threads: dirNode("threads", "/threads", {}) });
const outpost = dirNode("", "/", { inbox: dirNode("inbox", "/inbox", {}) });

const c = serveNineP(cadence, { port: 0 });
const r = serveNineP(relay, { port: 0 });
const o = serveNineP(outpost, { port: 0 });
writeFileSync(process.argv[2],
  `CADENCE_NINEP=127.0.0.1:${c.port}\nRELAY_NINEP=127.0.0.1:${r.port}\nOUTPOST_NINEP=127.0.0.1:${o.port}\n`);
await new Promise(() => {});
