#!/usr/bin/env node
// test_netdll_win0.js — Stage 1 verification for the win0 memory-layout port
// (see the port plan "SprinTalk: переход на трёхоконную раскладку
// WIN0+WIN1+WIN2"). Runs the actual build/NETDLL.EXE (a two-stage PRELOAD
// EXE, packed by win0_exe.py) through sprinter-rtl8019a's Z80/DSS software
// harness, proving end to end -- not just "it links" -- that:
//   - the stage-1 loader (lib/win0/loader.c) streams the WIN0/WIN2 blobs and
//     hands off correctly (harness.js's PRELOAD support, added alongside
//     this test);
//   - unetcore.s's _unet_call WIN1 swap survives the win0 RST trampolines
//     restoring WIN1 = _wrt_p1 on every DSS call made *during* the DLL call
//     (GETCAPS is called twice per LOAD, INIT reads from the still-open file
//     handle -- both exercise this repeatedly);
//   - unetldcore.s resolves the DLL next to the EXE via P0:0x0100 (staged by
//     the loader from the PSP), NOT dss_appinfo -- proven by placing the DLL
//     under a directory the harness's bare-name/currentDir fallback would
//     NOT find on its own (see pspPath/currentDir below);
//   - unetcore.s caches the DLL page's PHYSICAL number for its raw OUTs
//     rather than the DSS block id GETMEM returns (they are different things
//     -- BIOS EMM_FN2/FN4): scenario.distinctBlockIds below numbers block ids
//     from a separate space, so confusing the two maps an unallocated page
//     and the harness stops with an explicit error instead of limping along;
//   - crt0_win0's _DATA is actually zero at start (gsinit_zero_data.s):
//     unet_load's/unetld's state lives entirely in _DATA (l__BSS is 0 in
//     every build so far), so a single clean run already exercises this --
//     any leftover garbage would desync SELECT's "nothing loaded yet" state
//     or make g_block's zero "not loaded" sentinel read as a live block.
//
// Usage: node tools/test_netdll_win0.js
'use strict';

const assert = require('assert');
const fs = require('fs');
const path = require('path');

const HARNESS = process.env.EXE_HARNESS
  || '/Users/dmitry/dev/zx/sprinter/sprinter-rtl8019a/tools/exe-harness/harness.js';
const { runExe } = require(HARNESS);

const root = path.resolve(__dirname, '..');
const exePath = path.join(root, 'NETDLL.EXE');
const dllPath = process.env.UNET_TEST_DLL
  || '/Users/dmitry/dev/zx/sprinter/sources/unet_libs/unet_libs_core/dll/UNETRTL.DLL';

if (!fs.existsSync(exePath)) {
  console.error(`missing ${exePath} -- run 'make netdll' first`);
  process.exit(1);
}
const dllBytes = fs.readFileSync(dllPath);

// The DLL lives at C:\NET\..., but the harness's OWN cwd defaults to
// C:\OTHER: a bare-name lookup (the old dss_appinfo/no-directory fallback)
// would miss it, so RESULT OK here can only happen if unetldcore.s actually
// threaded the P0:0x0100 directory through.
const scenario = {
  environment: { NET: 'RTL' },
  currentDir: 'C:\\OTHER',
  pspPath: 'C:\\NET\\NETDLL.EXE',
  files: { 'C:\\NET\\UNETRTL.DLL': dllBytes },
  // Model real DSS: GETMEM returns a block id, not a physical page. Without
  // this the harness hands out ids that happen to equal pages, and a raw OUT
  // of a block id would silently "work" here while failing on hardware.
  distinctBlockIds: true,
  traceDss: true, traceCpu: true,
};

let r;
try {
  r = runExe(exePath, '', scenario);
} catch (error) {
  if (error && error.partialOutput !== undefined) {
    console.error('--- partial output before crash ---');
    console.error(error.partialOutput);
    console.error('--- dssEvents before crash ---');
    console.error(error.partialDssEvents.join('\n'));
  }
  throw error;
}

if (process.env.TRACE === '1') {
  console.log(r.output);
  console.log(r.dssEvents.join('\n'));
}

assert.match(r.output, /\[S1\] SELECT/, 'SELECT stage did not run');
assert.match(r.output, /NET tag    : RTL/, 'NET tag not resolved to RTL');
assert.match(r.output, /\[S2\] LOAD/, 'LOAD stage did not run');
assert.match(r.output, /LOAD OK/, `LOAD did not succeed -- output:\n${r.output}`);
assert.match(r.output, /RESULT OK/, `expected RESULT OK -- output:\n${r.output}`);
assert.strictEqual(r.exitCode, 0, 'expected clean exit(0)');
assert.ok(r.cleanup.filesClosed, 'a DSS file handle leaked (loader or unet_load did not close it)');
assert.ok(r.cleanup.isaClosed, 'the ISA window was left open');
// Page discipline. The stage-1 loader's own GETMEMs (P0, and P2 -- NETDLL's
// code fits WIN0, so there is no P1) are deliberately left to DSS.Exit, which
// releases a process's blocks on return; only the DLL's page is this program's
// to free, and unetld_unload must do it. r.cleanup.pagesFreed counts ALL
// blocks still held at EXIT, so it is expected false here -- assert the
// narrower, meaningful invariant instead.
const getmems = r.dssEvents.filter((e) => e.startsWith('GETMEM'));
const freemems = r.dssEvents.filter((e) => e.startsWith('FREEMEM'));
assert.strictEqual(getmems.length, 3, `expected 3 GETMEMs (loader P0/P2 + DLL) -- got:\n${getmems.join('\n')}`);
assert.strictEqual(freemems.length, 1, `expected exactly 1 FREEMEM (the DLL's page) -- got:\n${freemems.join('\n')}`);
const dllBlock = getmems[2].match(/block (\d+)/)[1];
assert.strictEqual(freemems[0], `FREEMEM ${dllBlock}`,
  `unetld_unload freed the wrong block: ${freemems[0]} (DLL was ${dllBlock})`);

// Second run: the live S5 stage (CONNECT/RECV/SEND against the modelled card
// and peer). Two things this guards that the loader-only run above cannot:
//   - a caller buffer must never sit in WIN0. The command line lives at
//     P0:0x0080, and UNETRTL's cold overlay maps its OWN page over WIN0 for the
//     ARP/DNS builders -- so a host string left there is gone by the time
//     PARSE_IPV4 reads it, and a literal dotted quad silently becomes a DNS
//     lookup. The assertion below is that the ARP goes to the host itself.
//   - the DLL's LASTERR line reaches the operator, which is the whole point of
//     shipping this program beside SPTALK.
const live = runExe(exePath, '192.168.7.10 6667', {
  ...scenario,
  environment: {
    NET: 'RTL',
    NET_IP: '192.168.7.50', NET_MASK: '255.255.255.0', NET_GW: '192.168.7.1',
    NET_MAC: '00:11:22:33:44:55', NET_DNS1: '192.168.7.1',
    NET_RTL_HW: '1/#300',
  },
  responders: { arp: {}, tcp: { greeting: [':srv NOTICE AUTH :*** hi\r\n'], greetingMs: 30, afterMs: 20 } },
  traceDss: false,
});
assert.match(live.output, /connect {4}: NERR 0\b/,
  `S5 CONNECT failed -- output:\n${live.output}`);
assert.match(live.output, /send {7}: bytes 13\b/,
  `S5 SEND failed -- output:\n${live.output}`);
assert.match(live.output, /lasterr {4}: RTL hw=/,
  `the backend's own LASTERR line never printed -- output:\n${live.output}`);
// No UDP to port 53: a DNS query here means the dotted quad was not parsed,
// which is exactly what a host string left in WIN0 produces.
const dnsFrames = live.transmittedFrames.filter((f) => {
  const b = Buffer.from(f, 'hex');
  return b.length > 37 && b[12] === 0x08 && b[13] === 0x00 && b[23] === 17
    && ((b[36] << 8) | b[37]) === 53;
});
assert.strictEqual(dnsFrames.length, 0,
  'a literal IP triggered a DNS lookup: the host string was passed from WIN0');

console.log('NETDLL.EXE (win0): SELECT/LOAD/GETCAPS/ABI via P0:0x0100 app-dir + live CONNECT/SEND -- RESULT OK');
