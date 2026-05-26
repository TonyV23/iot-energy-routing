#!/usr/bin/env bash
# run_experiments.sh — reproducible batch execution for IoT energy routing study
set -euo pipefail

PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
NS3_DIR="${NS3_DIR:-$HOME/ns-3.41}"
SIM_BIN="${NS3_DIR}/build/scratch/ns3.41-iot_energy_sim-default"
CSV_DIR="${PROJECT_DIR}/results/csv"
LOG_DIR="${PROJECT_DIR}/results/logs"

PROTOCOLS=(AODV DSR RPL)
NODE_COUNTS=(20 50 100 200)
SEEDS=(1 2 3)
SIM_TIME="${SIM_TIME:-120}"
PACKET_SIZE="${PACKET_SIZE:-512}"
DATA_RATE="${DATA_RATE:-32kb/s}"
GRID_SIZE="${GRID_SIZE:-250}"
DISTANCE="${DISTANCE:-25}"
INITIAL_ENERGY="${INITIAL_ENERGY:-10000}"

mkdir -p "${CSV_DIR}" "${LOG_DIR}" "${PROJECT_DIR}/results/plots"

echo "=== IoT Energy Routing Batch Experiments ==="
echo "Project: ${PROJECT_DIR}"
echo "NS-3:    ${NS3_DIR}"
echo "Sim time: ${SIM_TIME}s | Seeds: ${SEEDS[*]}"

if [[ ! -x "${SIM_BIN}" ]]; then
  echo "Simulator not built. Running make build..."
  make -C "${PROJECT_DIR}" NS3_DIR="${NS3_DIR}" build
fi

SUMMARY_BACKUP="${CSV_DIR}/summary.csv.bak.$(date +%Y%m%d_%H%M%S)"
if [[ -f "${CSV_DIR}/summary.csv" ]]; then
  cp "${CSV_DIR}/summary.csv" "${SUMMARY_BACKUP}"
  echo "Backed up existing summary.csv to ${SUMMARY_BACKUP}"
fi

RUN_COUNT=0
for protocol in "${PROTOCOLS[@]}"; do
  for nNodes in "${NODE_COUNTS[@]}"; do
    for seed in "${SEEDS[@]}"; do
      RUN_COUNT=$((RUN_COUNT + 1))
      TAG="${protocol}_n${nNodes}_seed${seed}"
      LOG_FILE="${LOG_DIR}/${TAG}.batch.log"

      echo ""
      echo "[${RUN_COUNT}] ${TAG} (simTime=${SIM_TIME}s)"

      cd "${PROJECT_DIR}"
      "${SIM_BIN}" \
        --protocol="${protocol}" \
        --nNodes="${nNodes}" \
        --simTime="${SIM_TIME}" \
        --packetSize="${PACKET_SIZE}" \
        --dataRate="${DATA_RATE}" \
        --gridSize="${GRID_SIZE}" \
        --distance="${DISTANCE}" \
        --seed="${seed}" \
        --initialEnergyJ="${INITIAL_ENERGY}" \
        --outputDir="${CSV_DIR}" \
        2>&1 | tee "${LOG_FILE}"
    done
  done
done

echo ""
echo "=== Batch complete (${RUN_COUNT} runs) ==="
echo "Summary CSV: ${CSV_DIR}/summary.csv"

if command -v python3 >/dev/null 2>&1; then
  echo "Generating plots..."
  python3 "${PROJECT_DIR}/scripts/plot_results.py" \
    --csv "${CSV_DIR}/summary.csv" \
    --output "${PROJECT_DIR}/results/plots"
else
  echo "python3 not found — skip plotting (install python3 + matplotlib + pandas)"
fi
