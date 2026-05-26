# Simulation Methodology

## Research Question

How do **AODV**, **DSR**, and an **IoT-oriented LLN routing approach** compare in terms of energy efficiency, delivery performance, and scalability in large many-to-one sensor deployments?

## Why Two Network Stacks?

| Protocol | Stack | Academic Justification |
|----------|-------|----------------------|
| **AODV** | WiFi 802.11 ad-hoc | Widely used MANET baseline in WSN/IoT gateway literature |
| **DSR** | WiFi 802.11 ad-hoc | Source-routing MANET baseline; contrasts with AODV on-demand distance-vector behavior |
| **RPL** | IEEE 802.15.4 + 6LoWPAN | Represents true constrained IoT LLN deployments |

### Critical Limitation: RPL in Stock NS-3.41

**The official NS-3.41 release does not include a native RPL (RFC 6550) implementation.** This is documented in NS-3 community discussions and release notes. We do **not** fabricate RPL DIO/DAO behavior or claim full RPL compliance.

Instead, the **RPL mode** uses:

- **IEEE 802.15.4** (`lr-wpan` module)
- **6LoWPAN** header compression (`sixlowpan` module)
- **Mesh-under flooding** toward the sink (`UseMeshUnder=true`)

This is an academically defensible **LLN IoT proxy** that:

1. Uses the correct physical/MAC layers for constrained IoT
2. Provides multi-hop many-to-one traffic toward a sink
3. Is explicitly labeled in CSV output and logs as **"RPL proxy — not native RPL"**

For a thesis requiring **full RPL**, integrate an external module (e.g., community RPL implementations) and replace the RPL code path — do not mislabel mesh-under results as RFC 6550 RPL.

## Topology

- **Grid deployment** in a square area (`gridSize` × `gridSize` meters)
- **Inter-node spacing** controlled by `distance`
- **Node 0** = gateway/sink (PacketSink only)
- **Nodes 1..N-1** = IoT sensors (OnOff UDP traffic)

This many-to-one pattern matches standard IoT data collection scenarios.

## Traffic Model

- **UDP** application traffic (lightweight, common in IoT telemetry)
- Configurable **packet size** and **data rate**
- Staggered start times to reduce synchronization artifacts

## Energy Model

### WiFi (AODV / DSR)

- NS-3 **BasicEnergySource** + **WifiRadioEnergyModel**
- Current values tuned for IoT-class WiFi modules (ESP8266 order of magnitude):
  - TX: 170 mA
  - RX: 60 mA
  - Idle: 15 mA
- Supply voltage: 3.0 V (NS-3 default in BasicEnergySource)

### LLN (RPL proxy)

- **CC2420-class** 802.15.4 current values (datasheet order of magnitude)
- TX: 17.4 mA, RX: 19.7 mA, idle: 0.42 mA @ 3.0 V
- Manual energy accounting via MAC Tx/Rx traces (no native LrWpanRadioEnergyModel in all builds)

## Network Lifetime Definition

Two metrics are reported:

1. **First node death** — time when any node's remaining energy reaches zero
2. **Network lifetime (50%)** — time when fewer than 50% of nodes remain alive

The 50% threshold follows common WSN lifetime literature (e.g., network partition studies).

## Reproducibility

- Fixed **RNG seed** via `--seed` and `RngSeedManager`
- All parameters exposed on command line
- CSV append mode preserves batch run history
- Batch scripts use seeds `{1, 2, 3}` with averaging in `batch_runner.py`

## Scalability Scope

Experiments target **20, 50, 100, 200 nodes**. We intentionally exclude 500+ nodes on WiFi MANET because:

1. **Simulation complexity** — AODV/DSR control traffic grows super-linearly
2. **WiFi scalability** — ad-hoc CSMA/CA does not model large-scale IoT deployments realistically
3. **Runtime** — 500-node WiFi MANET simulations may require hours per run on typical lab hardware

For 500+ node studies, use dedicated LLN simulators (Cooja/Contiki) or distributed NS-3 execution.

## References (for your report)

- Perkins, C. E., et al. "Ad hoc On-Demand Distance Vector (AODV) Routing." RFC 3561.
- Johnson, D. B., et al. "The Dynamic Source Routing Protocol (DSR)." RFC 4728.
- Winter, T., et al. "RPL: IPv6 Routing Protocol for Low-Power and Lossy Networks." RFC 6550.
- NS-3 Energy Framework documentation (ns-3.41 release notes).
- NS-3 6LoWPAN module documentation.
