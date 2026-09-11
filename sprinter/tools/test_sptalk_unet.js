#!/usr/bin/env node
// test_sptalk_unet.js — end-to-end check that SPTALK.EXE (NET_BACKEND=unet)
// actually brings a UNET DLL up, not merely that it links.
//
// This exists because tools/test_netdll_win0.js provably cannot catch the
// class of bug it was added for. NETDLL.EXE is a different program in two
// ways that matter:
//   - its whole payload fits in WIN0 (win0_exe.py reports win1=0), so it
//     never exercises a DLL swap against a WIN1 half full of program code,
//     which is SPTALK's normal state;
//   - it stops after LOAD/GETCAPS and never calls unetld_require(), so a
//     broken capability gate looked healthy there while net_init() rejected
//     every DLL in the real client.
// So this test drives the shipping binary through the same harness: SELECT,
// LOAD, the capability gate, NETSTART against the modelled RTL8019, and a
// clean double-ESC exit.
//
// Usage: node tools/test_sptalk_unet.js   (after `make NET_BACKEND=unet`)
'use strict';

const assert = require('assert');
const fs = require('fs');
const path = require('path');

const HARNESS = process.env.EXE_HARNESS
  || '/Users/dmitry/dev/zx/sprinter/sprinter-rtl8019a/tools/exe-harness/harness.js';
const { runExe } = require(HARNESS);

const root = path.resolve(__dirname, '..');
const exePath = path.join(root, 'SPTALK.EXE');
const dllPath = process.env.UNET_TEST_DLL
  || '/Users/dmitry/dev/zx/sprinter/sources/unet_libs/unet_libs_core/dll/UNETRTL.DLL';

if (!fs.existsSync(exePath)) {
  console.error(`missing ${exePath} -- run 'make NET_BACKEND=unet' first`);
  process.exit(1);
}
const dllBytes = fs.readFileSync(dllPath);

// The env block is what IFUP/NETCFG -i publish after a successful RTL
// bring-up (src/apps/ifup.asm's PUBLISH_NET_MARKER and friends); UNETRTL's
// NETINIT refuses to start without them. NET_RTL_HW names slot 1, which is
// where the harness's card model sits by default.
const scenario = {
  environment: {
    NET: 'RTL',
    NET_IP: '192.168.1.50', NET_MASK: '255.255.255.0', NET_GW: '192.168.1.1',
    NET_MAC: '00:11:22:33:44:55', NET_DNS1: '192.168.1.1',
    NET_RTL_HW: '1/#300',
  },
  responders: { arp: {}, tcp: {} },
  // The DLL sits next to the EXE and nowhere else, so a successful load also
  // re-proves the P0:0x0100 app-directory path (see test_netdll_win0.js).
  currentDir: 'C:\\OTHER',
  pspPath: 'C:\\NET\\SPTALK.EXE',
  files: { 'C:\\NET\\UNETRTL.DLL': dllBytes },
  // Real DSS hands out block ids, not physical pages; keep them in separate
  // number spaces so any raw OUT of a block id fails loudly.
  distinctBlockIds: true,
  keys: ['/server 192.168.1.10 6667', 'enter', 'escape', 'escape'], keyAtScan: 3,
  // Space the keys out. SPTALK drains its whole keyboard buffer per main-loop
  // pass (main.c), so without an interval the entire script -- including the
  // two quit presses -- lands in one iteration and the client exits before it
  // has even connected. Two polls is the minimum that still gives each key its
  // own pass; runs that need the server to get a word in raise it themselves.
  keyIntervalScans: 2,
  traceDss: true,
  stepLimit: 200_000_000,
};

const run = (extra) => {
  try {
    return runExe(exePath, '', { ...scenario, ...extra });
  } catch (error) {
    if (error.partialOutput) console.error(`--- output before the failure ---\n${error.partialOutput}`);
    throw error;
  }
};

const r = run({ responders: { arp: {}, tcp: {} } });

const out = r.output;
if (process.env.TRACE === '1') console.error(out);

assert.ok(out.includes('SprinTalk 0.1 UNET'),
  'SPTALK.EXE is not the unet build -- rebuild with `make NET_BACKEND=unet`');
assert.ok(out.includes('UNET: NET=RTL -> UNETRTL.DLL'),
  `SELECT did not resolve NET=RTL to UNETRTL.DLL -- got:\n${out}`);
assert.ok(!out.includes('Load failed:'),
  `net_init() rejected the DLL -- got:\n${out}`);
assert.ok(out.includes('Network ready via UNETRTL.DLL'),
  `net_init() did not reach NET_OK -- got:\n${out}`);
// The banner carries the DLL's own name field, so the log of a field run says
// which backend revision was actually loaded.
assert.match(out, /Network ready via UNETRTL\.DLL \(UNETRTL v\d+\.\d+\.\d+\)/,
  `the loaded DLL's own revision is missing from the banner -- got:\n${out}`);

assert.strictEqual(r.exitCode, 0, 'SPTALK did not exit cleanly on double-ESC');
assert.ok(r.cleanup.isaClosed, 'the ISA window was left open at exit');
assert.ok(r.cleanup.filesClosed, 'a DSS file handle leaked (unet_load did not close the DLL)');

// The DLL's own page is the one block this program allocates and must free
// itself; the win0 loader's P0/P1/P2 are released by DSS.Exit (see
// test_netdll_win0.js's note on the same split).
// unet_load GETMEMs the page and only then opens the file, so the last
// GETMEM before the OPEN is ours. (Anything after it belongs to the DLL's
// own INIT, which allocates a page of its own -- don't confuse the two.)
const freemems = r.dssEvents.filter((e) => e.startsWith('FREEMEM'));
const openAt = r.dssEvents.findIndex((e) => e.startsWith('OPEN') && e.includes('UNETRTL.DLL'));
assert.ok(openAt > 0, `the DLL was never opened -- events:\n${r.dssEvents.join('\n')}`);
const dllGetmem = r.dssEvents.slice(0, openAt).filter((e) => e.startsWith('GETMEM')).pop();
assert.ok(dllGetmem, `no GETMEM for the DLL page -- events:\n${r.dssEvents.slice(0, openAt).join('\n')}`);
const dllBlock = dllGetmem.match(/block (\d+)/)[1];
assert.ok(freemems.includes(`FREEMEM ${dllBlock}`),
  `the DLL's block ${dllBlock} was never freed -- got:\n${freemems.join('\n')}`);

// The /server line above really opens a socket: NICK and USER must reach the
// wire, which covers unet_connect/unet_send and the WIN1 swap under a WIN1 that
// is half full of SPTALK's own code.
const payloads = r.transmittedFrames
  .map((f) => Buffer.from(f, 'hex').slice(54).toString('latin1'));
assert.ok(payloads.some((p) => p.startsWith('NICK ')), 'NICK never reached the wire');
assert.ok(payloads.some((p) => p.startsWith('USER ')), 'USER never reached the wire');
assert.ok(!r.output.includes('connect failed'), `/server failed -- got:\n${r.output}`);

// A talkative peer. Every IRC server pushes a NOTICE AUTH banner the instant
// the handshake closes and keeps talking while the client is still sending its
// NICK/USER, so SPTALK's very first SEND waits for its ACK with unsolicited
// peer data -- carrying a LOWER ack number -- arriving first. A
// request/response peer never produces that, which is why the plain run above
// stayed green against a DLL that could not handle it: UNETRTL 0.3.0 answered
// NERR_SEND ("no ACK from peer") and USER never reached the wire at all.
// Keep this scenario: it is the only thing standing between a stale
// UNET_DLL_DIR and a client that cannot register with any real server.
const chatty = run({
  responders: {
    arp: {},
    tcp: {
      greeting: [
        ':irc.example.net NOTICE AUTH :*** Looking up your hostname...\r\n',
        ':irc.example.net NOTICE AUTH :*** Checking Ident\r\n',
        ':irc.example.net NOTICE AUTH :*** No Ident response\r\n',
      ],
      greetingMs: 50, afterMs: 40,
    },
  },
  keyIntervalScans: 400,   // leave room between ENTER and the quit for the banner
});
const chattyPayloads = chatty.transmittedFrames
  .map((f) => Buffer.from(f, 'hex').slice(54).toString('latin1'));
assert.ok(!chatty.output.includes('send failed'),
  `registration failed against a server that greets first -- got:\n${chatty.output}`);
assert.strictEqual(chattyPayloads.filter((p) => p.startsWith('NICK ')).length, 1,
  'NICK was retransmitted: the backend mis-handled peer data arriving during its ACK wait');
assert.ok(chattyPayloads.some((p) => p.startsWith('USER ')),
  'USER never reached the wire -- the NICK send stalled and took the rest of the burst with it');
assert.ok(chatty.output.includes('Looking up your hostname'),
  'the greeting never reached the chat window');

// Dead link: the server completes the handshake and then acknowledges nothing.
// SPTALK must give up quickly, name the backend status, and still exit at once
// instead of pushing a QUIT down the same ladder. The bound is on *modelled*
// milliseconds, since that is what "the UI froze for minutes" actually means.
const dead = run({ responders: { arp: {}, tcp: { dropDataAck: true } } });
assert.ok(dead.output.includes('WARNING: send failed: no ACK from peer'),
  `a wedged link did not report its status -- got:\n${dead.output}`);
assert.strictEqual(dead.exitCode, 0, 'SPTALK did not exit cleanly off a wedged link');
assert.ok(dead.logicalMs < 15000,
  `giving up on a wedged link took ${dead.logicalMs} modelled ms (budget 15000)`);

// Idle cost of one main-loop pass. This exists because SDCC 4.5 miscompiled
// both of the client's "has this changed?" guards (term.c's second counter and
// irc.c's link-warning flag) into a compare-then-store-the-DIFFERENCE, so each
// guard reported a change every single time: the clock repainted and the whole
// 80-column status row repainted on every pass. The client polled the keyboard
// roughly eight times less often than it should and typing visibly dragged.
// Neither miscompile changes any output, so only a cost measurement catches it.
//
// timeStepSeconds matters here: the harness's default advances the clock a full
// second per read, which makes a *correct* term_clock repaint every pass too.
const idle = run({
  responders: { arp: {}, tcp: {} },
  keyIntervalScans: 4000,   // connect, then sit connected and idle for a while
  timeStepSeconds: 0.004,   // ~1 second per 250 SYSTIME calls, i.e. wall-clock-ish
});
const passes = idle.keyPollCycles.length;
const perPass = (fn) => (idle.dssCalls[fn] || 0) / passes;
assert.ok(passes > 5000, `only ${passes} main-loop passes: the run was too short to judge`);
assert.ok(perPass(0x5b) < 1.0,
  `the idle main loop writes ${perPass(0x5b).toFixed(2)} characters to the screen per pass ` +
  '(budget 1.0). Something repaints unconditionally -- check the change guards in ' +
  'term_clock() and irc_net_warn() against the SDCC note in PLATFORM.md.');
assert.ok(perPass(0x52) < 0.5,
  `the idle main loop issues ${perPass(0x52).toFixed(2)} cursor moves per pass (budget 0.5)`);

// Cost of typing. The input row is redrawn after every pass that saw a key, so
// drawing it character by character charged ~80 DSS calls per character typed.
// term.c assembles the row and pushes it with one dss_puts instead.
const typed = 'hello everyone this is a reasonably long message typed into the input row';
const typing = run({
  responders: { arp: {}, tcp: {} },
  keys: ['/server 192.168.1.10 6667', 'enter', typed, 'escape', 'escape'],
  keyIntervalScans: 40, timeStepSeconds: 0.004,
});
// The budget is a whole-session figure, since the clock also spends a handful
// of single-character writes per modelled second. Per-column drawing of the
// input row put this run at ~3750; drawing the row in one call puts it at ~250.
assert.ok((typing.dssCalls[0x5b] || 0) < 800,
  `typing ${typed.length} characters cost ${typing.dssCalls[0x5b]} single-character DSS writes; ` +
  'a row should be drawn with one dss_puts (term.c rowbuf), not one call per column');

console.log('SPTALK.EXE (win0, unet): LOAD/CAP_TCP/NETSTART/CONNECT/SEND over UNETRTL.DLL -- RESULT OK');
