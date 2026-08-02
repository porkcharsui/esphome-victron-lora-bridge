#!/usr/bin/env python3
"""Render the ESPHome OLED layouts without flashing either node."""

from __future__ import annotations

import argparse
import math
from dataclasses import dataclass
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont


ROOT = Path(__file__).resolve().parents[1]
DISPLAY_SIZE = (128, 64)
DEFAULT_FONT_PATTERN = "Roboto Mono@400@False@*.ttf"
VAN_LEFT_COLUMN_WIDTH = 78
VAN_LEFT_COLUMN_CENTER = VAN_LEFT_COLUMN_WIDTH // 2
VAN_RIGHT_COLUMN_CENTER = (
    VAN_LEFT_COLUMN_WIDTH + (DISPLAY_SIZE[0] - VAN_LEFT_COLUMN_WIDTH) // 2
)
BATTERY_TERMINAL_WIDTH = 4
BATTERY_BODY_WIDTH = VAN_LEFT_COLUMN_WIDTH - BATTERY_TERMINAL_WIDTH
BATTERY_INNER_WIDTH = BATTERY_BODY_WIDTH - 4
BATTERY_TOP = 34
BATTERY_HEIGHT = DISPLAY_SIZE[1] - BATTERY_TOP


@dataclass(frozen=True)
class VanState:
    soc: float | None = 78.0
    voltage: float | None = 13.2
    current: float | None = -2.4
    shunt_fresh: bool = True
    solar_fresh: bool = True


@dataclass(frozen=True)
class UplinkState:
    wifi: str = "connected"
    ip: str = "192.168.1.42"
    link: str = "ok"
    age: float | None = 12.0
    rssi: float | None = -67.0
    snr: float | None = 9.0


def parse_optional_float(value: str) -> float | None:
    if value.lower() in {"none", "unknown", "nan"}:
        return None
    return float(value)


def resolve_font_path() -> Path:
    font_dir = ROOT / "esphome" / ".esphome" / "font"
    matches = sorted(font_dir.glob(DEFAULT_FONT_PATTERN))
    if matches:
        return matches[-1]
    raise FileNotFoundError(
        "Roboto Mono is not in ESPHome's font cache. Run "
        "`nix develop --command uv run esphome config esphome/victron.yml` first."
    )


def load_font(font_path: Path, size: int) -> ImageFont.FreeTypeFont:
    return ImageFont.truetype(font_path, size=size)


def draw_text(
    draw: ImageDraw.ImageDraw,
    xy: tuple[int, int],
    text: str,
    font: ImageFont.FreeTypeFont,
    anchor: str = "lt",
) -> None:
    draw.text(xy, text, font=font, fill=1, anchor=anchor, stroke_width=0)


def gauge_fill_width(soc: float | None) -> int:
    if soc is None or math.isnan(soc):
        return 0
    return math.floor(max(0.0, min(100.0, soc)) * BATTERY_INNER_WIDTH / 100.0 + 0.5)


def signal_bar_count(rssi: float | None, snr: float | None) -> int:
    if rssi is None or snr is None or math.isnan(rssi) or math.isnan(snr):
        return 0
    rssi_bars = 4 if rssi >= -75 else 3 if rssi >= -90 else 2 if rssi >= -105 else 1
    snr_bars = 4 if snr >= 7 else 3 if snr >= 0 else 2 if snr >= -5 else 1
    return min(rssi_bars, snr_bars)


def render_van(state: VanState, font_path: Path | None = None) -> Image.Image:
    font_path = font_path or resolve_font_path()
    tiny = load_font(font_path, 8)
    status = load_font(font_path, 10)
    soc_font = load_font(font_path, 28)
    image = Image.new("1", DISPLAY_SIZE, 0)
    draw = ImageDraw.Draw(image)

    shunt = "OK" if state.shunt_fresh else "--"
    solar = "OK" if state.solar_fresh else "--"
    draw_text(
        draw,
        (DISPLAY_SIZE[0] // 2, 0),
        f"SHUNT:{shunt}  SOLAR:{solar}",
        tiny,
        anchor="mt",
    )

    soc_text = "--%" if state.soc is None else f"{state.soc:.0f}%"
    draw_text(draw, (VAN_LEFT_COLUMN_CENTER, 10), soc_text, soc_font, anchor="mt")
    voltage = "--.-V" if state.voltage is None else f"{state.voltage:.1f}V"
    current = "--.-A" if state.current is None else f"{state.current:+.1f}A"
    draw_text(draw, (VAN_RIGHT_COLUMN_CENTER, 12), voltage, status, anchor="mt")
    draw_text(draw, (VAN_RIGHT_COLUMN_CENTER, 31), current, status, anchor="mt")
    draw_text(
        draw,
        (VAN_RIGHT_COLUMN_CENTER, 53),
        "FRESH" if state.shunt_fresh else "STALE",
        tiny,
        anchor="mt",
    )

    draw.rectangle(
        (0, BATTERY_TOP, BATTERY_BODY_WIDTH - 1, DISPLAY_SIZE[1] - 1),
        outline=1,
    )
    draw.rectangle(
        (
            BATTERY_BODY_WIDTH,
            BATTERY_TOP + 9,
            VAN_LEFT_COLUMN_WIDTH - 1,
            BATTERY_TOP + 20,
        ),
        fill=1,
    )
    fill_width = gauge_fill_width(state.soc)
    if fill_width:
        draw.rectangle(
            (2, BATTERY_TOP + 2, 1 + fill_width, DISPLAY_SIZE[1] - 3),
            fill=1,
        )
    return image


def render_uplink(state: UplinkState, font_path: Path | None = None) -> Image.Image:
    font_path = font_path or resolve_font_path()
    status = load_font(font_path, 10)
    image = Image.new("1", DISPLAY_SIZE, 0)
    draw = ImageDraw.Draw(image)

    draw_text(draw, (0, 0), "VICTRON LORA BRIDGE", status)
    draw.line((0, 13, 127, 13), fill=1)
    wifi_text = f"WiFi  {state.ip}" if state.wifi == "connected" else "WiFi  OFFLINE"
    draw_text(draw, (0, 16), wifi_text, status)

    if state.link == "waiting":
        draw_text(draw, (0, 28), "LoRa  WAITING", status)
        draw_text(draw, (0, 40), "Packet  never", status)
    else:
        draw_text(draw, (0, 28), f"LoRa  {state.link.upper()}", status)
        if state.age is None:
            age_text = "Packet  never"
        elif state.age < 120:
            age_text = f"Packet  {state.age:.0f} sec ago"
        else:
            age_text = f"Packet  {state.age / 60.0:.0f} min ago"
        draw_text(draw, (0, 40), age_text, status)

    if state.rssi is not None and state.snr is not None:
        radio_text = f"RF {state.rssi:.0f}dBm {state.snr:.0f}dB"
    else:
        radio_text = "RF  listening"
    draw_text(draw, (0, 52), radio_text, status)
    for bar in range(signal_bar_count(state.rssi, state.snr)):
        height = 3 + bar * 3
        draw.rectangle((110 + bar * 4, 64 - height, 112 + bar * 4, 63), fill=1)
    return image


def save_previews(image: Image.Image, output: Path, scale: int) -> Path | None:
    output.parent.mkdir(parents=True, exist_ok=True)
    image.save(output)
    if scale <= 1:
        return None
    scaled_output = output.with_name(f"{output.stem}@{scale}x{output.suffix}")
    scaled = image.resize((image.width * scale, image.height * scale), Image.Resampling.NEAREST)
    scaled.save(scaled_output)
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
    if args.node == "van":
        image = render_van(
            VanState(
                soc=args.soc,
                voltage=args.voltage,
                current=args.current,
                shunt_fresh=args.shunt_fresh,
                solar_fresh=args.solar_fresh,
            )
        )
    else:
        image = render_uplink(
            UplinkState(
                wifi=args.wifi,
                ip=args.ip,
                link=args.link,
                age=args.age,
                rssi=args.rssi,
                snr=args.snr,
            )
        )
    scaled_output = save_previews(image, args.output, args.scale)
    print(f"Wrote {args.output}")
    if scaled_output:
        print(f"Wrote {scaled_output}")


if __name__ == "__main__":
    main()
