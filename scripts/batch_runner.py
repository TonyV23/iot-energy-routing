#!/usr/bin/env python3
"""
batch_runner.py — Python batch experiment orchestrator with averaging across seeds.

Usage:
  python3 scripts/batch_runner.py --ns3-dir ~/ns-3.41
  python3 scripts/batch_runner.py --protocol AODV --nodes 20 50 --seeds 1 2 3
"""

from __future__ import annotations

import argparse
import csv
import subprocess
import sys
from collections import defaultdict
from dataclasses import dataclass
from pathlib import Path
from statistics import mean, stdev
from typing import Dict, Iterable, List


@dataclass
class RunConfig:
    protocol: str
    n_nodes: int
    sim_time: float
    packet_size: int
    data_rate: str
    grid_size: float
    distance: float
    seed: int
    initial_energy_j: float
    output_dir: Path


METRIC_COLUMNS = [
    "pdr",
    "throughput_kbps",
    "avg_delay_ms",
    "routing_overhead_ratio",
    "first_node_death_s",
    "network_lifetime_s",
    "avg_energy_consumed_j",
    "energy_fairness_index",
    "final_alive_fraction",
]


def parse_args() -> argparse.Namespace:
    root = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description="IoT energy routing batch runner")
    parser.add_argument("--ns3-dir", type=Path, default=Path.home() / "ns-3.41")
    parser.add_argument("--project-dir", type=Path, default=root)
    parser.add_argument("--protocol", nargs="+", default=["AODV", "DSR", "RPL"])
    parser.add_argument("--nodes", nargs="+", type=int, default=[20, 50, 100, 200])
    parser.add_argument("--seeds", nargs="+", type=int, default=[1, 2, 3])
    parser.add_argument("--sim-time", type=float, default=120.0)
    parser.add_argument("--packet-size", type=int, default=512)
    parser.add_argument("--data-rate", default="32kb/s")
    parser.add_argument("--grid-size", type=float, default=250.0)
    parser.add_argument("--distance", type=float, default=25.0)
    parser.add_argument("--initial-energy-j", type=float, default=10000.0)
    parser.add_argument("--build", action="store_true", help="Run make build before experiments")
    parser.add_argument("--dry-run", action="store_true")
    return parser.parse_args()


def sim_binary(ns3_dir: Path) -> Path:
    return ns3_dir / "build/scratch/ns3.41-iot_energy_sim-default"


def build_project(project_dir: Path, ns3_dir: Path) -> None:
    subprocess.run(
        ["make", "-C", str(project_dir), f"NS3_DIR={ns3_dir}", "build"],
        check=True,
    )


def run_simulation(binary: Path, cfg: RunConfig) -> None:
    cmd = [
        str(binary),
        f"--protocol={cfg.protocol}",
        f"--nNodes={cfg.n_nodes}",
        f"--simTime={cfg.sim_time}",
        f"--packetSize={cfg.packet_size}",
        f"--dataRate={cfg.data_rate}",
        f"--gridSize={cfg.grid_size}",
        f"--distance={cfg.distance}",
        f"--seed={cfg.seed}",
        f"--initialEnergyJ={cfg.initial_energy_j}",
        f"--outputDir={cfg.output_dir}",
    ]
    print("Running:", " ".join(cmd))
    subprocess.run(cmd, cwd=cfg.output_dir.parent.parent, check=True)


def read_summary_rows(summary_path: Path) -> List[Dict[str, str]]:
    if not summary_path.exists():
        return []
    with summary_path.open(newline="") as handle:
        return list(csv.DictReader(handle))


def write_averages(rows: Iterable[Dict[str, str]], output_path: Path) -> None:
    grouped: Dict[tuple, List[Dict[str, str]]] = defaultdict(list)
    for row in rows:
        key = (row["protocol"], row["n_nodes"])
        grouped[key].append(row)

    output_path.parent.mkdir(parents=True, exist_ok=True)
    with output_path.open("w", newline="") as handle:
        fieldnames = ["protocol", "n_nodes", "num_runs"] + [
            f"{m}_mean" for m in METRIC_COLUMNS
        ] + [f"{m}_std" for m in METRIC_COLUMNS]
        writer = csv.DictWriter(handle, fieldnames=fieldnames)
        writer.writeheader()

        for (protocol, n_nodes), group in sorted(grouped.items(), key=lambda x: (x[0][0], int(x[0][1]))):
            out_row = {
                "protocol": protocol,
                "n_nodes": n_nodes,
                "num_runs": len(group),
            }
            for metric in METRIC_COLUMNS:
                values = [float(r[metric]) for r in group if r.get(metric) not in ("", "-1.000000")]
                if not values:
                    out_row[f"{metric}_mean"] = ""
                    out_row[f"{metric}_std"] = ""
                    continue
                out_row[f"{metric}_mean"] = f"{mean(values):.6f}"
                out_row[f"{metric}_std"] = f"{stdev(values):.6f}" if len(values) > 1 else "0.000000"
            writer.writerow(out_row)


def main() -> int:
    args = parse_args()
    project_dir = args.project_dir
    ns3_dir = args.ns3_dir
    csv_dir = project_dir / "results" / "csv"
    csv_dir.mkdir(parents=True, exist_ok=True)

    if args.build:
        build_project(project_dir, ns3_dir)

    binary = sim_binary(ns3_dir)
    if not binary.exists() and not args.dry_run:
        print(f"Simulator binary not found: {binary}", file=sys.stderr)
        print("Run: make build  OR  python3 scripts/batch_runner.py --build", file=sys.stderr)
        return 1

    for protocol in args.protocol:
        for n_nodes in args.nodes:
            for seed in args.seeds:
                cfg = RunConfig(
                    protocol=protocol,
                    n_nodes=n_nodes,
                    sim_time=args.sim_time,
                    packet_size=args.packet_size,
                    data_rate=args.data_rate,
                    grid_size=args.grid_size,
                    distance=args.distance,
                    seed=seed,
                    initial_energy_j=args.initial_energy_j,
                    output_dir=csv_dir,
                )
                if args.dry_run:
                    print(f"DRY RUN: {protocol} n={n_nodes} seed={seed}")
                    continue
                run_simulation(binary, cfg)

    summary_path = csv_dir / "summary.csv"
    avg_path = csv_dir / "summary_averaged.csv"
    rows = read_summary_rows(summary_path)
    if rows:
        write_averages(rows, avg_path)
        print(f"Wrote averaged metrics: {avg_path}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
