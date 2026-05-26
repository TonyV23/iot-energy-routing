# Energy-Efficient Routing Protocols in Large-Scale IoT Deployments

NS-3.41 research simulation for comparing **AODV**, **DSR**, and an **IoT LLN routing proxy (RPL mode)** in many-to-one sensor-to-gateway scenarios with energy-aware metrics.

> **Course context:** Advanced Data Communications and Computer Networks — university presentation & reproducible experimentation.

---

## 1. Project Overview

This project simulates a large-scale IoT deployment where many sensor nodes send UDP telemetry to a single gateway/sink over multi-hop wireless networks. It measures:

- Packet Delivery Ratio (PDR)
- Throughput
- End-to-end delay
- Routing overhead
- Network lifetime
- Energy consumption & fairness
- Scalability (20 → 200 nodes)

All results export to **CSV** and **matplotlib plots** for your report and presentation.

---

## 2. Research Objectives

1. Compare MANET routing (AODV, DSR) on WiFi ad-hoc vs LLN IoT stack (RPL mode)
2. Evaluate energy efficiency under realistic radio current models
3. Analyze scalability as node count increases
4. Provide reproducible, script-driven experiments for statistical averaging

---

## 3. Folder Structure

```
iot-energy-routing/
├── scratch/
│   ├── iot_energy_sim.cc      # Main NS-3 simulation
│   └── iot_sim_metrics.h      # Metrics collector (CSV export)
├── results/
│   ├── csv/                   # summary.csv + per-run timeseries
│   ├── plots/                 # Generated PNG figures
│   └── logs/                  # Per-run console logs
├── scripts/
│   ├── run_experiments.sh     # Full bash batch runner
│   ├── batch_runner.py        # Python batch + averaging
│   └── plot_results.py        # Matplotlib visualization
├── docs/
│   ├── methodology.md         # Academic methodology & RPL limitation
│   ├── experiment-design.md   # Variables, matrix, hypotheses
│   └── metrics-explanation.md # How each metric is computed
├── README.md
└── Makefile                   # NS-3 integration
```

---

## 4. Ubuntu 22.04 Prerequisites

```bash
sudo apt update
sudo apt install -y \
  build-essential cmake git python3 python3-pip \
  g++ libgtk-3-dev libsqlite3-dev libxml2 libxml2-dev \
  python3-pandas python3-matplotlib
```

Optional (faster builds): `ccache`, `ninja-build`

---

## 5. NS-3.41 Installation

```bash
cd ~
wget https://www.nsnam.org/releases/ns-allinone-3.41.tar.bz2
tar xjf ns-allinone-3.41.tar.bz2
cd ns-3.41
./ns3 configure --enable-examples --enable-tests
./ns3 build
```

Verify:

```bash
./ns3 run hello-simulator
```

---

## 6. Build This Project

Clone or copy this repository, then:

```bash
cd iot-energy-routing
export NS3_DIR=$HOME/ns-3.41    # adjust if different
make build
```

This copies `scratch/iot_energy_sim.cc` into your NS-3 tree and builds the simulator.

---

## 7. Run Examples

### Quick smoke test (20 nodes, 30 s)

```bash
make run
```

### Manual single run

```bash
$NS3_DIR/build/scratch/ns3.41-iot_energy_sim-default \
  --protocol=AODV \
  --nNodes=50 \
  --simTime=120 \
  --seed=1 \
  --outputDir=results/csv
```

### Compare all three protocols (same parameters)

```bash
for p in AODV DSR RPL; do
  $NS3_DIR/build/scratch/ns3.41-iot_energy_sim-default \
    --protocol=$p --nNodes=50 --simTime=120 --seed=1 \
    --outputDir=results/csv
done
```

---

## 8. Command-Line Parameters

| Parameter | Default | Description |
|-----------|---------|-------------|
| `--protocol` | AODV | `AODV`, `DSR`, or `RPL` |
| `--nNodes` | 50 | Total nodes (1 sink + N−1 sensors) |
| `--simTime` | 120 | Simulation duration (seconds) |
| `--packetSize` | 512 | UDP payload size (bytes) |
| `--dataRate` | 32kb/s | Sensor transmission rate |
| `--gridSize` | 250 | Deployment area (meters) |
| `--distance` | 25 | Grid spacing between nodes (meters) |
| `--seed` | 1 | RNG seed (reproducibility) |
| `--outputDir` | results/csv | CSV output directory |
| `--initialEnergyJ` | 10000 | Initial battery energy per node (J) |
| `--verbose` | false | Enable NS-3 debug logs |

---

## 9. Batch Experiment Execution

### Bash (full matrix: 3×4×3 = 36 runs)

```bash
chmod +x scripts/run_experiments.sh
./scripts/run_experiments.sh
```

Environment overrides:

```bash
SIM_TIME=60 SEEDS="1 2" ./scripts/run_experiments.sh   # edit script for custom seeds
```

### Python (with averaging)

```bash
python3 scripts/batch_runner.py --build
python3 scripts/batch_runner.py --nodes 20 50 --seeds 1 2 3
```

Dry run:

```bash
python3 scripts/batch_runner.py --dry-run
```

---

## 10. CSV Generation

After each run, metrics append to:

- **`results/csv/summary.csv`** — headline metrics (all runs)
- **`results/csv/summary_averaged.csv`** — mean/std across seeds (via batch_runner.py)
- **`results/csv/energy_<protocol>_n<N>_seed<S>.csv`** — per-node energy
- **`results/csv/lifetime_*.csv`** — alive fraction over time
- **`results/csv/delay_*.csv`** — delay samples

---

## 11. Plot Generation

```bash
python3 scripts/plot_results.py
# or with explicit paths:
python3 scripts/plot_results.py \
  --csv results/csv/summary.csv \
  --output results/plots
```

Generated plots:

- `pdr.png`, `throughput_kbps.png`, `avg_delay_ms.png`
- `routing_overhead_ratio.png`, `network_lifetime_s.png`
- `avg_energy_consumed_j.png`, `energy_fairness.png`
- `scalability_dashboard.png` (combined 2×2 figure for slides)

---

## 12. Troubleshooting

| Problem | Solution |
|---------|----------|
| `NS-3 not found at ~/ns-3.41` | Set `export NS3_DIR=/path/to/ns-3.41` |
| Build fails: missing modules | Run `./ns3 configure --enable-examples` in NS-3 |
| `ns3.41-iot_energy_sim-default` not found | Run `make build` from project root |
| RPL run crashes / no traffic | LLN stack needs multi-hop mesh-under; increase `simTime` or reduce `nNodes` for first test |
| Very slow 200-node runs | Normal for WiFi MANET; start with 20/50 nodes for presentation |
| Empty plots | Ensure `summary.csv` exists and has data rows |
| `matplotlib` import error | `sudo apt install python3-matplotlib python3-pandas` |

---

## 13. Expected Outputs

Console (example):

```
=== IoT Energy Routing Simulation Results ===
Protocol: AODV (WiFi 802.11b ad-hoc MANET)
Nodes: 50 (sensors: 49)
PDR: 0.87
Throughput (kbps): 12.4
Avg Delay (ms): 45.2
Routing Overhead Ratio: 0.31
First Node Death (s): 98.0
Network Lifetime 50% (s): -1
Energy Fairness (Jain): 0.92
CSV summary: results/csv/summary.csv
```

Files after batch:

```
results/csv/summary.csv
results/csv/summary_averaged.csv
results/plots/scalability_dashboard.png
results/logs/AODV_n50_seed1.batch.log
```

---

## 14. Academic Notes & Limitations

### RPL honesty statement

**Stock NS-3.41 does not ship with RFC 6550 RPL.** Our `--protocol=RPL` mode uses:

- IEEE 802.15.4 (LR-WPAN)
- 6LoWPAN compression
- Mesh-under multi-hop forwarding

Results are labeled **"RPL proxy"** in CSV output. For your report:

> "Due to the absence of native RPL in NS-3.41, we evaluate an LLN IoT stack (802.15.4 + 6LoWPAN mesh-under) as a structurally equivalent constrained-network baseline, rather than claiming full RPL DIO/DAO semantics."

See [docs/methodology.md](docs/methodology.md) for full justification.

### WiFi vs LLN comparison

AODV and DSR share the **same WiFi stack** → directly comparable.  
RPL mode uses a **different PHY/MAC** → compare trends and energy-per-bit, not raw throughput alone.

### Scalability limit (why not 500+ nodes?)

WiFi ad-hoc MANET simulations at 500+ nodes face:

1. Super-linear routing control traffic (AODV/DSR)
2. Unrealistic CSMA/CA contention modeling at that scale
3. Multi-hour simulation times on typical lab PCs

For 500+ node IoT studies, use Cooja/Contiki or distributed simulation — document this in your presentation.

### Energy model

WiFi currents are IoT-module order of magnitude (not chip-exact). LLN uses CC2420-class values. Cite NS-3 energy framework and datasheet references in your methodology section.

---

## 15. Quick Start for Teammates (Presentation Prep)

```bash
# 1. Install deps + NS-3 (sections 4–5 above)
# 2. Build project
cd iot-energy-routing && make build

# 3. Fast demo (6 runs, ~5–15 min)
python3 scripts/batch_runner.py --nodes 20 50 --seeds 1 2 --sim-time 60

# 4. Plots
python3 scripts/plot_results.py

# 5. Open results/plots/scalability_dashboard.png for slides
```

---

## License & Citation

Academic use — cite NS-3 (nsnam.org) and relevant RFCs (3561, 4728, 6550) in your report.

For questions about methodology, read `docs/` before modifying simulation parameters.
