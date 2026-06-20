import argparse
import csv
from collections import defaultdict
from dataclasses import dataclass
from pathlib import Path

import matplotlib.pyplot as plt


@dataclass
class Summary:
    count: int = 0
    first_t_us: int | None = None
    last_t_us: int | None = None
    last_seq: int | None = None
    seq_gaps: int = 0
    reported_drops: int = 0

    def update(self, row: dict[str, int]) -> None:
        seq = row["seq"]
        t_us = row["t_us"]

        if self.first_t_us is None:
            self.first_t_us = t_us
        elif self.last_seq is not None and seq > self.last_seq + 1:
            self.seq_gaps += seq - self.last_seq - 1

        self.count += 1
        self.last_t_us = t_us
        self.last_seq = seq
        self.reported_drops = max(self.reported_drops, row["dropped"])

    @property
    def rate_hz(self) -> float:
        if self.count < 2 or self.first_t_us is None or self.last_t_us is None:
            return 0.0
        elapsed_s = (self.last_t_us - self.first_t_us) / 1_000_000.0
        if elapsed_s <= 0:
            return 0.0
        return (self.count - 1) / elapsed_s


def read_csv(path: Path) -> list[dict[str, int]]:
    with path.open(newline="") as file:
        reader = csv.DictReader(file)
        return [{key: int(value) for key, value in row.items()} for row in reader]


def summarize(rows: list[dict[str, int]]) -> dict[int, Summary]:
    output: dict[int, Summary] = defaultdict(Summary)
    for row in rows:
        output[row["stage"]].update(row)
    return output


def write_summary(path: Path, raw: dict[int, Summary], fir: dict[int, Summary]) -> None:
    stages = sorted(set(raw) | set(fir))
    with path.open("w", newline="") as file:
        writer = csv.writer(file)
        writer.writerow(
            [
                "stage",
                "raw_rate_hz",
                "fir_rate_hz",
                "raw_received",
                "fir_received",
                "raw_seq_gaps",
                "fir_seq_gaps",
                "raw_reported_drops",
                "fir_reported_drops",
            ]
        )
        for stage in stages:
            raw_item = raw.get(stage, Summary())
            fir_item = fir.get(stage, Summary())
            writer.writerow(
                [
                    stage,
                    f"{raw_item.rate_hz:.2f}",
                    f"{fir_item.rate_hz:.2f}",
                    raw_item.count,
                    fir_item.count,
                    raw_item.seq_gaps,
                    fir_item.seq_gaps,
                    raw_item.reported_drops,
                    fir_item.reported_drops,
                ]
            )


def plot_signal(
    raw_rows: list[dict[str, int]],
    fir_rows: list[dict[str, int]],
    axis: str,
    output: Path,
) -> None:
    raw_field = f"{axis}_mg"
    fir_field = f"{axis}f_mg"

    fig, ax = plt.subplots()

    if raw_rows:
        raw_t0 = raw_rows[0]["t_us"]
        ax.plot(
            [(row["t_us"] - raw_t0) / 1_000_000.0 for row in raw_rows],
            [row[raw_field] for row in raw_rows],
            label=f"raw {axis}",
            linewidth=0.8,
        )

    if fir_rows:
        fir_t0 = fir_rows[0]["t_us"]
        ax.plot(
            [(row["t_us"] - fir_t0) / 1_000_000.0 for row in fir_rows],
            [row[fir_field] for row in fir_rows],
            label=f"fir {axis}",
            linewidth=0.8,
        )

    ax.set_xlabel("time (s)")
    ax.set_ylabel("acceleration (mg)")
    ax.grid(True)
    ax.legend(loc="upper right")
    fig.tight_layout()
    fig.savefig(output, dpi=150)
    plt.close(fig)


def plot_rates(raw: dict[int, Summary], fir: dict[int, Summary], output: Path) -> None:
    stages = sorted(set(raw) | set(fir))
    positions = list(range(len(stages)))
    width = 0.35

    fig, ax = plt.subplots()
    ax.bar(
        [pos - width / 2 for pos in positions],
        [raw.get(stage, Summary()).rate_hz for stage in stages],
        width,
        label="no fir",
    )
    ax.bar(
        [pos + width / 2 for pos in positions],
        [fir.get(stage, Summary()).rate_hz for stage in stages],
        width,
        label="fir",
    )
    ax.set_xticks(positions, [str(stage) for stage in stages])
    ax.set_xlabel("stage")
    ax.set_ylabel("effective rate (Hz)")
    ax.grid(True, axis="y")
    ax.legend(loc="upper left")
    fig.tight_layout()
    fig.savefig(output, dpi=150)
    plt.close(fig)


def main() -> None:
    parser = argparse.ArgumentParser(description="Compare raw and FIR accelerometer runs")
    parser.add_argument("--raw-csv", required=True)
    parser.add_argument("--fir-csv", required=True)
    parser.add_argument("--out-dir", default="scripts/results")
    parser.add_argument("--axis", choices=("x", "y", "z"), default="z")
    args = parser.parse_args()

    out_dir = Path(args.out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)

    raw_rows = read_csv(Path(args.raw_csv))
    fir_rows = read_csv(Path(args.fir_csv))
    raw_summary = summarize(raw_rows)
    fir_summary = summarize(fir_rows)

    write_summary(out_dir / "rate_comparison.csv", raw_summary, fir_summary)
    plot_signal(raw_rows, fir_rows, args.axis, out_dir / "signal_comparison.png")
    plot_rates(raw_summary, fir_summary, out_dir / "rate_comparison.png")

    print(f"Wrote {out_dir / 'rate_comparison.csv'}")
    print(f"Wrote {out_dir / 'signal_comparison.png'}")
    print(f"Wrote {out_dir / 'rate_comparison.png'}")


if __name__ == "__main__":
    main()
