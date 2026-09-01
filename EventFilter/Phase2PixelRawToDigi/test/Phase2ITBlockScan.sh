#!/bin/bash
# Two measurements in one pass, for the GPU machine:
#
#   A. the flow comparison at the default block size -- legacy split, fused, and
#      the alpaka chain on both backends, so the GPU number has its references
#   B. a GPU kernel-occupancy scan -- the two stages swept separately, then the
#      best pair confirmed together
#
# Framework threads are pinned to 1 throughout. That is the whole point: this
# varies the work division *inside* one event's kernels, not the number of
# concurrent events. A threads>1 scan measures queue contention instead, which is
# what Phase2ITUnpackScan.sh is for.
#
#   ./Phase2ITBlockScan.sh              400 events per point
#   ./Phase2ITBlockScan.sh 200          fewer events per point
#   CHECK=0 ./Phase2ITBlockScan.sh      skip the per-size correctness runs
set -u

CFG=Phase2ITUnpackAlpaka_cfg.py
N=${1:-400}
BLOCKS=${BLOCKS:-"32 64 128 256 512 1024"}
DEFAULT=128
CHECK=${CHECK:-1}
GPU=${GPU:-gpu-nvidia}

S1=phase2ITRawToBitStream
S2=phase2ITBitStreamToPixel
MODULES="rawToBitStreamProducer bitstreamToPixelProducer rawToPixelProducer $S1 $S2"

OUT=blockscan_$(date +%Y%m%d_%H%M%S)
mkdir -p "$OUT"
CSV=$OUT/results.csv

# ---------------------------------------------------------------- input
# Same resolution as Phase2ITUnpackScan.sh: the sorted EOS list makes every point
# read the identical event sequence. Reading over xrootd adds seconds of variance
# and would swamp the effect being measured, so it is a last resort.
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
# report has to be complete and the event count has to be the one we asked for.
events_ran() {
  grep -q 'T---Report end!' "$1" || return 1
  local n; n=$(awk '/TrigReport Events total/{print $5; exit}' "$1")
  case "$n" in '' | *[!0-9]*) return 1 ;; esac
  [ "$n" -ge 1 ] || return 1
  echo "$n"
}

modtime() { awk -v m="$2" '$1=="TimeReport" && $4==m {print $2; exit}' "$1"; }

# run <tag> <backend> <bs1> <bs2>  -> appends one CSV row, echoes stage totals
run() {
  local tag=$1 be=$2 b1=$3 b2=$4
  local log=$OUT/${tag}_${be}_b${b1}-${b2}.log
  echo "=== $tag  $be  blockSize $b1/$b2  N=$N ==="
  cmsRun "$CFG" accelerator=$be threads=1 timing=1 maxEvents=$N \
         blockSize1=$b1 blockSize2=$b2 $IN > "$log" 2>&1
  local rc=$? ran
  if ! ran=$(events_ran "$log"); then
    echo "  REJECTED (no usable report; see $log)"
    echo "$tag,$be,$b1,$b2,$N,FAILED,,,,,,$rc" >> "$CSV"
    return 1
  fi
  # A short input silently caps every point at the same value, which looks like a
  # scan but is one measurement repeated. Say so rather than let it pass.
  [ "$ran" -lt "$N" ] && echo "  NOTE: asked for $N events, input supplied $ran"
  local row="$tag,$be,$b1,$b2,$N,$ran"
  for m in $MODULES; do row="$row,$(modtime "$log" "$m")"; done
  echo "$row,$rc" >> "$CSV"
  awk -v a="$(modtime "$log" $S1)" -v b="$(modtime "$log" $S2)" \
      'BEGIN{printf "  stage1 %.3f ms   stage2 %.3f ms   total %.3f ms\n", a*1e3, b*1e3, (a+b)*1e3}'
}

# correctness at one block-size pair; 2 events is enough to catch an indexing bug
check() {
  local be=$1 b1=$2 b2=$3
  local log=$OUT/check_${be}_b${b1}-${b2}.log
  cmsRun "$CFG" accelerator=$be maxEvents=2 gapMode=KEEP recovery=1 \
         blockSize1=$b1 blockSize2=$b2 $IN > "$log" 2>&1
  if grep -q 'exact round trip' "$log" && ! grep -q 'only-in [1-9]\|only-out [1-9]\|adc-differs [1-9]' "$log"; then
    echo "  check $be $b1/$b2: exact"
  else
    echo "  check $be $b1/$b2: *** MISMATCH *** see $log"
  fi
}

echo "tag,backend,blockSize1,blockSize2,maxEvents,eventsRun,$(echo $MODULES | tr ' ' ','),rc" > "$CSV"

# ---------------------------------------------------------------- A. flows
echo
echo "############ A. flow comparison at the default block size ############"
run flows cpu   $DEFAULT $DEFAULT
run flows $GPU  $DEFAULT $DEFAULT

# ---------------------------------------------------------------- B. occupancy
echo
echo "############ B. stage 2 (13k chips), stage 1 pinned at $DEFAULT ############"
for b in $BLOCKS; do run s2 $GPU $DEFAULT $b; [ "$CHECK" = 1 ] && check $GPU $DEFAULT $b; done

echo
echo "############ B. stage 1 (4k modules), stage 2 pinned at $DEFAULT ############"
for b in $BLOCKS; do run s1 $GPU $b $DEFAULT; done

# ---------------------------------------------------------------- C. best pair
best() {  # $1 = tag, $2 = module -> block size of the fastest row
  awk -F, -v t="$1" -v c="$2" 'NR==1{for(i=1;i<=NF;i++) if($i==c) k=i; next}
       $1==t && $6!="FAILED" && $k!="" {if(v==""||$k+0<v){v=$k+0; b=(t=="s1"?$3:$4)}} END{print (b==""?"'"$DEFAULT"'":b)}' "$CSV"
}
B1=$(best s1 $S1); B2=$(best s2 $S2)
echo
echo "############ C. best pair: stage1=$B1 stage2=$B2 ############"
run best $GPU $B1 $B2
[ "$CHECK" = 1 ] && check $GPU $B1 $B2

echo
echo "results: $CSV"
[ -n "$GPUNAME" ] && echo "GPU: $GPUNAME"
echo "default was $DEFAULT/$DEFAULT; compare the 'best' row against the 'flows' $GPU row."
echo "note: the measured run-to-run noise floor is ~1.3%, so ignore differences under ~3%."
