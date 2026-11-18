#!/bin/bash
# Runs Phase2ITUnpackTest.sh, then an Nsight Systems profile of the one-pass and the two-pass decode, in a CERN batch
# job that gets a GPU from HTCondor. Submit from lxplus after cmsenv and voms-proxy-init --voms cms; the CMSSW area
# must be on AFS, where the job reads it.
#
#   ./Phase2ITUnpackBatch.sh                  a T4 (compute capability 7.5), 100 events
#   ./Phase2ITUnpackBatch.sh 20               fewer events
#   GPUCAP=8.0 ./Phase2ITUnpackBatch.sh       another GPU model by its capability; GPUCAP= takes any GPU
#   MODE=... BLOCK=... ./Phase2ITUnpackBatch.sh   passed to the test script, default both decodes in both layouts at 32 and 64
#   FLAVOUR=nextweek ./Phase2ITUnpackBatch.sh     job flavour, default longlunch (2 hours)
#
# The job's files come back to unpackbatch/ in the current directory:
#   job<id>.out    the GPU's state at the start, the test table, and the kernel tables of the two profiles
#   job<id>/       the test logs, the nsys reports, and nvidia-smi at the start, every 5 s (dmon.txt) and at the end
# The proxy is copied to ~/private, where HTCondor can pick it up for the job.
set -u

if [ "${1:-}" = run ]; then
  res=$PWD/job$2
  mkdir -p "$res"
  export X509_USER_PROXY=$PWD/$PROXY
  chmod 600 "$X509_USER_PROXY"
  echo "== $(hostname), $(date)"
  nvidia-smi --query-gpu=index,name,utilization.gpu,memory.used,temperature.gpu,clocks.sm --format=csv 2>&1
  { echo "CUDA_VISIBLE_DEVICES=${CUDA_VISIBLE_DEVICES:-} _CONDOR_AssignedGPUs=${_CONDOR_AssignedGPUs:-}"; nvidia-smi; } > "$res/start.txt" 2>&1
  nvidia-smi dmon -s pucm -d 5 -o T > "$res/dmon.txt" 2>&1 &
  dmon=$!
  trap 'kill $dmon 2> /dev/null' EXIT

  source /cvmfs/cms.cern.ch/cmsset_default.sh
  cd "$CMSSW_BASE/src" && eval "$(scram runtime -sh)" && cd - > /dev/null || { echo "cmsenv failed in $CMSSW_BASE"; exit 1; }
  TEST=$CMSSW_BASE/src/EventFilter/Phase2PixelRawToDigi/test
  CFG=$TEST/PixelDigiToRawAlpakaProducer_cfg.py
  # the cfg's default input, given explicitly so that the test script doesn't need voms-proxy-info on the node
  if [ -z "${IN:-}" ]; then
    IN=$(python3 - "$CFG" << 'EOF' | sed -n 's/^INPUT //p'
import runpy, sys
sys.argv = sys.argv[1:]
print('INPUT', ','.join(runpy.run_path(sys.argv[0])['process'].source.fileNames))
EOF
)
  fi
  export IN

  "$TEST/Phase2ITUnpackTest.sh" "$EVENTS"
  mv unpacktest_* "$res/"

  NSYS=/cvmfs/projects.cern.ch/lcg/releases/cuda/13.5.0/x86_64-linux/nsight-systems-2026.5.2/bin/nsys
  if [ -x "$NSYS" ]; then
    for tp in 0 1; do
      rep=$res/it_tp${tp}_b64
      "$NSYS" profile -t cuda --sample=none --cpuctxsw=none -o "$rep" cmsRun "$CFG" accelerator=gpu-nvidia gapMode=KEEP \
        maxEvents=$EVENTS timing=1 blockSize=64 syncForTiming=1 decodeTwoPass=$tp ${IN:+inputFiles=$IN} > "$rep.log" 2>&1
      echo "=== nsys, blockSize 64, decodeTwoPass=$tp (exit $?)"
      "$NSYS" stats -r cuda_gpu_kern_sum "$rep.nsys-rep" 2>&1 |
        sed -E 's/void alpaka::detail::gpuKernel<([^,<>]*::)?([A-Za-z0-9_]+)[,>].*/\2/'
      rm -f "$rep.sqlite"
    done
  else
    echo "no nsys on $(hostname) ($NSYS), so no profiles"
  fi
  { date; nvidia-smi; } > "$res/end.txt" 2>&1
  exit 0
fi

EVENTS=${1:-100}
GPUCAP=${GPUCAP-7.5}
MODE=${MODE:-stage1fed_stage2chip stage1fed_stage2chip2pass stage1fed_stage2module stage1fed_stage2module2pass}
BLOCK=${BLOCK:-32 64}
FLAVOUR=${FLAVOUR:-longlunch}
case $EVENTS in '' | *[!0-9]*) echo "usage: $0 [events]"; exit 1 ;; esac
command -v condor_submit > /dev/null || { echo "condor_submit not found: submit from lxplus"; exit 1; }
[ -n "${CMSSW_BASE:-}" ] || { echo "run cmsenv first"; exit 1; }
case $CMSSW_BASE in /afs/*) ;; *) echo "the job reads the CMSSW area, so it must be on AFS: $CMSSW_BASE"; exit 1 ;; esac
voms-proxy-info -exists -valid 3:00 > /dev/null 2>&1 ||
  { echo "the job reads its input over xrootd and the proxy has under 3 hours left: voms-proxy-init --voms cms"; exit 1; }
[ -d "$HOME/private" ] || { echo "no $HOME/private to keep the proxy in"; exit 1; }
PROXY=$HOME/private/x509up_phase2itunpack
install -m 600 "${X509_USER_PROXY:-/tmp/x509up_u$(id -u)}" "$PROXY" || exit 1

SELF=$(cd "$(dirname "$0")" && pwd)/$(basename "$0")
DIR=$PWD/unpackbatch
mkdir -p "$DIR"
cat > "$DIR/job.sub" << EOF
executable              = $SELF
arguments               = run \$(ClusterId)
environment             = "CMSSW_BASE=$CMSSW_BASE EVENTS=$EVENTS MODE='$MODE' BLOCK='$BLOCK' PROXY=$(basename "$PROXY")"
transfer_input_files    = $PROXY
should_transfer_files   = YES
when_to_transfer_output = ON_EXIT
transfer_output_files   = job\$(ClusterId)
output                  = job\$(ClusterId).out
error                   = job\$(ClusterId).err
log                     = job\$(ClusterId).log
request_gpus            = 1
$([ -n "$GPUCAP" ] && printf 'gpus_minimum_capability = %s\ngpus_maximum_capability = %s' "$GPUCAP" "$GPUCAP")
request_cpus            = 2
MY.WantOS               = "el9"
+JobFlavour             = "$FLAVOUR"
queue
EOF
id=$(cd "$DIR" && condor_submit -terse job.sub) || exit 1
id=${id%%.*}
echo "submitted job $id; follow it with condor_q $id"
echo "when it ends: $DIR/job$id.out, with the logs and profiles in $DIR/job$id/"
