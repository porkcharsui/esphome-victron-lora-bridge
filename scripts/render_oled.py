#!/usr/bin/env python3
"""Render the firmware's OLED layouts through ESPHome Host and SDL."""

from __future__ import annotations

import argparse
import os
import subprocess
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
PREVIEW_CONFIG = ROOT / "esphome" / "oled-preview.yml"
PREVIEW_PROGRAM = (
    ROOT
    / "esphome"
    / ".esphome"
    / "build"
    / "oled-preview"
    / ".pioenvs"
    / "oled-preview"
    / "program"
)


def parse_optional_float(value: str) -> float | None:
    if value.lower() in {"none", "unknown", "nan"}:
        return None
    return float(value)


def scaled_output_path(output: Path, scale: int) -> Path | None:
    if scale <= 1:
        return None
    return output.with_name(f"{output.stem}@{scale}x{output.suffix}")


def preview_environment(args: argparse.Namespace) -> dict[str, str]:
    def number(value: float | None) -> str:
        return "unknown" if value is None else str(value)

    screen = "van" if args.node == "van" else f"uplink-{args.page}"
    return {
        "OLED_PREVIEW_SCREEN": screen,
        "OLED_SOC": number(args.soc),
        "OLED_VOLTAGE": number(args.voltage),
        "OLED_CURRENT": number(args.current),
        "OLED_SHUNT_FRESH": "1" if args.shunt_fresh else "0",
        "OLED_SOLAR_FRESH": "1" if args.solar_fresh else "0",
        "OLED_WIFI": "1" if args.wifi == "connected" else "0",
        "OLED_IP": args.ip,
        "OLED_LINK": args.link,
        "OLED_AGE": number(args.age),
        "OLED_RSSI": number(args.rssi),
        "OLED_SNR": number(args.snr),
    }


def compile_preview() -> None:
    subprocess.run(
        ["esphome", "compile", str(PREVIEW_CONFIG)],
        cwd=ROOT,
        check=True,
    )
    if not PREVIEW_PROGRAM.is_file():
        raise RuntimeError(f"ESPHome did not produce the expected Host program: {PREVIEW_PROGRAM}")


def render_preview(args: argparse.Namespace) -> Path | None:
    compile_preview()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    scaled_output = scaled_output_path(args.output, args.scale)

    with tempfile.TemporaryDirectory(prefix="oled-preview-") as temporary_directory:
        capture = Path(temporary_directory) / "capture.bmp"
        environment = os.environ.copy()
        environment.update(preview_environment(args))
        environment.update(
            {
                "OLED_PREVIEW_BMP": str(capture),
                "SDL_AUDIODRIVER": "dummy",
                "SDL_VIDEODRIVER": "dummy",
            }
        )
        subprocess.run([str(PREVIEW_PROGRAM)], cwd=ROOT, env=environment, check=True)
        subprocess.run(
            [
                "magick",
                str(capture),
                "-colorspace",
                "Gray",
                "-threshold",
                "50%",
                "-type",
                "bilevel",
                str(args.output),
            ],
            cwd=ROOT,
            check=True,
        )

    if scaled_output is not None:
        subprocess.run(
            [
                "magick",
                str(args.output),
                "-filter",
                "point",
                "-resize",
                f"{args.scale * 100}%",
                "-type",
                "bilevel",
                str(scaled_output),
            ],
            cwd=ROOT,
            check=True,
        )
    return scaled_output


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--node", choices=("van", "uplink"), required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument(
        "--scale",
        type=int,
        default=4,
        help="also write a nearest-neighbor enlarged preview; use 1 to disable",
    )
    parser.add_argument("--soc", type=parse_optional_float, default=78.0)
    parser.add_argument("--voltage", type=parse_optional_float, default=13.2)
    parser.add_argument("--current", type=parse_optional_float, default=-2.4)
    parser.add_argument(
        "--shunt-fresh", action=argparse.BooleanOptionalAction, default=True
    )
    parser.add_argument(
        "--solar-fresh", action=argparse.BooleanOptionalAction, default=True
    )
    parser.add_argument("--wifi", choices=("connected", "offline"), default="connected")
    parser.add_argument("--page", choices=("home", "battery"), default="home")
    parser.add_argument("--ip", default="192.168.1.42")
    parser.add_argument("--link", choices=("ok", "stale", "waiting"), default="ok")
    parser.add_argument("--age", type=parse_optional_float, default=12.0)
    parser.add_argument("--rssi", type=parse_optional_float, default=-67.0)
    parser.add_argument("--snr", type=parse_optional_float, default=9.0)
    return parser


def main() -> None:
    args = build_parser().parse_args()
    if args.scale < 1:
        raise SystemExit("--scale must be at least 1")
    scaled_output = render_preview(args)
    print(f"Wrote {args.output}")
    if scaled_output is not None:
        print(f"Wrote {scaled_output}")


if __name__ == "__main__":
    main()
