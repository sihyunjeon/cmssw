#!/bin/bash
# Two measurements in one pass, for the GPU machine:
#
#   A. the flow comparison -- legacy split, fused, and the alpaka chain on both
#      backends, so the GPU number has its references
#   B. a GPU kernel-occupancy sweep -- one block size applied to both stages
#
# Framework threads are pinned to 1 throughout. That is the whole point: this
# varies the work division *inside* one event's kernels, not the number of
# concurrent events. A threads>1 scan measures queue contention instead, which is
# what Phase2ITUnpackScan.sh is for -- and it is also why syncForTiming must not
# be combined with threads>1: concurrent events would serialize on the queue.
#
# syncForTiming is on by default here and is required for the GPU points to mean
# anything: the fill kernels are launched asynchronously, so without it TimeReport
# charges the module only the enqueue and stage 2 reads as ~0.1 ms.
#
#   ./Phase2ITBlockScan.sh              400 events per point
#   ./Phase2ITBlockScan.sh 200          fewer events per point
#   BLOCKS="128 256" ./Phase2ITBlockScan.sh
#   CHECK=0 ./Phase2ITBlockScan.sh      skip the per-size correctness runs
#   SYNC=0  ./Phase2ITBlockScan.sh      no timing sync (GPU numbers become meaningless)
set -u

CFG=Phase2ITUnpackAlpaka_cfg.py
N=${1:-400}
BLOCKS=${BLOCKS:-"64 128 512 1024"}
DEFAULT=128
CHECK=${CHECK:-1}
SYNC=${SYNC:-1}
GPU=${GPU:-gpu-nvidia}

S1=phase2ITRawToBitStream
S2=phase2ITBitStreamToPixel
MODULES="rawToBitStreamProducer bitstreamToPixelProducer rawToPixelProducer $S1 $S2"

OUT=blockscan_$(date +%Y%m%d_%H%M%S)
mkdir -p "$OUT"
CSV=$OUT/results.csv

# ---------------------------------------------------------------- input
# The sorted EOS list makes every point read the identical event sequence.
# Reading over xrootd adds seconds of variance and would swamp the effect being
# measured, so it is a last resort.
EOSDIR=/eos/cms/store/group/phase2tracker/IT/skimmed_GEN-SIM-DIGI-RAW/CMSSW_14_1_0_pre3/RelValTTbar_14TeV/GEN-SIM-DIGI-RAW/PU_140X_mcRun4_realistic_v3_SpecialRV296_Run4D112-v1
IN=""
if [ -d "$EOSDIR" ]; then
  F=$(ls "$EOSDIR"/*.root 2>/dev/null | sort | sed 's|^|file:|' | paste -sd, -)
  [ -n "$F" ] && IN="inputFiles=$F" && echo "input: $(echo "$F" | tr ',' '\n' | wc -l) files from EOS"
fi
if [ -z "$IN" ]; then
  REMOTE=root://cms-xrd-global.cern.ch//store/relval/CMSSW_14_1_0_pre3/RelValTTbar_14TeV/GEN-SIM-DIGI-RAW/PU_140X_mcRun4_realistic_v3_SpecialRV296_Run4D112-v1/2590000/0060c957-0236-4b79-abe3-8410dec69b26.root
  LOCAL=${TMPDIR:-/tmp}/itscan_input_$(id -u).root
  if [ -s "$LOCAL" ] || xrdcp -f "$REMOTE" "$LOCAL"; then
    IN="inputFiles=file:$LOCAL"; echo "input: local copy $LOCAL"
  else
    echo "WARNING: falling back to xrootd; timings will be noisy"
  fi
fi

GPUNAME=$(nvidia-smi --query-gpu=name --format=csv,noheader 2>/dev/null | head -1)
[ -n "$GPUNAME" ] || echo "WARNING: no nvidia-smi; the $GPU points will fail if no device is visible"

# ---------------------------------------------------------------- helpers
# A job can abort during service teardown long after the reports are printed, and
# those numbers are good. So the report decides, not the exit code -- but the
# report has to be complete and the event count has to be real.
events_ran() {
  grep -q 'T---Report end!' "$1" || return 1
  local n; n=$(awk '/TrigReport Events total/{print $5; exit}' "$1")
  case "$n" in '' | *[!0-9]*) return 1 ;; esac
  [ "$n" -ge 1 ] || return 1
  echo "$n"
}

modtime() { awk -v m="$2" '$1=="TimeReport" && $4==m {print $2; exit}' "$1"; }

# run <tag> <backend> <blockSize>
run() {
  local tag=$1 be=$2 bs=$3
  local log=$OUT/${tag}_${be}_b${bs}.log
  echo "=== $tag  $be  blockSize $bs  N=$N  sync=$SYNC ==="
  cmsRun "$CFG" accelerator=$be threads=1 timing=1 maxEvents=$N \
         blockSize=$bs syncForTiming=$SYNC $IN > "$log" 2>&1
  local rc=$? ran
  if ! ran=$(events_ran "$log"); then
    echo "  REJECTED (no usable report; see $log)"
    echo "$tag,$be,$bs,$N,FAILED,,,,,,$rc" >> "$CSV"
    return 1
  fi
  # A short input silently caps every point at the same value, which looks like a
  # scan but is one measurement repeated. Say so rather than let it pass.
  [ "$ran" -lt "$N" ] && echo "  NOTE: asked for $N events, input supplied $ran"
  local row="$tag,$be,$bs,$N,$ran"
  for m in $MODULES; do row="$row,$(modtime "$log" "$m")"; done
  echo "$row,$rc" >> "$CSV"
  awk -v a="$(modtime "$log" $S1)" -v b="$(modtime "$log" $S2)" \
      'BEGIN{printf "  stage1 %.3f ms   stage2 %.3f ms   total %.3f ms\n", a*1e3, b*1e3, (a+b)*1e3}'
}

# correctness at one block size; 2 events is enough to catch an indexing bug
check() {
  local be=$1 bs=$2
  local log=$OUT/check_${be}_b${bs}.log
  cmsRun "$CFG" accelerator=$be maxEvents=2 gapMode=KEEP recovery=1 blockSize=$bs $IN > "$log" 2>&1
  if grep -q 'exact round trip' "$log" && ! grep -q 'only-in [1-9]\|only-out [1-9]\|adc-differs [1-9]' "$log"; then
    echo "  check bs=$bs: exact"
  else
    echo "  check bs=$bs: *** MISMATCH *** see $log"
  fi
}

echo "tag,backend,blockSize,maxEvents,eventsRun,$(echo $MODULES | tr ' ' ','),rc" > "$CSV"

# ---------------------------------------------------------------- A. flows
echo
echo "############ A. flow comparison at blockSize $DEFAULT ############"
run flows cpu  $DEFAULT
run flows $GPU $DEFAULT

# ---------------------------------------------------------------- B. sweep
echo
echo "############ B. GPU block size sweep (both stages) ############"
for b in $BLOCKS; do
  run bs $GPU $b
  [ "$CHECK" = 1 ] && check $GPU $b
done

# ---------------------------------------------------------------- summary
echo
echo "############ summary: alpaka total per block size ############"
awk -F, -v s1="$S1" -v s2="$S2" '
  NR==1 { for (i=1;i<=NF;i++) { if ($i==s1) a=i; if ($i==s2) b=i } ; next }
  $1=="bs" && $5!="FAILED" && $a!="" && $b!="" {
    t=($a+$b)*1000; printf "  blockSize %-5s %8.3f ms\n", $3, t
    if (lo=="" || t<lo) { lo=t; best=$3 }
    if (hi=="" || t>hi) { hi=t } }
  END {
    if (best=="") { print "  no usable points"; exit }
    # A winner picked out of a spread this small is picking noise, not an optimum.
    if (lo>0 && (hi-lo)/lo < 0.03)
      printf "  -> all within %.1f%%: no measurable optimum, keep the default\n", (hi-lo)/lo*100
    else
      printf "  -> fastest: blockSize %s at %.3f ms (spread %.1f%%)\n", best, lo, (hi-lo)/lo*100
  }' "$CSV"

echo
echo "results: $CSV"
[ -n "$GPUNAME" ] && echo "GPU: $GPUNAME"
echo "note: measured run-to-run noise is ~1.3%, so ignore differences under ~3%."
[ "$SYNC" = 1 ] || echo "WARNING: SYNC=0 -- the GPU stage numbers are enqueue time, not kernel time."
