import argparse
import csv
import time
from collections import defaultdict, deque
from dataclasses import dataclass
from pathlib import Path

import matplotlib.pyplot as plt
import serial


CSV_FIELDS = [
    "stage",
    "seq",
    "t_us",
    "x_mg",
    "y_mg",
    "z_mg",
    "xf_mg",
    "yf_mg",
    "zf_mg",
    "dropped",
]


@dataclass
class StageStats:
    count: int = 0
    first_seq: int | None = None
    last_seq: int | None = None
    first_t_us: int | None = None
    last_t_us: int | None = None
    seq_gaps: int = 0
    max_reported_drop: int = 0

    def update(self, row: dict[str, int]) -> None:
        seq = row["seq"]
        t_us = row["t_us"]
        dropped = row["dropped"]

        if self.first_seq is None:
            self.first_seq = seq
            self.first_t_us = t_us
        elif self.last_seq is not None and seq > self.last_seq + 1:
            self.seq_gaps += seq - self.last_seq - 1

        self.count += 1
        self.last_seq = seq
        self.last_t_us = t_us
        self.max_reported_drop = max(self.max_reported_drop, dropped)

    @property
    def rate_hz(self) -> float:
        if self.count < 2 or self.first_t_us is None or self.last_t_us is None:
            return 0.0
        elapsed_s = (self.last_t_us - self.first_t_us) / 1_000_000.0
        if elapsed_s <= 0:
            return 0.0
        return (self.count - 1) / elapsed_s


def parse_record(line: str, marker: str) -> list[str] | None:
    index = line.find(marker)
    if index < 0:
        return None
    return line[index:].strip().split(",")


def parse_data(line: str) -> dict[str, int] | None:
    parts = parse_record(line, "DATA,")
    if parts is None or len(parts) < 11:
        return None

    try:
        values = [int(item) for item in parts[1:11]]
    except ValueError:
        return None

    return dict(zip(CSV_FIELDS, values, strict=True))


def parse_meta(line: str) -> str | None:
    parts = parse_record(line, "META,")
    if parts is None:
        return None
    return ",".join(parts)


def parse_stat(line: str) -> str | None:
    parts = parse_record(line, "STAT,")
    if parts is None:
        return None
    return ",".join(parts)


def print_summary(stats: dict[int, StageStats]) -> None:
    print("\nSummary by stage")
    print("stage,received,rate_hz,seq_gaps,reported_drops")
    for stage in sorted(stats):
        item = stats[stage]
        print(
            f"{stage},{item.count},{item.rate_hz:.2f},"
            f"{item.seq_gaps},{item.max_reported_drop}"
        )


def main() -> None:
    parser = argparse.ArgumentParser(description="Live plot for FRDM-KL25Z accelerometer data")
    parser.add_argument("--port", required=True, help="Serial port, for example COM7")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--seconds", type=float, default=0.0, help="0 means run until Ctrl+C")
    parser.add_argument("--csv", default="scripts/results/run.csv")
    parser.add_argument("--png", default="scripts/results/live_plot.png")
    parser.add_argument("--axis", choices=("x", "y", "z"), default="z")
    parser.add_argument("--window", type=int, default=500)
    parser.add_argument("--no-plot", action="store_true")
    args = parser.parse_args()

    csv_path = Path(args.csv)
    png_path = Path(args.png)
    csv_path.parent.mkdir(parents=True, exist_ok=True)
    png_path.parent.mkdir(parents=True, exist_ok=True)

    raw_field = f"{args.axis}_mg"
    fir_field = f"{args.axis}f_mg"
    stats: dict[int, StageStats] = defaultdict(StageStats)
    times = deque(maxlen=args.window)
    raw_values = deque(maxlen=args.window)
    fir_values = deque(maxlen=args.window)
    t0_us: int | None = None

    fig = ax = raw_line = fir_line = None
    if not args.no_plot:
        plt.ion()
        fig, ax = plt.subplots()
        (raw_line,) = ax.plot([], [], label=f"{args.axis} raw")
        (fir_line,) = ax.plot([], [], label=f"{args.axis} fir")
        ax.set_xlabel("time (s)")
        ax.set_ylabel("acceleration (mg)")
        ax.grid(True)
        ax.legend(loc="upper right")

    with serial.Serial(args.port, args.baud, timeout=0.1) as ser, csv_path.open(
        "w", newline=""
    ) as file:
        writer = csv.DictWriter(file, fieldnames=CSV_FIELDS)
        writer.writeheader()
        start = time.monotonic()
        last_draw = 0.0
        rows_since_flush = 0

        try:
            while args.seconds <= 0 or time.monotonic() - start < args.seconds:
                line = ser.readline().decode(errors="replace").strip()
                if not line:
                    continue

                meta = parse_meta(line)
                if meta:
                    print(meta)
                    if meta == "META,done":
                        break
                    continue

                stat = parse_stat(line)
                if stat:
                    print(stat)
                    continue

                row = parse_data(line)
                if row is None:
                    continue

                writer.writerow(row)
                rows_since_flush += 1
                if rows_since_flush >= 50:
                    file.flush()
                    rows_since_flush = 0

                stats[row["stage"]].update(row)

                if t0_us is None:
                    t0_us = row["t_us"]

                times.append((row["t_us"] - t0_us) / 1_000_000.0)
                raw_values.append(row[raw_field])
                fir_values.append(row[fir_field])

                if not args.no_plot and time.monotonic() - last_draw > 0.1:
                    raw_line.set_data(times, raw_values)
                    fir_line.set_data(times, fir_values)
                    ax.relim()
                    ax.autoscale_view()
                    fig.canvas.draw()
                    fig.canvas.flush_events()
                    last_draw = time.monotonic()
        except KeyboardInterrupt:
            pass

    if not args.no_plot and fig is not None:
        fig.savefig(png_path, dpi=150)

    print_summary(stats)
    print(f"\nCSV: {csv_path}")
    if not args.no_plot:
        print(f"PNG: {png_path}")


if __name__ == "__main__":
    main()
