#!/usr/bin/env python3
"""
plot_results.py — generate publication-style plots from summary.csv

Requires: pandas, matplotlib
  sudo apt install python3-pandas python3-matplotlib
"""

from __future__ import annotations

import argparse
from pathlib import Path

import matplotlib.pyplot as plt
import pandas as pd


def parse_args() -> argparse.Namespace:
    root = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description="Plot IoT energy routing results")
    parser.add_argument("--csv", type=Path, default=root / "results/csv/summary.csv")
    parser.add_argument("--output", type=Path, default=root / "results/plots")
    parser.add_argument("--averaged", type=Path, default=None, help="Optional averaged CSV")
    return parser.parse_args()


def load_data(csv_path: Path, averaged_path: Path | None) -> pd.DataFrame:
    if averaged_path and averaged_path.exists():
        df = pd.read_csv(averaged_path)
        return df

    df = pd.read_csv(csv_path)
    numeric_cols = [
        "n_nodes",
        "pdr",
        "throughput_kbps",
        "avg_delay_ms",
        "routing_overhead_ratio",
        "network_lifetime_s",
        "avg_energy_consumed_j",
        "energy_fairness_index",
    ]
    for col in numeric_cols:
        if col in df.columns:
            df[col] = pd.to_numeric(df[col], errors="coerce")

    grouped = (
        df.groupby(["protocol", "n_nodes"], as_index=False)
        .agg(
            pdr=("pdr", "mean"),
            throughput_kbps=("throughput_kbps", "mean"),
            avg_delay_ms=("avg_delay_ms", "mean"),
            routing_overhead_ratio=("routing_overhead_ratio", "mean"),
            network_lifetime_s=("network_lifetime_s", "mean"),
            avg_energy_consumed_j=("avg_energy_consumed_j", "mean"),
            energy_fairness_index=("energy_fairness_index", "mean"),
        )
        .sort_values(["protocol", "n_nodes"])
    )
    return grouped


def plot_metric(df: pd.DataFrame, metric: str, ylabel: str, output_dir: Path) -> None:
    plt.figure(figsize=(8, 5))
    for protocol in sorted(df["protocol"].unique()):
        subset = df[df["protocol"] == protocol].sort_values("n_nodes")
        plt.plot(subset["n_nodes"], subset[metric], marker="o", linewidth=2, label=protocol)

    plt.xlabel("Number of Nodes")
    plt.ylabel(ylabel)
    plt.title(f"{ylabel} vs Network Scale")
    plt.grid(True, alpha=0.3)
    plt.legend()
    plt.tight_layout()
    out = output_dir / f"{metric}.png"
    plt.savefig(out, dpi=150)
    plt.close()
    print(f"Wrote {out}")


def plot_energy_fairness(df: pd.DataFrame, output_dir: Path) -> None:
    plt.figure(figsize=(8, 5))
    pivot = df.pivot(index="n_nodes", columns="protocol", values="energy_fairness_index")
    pivot.plot(kind="bar", rot=0, ax=plt.gca())
    plt.ylabel("Jain Fairness Index")
    plt.xlabel("Number of Nodes")
    plt.title("Energy Consumption Fairness Across Protocols")
    plt.grid(True, axis="y", alpha=0.3)
    plt.tight_layout()
    out = output_dir / "energy_fairness.png"
    plt.savefig(out, dpi=150)
    plt.close()
    print(f"Wrote {out}")


def main() -> None:
    args = parse_args()
    if not args.csv.exists():
        raise SystemExit(f"CSV not found: {args.csv}. Run experiments first.")

    args.output.mkdir(parents=True, exist_ok=True)
    df = load_data(args.csv, args.averaged)

    plot_metric(df, "pdr", "Packet Delivery Ratio", args.output)
    plot_metric(df, "throughput_kbps", "Throughput (kbps)", args.output)
    plot_metric(df, "avg_delay_ms", "Average End-to-End Delay (ms)", args.output)
    plot_metric(df, "routing_overhead_ratio", "Routing Overhead Ratio", args.output)
    plot_metric(df, "network_lifetime_s", "Network Lifetime — 50% nodes dead (s)", args.output)
    plot_metric(df, "avg_energy_consumed_j", "Average Energy Consumed (J)", args.output)
    plot_energy_fairness(df, args.output)

    # Combined scalability dashboard
    fig, axes = plt.subplots(2, 2, figsize=(12, 9))
    metrics = [
        ("pdr", "PDR"),
        ("throughput_kbps", "Throughput (kbps)"),
        ("avg_delay_ms", "Delay (ms)"),
        ("routing_overhead_ratio", "Routing Overhead"),
    ]
    for ax, (metric, title) in zip(axes.flat, metrics):
        for protocol in sorted(df["protocol"].unique()):
            subset = df[df["protocol"] == protocol].sort_values("n_nodes")
            ax.plot(subset["n_nodes"], subset[metric], marker="o", label=protocol)
        ax.set_title(title)
        ax.set_xlabel("Nodes")
        ax.grid(True, alpha=0.3)
    axes[0, 0].legend()
    fig.suptitle("Scalability Analysis — IoT Energy Routing Protocols", fontsize=14)
    fig.tight_layout()
    dashboard = args.output / "scalability_dashboard.png"
    fig.savefig(dashboard, dpi=150)
    plt.close(fig)
    print(f"Wrote {dashboard}")


if __name__ == "__main__":
    main()
