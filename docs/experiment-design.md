# Experiment Design

## Objective

Systematically compare routing protocols across increasing network scales while controlling all non-routing variables.

## Independent Variables

| Variable | Values | Purpose |
|----------|--------|---------|
| Protocol | AODV, DSR, RPL (LLN proxy) | Primary comparison factor |
| Node count | 20, 50, 100, 200 | Scalability analysis |
| Seed | 1, 2, 3 | Statistical repeatability |

## Controlled Variables (defaults)

| Parameter | Default | Rationale |
|-----------|---------|-----------|
| `simTime` | 120 s | Sufficient for steady-state routing + energy drain |
| `packetSize` | 512 B | Typical IoT telemetry payload |
| `dataRate` | 32 kb/s | Moderate sensor reporting rate |
| `gridSize` | 250 m | Medium outdoor deployment |
| `distance` | 25 m | Multi-hop connectivity without overcrowding |
| `initialEnergyJ` | 10000 J | Allows observable depletion in 120 s |

## Dependent Variables (Metrics)

See [metrics-explanation.md](metrics-explanation.md).

## Experimental Matrix

Total runs per full batch:

```
3 protocols × 4 node counts × 3 seeds = 36 simulation runs
```

Estimated runtime (rough guide on 4-core laptop):

| Nodes | ~Time per run (AODV/DSR) | ~Time per run (RPL/LLN) |
|-------|--------------------------|-------------------------|
| 20 | 30–60 s | 60–120 s |
| 50 | 2–5 min | 5–10 min |
| 100 | 10–20 min | 15–30 min |
| 200 | 30–60 min | 45–90 min |

**Full batch may take several hours.** For presentation prep, run a reduced matrix first:

```bash
python3 scripts/batch_runner.py --protocol AODV DSR RPL --nodes 20 50 --seeds 1 2
```

## Hypotheses (example — adapt for your report)

1. **H1:** DSR exhibits higher routing overhead than AODV at scale due to source route headers in every packet.
2. **H2:** LLN (RPL proxy) achieves better energy fairness than WiFi MANET at equivalent node counts (different stack — compare cautiously).
3. **H3:** PDR decreases with node count for all protocols due to contention and congestion.

## Fair Comparison Notes

- **AODV vs DSR** — same WiFi stack, same energy model → directly comparable.
- **RPL vs AODV/DSR** — different PHY/MAC stacks → compare trends, not absolute values, unless you normalize by bits delivered per Joule.
- Report this limitation explicitly in your presentation.

## Output Artifacts

| File | Description |
|------|-------------|
| `results/csv/summary.csv` | One row per run, all headline metrics |
| `results/csv/summary_averaged.csv` | Mean/std across seeds |
| `results/csv/energy_*.csv` | Per-node energy at end of run |
| `results/csv/lifetime_*.csv` | Alive fraction over time |
| `results/csv/delay_*.csv` | Per-flow delay samples |
| `results/plots/*.png` | Matplotlib figures |
| `results/logs/*.log` | Console output per run |

## Presentation Workflow

1. Run reduced batch (20 + 50 nodes, 2 seeds)
2. Generate plots: `python3 scripts/plot_results.py`
3. Use `scalability_dashboard.png` as main slide figure
4. Discuss RPL limitation slide (see methodology.md)
