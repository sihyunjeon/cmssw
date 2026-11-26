#!/bin/bash
# Throughput of the IT unpackers with the benchmark of patatrack-scripts, the one the TSG timing server runs: one job
# processes the same raw data over and over with several streams, and the benchmark reports events per second.
#   input       reading the raw data only, the limit for the other flows
#   cpu         RawToPixelProducer
#   alpaka-cpu  the Alpaka stages on the serial CPU backend
#   gpu         the Alpaka stages on the GPU, once per layout mode, block size and number of streams
# The raw data is packed once from the input's first events and written uncompressed, so that reading it costs little.
# The CPU flows run one stream per thread. Nothing is pinned, so the job runs on the CPUs and the GPU the node gives it.
#
#   ./Phase2ITUnpackBench.sh                  20000 events per measurement, 3 measurements, 8 threads
#   ./Phase2ITUnpackBench.sh 5000             fewer events per measurement
#   FLOW="input gpu" ./Phase2ITUnpackBench.sh some of the flows, in this order; default input gpu cpu alpaka-cpu
#   THREADS=16 STREAMS="8 16" ./Phase2ITUnpackBench.sh   threads, and the streams of the gpu flow, default 1 2 4 8
#   MODE=... BLOCK=... GPU=... GAP=...        as for the test script; default the per-chip decode in one and in two
#                                             passes, at blockSize 64
#   REPEATS=5 PACK=100 ./Phase2ITUnpackBench.sh  measurements per point, and the events packed
#   IN=file:/path/a.root ./Phase2ITUnpackBench.sh  the input to pack, as for the test script
#   PATATRACK=/path/patatrack-scripts         a checkout of the benchmark; default $CMSSW_BASE/patatrack-scripts at the
#                                             timing server's commit, cloned if missing
#   ./Phase2ITUnpackBench.sh clone            only check the options and get that checkout, e.g. before a batch job
set -u

HERE=$(cd "$(dirname "$0")" && pwd)
CFG=$HERE/PixelDigiToRawAlpakaBench_cfg.py
PIN=ecb68ea4784fed99b2f89980b95355890a55aa7f
EV=${1:-20000}
FLOWS=${FLOW:-input gpu cpu alpaka-cpu}
GPU=${GPU:-gpu-nvidia}
MODES=${MODE:-stage1fed_stage2chip stage1fed_stage2chip2pass}
BLOCKS=${BLOCK:-64}
GAP=${GAP:-KEEP}
THREADS=${THREADS:-8}
STREAMS=${STREAMS:-1 2 4 8}
REPEATS=${REPEATS:-3}
PACK=${PACK:-100}

case $EV in clone) ;; '' | *[!0-9]* | 0) echo "usage: $0 [events per measurement | clone]"; exit 1 ;; esac
for v in THREADS REPEATS PACK; do
  case ${!v} in '' | *[!0-9]* | 0) echo "$v must be a positive number"; exit 1 ;; esac
done
case "$BLOCKS $STREAMS" in *[!0-9\ ]*) echo "BLOCK and STREAMS must be numbers separated by spaces"; exit 1 ;; esac
for f in $FLOWS; do
  case $f in input | cpu | alpaka-cpu | gpu) ;; *) echo "FLOW must be some of input, cpu, alpaka-cpu and gpu"; exit 1 ;; esac
done
for m in $MODES; do
  case ${m%2pass} in
    stage1fed_stage2chip | stage1fed_stage2module | stage1flat_stage2chip | stage1flat_stage2module) ;;
    *) echo "MODE must be stage1fed or stage1flat, then _stage2chip or _stage2module, optionally followed by 2pass," \
         "e.g. MODE=\"stage1fed_stage2chip stage1fed_stage2chip2pass\""; exit 1 ;;
  esac
done
case $GAP in DROP | KEEP | AGGREGATE) ;; *) echo "GAP must be DROP, KEEP or AGGREGATE"; exit 1 ;; esac
case $GPU in
  gpu-nvidia) PROBE=cudaIsEnabled ;;
  gpu-amd) PROBE=rocmIsEnabled ;;
  *) echo "GPU must be gpu-nvidia or gpu-amd"; exit 1 ;;
esac
[ -n "${CMSSW_BASE:-}" ] && command -v cmsRun > /dev/null || { echo "cmsRun not found: run cmsenv first"; exit 1; }
[ -f "$CFG" ] || { echo "$CFG not found: keep this script in test/ next to the cfgs"; exit 1; }

if [ -z "${PATATRACK:-}" ]; then
  PATATRACK=$CMSSW_BASE/patatrack-scripts
  { [ -d "$PATATRACK/.git" ] || git clone -q https://github.com/cms-patatrack/patatrack-scripts.git "$PATATRACK"; } &&
    git -C "$PATATRACK" checkout -q $PIN || { echo "could not check out patatrack-scripts $PIN in $PATATRACK"; exit 1; }
fi
[ -x "$PATATRACK/benchmark" ] || { echo "no benchmark in $PATATRACK"; exit 1; }
if [ "$EV" = clone ]; then
  echo "patatrack-scripts in $PATATRACK: $(git -C "$PATATRACK" log --oneline -1 2> /dev/null)"
  exit 0
fi

IN=${IN:-}
if [ -z "$IN" ] && [ -d "${EOSDIR:-}" ]; then
  IN=$(ls "$EOSDIR"/*.root 2> /dev/null | sort | sed 's|^|file:|' | paste -sd, -)
fi
LOCAL=${TMPDIR:-/tmp}/itscan_input_$(id -u).root
[ -z "$IN" ] && [ -s "$LOCAL" ] && IN=file:$LOCAL
if [ -n "$IN" ]; then
  INARG=inputFiles=$IN
  SRC=$(echo "$IN" | cut -d, -f1)
else
  voms-proxy-info -exists > /dev/null 2>&1 ||
    { echo "the default input is read over xrootd: run voms-proxy-init --voms cms first"; exit 1; }
  INARG=
  SRC="the cfg's default RelVal file"
fi

OUT=$PWD/unpackbench_$(date +%Y%m%d_%H%M%S)
mkdir -p "$OUT"
RAW=$OUT/raw.root
if $PROBE > /dev/null 2>&1; then GPUOK=1; else GPUOK=0; fi
GPUNAME=$(nvidia-smi --query-gpu=name --format=csv,noheader 2> /dev/null | head -1)
CPUNAME=$(lscpu 2> /dev/null | sed -n 's/^Model name: *//p' | head -1)
nvidia-smi > "$OUT/gpu_start.txt" 2>&1

echo "=== pack: $PACK events of $SRC, gapMode $GAP"
(cd "$OUT" && BENCH_PACK=$RAW BENCH_OPTS="gapMode=$GAP maxEvents=$PACK $INARG" cmsRun "$CFG" > pack.log 2>&1) ||
  { echo "packing failed: $OUT/pack.log"; exit 1; }
N=$(grep -c '^Begin processing the' "$OUT/pack.log")
[ "$N" -gt 0 ] || { echo "no events packed: $OUT/pack.log"; exit 1; }
PASSES=$(( (EV + N - 1) / N + 1 ))
SKIP=$(( EV / 20 ))

: > "$OUT/runs.tsv"
run() {
  local name=$1 flow=$2 what=$3 part=$4 s=$5 opts=$6
  echo "=== $name: $THREADS threads, $s streams, $opts"
  (cd "$OUT" && BENCH_FLOW=$part BENCH_OPTS="gapMode=$GAP $opts" BENCH_INPUT=file:$RAW BENCH_PASSES=$PASSES \
    python3 "$PATATRACK/benchmark" "$CFG" -j 1 -t $THREADS -s $s -e $EV --event-skip $SKIP -r $REPEATS \
    --no-input-benchmark --no-cpu-affinity --no-gpu-affinity --tmpdir "$OUT/tmp" -l "logs/$name" --csv "$name.csv" \
    > "$name.log" 2>&1)
  printf '%s\t%s\t%s\t%s\t%s\n' "$name" "$flow" "$what" "$s" $? >> "$OUT/runs.tsv"
}

for f in $FLOWS; do
  case $f in
    input) run input input "reading the raw data" input $THREADS accelerator=cpu ;;
    cpu) run cpu cpu RawToPixelProducer cpu $THREADS accelerator=cpu ;;
    alpaka-cpu) run alpaka-cpu alpaka-cpu "serial backend" alpaka $THREADS accelerator=cpu ;;
    gpu)
      if [ $GPUOK = 0 ]; then
        printf 'gpu\tgpu\tno %s device visible (%s)\t-\tSKIP\n' "${GPU#gpu-}" $PROBE >> "$OUT/runs.tsv"
        continue
      fi
      for m in $MODES; do
        perfed=1 perchip=1 twopass=0
        [ ${m%_*} = stage1flat ] && perfed=0
        [ ${m%2pass} != $m ] && twopass=1
        case ${m#*_} in stage2module*) perchip=0 ;; esac
        for b in $BLOCKS; do
          for s in $STREAMS; do
            run gpu_${m}_b${b}_s$s gpu "$m, blockSize $b" alpaka $s \
              "accelerator=$GPU blockSize=$b tablePerFed=$perfed decodePerChip=$perchip decodeTwoPass=$twopass"
          done
        done
      done ;;
  esac
done
rm -f "$RAW"
nvidia-smi > "$OUT/gpu_end.txt" 2>&1

python3 - "$OUT" "$EV" "$SKIP" "$THREADS" "$REPEATS" "$N" "$SRC" "$GAP" "$CPUNAME" "$GPUNAME" << 'EOF'
import csv, os, statistics, sys

out, ev, skip, threads, repeats, packed, src, gap, cpuname, gpuname = sys.argv[1:]
repeats = int(repeats)

rows = []
for line in open(out + '/runs.tsv'):
    name, flow, what, streams, rc = line.rstrip('\n').split('\t')
    rate = spread = None
    if rc == 'SKIP':
        rows.append([flow, '-', streams, None, None, 'SKIP ' + what])
        continue
    path = '%s/%s.csv' % (out, name)
    meas = list(csv.DictReader(open(path), skipinitialspace=True)) if os.path.exists(path) else []
    # the benchmark's own rule: measurements where the jobs overlap less than 90% don't count
    good = [float(m['average throughput (ev/s)']) for m in meas if float(m['overlap']) >= 0.9]
    note = ''
    if not good:
        note = 'FAIL, see %s.log and logs/%s' % (name, name)
    else:
        rate = statistics.mean(good)
        spread = statistics.stdev(good) if len(good) > 1 else None
        if len(good) < repeats:
            note = '%d of %d measurements' % (len(good), repeats)
    rows.append([flow, what, streams, rate, spread, note])

cpu = next((r[3] for r in rows if r[0] == 'cpu' and r[3]), None)
inp = next((r[3] for r in rows if r[0] == 'input' and r[3]), None)
table = []
for flow, what, streams, rate, spread, note in rows:
    vs = '%.1fx' % (rate / cpu) if rate and cpu and flow in ('alpaka-cpu', 'gpu') else ''
    if rate and inp and flow != 'input' and rate > 0.9 * inp:
        note = (note + '; ' if note else '') + 'near the input rate'
    table.append([flow, what, streams, '%.1f' % rate if rate else '-', '%.1f' % spread if spread else '-', vs, note])

print()
print('IT unpacker benchmark: %s events per measurement, the first %s left out, %s threads, %d measurements each'
      % (ev, skip, threads, repeats))
print('  raw data: %s events of %s, gapMode %s; CPU %s%s' % (packed, src, gap, cpuname, '; GPU ' + gpuname if gpuname else ''))
head = ['flow', 'what', 'streams', 'ev/s', 'spread', 'vs cpu', 'note']
width = [max(len(str(x)) for x in col) for col in zip(head, *table)]
for row in [head] + table:
    print('  ' + '  '.join(str(x).ljust(w) for x, w in zip(row, width)).rstrip())
print('logs: ' + out)
sys.exit(1 if any(r[6].startswith('FAIL') for r in table) else 0)
EOF
