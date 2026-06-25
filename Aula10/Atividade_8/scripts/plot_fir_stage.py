import argparse
from pathlib import Path

import matplotlib.pyplot as plt
import pandas as pd


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--csv", default="scripts/results/fir.csv")
    parser.add_argument("--stage", type=int, default=2)
    parser.add_argument("--axis", choices=("x", "y", "z"), default="z")
    parser.add_argument("--out", default="scripts/results/fir_stage_comparison.png")
    parser.add_argument("--seconds", type=float, default=5.0)
    args = parser.parse_args()

    df = pd.read_csv(args.csv)
    df = df[df["stage"] == args.stage].copy()

    if df.empty:
        raise RuntimeError(f"Nenhuma amostra encontrada para stage={args.stage}")

    raw_col = f"{args.axis}_mg"
    fir_col = f"{args.axis}f_mg"

    t0 = df["t_us"].iloc[0]
    df["time_s"] = (df["t_us"] - t0) / 1_000_000.0
    df = df[df["time_s"] <= args.seconds]

    fig, ax = plt.subplots()
    ax.plot(df["time_s"], df[raw_col], label=f"{args.axis} original", linewidth=0.8)
    ax.plot(df["time_s"], df[fir_col], label=f"{args.axis} filtrado FIR", linewidth=1.2)

    ax.set_xlabel("tempo (s)")
    ax.set_ylabel("aceleração (mg)")
    ax.set_title(f"Comparação do sinal original e filtrado - stage {args.stage}")
    ax.grid(True)
    ax.legend(loc="upper right")

    fig.tight_layout()
    Path(args.out).parent.mkdir(parents=True, exist_ok=True)
    fig.savefig(args.out, dpi=150)
    print(f"Gráfico salvo em: {args.out}")


if __name__ == "__main__":
    main()