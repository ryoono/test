"""AoA IQ CSV (ヘッダーなし) -> 1行につき1枚の3x3解析グラフ。

例:
    python aoa_plot_from_csv.py --csv aoa_iq_000.csv --outdir out_plots --ref_n 8 --slot_us 1.0

CSV列 (0始まり):
    0: epoch, 1: channel, 2: rssi, 3: sequence, 4以降: I,Q,I,Q,...

現時点ではReference 8 IQサンプル + 最初の32 IQサンプルだけを分析する。
V1,H1,V2,H2,...,V16,H16 の切替順を仮定する。
"""

from __future__ import annotations

import argparse
import csv
from pathlib import Path

import matplotlib.pyplot as plt
import numpy as np

REFERENCE_SAMPLE_US = 1.0  # Reference期間は1 µs間隔でサンプリングする想定
ANTENNAS_PER_POLARIZATION = 16
FIRST_SWEEP_SAMPLES = ANTENNAS_PER_POLARIZATION * 2  # V/H交互の計32 IQサンプル


def parse_iq_interleaved(values: list[str]) -> np.ndarray:
    """I,Qが交互に並ぶCSV列 -> shape=(N,2) のfloat配列。"""
    if len(values) % 2:
        raise ValueError(f"IQの列数が奇数です: {len(values)}")
    try:
        iq = np.asarray([float(x) for x in values], dtype=float).reshape(-1, 2)
    except ValueError as exc:
        raise ValueError("IQデータに数値以外の値が含まれています") from exc
    if not np.isfinite(iq).all():
        raise ValueError("IQデータにNaNまたは無限大が含まれています")
    return iq


def fit_cfo_from_reference(iq_ref: np.ndarray) -> tuple[float, np.ndarray]:
    """Reference位相の直線近似からCFO相当の傾き[rad/µs]を算出。"""
    phi_ref = np.unwrap(np.angle(iq_ref[:, 0] + 1j * iq_ref[:, 1]))
    reference_time_us = np.arange(len(phi_ref)) * REFERENCE_SAMPLE_US
    slope, _ = np.polyfit(reference_time_us, phi_ref, 1)
    return float(slope), phi_ref


def analyze_first_sweep(
    iq: np.ndarray,
    ref_n: int = 8,
    slot_duration_us: float = 1.0,
) -> dict[str, np.ndarray | float]:
    """Referenceと1周目の32サンプルだけを解析する。"""
    if ref_n < 2:
        raise ValueError("--ref_n は2以上にしてください")
    if slot_duration_us <= 0:
        raise ValueError("--slot_us は正の値にしてください")
    if len(iq) < ref_n + FIRST_SWEEP_SAMPLES:
        raise ValueError(
            f"IQサンプル数が不足しています: {len(iq)} "
            f"(最低 {ref_n + FIRST_SWEEP_SAMPLES} 組必要)"
        )

    iq_ref = iq[:ref_n]
    iq_first = iq[ref_n : ref_n + FIRST_SWEEP_SAMPLES]

    # Referenceだけは同じアンテナからの連続サンプルなのでunwrap可能。
    slope_rad_per_us, phi_ref = fit_cfo_from_reference(iq_ref)

    # V1,H1,V2,H2,...,V16,H16 を仮定。
    # CTEの1 µsサンプリングとslot_duration_usの切替時間が交互に来るので、
    # 1サンプルの時間間隔は (1 + slot_duration_us) µs とする。
    # 実際の切替・サンプリング設定が異なる場合、ここを修正する。
    sample_interval_us = REFERENCE_SAMPLE_US + slot_duration_us
    times_us = (
        ref_n * REFERENCE_SAMPLE_US
        + np.arange(FIRST_SWEEP_SAMPLES) * sample_interval_us
    )

    complex_first = iq_first[:, 0] + 1j * iq_first[:, 1]
    # アンテナ切替をまたいでunwrapしないことが重要。
    # 複素IQにCFOの回転を打ち消してから、各アンテナの位相を取得する。
    corrected = complex_first * np.exp(-1j * slope_rad_per_us * times_us)

    iq_v, iq_h = iq_first[0::2], iq_first[1::2]
    amp_v = np.hypot(iq_v[:, 0], iq_v[:, 1])
    amp_h = np.hypot(iq_h[:, 0], iq_h[:, 1])

    phase_v_raw_deg = np.mod(np.degrees(np.angle(complex_first[0::2])), 360.0)
    phase_h_raw_deg = np.mod(np.degrees(np.angle(complex_first[1::2])), 360.0)

    # 4素子ずつの物理的な行の中だけでunwrapする。
    # その後、各行の最初の素子を0°として比較する。
    # ※ unwrapは180°を超える真の位相差を一意に復元できない点に注意。
    phase_v = np.unwrap(np.angle(corrected[0::2]).reshape(4, 4), axis=1)
    phase_h = np.unwrap(np.angle(corrected[1::2]).reshape(4, 4), axis=1)
    phase_v_rel_deg = np.degrees(phase_v - phase_v[:, :1])
    phase_h_rel_deg = np.degrees(phase_h - phase_h[:, :1])

    # ----- 将来2周目以降を扱う場合の注意 -----
    # このCSVはReferenceの後に74組のIQサンプルがある（全82組）。
    # 1周目は iq[ref_n:ref_n+32] の32組を用いる。
    # 実測データには33組前後の周期性が見られるが、追加の1組が
    # 「どのアンテナ」または「参照サンプル」に当たるかは未確定。
    # 2周目以降を単純に32組ずつ切り出すと番号の対応がずれる恐れがある。
    # 拡張時には実機の antenna switching pattern / reference設定から
    # 周回開始位置、追加サンプルの位置、各サンプルの取得時刻を確定すること。
    # CFO補正では、時刻を周回ごとに0に戻さず、連続した実測時刻を用いる。
    # さらに、同じアンテナの周回間位相差で再現性を検証するとよい。

    return {
        "phi_ref_deg": np.degrees(phi_ref),
        "cfo_slope_deg_per_us": np.degrees(slope_rad_per_us),
        "amp_v": amp_v,
        "amp_h": amp_h,
        "phase_v_raw_deg": phase_v_raw_deg,
        "phase_h_raw_deg": phase_h_raw_deg,
        "phase_v_rel_deg": phase_v_rel_deg,
        "phase_h_rel_deg": phase_h_rel_deg,
    }


def plot_four_groups(
    ax: plt.Axes,
    y: np.ndarray,
    *,
    color: str,
    ylabel: str,
    fixed_phase_range: bool = False,
) -> None:
    """4個ずつの行を線で結び、異なる行を結ばない。"""
    y = np.asarray(y, dtype=float).reshape(4, 4)
    for group in range(4):
        x = np.arange(group * 4 + 1, group * 4 + 5)
        ax.plot(x, y[group], marker="o", color=color)
    for boundary in (4.5, 8.5, 12.5):
        ax.axvline(boundary, color="gray", linestyle="--", linewidth=0.8, alpha=0.6)
    ax.set_xticks([1, 4, 5, 8, 9, 12, 13, 16])
    ax.set_xlim(0.5, 16.5)
    ax.set_xlabel("Antenna index")
    ax.set_ylabel(ylabel)
    if fixed_phase_range:
        ax.set_ylim(0, 360)
    ax.grid(True, alpha=0.6)


def make_figure_3x3(
    result: dict[str, np.ndarray | float],
    figure_title: str,
) -> plt.Figure:
    """元の3×3構成を維持し、下段に行内相対位相を表示する。"""
    fig, axes = plt.subplots(3, 3, figsize=(14, 10), dpi=150)
    fig.suptitle(figure_title)
    axes[1, 0].axis("off")
    axes[2, 0].axis("off")

    ax = axes[0, 0]
    phi_ref = np.asarray(result["phi_ref_deg"])
    ax.plot(np.arange(len(phi_ref)), phi_ref, marker="o")
    ax.set_title("① Reference phase")
    ax.set_xlabel("Reference sample index")
    ax.set_ylabel("Unwrapped phase [deg]")
    ax.grid(True, alpha=0.6)
    ax.text(
        0.02, 0.04,
        f"slope = {result['cfo_slope_deg_per_us']:.2f} deg/us",
        transform=ax.transAxes,
        fontsize=9,
        va="bottom",
    )

    ax = axes[0, 1]
    plot_four_groups(ax, result["amp_v"], color="tab:blue", ylabel="Amplitude [arb]")
    ax.set_title("⑥ V amplitude - 1st sweep")

    ax = axes[0, 2]
    plot_four_groups(ax, result["amp_h"], color="tab:orange", ylabel="Amplitude [arb]")
    ax.set_title("⑥ H amplitude - 1st sweep")

    ax = axes[1, 1]
    plot_four_groups(
        ax, result["phase_v_raw_deg"],
        color="tab:blue", ylabel="Phase [deg] (0-360)", fixed_phase_range=True,
    )
    ax.set_title("② V phase (no CFO correction)")

    ax = axes[1, 2]
    plot_four_groups(
        ax, result["phase_h_raw_deg"],
        color="tab:orange", ylabel="Phase [deg] (0-360)", fixed_phase_range=True,
    )
    ax.set_title("③ H phase (no CFO correction)")

    ax = axes[2, 1]
    plot_four_groups(
        ax, result["phase_v_rel_deg"],
        color="tab:blue", ylabel="Relative phase [deg]",
    )
    ax.set_title("④ V phase (CFO corrected, group-relative)")

    ax = axes[2, 2]
    plot_four_groups(
        ax, result["phase_h_rel_deg"],
        color="tab:orange", ylabel="Relative phase [deg]",
    )
    ax.set_title("⑤ H phase (CFO corrected, group-relative)")

    fig.tight_layout(rect=[0, 0.03, 1, 0.96])
    return fig


def main() -> None:
    parser = argparse.ArgumentParser(description="Headerless AoA IQ CSV -> 1st-sweep plots")
    parser.add_argument("--csv", required=True, help="入力CSVファイル（ヘッダーなし）")
    parser.add_argument("--outdir", required=True, help="出力フォルダ")
    parser.add_argument("--ref_n", type=int, default=8, help="Reference IQサンプル数（既定値8）")
    parser.add_argument("--slot_us", type=float, default=1.0, help="切替スロット長[µs]（既定値1.0）")
    parser.add_argument("--samples_start_col", type=int, default=4, help="IQ開始列（0始まり、既定値4）")
    parser.add_argument("--max_rows", type=int, default=0, help="最大処理行数（0なら全行）")
    args = parser.parse_args()

    if args.samples_start_col < 0 or args.max_rows < 0:
        parser.error("--samples_start_col と --max_rows は0以上にしてください")
    if args.ref_n < 2 or args.slot_us <= 0:
        parser.error("--ref_n は2以上、--slot_us は正の値にしてください")

    outdir = Path(args.outdir)
    outdir.mkdir(parents=True, exist_ok=True)
    success = 0
    skipped = 0

    # ヘッダーなしCSVなので最初の行から処理（next(reader)しない）。
    with Path(args.csv).open("r", newline="", encoding="utf-8-sig") as f:
        reader = csv.reader(f)
        for row_idx, row in enumerate(reader, start=1):
            if args.max_rows > 0 and row_idx > args.max_rows:
                break
            if not row or not any(v.strip() for v in row):
                continue
            if len(row) <= args.samples_start_col:
                print(f"[WARN] CSV {row_idx}行目: 列数不足のためスキップ")
                skipped += 1
                continue

            epoch = row[0] if len(row) > 0 else ""
            channel = row[1] if len(row) > 1 else ""
            rssi = row[2] if len(row) > 2 else ""
            sequence = row[3] if len(row) > 3 else ""

            try:
                iq = parse_iq_interleaved(row[args.samples_start_col:])
                result = analyze_first_sweep(iq, args.ref_n, args.slot_us)
                title = f"seq={sequence} ch={channel} rssi={rssi} epoch={epoch}"
                fig = make_figure_3x3(result, title)
            except Exception as exc:
                print(f"[WARN] CSV {row_idx}行目: 解析失敗 ({exc})")
                skipped += 1
                continue

            # row番号を付けて、sequenceが重複・周回した場合もファイル名が衝突しないようにする。
            safe_seq = "".join(c for c in sequence if c.isdigit()) or "unknown"
            out_path = outdir / f"seq{safe_seq}_row{row_idx:05d}.png"
            try:
                fig.savefig(out_path, bbox_inches="tight")
                success += 1
            finally:
                plt.close(fig)

            if success == 1 or success % 10 == 0:
                print(f"[INFO] {success}件保存: {out_path}")

    print(f"[INFO] 完了: 保存 {success}件、スキップ {skipped}件")


if __name__ == "__main__":
    main()
