import argparse
import csv
from collections import defaultdict
from dataclasses import dataclass
from pathlib import Path

import matplotlib.pyplot as plt


TEST_RATES_HZ = {
    0: 50,
    1: 100,
    2: 200,
    3: 400,
    4: 800,
}

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
        self.reported_drops = max(self.reported_drops, row.get("dropped", 0))

    @property
    def rate_hz(self) -> float:
        if self.count < 2 or self.first_t_us is None or self.last_t_us is None:
            return 0.0
        elapsed_s = (self.last_t_us - self.first_t_us) / 1_000_000.0
        if elapsed_s <= 0:
            return 0.0
        return (self.count - 1) / elapsed_s

    @property
    def lost_messages(self) -> int:
        return max(self.seq_gaps, self.reported_drops)


def read_csv(path: Path) -> list[dict[str, int]]:
    with path.open(newline="") as file:
        reader = csv.DictReader(file)
        rows: list[dict[str, int]] = []
        for row in reader:
            parsed: dict[str, int] = {}
            for field in CSV_FIELDS:
                value = row.get(field, "0")
                parsed[field] = int(value) if value not in (None, "") else 0
            rows.append(parsed)
        return rows


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
                "target_rate_hz",
                "raw_effective_rate_hz",
                "fir_effective_rate_hz",
                "raw_received",
                "fir_received",
                "raw_seq_gaps",
                "fir_seq_gaps",
                "raw_reported_drops",
                "fir_reported_drops",
                "raw_lost_messages",
                "fir_lost_messages",
                "fir_rate_delta_hz",
                "fir_rate_delta_pct",
            ]
        )
        for stage in stages:
            raw_item = raw.get(stage, Summary())
            fir_item = fir.get(stage, Summary())
            delta_hz = fir_item.rate_hz - raw_item.rate_hz
            delta_pct = (delta_hz / raw_item.rate_hz * 100.0) if raw_item.rate_hz > 0 else 0.0
            writer.writerow(
                [
                    stage,
                    TEST_RATES_HZ.get(stage, ""),
                    f"{raw_item.rate_hz:.2f}",
                    f"{fir_item.rate_hz:.2f}",
                    raw_item.count,
                    fir_item.count,
                    raw_item.seq_gaps,
                    fir_item.seq_gaps,
                    raw_item.reported_drops,
                    fir_item.reported_drops,
                    raw_item.lost_messages,
                    fir_item.lost_messages,
                    f"{delta_hz:.2f}",
                    f"{delta_pct:.2f}",
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
    ax.set_xticks(positions, [str(TEST_RATES_HZ.get(stage, stage)) for stage in stages])
    ax.set_xlabel("target rate (Hz)")
    ax.set_ylabel("effective rate (Hz)")
    ax.grid(True, axis="y")
    ax.legend(loc="upper left")
    fig.tight_layout()
    fig.savefig(output, dpi=150)
    plt.close(fig)


def write_analysis(path: Path, raw: dict[int, Summary], fir: dict[int, Summary]) -> None:
    stages = sorted(set(raw) | set(fir))
    raw_best_stage = max(stages, key=lambda stage: raw.get(stage, Summary()).rate_hz, default=None)
    fir_best_stage = max(stages, key=lambda stage: fir.get(stage, Summary()).rate_hz, default=None)

    def fmt_stage(stage: int | None, data: dict[int, Summary]) -> str:
        if stage is None:
            return "sem dados"
        target = TEST_RATES_HZ.get(stage, stage)
        return f"stage {stage} ({target} Hz alvo): {data.get(stage, Summary()).rate_hz:.2f} Hz efetivos"

    loss_stages = [
        stage
        for stage in stages
        if raw.get(stage, Summary()).lost_messages > 0 or fir.get(stage, Summary()).lost_messages > 0
    ]
    no_loss_stages = [
        stage
        for stage in stages
        if raw.get(stage, Summary()).lost_messages == 0 and fir.get(stage, Summary()).lost_messages == 0
    ]

    with path.open("w", encoding="utf-8") as file:
        file.write("Respostas automáticas para a análise\n")
        file.write("===================================\n\n")
        file.write(f"1. Maior taxa sem FIR: {fmt_stage(raw_best_stage, raw)}.\n")
        file.write(f"2. Maior taxa com FIR: {fmt_stage(fir_best_stage, fir)}.\n")
        file.write(
            "3. Impacto do FIR: compare a coluna fir_rate_delta_pct em rate_comparison.csv; "
            "valores próximos de 0 indicam baixo impacto de processamento.\n"
        )
        file.write(
            "4. Logging/UART transmitiu todos os dados quando seq_gaps e reported_drops ficaram zerados.\n"
        )
        if loss_stages:
            readable = ", ".join(str(TEST_RATES_HZ.get(stage, stage)) for stage in loss_stages)
            file.write(f"5. Houve perda nas taxas alvo: {readable} Hz.\n")
        else:
            file.write("5. Não houve perda detectada nos arquivos capturados.\n")
        file.write(
            "6. Gargalo provável: comunicação, quando há drops/seq_gaps sem grande queda entre raw e FIR; "
            "processamento, quando a taxa efetiva cai apenas com FIR.\n"
        )
        if no_loss_stages:
            best_no_loss = max(no_loss_stages, key=lambda stage: TEST_RATES_HZ.get(stage, stage))
            file.write(
                "7. Melhor compromisso sugerido: "
                f"stage {best_no_loss} ({TEST_RATES_HZ.get(best_no_loss, best_no_loss)} Hz), "
                "por ser a maior taxa sem perda detectada.\n"
            )
        else:
            file.write(
                "7. Melhor compromisso: usar a menor taxa com menor lost_messages em rate_comparison.csv.\n"
            )


def main() -> None:
    parser = argparse.ArgumentParser(description="Compare runs sem FIR e com FIR do acelerometro")
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
    write_analysis(out_dir / "analysis_answers.txt", raw_summary, fir_summary)
    plot_signal(raw_rows, fir_rows, args.axis, out_dir / "signal_comparison.png")
    plot_rates(raw_summary, fir_summary, out_dir / "rate_comparison.png")

    print(f"Wrote {out_dir / 'rate_comparison.csv'}")
    print(f"Wrote {out_dir / 'analysis_answers.txt'}")
    print(f"Wrote {out_dir / 'signal_comparison.png'}")
    print(f"Wrote {out_dir / 'rate_comparison.png'}")


if __name__ == "__main__":
    main()
