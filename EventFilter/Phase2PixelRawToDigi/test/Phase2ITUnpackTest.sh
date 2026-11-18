#!/bin/bash
# Runs the three IT unpacking flows on the same events and reports whether each one ran, and its time:
#   cpu         RawToPixelProducer
#   alpaka-cpu  the Alpaka stages on the serial CPU backend
#   gpu         the Alpaka stages on the GPU, once each with 16, 32, 64 and 128 threads per block
# Jobs of PixelDigiToRawAlpakaProducer_cfg.py: accelerator=cpu runs the first two flows, one GPU job per block size
# and layout mode the third.
#
#   ./Phase2ITUnpackTest.sh                   100 events, gapMode KEEP
#   ./Phase2ITUnpackTest.sh 20                fewer events
#   GAP="DROP KEEP AGGREGATE" ./Phase2ITUnpackTest.sh
#   GPU=gpu-amd BLOCK="64 128" ./Phase2ITUnpackTest.sh
#   MODE="stage1fed_stage2chip stage1flat_stage2module" ./Phase2ITUnpackTest.sh
#                                             GPU layouts: stage1fed or stage1flat, then stage2chip or stage2module
#   THREADS=8 ./Phase2ITUnpackTest.sh         framework threads; the GPU times are then launch times only
#   IN=file:/path/a.root ./Phase2ITUnpackTest.sh   default: EOSDIR, the scan scripts' local copy, or xrootd
set -u

HERE=$(cd "$(dirname "$0")" && pwd)
CFG=$HERE/PixelDigiToRawAlpakaProducer_cfg.py
N=${1:-100}
GAPS=${GAP:-KEEP}
GPU=${GPU:-gpu-nvidia}
BLOCKS=${BLOCK:-16 32 64 128}
MODES=${MODE:-stage1fed_stage2chip}
THREADS=${THREADS:-1}

case $N in '' | *[!0-9]*) echo "usage: $0 [events]"; exit 1 ;; esac
case $BLOCKS in *[!0-9\ ]*) echo "BLOCK must be block sizes separated by spaces, e.g. BLOCK=\"64 128\""; exit 1 ;; esac
for m in $MODES; do
  case $m in
    stage1fed_stage2chip | stage1fed_stage2module | stage1flat_stage2chip | stage1flat_stage2module) ;;
    *) echo "MODE must be stage1fed or stage1flat, then _stage2chip or _stage2module, e.g. MODE=\"stage1fed_stage2chip\""; exit 1 ;;
  esac
done
case $GPU in
  gpu-nvidia) PROBE=cudaIsEnabled ;;
  gpu-amd) PROBE=rocmIsEnabled ;;
  *) echo "GPU must be gpu-nvidia or gpu-amd"; exit 1 ;;
esac
command -v cmsRun > /dev/null || { echo "cmsRun not found: run cmsenv first"; exit 1; }
[ -f "$CFG" ] || { echo "$CFG not found: keep this script in test/ next to the cfg"; exit 1; }

IN=${IN:-}
if [ -z "$IN" ] && [ -d "${EOSDIR:-}" ]; then
  IN=$(ls "$EOSDIR"/*.root 2> /dev/null | sort | sed 's|^|file:|' | paste -sd, -)
fi
LOCAL=${TMPDIR:-/tmp}/itscan_input_$(id -u).root
[ -z "$IN" ] && [ -s "$LOCAL" ] && IN=file:$LOCAL
if [ -n "$IN" ]; then
  INARG=inputFiles=$IN
  SRC="$(echo "$IN" | tr ',' '\n' | wc -l) file(s) from $(echo "$IN" | cut -d, -f1)"
else
  voms-proxy-info -exists > /dev/null 2>&1 ||
    { echo "the default input is read over xrootd: run voms-proxy-init --voms cms first"; exit 1; }
  INARG=
  SRC="the cfg's default RelVal file over xrootd"
fi

OUT=$PWD/unpacktest_$(date +%Y%m%d_%H%M%S)
mkdir -p "$OUT"
SYNC=
[ "$THREADS" = 1 ] && SYNC=syncForTiming=1
if $PROBE > /dev/null 2>&1; then GPUOK=1; else GPUOK=0; fi
GPUNAME=$(nvidia-smi --query-gpu=name --format=csv,noheader 2> /dev/null | head -1)

: > "$OUT/jobs.tsv"
for gap in $GAPS; do
  # - is the cpu job, then one GPU job per layout mode and block size
  for run in - $(for m in $MODES; do for b in $BLOCKS; do echo $m:$b; done; done); do
    if [ $run = - ]; then
      job=cpu acc=cpu block=- extra= log=$OUT/cpu_$gap.log
    else
      mode=${run%:*} b=${run#*:} perfed=1 perchip=1
      [ ${mode%_*} = stage1flat ] && perfed=0
      [ ${mode#*_} = stage2module ] && perchip=0
      job=gpu acc=$GPU block="$b, $mode" extra="blockSize=$b tablePerFed=$perfed decodePerChip=$perchip $SYNC"
      log=$OUT/gpu_${gap}_${mode}_b$b.log
    fi
    if [ $job = gpu ] && [ $GPUOK = 0 ]; then
      printf '%s\t%s\t%s\t%s\t\tSKIP no %s device visible (%s)\n' "$gap" $job "$acc" "$BLOCKS" "${GPU#gpu-}" $PROBE >> "$OUT/jobs.tsv"
      break
    fi
    echo "=== $job: accelerator=$acc gapMode=$gap $extra"
    (cd "$OUT" && cmsRun "$CFG" accelerator=$acc gapMode=$gap maxEvents=$N threads=$THREADS timing=1 $extra $INARG > "$log" 2>&1)
    printf '%s\t%s\t%s\t%s\t%s\t%s\n' "$gap" $job "$acc" "$block" $? "$log" >> "$OUT/jobs.tsv"
  done
done

python3 - "$OUT" "$N" "$THREADS" "$SRC" "$GPUNAME" << 'EOF'
import re, sys

out, n, threads, src, gpuname = sys.argv[1], int(sys.argv[2]), sys.argv[3], sys.argv[4], sys.argv[5]
S1, S2 = 'Phase2ITRawToBitStreamProducer', 'Phase2ITBitStreamToPixelProducer'
CPU = {'RawToPixelProducer'}
ALPAKA = {'Phase2ITModuleMapESProducer', S1, S2}


def parse(path):
    t = open(path, errors='replace').read()
    errors, first = {}, ''
    for block in t.split("An exception of category '")[1:]:
        cat, rest = block.split("'", 1)
        if cat == 'MultipleExceptions':
            continue
        context, _, msg = rest.partition('Exception Message:\n')
        msg = '%s: %s' % (cat, ' '.join(msg.strip().splitlines()[:2]))
        first = first or msg
        for label in re.findall(r"for module [^/\s]+/'(\w+)'|label='(\w+)'", context):
            errors.setdefault(label[0] or label[1], msg)
    ev = re.search(r'TrigReport Events total = (\d+)', t)
    times = {}
    for sec, mod in re.findall(r'^TimeReport\s+([\d.]+)\s+[\d.]+\s+(\w+)\s*$', t, re.M):
        times.setdefault(mod, 1e3 * float(sec))
    return {'events': int(ev.group(1)) if ev else 0, 'errors': errors, 'first': first, 'times': times}


rows = []
for line in open(out + '/jobs.tsv'):
    gap, job, acc, block, rc, log = line.rstrip('\n').split('\t')
    if log.startswith('SKIP'):
        rows.append([gap, 'gpu', '%s, blockSize %s' % (acc, '/'.join(block.split())), 'SKIP', '-', '-', '-', log[5:]])
        continue
    r = parse(log)
    err = set(r['errors'])
    ran = int(rc) == 0 and r['events'] > 0
    # a failure that names no module of either flow counts against both
    shared = not ran and (not err or bool(err - CPU - ALPAKA))
    flows = [('cpu', 'RawToPixelProducer', CPU, ['RawToPixelProducer'])] if job == 'cpu' else []
    flows.append(('alpaka-cpu' if job == 'cpu' else 'gpu',
                  'serial backend' if job == 'cpu' else '%s, blockSize %s' % (acc, block), ALPAKA, [S1, S2]))
    for flow, what, mods, timed in flows:
        if ran:
            result, note = 'PASS', 'only %d of %d events' % (r['events'], n) if r['events'] < n else ''
        elif shared or mods & err:
            result = 'FAIL'
            note = '; '.join(sorted({r['errors'][m] for m in mods & err}) or [r['first'] or 'exit ' + rc])[:200]
        else:
            result, note = 'STOPPED', 'the job stopped on the other flow'
        t = [r['times'].get(m) for m in timed]
        total = '%.2f' % sum(t) if None not in t else '-'
        split = ' + '.join('%.2f' % x for x in t) if len(t) > 1 and None not in t else '-'
        rows.append([gap, flow, what, result, r['events'] or '-', total, split, note])

print()
print('IT unpacker test: %d events, %s thread(s), input %s%s' % (n, threads, src, ', GPU ' + gpuname if gpuname else ''))
if threads != '1':
    print('  the gpu ms/event are launch times only: syncForTiming needs a single thread')
head = ['gapMode', 'flow', 'what', 'result', 'events', 'ms/event', 'stage 1 + 2', 'note']
width = [max(len(str(x)) for x in col) for col in zip(head, *rows)]
for row in [head] + rows:
    print('  ' + '  '.join(str(x).ljust(w) for x, w in zip(row, width)).rstrip())
print('logs: ' + out)
sys.exit(1 if any(row[3] == 'FAIL' for row in rows) else 0)
EOF
