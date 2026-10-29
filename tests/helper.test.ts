// helper.test.ts — deterministic 9P helper agent tests.
//
// Routing is pure: the same question must always map to the same
// intent. Man search is tested against a fake $PLAN9/man tree, and
// the live commands against stub 9P trees, so `bun test` passes in
// a fresh clone with no sibling checkouts and no real desk running.
import { describe, test, expect, beforeAll, afterAll } from "bun:test";
import { mkdtempSync, mkdirSync, writeFileSync, rmSync } from "node:fs";
import { join } from "node:path";
import { tmpdir } from "node:os";
import { serveNineP, type DirNode, type FileNode, type Node } from "../src/ninep.ts";
import { route, searchMan, deskStatus, listDrafts, readToday } from "../src/helper.ts";

// ---------- routing ----------

const ROUTES: [string, string][] = [
  ["how do tags work in acme", "acme-tags"],
  ["how do I search in acme", "acme-look"],
  ["what does Get do in acme", "acme-windows"],
  ["how does plumbing work", "acme-plumbing"],
  ["what is factotum", "factotum"],
  ["what is 9p", "ninep-basics"],
  ["what is a namespace", "namespace"],
  ["how does the translator decide", "translator-rules"],
  ["how do I approve a draft", "translator-approve"],
  ["how do I start the 9p servers", "desk-start"],
  ["is the desk secure", "security-loopback"],
  ["how do I back up the desk", "durability-backup"],
  ["show me the drafts", "desk-drafts-live"],
  ["what is due today", "desk-today-live"],
  ["are the servers up", "desk-status-live"],
  ["how do I search the man pages", "man-howto"],
  ["man factotum", "man"],
  ["what is the weather", "fallback"],
  ["help", "help"],
];

describe("routing", () => {
  for (const [question, intent] of ROUTES) {
    test(`"${question}" -> ${intent}`, () => {
      expect(route(question).intent).toBe(intent);
    });
  }
  test("same question twice gives the same answer", () => {
    expect(route("what is factotum").answer).toBe(route("what is factotum").answer);
  });
  test("approve-a-draft beats show-drafts on a tie", () => {
    // Both intents match "draft" in "how do I approve a draft"; the
    // approve intent is listed first, so it wins the tie.
    expect(route("how do I approve a draft").intent).toBe("translator-approve");
    expect(route("show me the drafts").intent).toBe("desk-drafts-live");
  });
});

// ---------- man search ----------

describe("man search", () => {
  let plan9: string;
  beforeAll(() => {
    plan9 = mkdtempSync(join(tmpdir(), "ninep-helper-man-"));
    mkdirSync(join(plan9, "man", "man1"), { recursive: true });
    writeFileSync(
      join(plan9, "man", "man1", "acme.1"),
      `.TH ACME 1\n.SH NAME\nacme \\- window system\n.SH DESCRIPTION\nAcme is a text editor and window system.\n.PP\nSecond paragraph.\n`
    );
  });
  afterAll(() => rmSync(plan9, { recursive: true, force: true }));

  test("finds a page in $PLAN9/man and strips troff", () => {
    const r = searchMan("acme", plan9);
    expect(r.found).toBe(true);
    expect(r.text).toContain("window system");
    expect(r.text).toContain("text editor");
    expect(r.text).not.toContain(".TH");
    expect(r.text).not.toContain(".SH");
  });
  test("falls back to bundled notes for known topics", () => {
    const r = searchMan("factotum", plan9);
    expect(r.found).toBe(true);
    expect(r.text).toContain("Helper note");
    expect(r.text).toContain("factotum");
  });
  test("unknown topic reports no page and no note", () => {
    const r = searchMan("nosuchtopic", plan9);
    expect(r.found).toBe(false);
    expect(r.text).toContain("No man page");
  });
});

// ---------- live commands against stub trees ----------

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

describe("live commands", () => {
  const servers: { stop(): void }[] = [];
  const saved: Record<string, string | undefined> = {};
  beforeAll(() => {
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
    const relay = dirNode("", "/", {
      threads: dirNode("threads", "/threads", {}),
    });
    const outpost = dirNode("", "/", {
      inbox: dirNode("inbox", "/inbox", {}),
    });
    const c = serveNineP(cadence, { port: 0 });
    const r = serveNineP(relay, { port: 0 });
    const o = serveNineP(outpost, { port: 0 });
    servers.push(c, r, o);
    for (const k of ["CADENCE_NINEP", "RELAY_NINEP", "OUTPOST_NINEP"]) saved[k] = process.env[k];
    process.env.CADENCE_NINEP = `127.0.0.1:${c.port}`;
    process.env.RELAY_NINEP = `127.0.0.1:${r.port}`;
    process.env.OUTPOST_NINEP = `127.0.0.1:${o.port}`;
  });
  afterAll(() => {
    for (const s of servers) s.stop();
    for (const [k, v] of Object.entries(saved)) {
      if (v === undefined) delete process.env[k];
      else process.env[k] = v;
    }
  });

  test("status reports all three stub servers up", async () => {
    const s = await deskStatus();
    expect(s).toContain("cadence: up");
    expect(s).toContain("relay: up");
    expect(s).toContain("outpost: up");
  });
  test("drafts lists the stub draft with its fields", async () => {
    const d = await listDrafts();
    expect(d).toContain("relay-t1");
    expect(d).toContain("New Person");
    expect(d).toContain("onboard");
  });
  test("today reads the stub today file", async () => {
    expect(await readToday()).toContain("Call New Person");
  });
  test("down server is reported, not thrown", async () => {
    const dead = serveNineP(dirNode("", "/", {}), { port: 0 });
    const port = dead.port;
    dead.stop();
    process.env.OUTPOST_NINEP = `127.0.0.1:${port}`;
    const s = await deskStatus();
    expect(s).toContain("outpost: not answering");
  });
});
