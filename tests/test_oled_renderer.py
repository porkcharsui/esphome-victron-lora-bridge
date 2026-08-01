from __future__ import annotations

import importlib.util
import sys
from pathlib import Path

import pytest
from PIL import Image


ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("render_oled", ROOT / "scripts/render_oled.py")
assert SPEC is not None and SPEC.loader is not None
render_oled = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = render_oled
SPEC.loader.exec_module(render_oled)


@pytest.mark.parametrize(
    ("soc", "expected"),
    ((None, 0), (-10.0, 0), (0.0, 0), (50.0, 60), (100.0, 120), (110.0, 120)),
)
def test_gauge_fill_is_clamped(soc: float | None, expected: int) -> None:
    assert render_oled.gauge_fill_width(soc) == expected


@pytest.mark.parametrize(
    ("rssi", "snr", "expected"),
    (
        (None, 9.0, 0),
        (-67.0, None, 0),
        (-67.0, 9.0, 4),
        (-80.0, 9.0, 3),
        (-67.0, 2.0, 3),
        (-98.0, -2.0, 2),
        (-110.0, 8.0, 1),
        (-70.0, -8.0, 1),
    ),
)
def test_signal_bars_use_weaker_rssi_or_snr(
    rssi: float | None, snr: float | None, expected: int
) -> None:
    assert render_oled.signal_bar_count(rssi, snr) == expected


@pytest.mark.parametrize(
    "state",
    (
        render_oled.VanState(soc=0.0),
        render_oled.VanState(soc=50.0),
        render_oled.VanState(soc=100.0),
        render_oled.VanState(soc=None, voltage=None, current=None, shunt_fresh=False),
        render_oled.VanState(soc=110.0),
    ),
)
def test_van_render_is_native_monochrome(state: object) -> None:
    image = render_oled.render_van(state)
    assert image.mode == "1"
    assert image.size == (128, 64)


@pytest.mark.parametrize(
    "state",
    (
        render_oled.UplinkState(),
        render_oled.UplinkState(link="waiting", age=None, rssi=None, snr=None),
        render_oled.UplinkState(wifi="offline", link="stale", age=300),
    ),
)
def test_uplink_render_is_native_monochrome(state: object) -> None:
    image = render_oled.render_uplink(state)
    assert image.mode == "1"
    assert image.size == (128, 64)


def test_scaled_preview_uses_nearest_neighbor(tmp_path: Path) -> None:
    image = render_oled.render_van(render_oled.VanState(soc=78.0))
    output = tmp_path / "van.png"
    scaled_output = render_oled.save_previews(image, output, scale=4)

    assert output.exists()
    assert scaled_output == tmp_path / "van@4x.png"
    with Image.open(scaled_output) as scaled:
        assert scaled.mode == "1"
        assert scaled.size == (512, 256)
        assert bool(scaled.getpixel((8, 212))) == bool(image.getpixel((2, 53)))
