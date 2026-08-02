#!/usr/bin/env python3
# ruff: noqa: F821
import os
import re
from pathlib import Path

Import("env")

project_dir = Path(env.subst("$PROJECT_DIR"))
module_dir = (project_dir / "../victron-ble").resolve()
bootstrap = str(module_dir / "src/bootstrap.h")
external_sources = [
    module_dir / "src/bootstrap.h",
    module_dir / "src/register.cpp",
    module_dir / "src/VictronDecoder.cpp",
    module_dir / "src/VictronDecoder.h",
    module_dir / "src/VictronTelemetryModule.cpp",
    module_dir / "src/VictronTelemetryModule.h",
]


def inject_external_module(node):
    try:
        source_path = node.srcnode().get_abspath().replace("\\", "/")
    except AttributeError:
        # Some platform builders pass library node collections through build
        # middleware. They cannot be the single Meshtastic project source we
        # need to replace.
        return node
    if not source_path.endswith("/src/modules/Modules.cpp"):
        return node

    try:
        from SCons.Script.SConscript import global_exports

        project_env = global_exports.get("projenv", env)
    except Exception:
        project_env = env
    # Keep the project's compiler flags untouched. Overriding CCFLAGS here would
    # recursively capture SCons' deferred CCFLAGS callable on some platforms.
    # CPPFLAGS is part of the normal compile command and is the appropriate home
    # for a forced include.
    injected_object = project_env.Object(
        node, CPPFLAGS=list(project_env.get("CPPFLAGS", [])) + ["-include", bootstrap]
    )
    project_env.Depends(
        injected_object,
        [project_env.File(str(path)) for path in external_sources]
        + [project_env.File(str(header))],
    )
    return injected_object


env.AddBuildMiddleware(inject_external_module)


def load_env_file(path):
    values = {}
    if not path.is_file():
        return values
    for raw_line in path.read_text().splitlines():
        line = raw_line.strip()
        if not line or line.startswith("#") or "=" not in line:
            continue
        name, value = line.split("=", 1)
        value = value.strip()
        if len(value) >= 2 and value[0] == value[-1] and value[0] in "\"'":
            value = value[1:-1]
        values[name.strip()] = value
    return values


file_values = load_env_file((project_dir / "../../.env").resolve())


def setting(name, default=None):
    value = os.environ.get(name, file_values.get(name, default))
    if value is None or value == "":
        raise RuntimeError(f"{name} is required for a Victron firmware build")
    return value


def parse_mac(name, required=True):
    value = os.environ.get(name, file_values.get(name, ""))
    if not value and not required:
        return None
    if not re.fullmatch(r"(?:[0-9A-Fa-f]{2}:){5}[0-9A-Fa-f]{2}", value):
        raise RuntimeError(f"{name} must be a six-byte colon-separated MAC address")
    return [int(octet, 16) for octet in value.split(":")]


def parse_key(name, required=True):
    value = os.environ.get(name, file_values.get(name, ""))
    if not value and not required:
        return None
    if not re.fullmatch(r"[0-9A-Fa-f]{32}", value):
        raise RuntimeError(f"{name} must contain exactly 32 hexadecimal characters")
    return [int(value[offset : offset + 2], 16) for offset in range(0, 32, 2)]


def integer_setting(name, default, minimum, maximum):
    try:
        value = int(setting(name, str(default)))
    except ValueError as exc:
        raise RuntimeError(f"{name} must be an integer") from exc
    if not minimum <= value <= maximum:
        raise RuntimeError(f"{name} must be between {minimum} and {maximum}")
    return value


shunt_mac = parse_mac("VICTRON_SHUNT_MAC")
shunt_key = parse_key("VICTRON_SHUNT_KEY")
solar_mac = parse_mac("VICTRON_SOLAR_MAC", required=False)
solar_key = parse_key("VICTRON_SOLAR_KEY", required=False)
if (solar_mac is None) != (solar_key is None):
    raise RuntimeError("VICTRON_SOLAR_MAC and VICTRON_SOLAR_KEY must be set together")

telemetry_interval = integer_setting(
    "VICTRON_TELEMETRY_INTERVAL_SECONDS", 600, 300, 3600
)
minimum_send_interval = integer_setting(
    "VICTRON_MIN_SEND_INTERVAL_SECONDS", 60, 15, telemetry_interval
)
stale_after = integer_setting("VICTRON_STALE_AFTER_SECONDS", 120, 30, 3600)
soc_delta = integer_setting("VICTRON_SOC_DELTA_PERCENT", 1, 1, 100)
voltage_delta_mv = integer_setting("VICTRON_VOLTAGE_DELTA_MV", 100, 10, 10000)
current_delta_ma = integer_setting("VICTRON_CURRENT_DELTA_MA", 1000, 100, 100000)
alert_soc = integer_setting("VICTRON_LOW_SOC_PERCENT", 25, 0, 100)
rearm_soc = integer_setting("VICTRON_LOW_SOC_REARM_PERCENT", 30, alert_soc + 1, 100)


def bytes_literal(values):
    return ", ".join(f"0x{value:02x}" for value in values)


generated_dir = Path(env.subst("$BUILD_DIR")) / "generated"
generated_dir.mkdir(parents=True, exist_ok=True)
header = generated_dir / "victron_secrets.h"
solar_enabled = solar_mac is not None
header_contents = (
    "#pragma once\n"
    "#include <array>\n"
    "#include <cstdint>\n"
    "namespace VictronConfig {\n"
    f"inline constexpr std::array<uint8_t, 6> SHUNT_MAC = {{{bytes_literal(shunt_mac)}}};\n"
    f"inline constexpr std::array<uint8_t, 16> SHUNT_KEY = {{{bytes_literal(shunt_key)}}};\n"
    f"inline constexpr bool SOLAR_ENABLED = {'true' if solar_enabled else 'false'};\n"
    f"inline constexpr std::array<uint8_t, 6> SOLAR_MAC = {{{bytes_literal(solar_mac or [0] * 6)}}};\n"
    f"inline constexpr std::array<uint8_t, 16> SOLAR_KEY = {{{bytes_literal(solar_key or [0] * 16)}}};\n"
    f"inline constexpr uint32_t TELEMETRY_INTERVAL_MS = {telemetry_interval}UL * 1000UL;\n"
    f"inline constexpr uint32_t MIN_SEND_INTERVAL_MS = {minimum_send_interval}UL * 1000UL;\n"
    f"inline constexpr uint32_t STALE_AFTER_MS = {stale_after}UL * 1000UL;\n"
    f"inline constexpr float SOC_DELTA_PERCENT = {soc_delta}.0f;\n"
    f"inline constexpr float VOLTAGE_DELTA = {voltage_delta_mv}.0f / 1000.0f;\n"
    f"inline constexpr float CURRENT_DELTA = {current_delta_ma}.0f / 1000.0f;\n"
    f"inline constexpr uint8_t LOW_SOC_PERCENT = {alert_soc};\n"
    f"inline constexpr uint8_t LOW_SOC_REARM_PERCENT = {rearm_soc};\n"
    "}\n"
)
if not header.is_file() or header.read_text() != header_contents:
    header.write_text(header_contents)
env.Append(CPPPATH=[str(generated_dir)])
print("Victron BLE configuration generated (secrets hidden)")
