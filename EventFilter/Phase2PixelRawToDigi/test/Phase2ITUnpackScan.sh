#!/bin/bash
# Multiscale timing scan of the IT unpacker: events x threads x backend.
# Runs sequentially (timing runs must not share the machine) and collects the
# per-module TimeReport and the event summary into one CSV.
#
#   ./Phase2ITUnpackScan.sh                cpu + gpu-nvidia
#   ./Phase2ITUnpackScan.sh cpu            one backend only
set -u

CFG=Phase2ITUnpackAlpaka_cfg.py
EVENTS="20 50 100 200 400 600"
THREADS="1 2 4"
BACKENDS="${@:-cpu gpu-nvidia}"
MODULES="rawToBitStreamProducer bitstreamToPixelProducer rawToPixelProducer phase2ITRawToBitStream phase2ITBitStreamToPixel"

OUT=scan_$(date +%Y%m%d_%H%M%S)
mkdir -p "$OUT"
CSV=$OUT/results.csv

# Input: the skimmed sample on EOS. The sorted list makes every run read the
# identical event sequence, so all points of the scan are directly comparable.
EOSDIR=/eos/cms/store/group/phase2tracker/IT/skimmed_GEN-SIM-DIGI-RAW/CMSSW_14_1_0_pre3/RelValTTbar_14TeV/GEN-SIM-DIGI-RAW/PU_140X_mcRun4_realistic_v3_SpecialRV296_Run4D112-v1
EXTRA=""
SRC=xrootd
if [ -d "$EOSDIR" ]; then
  FILES=$(ls "$EOSDIR"/*.root 2>/dev/null | sort | sed 's|^|file:|' | paste -sd, -)
  if [ -n "$FILES" ]; then
    EXTRA="inputFiles=$FILES"
    SRC=eos
    echo "using $(echo "$FILES" | tr ',' '\n' | wc -l) files from $EOSDIR"
  fi
fi
if [ -z "$EXTRA" ]; then
  # fallback off lxplus: one local copy, or xrootd as a last resort
  REMOTE=root://cms-xrd-global.cern.ch//store/relval/CMSSW_14_1_0_pre3/RelValTTbar_14TeV/GEN-SIM-DIGI-RAW/PU_140X_mcRun4_realistic_v3_SpecialRV296_Run4D112-v1/2590000/0060c957-0236-4b79-abe3-8410dec69b26.root
  LOCAL=${TMPDIR:-/tmp}/itscan_input_$(id -u).root
  if [ -s "$LOCAL" ] || xrdcp -f "$REMOTE" "$LOCAL"; then
    EXTRA="inputFiles=file:$LOCAL"
    SRC=local
    echo "using local copy: $LOCAL"
  else
    SRC=xrootd
    echo "WARNING: reading over xrootd (check CPU/Real efficiency per run)"
  fi
fi

echo "backend,threads,maxEvents,eventsRun,$(echo $MODULES | tr ' ' ','),cpu_per_event,real_per_event,gpu_name,rc,input" > "$CSV"

for BE in $BACKENDS; do
  for T in $THREADS; do
    for N in $EVENTS; do
      LOG=$OUT/time_${BE}_t${T}_n${N}.log
      echo "=== $BE threads=$T maxEvents=$N ==="
      cmsRun "$CFG" accelerator=$BE threads=$T timing=1 maxEvents=$N $EXTRA > "$LOG" 2>&1
      RC=$?
      RUN=$(awk '/TrigReport Events total/{print $5; exit}' "$LOG")

      # A job can abort during service teardown - e.g. a filesystem error while ROOT
      # flushes an output file - long after the reports are printed, and those numbers
      # are perfectly good. So the report, not the exit code, decides. But the report
      # alone is not enough:
      #   - "TrigReport Events total" is the FIRST line of the summary, so a log cut off
      #     mid-summary would pass; require the closing marker instead.
      #   - a job that processed no event still prints a summary, and the framework
      #     divides per-event times by max(1, events), so those columns would be
      #     whole-job totals. This check must not depend on RC: an empty input gives
      #     zero events with a clean exit.
      #   - a job that died mid-loop reports a complete summary at a shorter length,
      #     with whatever slowdown caused the crash baked in.
      REJECT=""
      grep -q 'T---Report end!' "$LOG" || REJECT="incomplete report"
      case "$RUN" in '' | *[!0-9]*) REJECT="${REJECT:-no event count}" ;; esac
      if [ -z "$REJECT" ] && [ "$RUN" -lt 1 ]; then REJECT="zero events processed"; fi
      if [ -z "$REJECT" ] && [ "$RC" -ne 0 ] && [ "$RUN" -ne "$N" ]; then
        REJECT="died after $RUN of $N events"
      fi
      if [ -n "$REJECT" ]; then
        echo "    FAILED ($REJECT, exit $RC) - see $LOG"
        echo "$BE,$T,$N,FAILED,,,,,,,,,$RC,$SRC" >> "$CSV"
        continue
      fi
      if [ "$RC" -ne 0 ]; then
        echo "    note: exit $RC after a complete $RUN-event report - numbers kept"
      fi
      ROW="$BE,$T,$N,$RUN"
      for M in $MODULES; do
        V=$(awk -v m="$M" '$1=="TimeReport" && $4==m {print $2; exit}' "$LOG")
        ROW="$ROW,${V:-}"
      done
      CPUE=$(awk '/event loop CPU\/event/{print $NF; exit}' "$LOG")
      REALE=$(awk '/event loop Real\/event/{print $NF; exit}' "$LOG")
      GPU=$(grep -m1 "CUDA device" "$LOG" | sed 's/.*: //; s/,.*//' | tr -d ',')
      echo "$ROW,$CPUE,$REALE,${GPU:-},$RC,$SRC" >> "$CSV"
      echo "    events=$RUN  real/event=${REALE}s"
      if [ "$RUN" -lt "$N" ]; then
        echo "    input exhausted at $RUN events; skipping larger N for this setting"
        break
      fi
    done
  done
done

echo
echo "results: $CSV"
column -s, -t "$CSV"
