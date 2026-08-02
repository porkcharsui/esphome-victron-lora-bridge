from __future__ import annotations

import subprocess
from pathlib import Path

import pytest


ROOT = Path(__file__).resolve().parents[1]
CONFIGS = (
    "esphome/victron.yml",
    "esphome/victron-maintenance.yml",
    "esphome/bridge.yml",
    "esphome/oled-preview.yml",
    "tests/fixtures/raw-radio.yml",
)


@pytest.mark.parametrize("config", CONFIGS)
def test_esphome_configuration(config: str) -> None:
    result = subprocess.run(
        ["esphome", "config", config], cwd=ROOT, text=True, capture_output=True
    )
    assert result.returncode == 0, result.stdout + result.stderr


@pytest.mark.parametrize(
    ("replacements", "message"),
    (
        ((("output_power: 3", "output_power: 4"),), "at most 3"),
        ((("rx_enable_pin: GPIO21", ""),), "required"),
        ((("rx_enable_pin: GPIO21", "rx_enable_pin: GPIO10"),), "aliases"),
        ((("spreading_factor: 7", "spreading_factor: 4"),), "at least 5"),
        ((("tx_queue_size: 8", "tx_queue_size: 33"),), "at most 32"),
    ),
)
def test_invalid_radio_schema_is_rejected(
    tmp_path: Path, replacements: tuple[tuple[str, str], ...], message: str
) -> None:
    source = (ROOT / "tests/fixtures/raw-radio.yml").read_text()
    source = source.replace("../../components", str(ROOT / "components"))
    source = source.replace(
        "board: !include ../../esphome/packages/board.yaml",
        f"board: !include {ROOT / 'esphome/packages/board.yaml'}",
    )
    radio = (ROOT / "esphome/packages/radio.yaml").read_text()
    for replacement in replacements:
        radio = radio.replace(*replacement)
    radio_path = tmp_path / "radio.yaml"
    radio_path.write_text(radio)
    source = source.replace(
        "radio: !include ../../esphome/packages/radio.yaml", f"radio: !include {radio_path}"
    )
    candidate = tmp_path / "invalid.yml"
    candidate.write_text(source)
    result = subprocess.run(
        ["esphome", "config", str(candidate)], cwd=ROOT, text=True, capture_output=True
    )
    assert result.returncode != 0
    assert message in (result.stdout + result.stderr).lower()
