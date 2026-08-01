#!/usr/bin/env python3
"""Reject committed credentials and exact values from the operator dotenv file."""

from __future__ import annotations

import re
import subprocess
from pathlib import Path

from dotenv import dotenv_values


ROOT = Path(__file__).resolve().parents[1]
ENV_FILE = ROOT / ".env"
TEXT_SUFFIXES = {".md", ".nix", ".py", ".toml", ".yaml", ".yml", ".example"}
LITERAL_PATTERNS = (
    re.compile(r"^\s*(?:password|ota_password|transport_key):\s*(?![!$])\S+", re.I | re.M),
    re.compile(r"^\s*key:\s*(?![!$])(?:[A-Za-z0-9+/]{32,}={0,2}|[0-9A-Fa-f]{32})\s*$", re.M),
    # 1Password item UUIDs are 26 lowercase alphanumeric characters.
    re.compile(r"\b[a-z0-9]{26}\b"),
)
SENSITIVE_ENV_KEYS = {
    "VICTRON_SHUNT_MAC",
    "VICTRON_SOLAR_MAC",
    "VICTRON_SHUNT_KEY",
    "VICTRON_SOLAR_KEY",
    "WIFI_SSID",
    "WIFI_PASSWORD",
    "API_ENCRYPTION_KEY",
    "OTA_PASSWORD",
    "TRANSPORT_KEY",
    "OP_SOLAR_ITEM_ID",
    "OP_SHUNT_ITEM_ID",
    "OP_NETWORK_ITEM_ID",
    "OP_NETWORK_VAULT",
    "OP_VAULT",
}


def tracked_files() -> list[Path]:
    result = subprocess.run(
        ["git", "ls-files", "-z", "--cached", "--others", "--exclude-standard"],
        cwd=ROOT,
        check=True,
        capture_output=True,
    )
    return [ROOT / item.decode() for item in result.stdout.split(b"\0") if item]


def main() -> int:
    dotenv = dotenv_values(ENV_FILE, interpolate=False)
    values = {
        dotenv[key]
        for key in SENSITIVE_ENV_KEYS
        if dotenv.get(key) is not None and len(dotenv[key] or "") >= 8
    }
    failures: list[str] = []
    for path in tracked_files():
        if path.name == ".env.example" or path.suffix not in TEXT_SUFFIXES:
            continue
        try:
            text = path.read_text(encoding="utf-8")
        except UnicodeDecodeError:
            continue
        if any(value in text for value in values):
            failures.append(f"{path.relative_to(ROOT)}: contains a value from .env")
        if any(pattern.search(text) for pattern in LITERAL_PATTERNS):
            failures.append(f"{path.relative_to(ROOT)}: contains a credential-like literal")
    if failures:
        print("Secret scan failed (values are intentionally not shown):")
        for failure in failures:
            print(f"- {failure}")
        return 1
    print("Secret scan passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
