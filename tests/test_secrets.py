from __future__ import annotations

import base64
import importlib.util
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location(
    "generate_secrets", ROOT / "scripts" / "generate_secrets.py"
)
assert SPEC and SPEC.loader
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


def valid_values() -> dict[str, str]:
    return {
        "VICTRON_SHUNT_MAC": ":".join(("AA", "BB", "CC", "DD", "EE", "01")),
        "VICTRON_SOLAR_MAC": ":".join(("AA", "BB", "CC", "DD", "EE", "02")),
        "VICTRON_SHUNT_KEY": "1" * 32,
        "VICTRON_SOLAR_KEY": "2" * 32,
        "WIFI_SSID": "-".join(("test", "network")),
        "WIFI_PASSWORD": "-".join(("long", "test", "password")),
        "API_ENCRYPTION_KEY": base64.b64encode(b"a" * 32).decode(),
        "OTA_PASSWORD": "-".join(("long", "test", "ota", "password")),
        "TRANSPORT_KEY": "-".join(("transport", "test", "key", "with", "32", "characters")),
    }


def test_valid_environment_is_accepted() -> None:
    assert MODULE.validate(valid_values()) == []


def test_missing_and_malformed_values_are_rejected() -> None:
    values = valid_values()
    values["VICTRON_SHUNT_MAC"] = "aa:bb:cc:dd:ee:ff"
    values["VICTRON_SOLAR_KEY"] = "not-a-key"
    values["API_ENCRYPTION_KEY"] = "not-base64"
    values["TRANSPORT_KEY"] = "short"
    errors = MODULE.validate(values)
    assert len(errors) == 4
    assert all(secret not in "\n".join(errors) for secret in values.values())
