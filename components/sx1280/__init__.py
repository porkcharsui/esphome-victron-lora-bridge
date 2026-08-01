from __future__ import annotations

from typing import Any

from esphome import pins
import esphome.codegen as cg
from esphome.components import spi
import esphome.config_validation as cv
from esphome.const import CONF_CS_PIN, CONF_FREQUENCY, CONF_ID, CONF_NUMBER


CODEOWNERS = []
DEPENDENCIES = ["spi"]
AUTO_LOAD = ["sensor"]
MULTI_CONF = True

CONF_SX1280_ID = "sx1280_id"
CONF_RST_PIN = "rst_pin"
CONF_BUSY_PIN = "busy_pin"
CONF_DIO1_PIN = "dio1_pin"
CONF_TX_ENABLE_PIN = "tx_enable_pin"
CONF_RX_ENABLE_PIN = "rx_enable_pin"
CONF_BANDWIDTH = "bandwidth"
CONF_SPREADING_FACTOR = "spreading_factor"
CONF_CODING_RATE = "coding_rate"
CONF_PREAMBLE = "preamble"
CONF_CRC = "crc"
CONF_OUTPUT_POWER = "output_power"
CONF_RX_START = "rx_start"
CONF_TX_QUEUE_SIZE = "tx_queue_size"
CONF_RECOVERY_THRESHOLD = "recovery_threshold"

sx1280_ns = cg.esphome_ns.namespace("sx1280")
SX1280 = sx1280_ns.class_("SX1280", cg.Component, spi.SPIDevice)
SX1280Listener = sx1280_ns.class_("SX1280Listener")

BANDWIDTHS = {
    "203.125kHz": 203.125,
    "406.25kHz": 406.25,
    "812.5kHz": 812.5,
    "1625kHz": 1625.0,
}
CODING_RATES = {"4/5": 5, "4/6": 6, "4/7": 7, "4/8": 8}


def _pin_number(value: Any) -> int | None:
    if isinstance(value, dict):
        return value.get(CONF_NUMBER)
    return getattr(value, "number", None)


def _validate(config: dict[str, Any]) -> dict[str, Any]:
    pin_fields = (
        CONF_CS_PIN,
        CONF_RST_PIN,
        CONF_BUSY_PIN,
        CONF_DIO1_PIN,
        CONF_TX_ENABLE_PIN,
        CONF_RX_ENABLE_PIN,
    )
    seen: dict[int, str] = {}
    for field in pin_fields:
        number = _pin_number(config[field])
        if number is not None and number in seen:
            raise cv.Invalid(f"{field} aliases {seen[number]} on GPIO{number}")
        if number is not None:
            seen[number] = field

    frequency_mhz = config[CONF_FREQUENCY] / 1_000_000
    if not 2400.0 <= frequency_mhz <= 2500.0:
        raise cv.Invalid("SX1280 LoRa frequency must be between 2400 and 2500 MHz")
    return config


CONFIG_SCHEMA = (
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(SX1280),
            cv.Required(CONF_RST_PIN): pins.gpio_output_pin_schema,
            cv.Required(CONF_BUSY_PIN): pins.gpio_input_pin_schema,
            cv.Required(CONF_DIO1_PIN): pins.gpio_input_pin_schema,
            cv.Required(CONF_TX_ENABLE_PIN): pins.gpio_output_pin_schema,
            cv.Required(CONF_RX_ENABLE_PIN): pins.gpio_output_pin_schema,
            cv.Required(CONF_FREQUENCY): cv.All(
                cv.frequency, cv.int_range(min=2_400_000_000, max=2_500_000_000)
            ),
            cv.Optional(CONF_BANDWIDTH, default="406.25kHz"): cv.enum(BANDWIDTHS),
            cv.Optional(CONF_SPREADING_FACTOR, default=7): cv.int_range(min=5, max=12),
            cv.Optional(CONF_CODING_RATE, default="4/6"): cv.enum(CODING_RATES),
            cv.Optional(CONF_PREAMBLE, default=12): cv.int_range(min=6, max=65535),
            cv.Optional(CONF_CRC, default=True): cv.boolean,
            cv.Optional(CONF_OUTPUT_POWER, default=3): cv.int_range(min=-18, max=3),
            cv.Optional(CONF_RX_START, default=True): cv.boolean,
            cv.Optional(CONF_TX_QUEUE_SIZE, default=8): cv.int_range(min=1, max=32),
            cv.Optional(CONF_RECOVERY_THRESHOLD, default=3): cv.int_range(min=1, max=20),
        }
    )
    .extend(cv.COMPONENT_SCHEMA)
    .extend(spi.spi_device_schema(cs_pin_required=True, default_data_rate=8_000_000, default_mode="mode0"))
    .add_extra(_validate)
)


async def to_code(config: dict[str, Any]) -> None:
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await spi.register_spi_device(var, config)
    for field, setter in (
        (CONF_RST_PIN, var.set_rst_pin),
        (CONF_BUSY_PIN, var.set_busy_pin),
        (CONF_DIO1_PIN, var.set_dio1_pin),
        (CONF_TX_ENABLE_PIN, var.set_tx_enable_pin),
        (CONF_RX_ENABLE_PIN, var.set_rx_enable_pin),
    ):
        pin = await cg.gpio_pin_expression(config[field])
        cg.add(setter(pin))
    cg.add(var.set_frequency(config[CONF_FREQUENCY] / 1_000_000.0))
    cg.add(var.set_bandwidth(config[CONF_BANDWIDTH]))
    cg.add(var.set_spreading_factor(config[CONF_SPREADING_FACTOR]))
    cg.add(var.set_coding_rate(config[CONF_CODING_RATE]))
    cg.add(var.set_preamble(config[CONF_PREAMBLE]))
    cg.add(var.set_crc(config[CONF_CRC]))
    cg.add(var.set_output_power(config[CONF_OUTPUT_POWER]))
    cg.add(var.set_rx_start(config[CONF_RX_START]))
    cg.add(var.set_tx_queue_size(config[CONF_TX_QUEUE_SIZE]))
    cg.add(var.set_recovery_threshold(config[CONF_RECOVERY_THRESHOLD]))
    # ESPHome 2026.6 selectively compiles Arduino libraries. RadioLib's HAL
    # headers reference SPI even though this component supplies a custom HAL.
    cg.add_library("SPI", None)
    cg.add_library("jgromes/RadioLib", "7.7.1")
