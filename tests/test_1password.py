from __future__ import annotations

import importlib.util
from io import StringIO
import json
from pathlib import Path
import subprocess

import pytest
from dotenv import dotenv_values


ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location(
    "inject_1password", ROOT / "scripts" / "inject_1password.py"
)
assert SPEC and SPEC.loader
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


@pytest.mark.parametrize(
    "value",
    (
        "aa:bb:cc:dd:ee:01",
        "AA-BB-CC-DD-EE-01",
        "aabbccddee01",
        "  aa:bb:cc:dd:ee:01  ",
    ),
)
def test_normalizes_common_mac_formats(value: str) -> None:
    expected = ":".join(("AA", "BB", "CC", "DD", "EE", "01"))
    assert MODULE.normalize_mac(value) == expected


@pytest.mark.parametrize("value", ("", "AA:BB:CC", "GG:BB:CC:DD:EE:01", "AABBCCDDEE0123"))
def test_rejects_malformed_mac_addresses(value: str) -> None:
    assert MODULE.normalize_mac(value) is None


def test_fetches_only_named_fields_with_optional_vault() -> None:
    observed: list[list[str]] = []
    mac = ":".join(("AA", "BB", "CC", "DD", "EE", "01"))

    def runner(command: list[str], **kwargs: object) -> subprocess.CompletedProcess[str]:
        observed.append(command)
        payload = [
            {"label": "mac", "value": mac},
            {"label": "encryptionKey", "value": "1" * 32},
        ]
        return subprocess.CompletedProcess(command, 0, json.dumps(payload), "")

    fields = MODULE.fetch_fields(
        "item-id", ("mac", "encryptionKey"), vault="Telemetry", runner=runner
    )
    assert fields == {"mac": mac, "encryptionKey": "1" * 32}
    assert observed == [
        [
            "op",
            "item",
            "get",
            "item-id",
            "--fields",
            "label=mac,label=encryptionKey",
            "--format",
            "json",
            "--reveal",
            "--vault",
            "Telemetry",
        ]
    ]


def test_missing_field_is_rejected_without_value_disclosure() -> None:
    def runner(command: list[str], **kwargs: object) -> subprocess.CompletedProcess[str]:
        return subprocess.CompletedProcess(command, 0, json.dumps([]), "")

    with pytest.raises(MODULE.OnePasswordError, match="missing requested field"):
        MODULE.fetch_fields("item-id", ("mac",), runner=runner)


def test_dotenv_replacement_preserves_unrelated_values() -> None:
    source = "WIFI_SSID=house\nVICTRON_SHUNT_MAC=old\nTRANSPORT_KEY=keep\n"
    mac = ":".join(("AA", "BB", "CC", "DD", "EE", "01"))
    result = MODULE.replace_dotenv_values(
        source,
        {
            "VICTRON_SHUNT_MAC": mac,
            "VICTRON_SHUNT_KEY": "1" * 32,
        },
    )
    parsed = dotenv_values(stream=StringIO(result), interpolate=False)
    assert parsed["WIFI_SSID"] == "house"
    assert parsed["TRANSPORT_KEY"] == "keep"
    assert parsed["VICTRON_SHUNT_MAC"] == mac
    assert parsed["VICTRON_SHUNT_KEY"] == "1" * 32


def test_dotenv_replacement_round_trips_special_wifi_characters() -> None:
    password = "".join(("pa", "ss'", "\\word #", "${LITERAL}"))
    result = MODULE.replace_dotenv_values(
        "WIFI_SSID=old\nWIFI_PASSWORD=old\n",
        {"WIFI_SSID": "House Wi-Fi", "WIFI_PASSWORD": password},
    )
    parsed = dotenv_values(stream=StringIO(result), interpolate=False)
    assert parsed["WIFI_SSID"] == "House Wi-Fi"
    assert parsed["WIFI_PASSWORD"] == password


def test_main_injects_victron_and_wifi_atomically(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    target = tmp_path / ".env"
    solar_item = "solar-example-item"
    shunt_item = "shunt-example-item"
    network_item = "network-example-item"
    network_vault = "Telemetry Example"
    target.write_text(
        "WIFI_SSID=old\n"
        "WIFI_PASSWORD=old\n"
        f"OP_SOLAR_ITEM_ID={solar_item}\n"
        f"OP_SHUNT_ITEM_ID={shunt_item}\n"
        f"OP_NETWORK_ITEM_ID={network_item}\n"
        f"OP_NETWORK_VAULT={network_vault}\n"
    )
    monkeypatch.setattr(MODULE, "ENV_FILE", target)
    password = "-".join(("long", "base", "station", "password"))
    ssid = "Base Station"

    def fetch(item_id: str, labels: tuple[str, ...], vault: str | None = None) -> dict[str, str]:
        if item_id == network_item:
            assert vault == network_vault
            return {
                MODULE.DEFAULT_WIFI_PASSWORD_FIELD: password,
                MODULE.DEFAULT_WIFI_SSID_FIELD: ssid,
            }
        suffix = "01" if item_id == shunt_item else "02"
        return {
            MODULE.DEFAULT_MAC_FIELD: ":".join(("aa", "bb", "cc", "dd", "ee", suffix)),
            MODULE.DEFAULT_KEY_FIELD: suffix[0] * 32,
        }

    monkeypatch.setattr(MODULE, "fetch_fields", fetch)
    assert MODULE.main() == 0
    parsed = dotenv_values(target, interpolate=False)
    assert parsed["WIFI_SSID"] == ssid
    assert parsed["WIFI_PASSWORD"] == password
    assert parsed["VICTRON_SHUNT_MAC"] == ":".join(("AA", "BB", "CC", "DD", "EE", "01"))
    assert parsed["VICTRON_SOLAR_MAC"] == ":".join(("AA", "BB", "CC", "DD", "EE", "02"))
    assert target.stat().st_mode & 0o777 == 0o600
    output = capsys.readouterr().out
    assert password not in output
    assert ssid not in output


def test_main_rejects_missing_1password_configuration(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    target = tmp_path / ".env"
    target.write_text("OP_SOLAR_ITEM_ID=solar-example-item\n")
    monkeypatch.setattr(MODULE, "ENV_FILE", target)

    def unexpected_fetch(*args: object, **kwargs: object) -> dict[str, str]:
        pytest.fail("fetch_fields must not run with incomplete configuration")

    monkeypatch.setattr(MODULE, "fetch_fields", unexpected_fetch)
    assert MODULE.main() == 2
    output = capsys.readouterr().out
    assert "OP_SHUNT_ITEM_ID" in output
    assert "OP_NETWORK_ITEM_ID" in output
    assert "OP_NETWORK_VAULT" in output


def test_atomic_write_uses_owner_only_permissions(tmp_path: Path) -> None:
    target = tmp_path / ".env"
    MODULE.atomic_write(target, "KEY=value\n")
    assert target.read_text() == "KEY=value\n"
    assert target.stat().st_mode & 0o777 == 0o600
