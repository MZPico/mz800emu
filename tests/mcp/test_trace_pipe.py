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

Binary: $MZ_EMU, else <repo>/mz800emu, else <repo>/build/build-mz800emu/mz800emu.
Exit code 0 = all passed.
"""
import base64, json, os, subprocess, sys, zlib
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
    def __init__(self, exe):
        self.p = subprocess.Popen([str(exe), '--mcp-pipe', '--no-save-ini'], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                  stderr=subprocess.DEVNULL, text=True, bufsize=1)
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

def main():
    e = Pipe(find_exe())
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
    print('FAILED: %s' % FAILS if FAILS else 'all passed')
    sys.exit(1 if FAILS else 0)

if __name__ == '__main__':
    main()
