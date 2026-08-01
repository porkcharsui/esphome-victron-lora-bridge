#!/usr/bin/env python3
"""Validate .env and atomically create ESPHome's ignored secrets file."""

from __future__ import annotations

import base64
import os
from pathlib import Path
import re
import tempfile

from dotenv import dotenv_values


ROOT = Path(__file__).resolve().parents[1]
ENV_FILE = ROOT / ".env"
OUTPUT = ROOT / "esphome" / "secrets.yaml"
REQUIRED = (
    "VICTRON_SHUNT_MAC",
    "VICTRON_SOLAR_MAC",
    "VICTRON_SHUNT_KEY",
    "VICTRON_SOLAR_KEY",
    "WIFI_SSID",
    "WIFI_PASSWORD",
    "API_ENCRYPTION_KEY",
    "OTA_PASSWORD",
    "TRANSPORT_KEY",
)
MAC_RE = re.compile(r"(?:[0-9A-F]{2}:){5}[0-9A-F]{2}\Z")
HEX_KEY_RE = re.compile(r"[0-9a-fA-F]{32}\Z")


def validate(values: dict[str, str | None]) -> list[str]:
    errors = [f"{key}: missing" for key in REQUIRED if not values.get(key)]
    for key in ("VICTRON_SHUNT_MAC", "VICTRON_SOLAR_MAC"):
        if values.get(key) and not MAC_RE.fullmatch(values[key] or ""):
            errors.append(f"{key}: expected uppercase XX:XX:XX:XX:XX:XX")
    for key in ("VICTRON_SHUNT_KEY", "VICTRON_SOLAR_KEY"):
        if values.get(key) and not HEX_KEY_RE.fullmatch(values[key] or ""):
            errors.append(f"{key}: expected exactly 32 hexadecimal characters")
    if values.get("WIFI_PASSWORD") and len(values["WIFI_PASSWORD"] or "") < 12:
        errors.append("WIFI_PASSWORD: must contain at least 12 characters")
    if values.get("OTA_PASSWORD") and len(values["OTA_PASSWORD"] or "") < 16:
        errors.append("OTA_PASSWORD: must contain at least 16 characters")
    if values.get("TRANSPORT_KEY") and len(values["TRANSPORT_KEY"] or "") < 32:
        errors.append("TRANSPORT_KEY: must contain at least 32 characters")
    if api_key := values.get("API_ENCRYPTION_KEY"):
        try:
            decoded = base64.b64decode(api_key, validate=True)
            if len(decoded) != 32:
                errors.append("API_ENCRYPTION_KEY: must encode exactly 32 bytes")
        except ValueError:
            errors.append("API_ENCRYPTION_KEY: expected valid base64")
    return errors


def yaml_quote(value: str) -> str:
    return '"' + value.replace("\\", "\\\\").replace('"', '\\"') + '"'


def main() -> int:
    values = {
        key: value for key, value in dotenv_values(ENV_FILE, interpolate=False).items()
    }
    errors = validate(values)
    if errors:
        print("Refusing to generate secrets.yaml:")
        for error in errors:
            print(f"- {error}")
        return 2

    mapping = {
        "victron_shunt_mac": "VICTRON_SHUNT_MAC",
        "victron_solar_mac": "VICTRON_SOLAR_MAC",
        "victron_shunt_key": "VICTRON_SHUNT_KEY",
        "victron_solar_key": "VICTRON_SOLAR_KEY",
        "wifi_ssid": "WIFI_SSID",
        "wifi_password": "WIFI_PASSWORD",
        "api_encryption_key": "API_ENCRYPTION_KEY",
        "ota_password": "OTA_PASSWORD",
        "transport_key": "TRANSPORT_KEY",
    }
    payload = "".join(
        f"{yaml_key}: {yaml_quote(values[env_key] or '')}\n"
        for yaml_key, env_key in mapping.items()
    )
    OUTPUT.parent.mkdir(parents=True, exist_ok=True)
    descriptor, temp_name = tempfile.mkstemp(prefix=".secrets-", dir=OUTPUT.parent, text=True)
    try:
        os.fchmod(descriptor, 0o600)
        with os.fdopen(descriptor, "w", encoding="utf-8") as handle:
            handle.write(payload)
            handle.flush()
            os.fsync(handle.fileno())
        os.replace(temp_name, OUTPUT)
        os.chmod(OUTPUT, 0o600)
    finally:
        if os.path.exists(temp_name):
            os.unlink(temp_name)
    print("Generated esphome/secrets.yaml with mode 0600")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
