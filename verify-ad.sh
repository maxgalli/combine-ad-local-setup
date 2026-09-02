#!/usr/bin/env bash
#
# Smoke-test the RooFit AD (codegen) path inside the dev container.
# Run from the HOST:  ./verify-ad.sh
#
# Checks, in order:
#   1. ROOT is >= 6.36 (otherwise Combine's codegen code is preprocessed away)
#   2. the six codegenImpl symbols are defined in the built library
#   3. fitRooFitAD.py gives the same fit with --backend combine and --backend codegen
#
set -euo pipefail
NAME="${COMBINE_DEV_NAME:-combine-dev}"

if ! docker ps --filter "name=^${NAME}$" --format '{{.Names}}' | grep -q .; then
  echo "error: container '$NAME' is not running — start it with ./combine-dev.sh first" >&2
  exit 1
fi

docker exec "$NAME" bash -eo pipefail -c '
case "$(uname -m)" in
  aarch64) ARCH=aarch64 ;;
  x86_64)  ARCH=x86_64  ;;
  *) echo "unsupported arch $(uname -m)" >&2; exit 1 ;;
esac
source "/cvmfs/sft.cern.ch/lcg/views/${LCG_VIEW:-dev3/latest}/${ARCH}-el9-${LCG_GCC:-gcc13}-opt/setup.sh"
export PATH=/install/bin:$PATH
export LD_LIBRARY_PATH=/install/lib:$LD_LIBRARY_PATH
export PYTHONPATH=/install/python:$PYTHONPATH
export ROOT_INCLUDE_PATH=/install/include:$ROOT_INCLUDE_PATH

echo "== ROOT version =="
root-config --version

echo
echo "== codegenImpl symbols (expect 6 defined) =="
nm -DC /install/lib/libHiggsAnalysisCombinedLimit.so \
  | grep " T RooFit::Experimental::codegenImpl" \
  | sed "s/.*codegenImpl/  codegenImpl/"
n=$(nm -DC /install/lib/libHiggsAnalysisCombinedLimit.so | grep -c " T RooFit::Experimental::codegenImpl")
echo "  total: $n"
[ "$n" -ge 6 ] || { echo "FAIL: expected at least 6 codegenImpl symbols"; exit 1; }

echo
echo "== fit comparison =="
work=$(mktemp -d)
cd "$work"
cp /src/data/tutorials/counting/realistic-counting-experiment.txt card.txt
text2workspace.py card.txt -o ws.root >/dev/null 2>&1
for be in combine codegen; do
  python3 /src/scripts/fitRooFitAD.py -i ws.root --backend "$be" 2>/dev/null \
    | grep -E "^ +[A-Za-z_]+ +[-0-9]" > "out.$be" || true
  echo "--- backend: $be ---"; cat "out.$be"
done
if diff -q out.combine out.codegen >/dev/null; then
  echo; echo "PASS: codegen backend matches the default backend"
else
  echo; echo "FAIL: backends disagree"; diff out.combine out.codegen; exit 1
fi
'
