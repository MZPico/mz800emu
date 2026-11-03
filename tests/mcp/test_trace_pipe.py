#!/usr/bin/env python3
"""End-to-end test of the non-stopping tracepoints, the PSG write log and the VRAM hash
(trace_add / trace_read / trace_clear / trace_config, psg_log_start / psg_log_read /
psg_log_stop, vram_hash) over the pipe transport with the real binary.

A small Z80 loop at 0x2000 writes B to the PSG port (0xF2) and to 0x3000, then
increments B. A tracepoint at the INC records BC and the byte at 0x3000:

  1. hello lists the commands
  2. records: B = the byte at 0x3000, constant cycles per loop, ticks = 5 x cycles
     (+ a constant), frames counted; the PSG log has the same bytes, each write
     between its loop's start and its record (by ticks)
  3. with a breakpoint at the same address: run stops there, the hit is recorded
     once (when the instruction executes after the resume)
  4. vram_hash = zlib.crc32 of region_read's planes 8..11
  5. a small buffer: run pauses with stopped_by "trace_full", nothing lost;
     on_full "drop": the overflow is reported with the dropped count
  6. trace_clear: no more records; the old commands answer as before
  7. the in-memory RAM disk (mr1r18_type = STANDARD) with 512K / 1M / 16M: regions_list
     lists all its banks (8 / 16 / 256 ramdisk_std regions, beyond the former limit of
     128 regions), filled with 0xFF; on 16M, bank 200 written by region_write is what
     the Z80 reads through the ports (E9h page, EBh address, EAh data) and the byte the
     Z80 writes there is what region_read returns; no file is written
  8. mr1r18_wait = 0 / 1 / 3 / 9 / 10 / 16 / 30 / 255, decimal, and 0x0A (1M): the RAM disk's port waits stretch INIR / OTIR by that many T a byte
     (21 + w per repeated byte, 16 + w the last), the GDG ticks along (5 per T), and the bytes are the same

Binary: $MZ_EMU, else <repo>/mz800emu, else <repo>/build/build-mz800emu/mz800emu.
Exit code 0 = all passed.
"""
import base64, json, os, subprocess, sys, tempfile, zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent
FAILS = []

def find_exe():
    env = os.environ.get('MZ_EMU')
    if env and Path(env).is_file(): return Path(env)
    for c in (ROOT / 'mz800emu', ROOT / 'mz800emu.exe', ROOT / 'build' / 'build-mz800emu' / 'mz800emu'):
        if c.is_file(): return c
    print('ERROR: mz800emu binary not found', file=sys.stderr); sys.exit(1)

class Pipe:
    def __init__(self, exe, args=(), cwd=None):
        self.p = subprocess.Popen([str(exe), '--mcp-pipe', '--no-save-ini'] + list(args), stdin=subprocess.PIPE,
                                  stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True, bufsize=1, cwd=cwd)
        self.n = 0
        self.hello = self.read()
        for _ in range(100):
            try: self.call('ping'); self.call('run', frames=1); break
            except RuntimeError: pass
    def read(self):
        while True:
            line = self.p.stdout.readline()
            if not line: raise RuntimeError('emulator closed the pipe')
            if line.startswith('{'):
                try: return json.loads(line)
                except json.JSONDecodeError: pass
    def call(self, cmd, **data):
        self.n += 1
        req = {'req_id': self.n, 'cmd': cmd}
        if data: req['data'] = data
        self.p.stdin.write(json.dumps(req) + '\n'); self.p.stdin.flush()
        while True:
            r = self.read()
            if r.get('req_id') == self.n: break
        if not r.get('success', True) or 'error' in r: raise RuntimeError('%s failed: %s' % (cmd, r))
        return r.get('data', r)
    def close(self):
        try: self.call('shutdown')
        except Exception: pass
        try: self.p.wait(timeout=10)
        except Exception: self.p.kill()

def check(what, ok, detail=''):
    print('  %-70s %s%s' % (what, 'PASS' if ok else 'FAIL', '' if ok else '  ' + str(detail)))
    if not ok: FAILS.append(what)

LOOP = 0x2003; INC = 0x2009
PROGRAM = bytes([0xF3, 0x06, 0x00,              # 2000 di; ld b, 0
                 0x78, 0xD3, 0xF2,              # 2003 ld a, b; out (0xF2), a
                 0x32, 0x00, 0x30,              # 2006 ld (0x3000), a
                 0x04, 0x18, 0xF7])             # 2009 inc b; jr 2003

def start(e):
    e.call('pause'); e.call('bp_clear'); e.call('trace_clear')
    e.call('mem_write', addr=0x2000, data_hex=PROGRAM.hex())
    e.call('set_register', reg='PC', value=0x2000)

def read_all(e):
    out = []
    while True:
        d = e.call('trace_read', max=50000); out += d['records']
        if not d['pending']: return out, d

RD_PROGRAM = bytes([0xF3,                       # 2000 di
                    0x3E, 0xC8, 0xD3, 0xE9,     # 2001 ld a, 200; out (0xE9), a   = page 200
                    0x01, 0xEB, 0x12,           # 2005 ld bc, 0x12EB
                    0x3E, 0x34, 0xED, 0x79,     # 2008 ld a, 0x34; out (c), a     = address 0x1234
                    0xDB, 0xEA,                 # 200C in a, (0xEA)               = page 200 : 0x1234
                    0x32, 0x00, 0x30,           # 200E ld (0x3000), a
                    0x3E, 0x5A, 0xD3, 0xEA,     # 2011 ld a, 0x5A; out (0xEA), a  = page 200 : 0x1235
                    0x18, 0xFE])                # 2015 jr $

def ramdisk_sizes(exe):
    for size, banks in (('512K', 8), ('1M', 16), ('16M', 256)):
        with tempfile.TemporaryDirectory() as d:
            ini = Path(d) / 'rd.ini'
            ini.write_text('[RAMDISK]\nmr1r18_pluged = 1\nmr1r18_type = STANDARD\nmr1r18_size = %s\n'
                           'mr1r18_filepath = rd-test.dat\n' % size)
            e = Pipe(exe, ['--config', str(ini)], cwd=d)
            try:
                regs = e.call('regions_list')['regions']
                rd = [r for r in regs if r['kind'] == 'ramdisk_std']
                check('7. %s in-memory RAM disk: %d ramdisk_std regions of 64 KB (%d regions in all)' % (size, len(rd), len(regs)),
                      [r['sub_id'] for r in rd] == list(range(banks)) and all(r['size'] == 0x10000 and r['writable'] for r in rd),
                      [(r['id'], r['sub_id']) for r in rd][-3:])
                top = rd[-1]
                b = base64.b64decode(e.call('region_read', region_id=top['id'], offset=0, length=0x10000)['data_b64'])
                check('   the top bank (region %d) is readable, filled with 0xFF' % top['id'], b == b'\xff' * 0x10000, b[:4])
                if banks == 256:
                    rid = rd[200]['id']
                    e.call('pause')
                    e.call('region_write', region_id=rid, offset=0x1234, data_b64=base64.b64encode(b'\xa7').decode())
                    e.call('mem_write', addr=0x2000, data_hex=RD_PROGRAM.hex())
                    e.call('set_register', reg='PC', value=0x2000)
                    e.call('run', frames=1)
                    got = base64.b64decode(e.call('mem_read', addr=0x3000, len=1)['data_b64'])
                    back = base64.b64decode(e.call('region_read', region_id=rid, offset=0x1234, length=2)['data_b64'])
                    check('   bank 200 (region %d): the Z80 reads the byte region_write put there' % rid, got == b'\xa7', got)
                    check('   ... and region_read returns the byte the Z80 wrote', back == b'\xa7\x5a', back)
            finally:
                e.close()
            # (the emulator creates its SD-mz800/ and cdl-export/ directories regardless; the RAM-disk
            # file would be rd-test.dat in the directory itself)
            left = sorted(p.name for p in Path(d).iterdir() if not p.is_dir())
            dat = sorted(str(p.relative_to(d)) for p in Path(d).rglob('*.dat'))
            check('   no RAM-disk file written (files: %s)' % left, left == ['rd.ini'] and not dat, (left, dat))

WAIT_PROGRAM = bytes([0xF3,                     # 2000 di
                      0x3E, 0x03, 0xD3, 0xE9,   # 2001 ld a, 3; out (0xE9), a      = page 3
                      0x01, 0xEB, 0x00, 0xAF, 0xED, 0x79,   # 2005 ld bc, 0x00EB; xor a; out (c), a  = address 0
                      0x21, 0x00, 0x40,         # 200B ld hl, 0x4000
                      0x01, 0xEA, 0x00,         # 200E ld bc, 0x00EA (B = 0: 256 bytes)
                      0xED, 0xB2,               # 2011 inir                       = page 3 : 0-255 -> 0x4000
                      0x21, 0x00, 0x40,         # 2013 ld hl, 0x4000
                      0x01, 0xEA, 0x00,         # 2016 ld bc, 0x00EA
                      0xED, 0xB3,               # 2019 otir                       = 0x4000 -> page 3 : 256-511
                      0x18, 0xFE])              # 201B jr $
W_INIR, W_LDHL2, W_OTIR, W_END = 0x2011, 0x2013, 0x2019, 0x201B

def ramdisk_wait(exe):
    pat = bytes((i * 37 + 11) & 255 for i in range(256))
    for txt, w in [(str(n), n) for n in (0, 1, 3, 9, 10, 16, 30, 255)] + [('0x0A', 10)]:
        with tempfile.TemporaryDirectory() as d:
            ini = Path(d) / 'rd.ini'
            ini.write_text('[RAMDISK]\nmr1r18_pluged = 1\nmr1r18_type = STANDARD\nmr1r18_size = 1M\nmr1r18_wait = %s\n' % txt)
            e = Pipe(exe, ['--config', str(ini)], cwd=d)
            try:
                rid = [r for r in e.call('regions_list')['regions'] if r['kind'] == 'ramdisk_std'][3]['id']
                e.call('pause'); e.call('bp_clear'); e.call('trace_clear')
                e.call('region_write', region_id=rid, offset=0, data_b64=base64.b64encode(pat).decode())
                e.call('mem_write', addr=0x2000, data_hex=WAIT_PROGRAM.hex())
                e.call('set_register', reg='PC', value=0x2000)
                ids = [e.call('trace_add', addr=a)['id'] for a in (W_INIR, W_LDHL2, W_OTIR, W_END)]
                e.call('set_speed', mode='max')
                recs = []
                for _ in range(10):                             # (255 waits: 2 x 70,651 T, two frames)
                    e.call('run', frames=1)
                    recs += read_all(e)[0]
                    if any(x['id'] == ids[3] for x in recs): break
                at = {}
                for x in recs: at.setdefault(x['id'], x)          # (the first hit of each; jr $ repeats)
                t = [at.get(i) for i in ids]
                if None in t:
                    check('8. mr1r18_wait = %s: the tracepoints are hit' % txt, False, recs[:4]); continue
                want = 255 * (21 + w) + 16 + w
                ci, co = t[1]['cycles'] - t[0]['cycles'], t[3]['cycles'] - t[2]['cycles']
                ti, to = t[1]['ticks'] - t[0]['ticks'], t[3]['ticks'] - t[2]['ticks']
                check('8. mr1r18_wait = %s: INIR / OTIR of 256 bytes take %d / %d T (%d expected)' % (txt, ci, co, want),
                      ci == want and co == want, (ci, co, want))
                check('   the GDG clock runs on with the waits (5 ticks a T)', ti == 5 * ci and to == 5 * co, (ti, ci, to, co))
                got = base64.b64decode(e.call('mem_read', addr=0x4000, len=256)['data_b64'])
                back = base64.b64decode(e.call('region_read', region_id=rid, offset=256, length=256)['data_b64'])
                check('   the bytes read and written are the same', got == pat and back == pat)
            finally:
                e.close()

def main():
    exe = find_exe()
    e = Pipe(exe)
    try:
        cmds = set(e.hello.get('commands', []))
        new = {'trace_add', 'trace_clear', 'trace_read', 'trace_config', 'psg_log_start', 'psg_log_read', 'psg_log_stop', 'vram_hash'}
        check('1. hello lists the 8 commands', new <= cmds, new - cmds)
        e.call('set_speed', mode='max')

        start(e)
        tp = e.call('trace_add', addr=INC, regs=['BC'], mem=[[0x3000, 1]])
        check('   trace_add answers id, addr, record_bytes', tp['addr'] == INC and tp['id'] >= 1 and tp['record_bytes'] == 28 + 3 + 7, tp)
        e.call('psg_log_start')
        r = e.call('run', frames=3)
        check('2. run(3) runs 3 frames with a tracepoint (stopped_by frames)', r['stopped_by'] == 'frames' and r['complete'], r)
        recs, d = read_all(e)
        w = e.call('psg_log_read', max=100000)['writes']
        check('   records taken, no overflow (%d)' % len(recs), len(recs) > 1000 and not d['overflow'] and d['dropped'] == 0, d)
        b_ok = all((x['regs']['BC'] >> 8) == int(x['mem'][0], 16) for x in recs)
        check('   each record: B = the byte the loop stored', b_ok)
        dc = {b['cycles'] - a['cycles'] for a, b in zip(recs, recs[1:])}
        check('   constant cycles per loop (%s T)' % sorted(dc), len(dc) == 1, dc)
        off = {x['ticks'] - 5 * x['cycles'] for x in recs}
        check('   ticks = 5 x cycles + a constant', len(off) == 1, sorted(off)[:3])
        fr = [x['frame'] for x in recs]
        check('   frames counted (%d..%d)' % (fr[0], fr[-1]), fr == sorted(fr) and fr[-1] - fr[0] in (2, 3), (fr[0], fr[-1]))
        check('   the PSG log: one write a loop (%d writes, %d records)' % (len(w), len(recs)), len(w) - len(recs) in (0, 1), (len(w), len(recs)))
        vals = all(wr[3] == (x['regs']['BC'] >> 8) for wr, x in zip(w, recs))
        order = all(wr[0] <= x['ticks'] and (i + 1 >= len(w) or w[i + 1][0] > x['ticks']) for i, (wr, x) in enumerate(zip(w, recs)))
        check('   each write: its byte, between its loop\'s start and its record (ticks)', vals and order)
        check('   a write\'s cycles = its ticks / 5, frame given', all(wr[1] == (wr[0] // 5) & 0xFFFFFFFF for wr in w) and w[0][2] == recs[0]['frame'], w[:2])

        start(e)
        e.call('trace_add', addr=INC, regs=['BC'])
        e.call('bp_add', addr=INC)
        r = e.call('run', frames=3)
        regs = e.call('get_registers')
        recs1, _ = read_all(e)
        check('3. a breakpoint at the tracepoint\'s address stops (PC %04X)' % regs['PC'], r['stopped_by'] == 'breakpoint' and regs['PC'] == INC, r)
        check('   the hit is not recorded before the instruction runs', recs1 == [], recs1[:1])
        e.call('bp_clear')
        e.call('run', frames=1)
        recs2, _ = read_all(e)
        check('   after the resume: recorded once, B = 0 first', recs2 and (recs2[0]['regs']['BC'] >> 8) == 0 and
              len({x['cycles'] for x in recs2}) == len(recs2), recs2[:2])

        h = e.call('vram_hash')['crc32']
        planes = [base64.b64decode(e.call('region_read', region_id=i, offset=0, length=8000)['data_b64']) for i in (8, 9, 10, 11)]
        check('4. vram_hash = zlib.crc32 of the planes (region_read 8..11)', h == [zlib.crc32(p) for p in planes], (h, [zlib.crc32(p) for p in planes]))
        start(e)
        e.call('trace_add', addr=INC, vram_hash=True)
        e.call('run', frames=1)
        recs3, _ = read_all(e)
        check('   a tracepoint\'s vram_crc32 = vram_hash', recs3 and recs3[-1]['vram_crc32'] == e.call('vram_hash')['crc32'])

        start(e)
        e.call('trace_add', addr=INC, regs=['BC'])
        e.call('trace_config', capacity=4096)
        r = e.call('run', frames=3)
        recs4, d = read_all(e)
        check('5. a full buffer pauses the run (stopped_by %s, %d records)' % (r['stopped_by'], len(recs4)),
              r['stopped_by'] == 'trace_full' and not r['complete'] and len(recs4) == 4096 // 31 and not d['overflow'], (r, len(recs4), d))
        r = e.call('run', frames=3); recs5, _ = read_all(e)
        bs = [x['regs']['BC'] >> 8 for x in recs4 + recs5]
        check('   nothing lost across the pause (B counts on)', all((b - a) & 255 == 1 for a, b in zip(bs, bs[1:])), bs[:5])
        e.call('trace_config', on_full='drop')
        r = e.call('run', frames=3)
        d = e.call('trace_read', max=50000)
        check('   on_full drop: overflow reported (%d dropped)' % d['dropped'], r['stopped_by'] == 'frames' and d['overflow'] and d['dropped'] > 0, (r, {k: v for k, v in d.items() if k != 'records'}))
        e.call('trace_config', capacity=64 << 20, on_full='pause')

        e.call('trace_clear'); e.call('psg_log_stop')
        while e.call('psg_log_read', max=100000)['pending']: pass
        e.call('run', frames=2)
        check('6. trace_clear: no more records', e.call('trace_read')['count'] == 0)
        check('   psg_log_stop: no more writes', e.call('psg_log_read')['count'] == 0)
        r = e.call('run', frames=2)
        check('   run without tracepoints as before', r['stopped_by'] == 'frames' and r['complete'] and r['actual_frames'] == 2, r)
    finally:
        e.close()
    ramdisk_sizes(exe)
    ramdisk_wait(exe)
    print('FAILED: %s' % FAILS if FAILS else 'all passed')
    sys.exit(1 if FAILS else 0)

if __name__ == '__main__':
    main()
