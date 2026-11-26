#!/bin/bash
# Runs Phase2ITUnpackBench.sh once per GPU, one after another, with only that GPU visible and one stream on it. After
# its checks it goes on in the background under nohup, so the session can be closed. After cmsenv:
#   ./Phase2ITUnpackBenchGPUs.sh              the L4, then the L40, in benchgpus_<date>/ here: benchgpus.log with both
#                                             tables at the end, and each run's output in bench_<GPU>.out
#   ./Phase2ITUnpackBenchGPUs.sh L40          GPUs by their nvidia-smi name without "NVIDIA "
#   STREAMS=8 ./Phase2ITUnpackBenchGPUs.sh    streams on the GPU, default 1; the CPU flows keep one per thread
#   EVENTS=5000 ./Phase2ITUnpackBenchGPUs.sh  events per measurement; the benchmark's other options (FLOW, THREADS,
#                                             IN, ...) come from the environment as usual
# A GPU in use when its turn comes (GPU-Util above 5%, more than 500 MiB used, or a process on it) is left out.
set -u
[ -n "${CMSSW_BASE:-}" ] || { echo "run cmsenv first"; exit 1; }
BENCH=$CMSSW_BASE/src/EventFilter/Phase2PixelRawToDigi/test/Phase2ITUnpackBench.sh
[ -x "$BENCH" ] || { echo "no $BENCH: pull the branch"; exit 1; }
command -v nvidia-smi > /dev/null || { echo "nvidia-smi not found"; exit 1; }
if [ -z "${IN:-}" ]; then
  voms-proxy-info -exists -valid 2:00 > /dev/null 2>&1 ||
    { echo "the input is read over xrootd and the proxy has under 2 hours left: voms-proxy-init --voms cms"; exit 1; }
fi
NAMES=${*:-L4 L40}
declare -A UUID
for n in $NAMES; do
  u=$(nvidia-smi --query-gpu=name,uuid --format=csv,noheader | awk -F', ' -v m="NVIDIA $n" '$1 == m {print $2}')
  [ "$(echo "$u" | grep -c .)" = 1 ] || { echo "need one GPU named NVIDIA $n, found ${u:-none}"; exit 1; }
  UUID[$n]=$u
done

if [ -z "${BENCHGPUS_DIR:-}" ]; then
  export BENCHGPUS_DIR=$PWD/benchgpus_$(date +%Y%m%d_%H%M%S)
  mkdir -p "$BENCHGPUS_DIR" || exit 1
  nohup bash "$0" "$@" > "$BENCHGPUS_DIR/benchgpus.log" 2>&1 < /dev/null &
  echo "running in the background as process $!, so you can log out"
  echo "follow it with: tail -f $BENCHGPUS_DIR/benchgpus.log"
  exit 0
fi
cd "$BENCHGPUS_DIR" || exit 1
export STREAMS=${STREAMS:-1}

for n in $NAMES; do
  u=${UUID[$n]}
  IFS=', ' read -r util mem <<< "$(nvidia-smi -i $u --query-gpu=utilization.gpu,memory.used --format=csv,noheader,nounits)"
  procs=$(nvidia-smi -i $u --query-compute-apps=pid --format=csv,noheader | grep -c .)
  if [ "${util:-0}" -gt 5 ] 2> /dev/null || [ "${mem:-0}" -gt 500 ] 2> /dev/null || [ "$procs" -gt 0 ]; then
    echo "### $n ($u) in use: $util% GPU-Util, $mem MiB, $procs processes; not run, $(date)" | tee bench_$n.out
    continue
  fi
  echo "### $n ($u), streams $STREAMS, $(date)" | tee bench_$n.out
  CUDA_VISIBLE_DEVICES=$u cudaComputeCapabilities >> bench_$n.out 2>&1
  CUDA_VISIBLE_DEVICES=$u "$BENCH" ${EVENTS:-} >> bench_$n.out 2>&1
  echo "### $n exit $?, $(date)" | tee -a bench_$n.out
done

for n in $NAMES; do
  echo
  grep '^### ' bench_$n.out
  sed -n '/^IT unpacker benchmark/,/^logs: /p' bench_$n.out
done
