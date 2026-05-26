# Metrics Explanation

All metrics are computed from simulation traces at runtime. **No values are hardcoded.**

## 1. Packet Delivery Ratio (PDR)

```
PDR = packets_received_at_sink / packets_sent_by_sensors
```

- **Source:** Application-layer traces on `OnOffApplication` (Tx) and `PacketSink` (Rx)
- **Range:** [0, 1] — higher is better
- **Interpretation:** Measures reliability of data collection

## 2. Throughput

```
Throughput (kbps) = (total_data_bytes_received × 8) / simulation_time / 1000
```

- **Source:** Application-layer byte counters
- **Note:** Uses full `simTime` as denominator (conservative average over entire simulation)

## 3. End-to-End Delay

- **Source:** NS-3 `FlowMonitor` per-flow `delaySum / rxPackets`
- **Unit:** milliseconds (averaged across flows)
- **Interpretation:** Latency experienced by successfully delivered packets

## 4. Routing Overhead

```
Overhead Ratio = routing_control_packets_transmitted / data_packets_delivered
```

### WiFi (AODV)
Control packets identified by **UDP port 654** (AODV standard port).

### WiFi (DSR)
Non-application IP traffic counted as routing overhead (DSR encapsulation varies).

### LLN (RPL proxy)
6LoWPAN mesh-under **Tx** on relay nodes counted as routing overhead.

**Higher overhead** → more energy spent on control, less scalable.

## 5. Network Lifetime

Two definitions exported:

| Metric | Definition |
|--------|------------|
| `first_node_death_s` | Simulation time when first node energy ≤ 0 |
| `network_lifetime_s` | Time when alive fraction ≤ 50% |

If no node dies during simulation, values remain `-1` (report as "not reached").

## 6. Energy Consumption

- **Per-node initial energy:** `initialEnergyJ` parameter (default 10000 J)
- **Average consumed:** mean of `(initial - remaining)` across all nodes
- **WiFi path:** NS-3 energy framework tracks radio states
- **LLN path:** MAC trace-based accounting + idle drain

## 7. Energy Fairness (Jain's Index)

```
J(x₁, ..., xₙ) = (Σxᵢ)² / (n × Σxᵢ²)
```

Where `xᵢ` = energy consumed by node `i`.

- **Range:** [1/n, 1]
- **1.0** = perfectly fair (all nodes consumed equal energy)
- **Lower** = some nodes are heavily loaded (hotspots)

Important for IoT deployments where hotspot nodes die early and partition the network.

## 8. Scalability Analysis

Derived by plotting metrics vs `n_nodes` for each protocol:

- PDR vs nodes
- Throughput vs nodes
- Delay vs nodes
- Overhead vs nodes
- Lifetime vs nodes
- Energy vs nodes

Generated automatically by `scripts/plot_results.py`.

## CSV Column Reference (`summary.csv`)

| Column | Description |
|--------|-------------|
| `protocol` | AODV, DSR, or RPL |
| `stack` | Network stack description (includes RPL proxy disclaimer) |
| `n_nodes` | Total nodes including sink |
| `n_sensors` | n_nodes - 1 |
| `seed` | RNG seed |
| `pdr` | Packet delivery ratio |
| `throughput_kbps` | Average throughput |
| `avg_delay_ms` | Mean end-to-end delay |
| `routing_overhead_ratio` | Control/data packet ratio |
| `first_node_death_s` | First death time (-1 if none) |
| `network_lifetime_s` | 50% death time (-1 if none) |
| `avg_energy_consumed_j` | Mean energy consumed per node |
| `energy_fairness_index` | Jain fairness index |
| `final_alive_fraction` | Fraction of nodes alive at end |

## Reporting Tips for Your Course

1. Always cite **how** each metric is measured (this document).
2. Show **confidence intervals** using seed std dev from `summary_averaged.csv`.
3. Separate **WiFi MANET comparison** (AODV vs DSR) from **LLN discussion** (RPL proxy).
4. Include at least one **energy fairness** chart — it distinguishes energy-aware routing research.
