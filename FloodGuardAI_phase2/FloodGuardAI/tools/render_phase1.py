"""Render Phase 1 figures from engine batch output (results/<scenario>.json).

Usage:  python tools/render_phase1.py results/02_heavy_rainfall.json [more.json ...] --out results/phase1_figures.png
All plotted values come straight from the engine's JSON output (no hardcoded numbers).
"""
import argparse
import json

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np
from matplotlib.colors import ListedColormap, BoundaryNorm
from matplotlib.patches import Patch

LAND_COLORS = ["#d9e4c3", "#c9b8a8", "#6b6b6b", "#3b7dd8", "#6fb7c4",
               "#d62728", "#ff9f1c", "#2ca02c", "#9467bd"]


def grid(a, rows, cols):
    return np.array(a, dtype=float).reshape(rows, cols)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("files", nargs="+")
    ap.add_argument("--out", default="results/phase1_figures.png")
    a = ap.parse_args()
    runs = [json.load(open(f)) for f in a.files]
    town = runs[0]["town"]
    R, C = town["rows"], town["cols"]
    n = len(runs)
    fig, axes = plt.subplots(2, n + 1, figsize=(4.6 * (n + 1), 9), squeeze=False)

    # Terrain elevation + land use (identical town for all scenarios)
    elev = grid(town["elevation_m"], R, C)
    im = axes[0][0].imshow(elev, cmap="terrain")
    axes[0][0].set_title("Terrain elevation [m]")
    fig.colorbar(im, ax=axes[0][0], fraction=0.046)
    lu = grid(town["land_use"], R, C)
    cmap = ListedColormap(LAND_COLORS)
    axes[1][0].imshow(lu, cmap=cmap, norm=BoundaryNorm(range(10), cmap.N))
    legend = town["land_use_legend"]
    axes[1][0].legend(handles=[Patch(color=LAND_COLORS[int(k)], label=v) for k, v in legend.items()],
                      loc="upper left", bbox_to_anchor=(0, -0.02), ncol=3, fontsize=7, frameon=False)
    for f in town["facilities"]:
        axes[1][0].text(f["col"], f["row"], f["name"].split()[0][0] + f["name"].split()[-1][-1],
                        fontsize=6, ha="center", va="center", color="white", weight="bold")
    axes[1][0].set_title("Land use and facilities")

    for j, run in enumerate(runs, start=1):
        series = run["series"]
        peak = max(series, key=lambda s: s["inundated_cells"])
        frame = min(run["frames"], key=lambda fr: abs(fr["time_min"] - peak["time_min"]))
        depth = grid(frame["depth_m"], R, C)
        lu_mask = grid(town["land_use"], R, C) == 3
        masked = np.ma.masked_where(depth < run["scenario"]["thresholds"]["inundated_m"], depth)
        ax = axes[0][j]
        ax.imshow(elev, cmap="Greys", alpha=0.5)
        im = ax.imshow(np.ma.masked_where(lu_mask, masked), cmap="Blues", vmin=0, vmax=max(0.5, float(masked.max() if masked.count() else 0.5)))
        ax.imshow(np.ma.masked_where(~lu_mask, depth), cmap="winter", alpha=0.6)
        ax.set_title(f"{run['scenario']['name']}\nflood depth at t={frame['time_min']:.0f} min [m]", fontsize=9)
        fig.colorbar(im, ax=ax, fraction=0.046)
        ax2 = axes[1][j]
        t = [s["time_min"] for s in series]
        ax2.plot(t, [s["inundated_pct"] for s in series], color="#1f77b4", label="inundated floodplain [%]")
        ax2.set_xlabel("time [min]"); ax2.set_ylabel("inundated floodplain [%]")
        ax3 = ax2.twinx()
        ax3.plot(t, [s["max_floodplain_depth_m"] for s in series], color="#d62728", label="max depth [m]")
        ax3.set_ylabel("max floodplain depth [m]")
        ax2.set_title(f"rain {run['final']['accumulated_rain_mm']:.0f} mm | peak {peak['inundated_pct']:.1f}%", fontsize=9)
    fig.suptitle("FloodGuard AI - Phase 1 (simulated, synthetic town; not a forecast)", fontsize=11)
    fig.tight_layout()
    fig.savefig(a.out, dpi=110)
    print("wrote", a.out)


if __name__ == "__main__":
    main()
