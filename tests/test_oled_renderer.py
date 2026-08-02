from __future__ import annotations

import argparse
import importlib.util
import re
import struct
import subprocess
import sys
from pathlib import Path

import pytest


ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("render_oled", ROOT / "scripts/render_oled.py")
assert SPEC is not None and SPEC.loader is not None
render_oled = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = render_oled
SPEC.loader.exec_module(render_oled)


@pytest.mark.parametrize("value", ("none", "unknown", "nan", "NaN"))
def test_optional_float_accepts_unknown_values(value: str) -> None:
    assert render_oled.parse_optional_float(value) is None


def test_preview_environment_maps_uplink_battery_state() -> None:
    args = argparse.Namespace(
        node="uplink",
        page="battery",
        soc=None,
        voltage=12.8,
        current=4.2,
        shunt_fresh=False,
        solar_fresh=True,
        wifi="offline",
        ip="192.168.4.2",
        link="stale",
        age=300.0,
        rssi=None,
        snr=-2.0,
    )

    environment = render_oled.preview_environment(args)

    assert environment == {
        "OLED_PREVIEW_SCREEN": "uplink-battery",
        "OLED_SOC": "unknown",
        "OLED_VOLTAGE": "12.8",
        "OLED_CURRENT": "4.2",
        "OLED_SHUNT_FRESH": "0",
        "OLED_SOLAR_FRESH": "1",
        "OLED_WIFI": "0",
        "OLED_IP": "192.168.4.2",
        "OLED_LINK": "stale",
        "OLED_AGE": "300.0",
        "OLED_RSSI": "unknown",
        "OLED_SNR": "-2.0",
    }


def test_scaled_output_path() -> None:
    output = Path("preview.png")
    assert render_oled.scaled_output_path(output, 1) is None
    assert render_oled.scaled_output_path(output, 4) == Path("preview@4x.png")


def png_dimensions(path: Path) -> tuple[int, int]:
    data = path.read_bytes()
    assert data[:16] == b"\x89PNG\r\n\x1a\n\x00\x00\x00\rIHDR"
    return struct.unpack(">II", data[16:24])


def trimmed_bounds(path: Path, crop: str) -> tuple[int, int, int, int]:
    result = subprocess.run(
        ["magick", str(path), "-crop", crop, "-trim", "-format", "%wx%h%O", "info:"],
        check=True,
        text=True,
        capture_output=True,
    )
    match = re.fullmatch(r"(\d+)x(\d+)([+-]\d+)([+-]\d+)", result.stdout)
    assert match is not None
    width, height, x, y = map(int, match.groups())
    return x, y, width, height


def test_esphome_host_renderer_writes_native_and_scaled_previews(tmp_path: Path) -> None:
    output = tmp_path / "van.png"
    args = render_oled.build_parser().parse_args(
        ["--node", "van", "--output", str(output), "--scale", "4"]
    )

    scaled_output = render_oled.render_preview(args)

    assert png_dimensions(output) == (128, 64)
    assert scaled_output == tmp_path / "van@4x.png"
    assert png_dimensions(scaled_output) == (512, 256)
    assert output.stat().st_size > 100

    _, header_y, _, header_height = trimmed_bounds(output, "128x10+0+0")
    _, soc_y, _, soc_height = trimmed_bounds(output, "78x31+0+9")
    header_bottom = header_y + header_height - 1
    soc_bottom = soc_y + soc_height - 1
    gap_below_header = soc_y - header_bottom - 1
    gap_above_battery = 40 - soc_bottom - 1
    assert abs(gap_below_header - gap_above_battery) <= 1
