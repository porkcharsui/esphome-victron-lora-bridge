#!/usr/bin/env python3
"""Atomically inject Victron BLE identities from 1Password into the ignored .env."""

from __future__ import annotations

import json
import os
from pathlib import Path
import re
import subprocess
import tempfile
from typing import Callable

from dotenv import dotenv_values

ROOT = Path(__file__).resolve().parents[1]
ENV_FILE = ROOT / ".env"
DEFAULT_MAC_FIELD = "mac"
DEFAULT_KEY_FIELD = "encryptionKey"
DEFAULT_WIFI_PASSWORD_FIELD = "base station password"
DEFAULT_WIFI_SSID_FIELD = "base station name"
HEX_KEY_RE = re.compile(r"[0-9a-fA-F]{32}\Z")
Runner = Callable[..., subprocess.CompletedProcess[str]]


class OnePasswordError(RuntimeError):
    """A safe-to-display error that never contains a fetched secret."""


def normalize_mac(value: str) -> str | None:
    """Return ESPHome's uppercase colon format for common valid MAC representations."""
    compact = value.strip().replace(":", "").replace("-", "")
    if not re.fullmatch(r"[0-9a-fA-F]{12}", compact):
        return None
    compact = compact.upper()
    return ":".join(compact[index : index + 2] for index in range(0, 12, 2))


def fetch_fields(
    item_id: str,
    labels: tuple[str, ...],
    vault: str | None = None,
    runner: Runner = subprocess.run,
) -> dict[str, str]:
    command = [
        "op",
        "item",
        "get",
        item_id,
        "--fields",
        ",".join(f"label={label}" for label in labels),
        "--format",
        "json",
        "--reveal",
    ]
    if vault:
        command.extend(("--vault", vault))
    try:
        result = runner(command, check=True, capture_output=True, text=True)
        payload = json.loads(result.stdout)
    except (FileNotFoundError, subprocess.CalledProcessError, json.JSONDecodeError) as error:
        raise OnePasswordError(f"could not read requested fields from item {item_id}") from error
    if not isinstance(payload, list):
        raise OnePasswordError(f"unexpected field response for item {item_id}")
    fields = {
        field.get("label"): field.get("value")
        for field in payload
        if isinstance(field, dict) and isinstance(field.get("value"), str)
    }
    missing = [label for label in labels if not fields.get(label)]
    if missing:
        raise OnePasswordError(
            f"item {item_id} is missing requested field(s): {', '.join(missing)}"
        )
    return {label: fields[label] for label in labels}


def replace_dotenv_values(source: str, replacements: dict[str, str]) -> str:
    remaining = dict(replacements)
    output: list[str] = []
    for line in source.splitlines(keepends=True):
        key, separator, _ = line.partition("=")
        if separator and key in remaining:
            output.append(f"{key}={dotenv_quote(remaining.pop(key))}\n")
        else:
            output.append(line)
    if output and not output[-1].endswith("\n"):
        output[-1] += "\n"
    if remaining:
        output.extend(f"{key}={dotenv_quote(value)}\n" for key, value in remaining.items())
    return "".join(output)


def dotenv_quote(value: str) -> str:
    """Quote a literal value using python-dotenv's single-quoted syntax."""
    return "'" + value.replace("\\", "\\\\").replace("'", "\\'") + "'"


def atomic_write(path: Path, payload: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    descriptor, temp_name = tempfile.mkstemp(prefix=f".{path.name}-", dir=path.parent, text=True)
    try:
        os.fchmod(descriptor, 0o600)
        with os.fdopen(descriptor, "w", encoding="utf-8") as handle:
            handle.write(payload)
            handle.flush()
            os.fsync(handle.fileno())
        os.replace(temp_name, path)
        os.chmod(path, 0o600)
    finally:
        if os.path.exists(temp_name):
            os.unlink(temp_name)


def setting(values: dict[str, str | None], name: str, default: str = "") -> str:
    return os.environ.get(name) or values.get(name) or default


def main() -> int:
    if not ENV_FILE.exists():
        print("1Password injection failed: copy .env.example to .env and configure it")
        return 2
    source_path = ENV_FILE
    values = {
        key: value for key, value in dotenv_values(source_path, interpolate=False).items()
    }
    required_settings = (
        "OP_SOLAR_ITEM_ID",
        "OP_SHUNT_ITEM_ID",
        "OP_NETWORK_ITEM_ID",
        "OP_NETWORK_VAULT",
    )
    missing_settings = [name for name in required_settings if not setting(values, name)]
    if missing_settings:
        print("1Password injection failed: missing configuration:")
        for name in missing_settings:
            print(f"- {name}")
        return 2

    solar_item = setting(values, "OP_SOLAR_ITEM_ID")
    shunt_item = setting(values, "OP_SHUNT_ITEM_ID")
    network_item = setting(values, "OP_NETWORK_ITEM_ID")
    mac_field = setting(values, "OP_MAC_FIELD", DEFAULT_MAC_FIELD)
    key_field = setting(values, "OP_ENCRYPTION_KEY_FIELD", DEFAULT_KEY_FIELD)
    wifi_password_field = setting(
        values, "OP_WIFI_PASSWORD_FIELD", DEFAULT_WIFI_PASSWORD_FIELD
    )
    wifi_ssid_field = setting(values, "OP_WIFI_SSID_FIELD", DEFAULT_WIFI_SSID_FIELD)
    vault = setting(values, "OP_VAULT") or None
    network_vault = setting(values, "OP_NETWORK_VAULT")

    try:
        solar = fetch_fields(solar_item, (mac_field, key_field), vault)
        shunt = fetch_fields(shunt_item, (mac_field, key_field), vault)
        network = fetch_fields(
            network_item, (wifi_password_field, wifi_ssid_field), network_vault
        )
    except OnePasswordError as error:
        print(f"1Password injection failed: {error}")
        return 2

    replacements = {
        "VICTRON_SOLAR_MAC": solar[mac_field].strip(),
        "VICTRON_SOLAR_KEY": solar[key_field].strip(),
        "VICTRON_SHUNT_MAC": shunt[mac_field].strip(),
        "VICTRON_SHUNT_KEY": shunt[key_field].strip(),
        "WIFI_PASSWORD": network[wifi_password_field],
        "WIFI_SSID": network[wifi_ssid_field],
    }
    errors: list[str] = []
    for name in ("VICTRON_SOLAR_MAC", "VICTRON_SHUNT_MAC"):
        normalized = normalize_mac(replacements[name])
        if normalized is None:
            errors.append(f"{name}: expected exactly 12 hexadecimal digits")
        else:
            replacements[name] = normalized
    for name in ("VICTRON_SOLAR_KEY", "VICTRON_SHUNT_KEY"):
        if not HEX_KEY_RE.fullmatch(replacements[name]):
            errors.append(f"{name}: expected exactly 32 hexadecimal characters")
    if not replacements["WIFI_SSID"] or any(
        character in replacements["WIFI_SSID"] for character in "\0\r\n"
    ):
        errors.append("WIFI_SSID: must be non-empty and contain no control line breaks")
    elif len(replacements["WIFI_SSID"].encode("utf-8")) > 32:
        errors.append("WIFI_SSID: must be at most 32 bytes")
    if len(replacements["WIFI_PASSWORD"]) < 12 or any(
        character in replacements["WIFI_PASSWORD"] for character in "\0\r\n"
    ):
        errors.append("WIFI_PASSWORD: must contain at least 12 characters and no line breaks")
    if errors:
        print("1Password values were not written:")
        for error in errors:
            print(f"- {error}")
        return 2

    source = source_path.read_text(encoding="utf-8")
    atomic_write(ENV_FILE, replace_dotenv_values(source, replacements))
    print("Injected four Victron and two Wi-Fi values into .env with mode 0600")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
