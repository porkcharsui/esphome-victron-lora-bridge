from __future__ import annotations

from esphome import automation
from esphome.components.packet_transport import (
    CONF_ENCRYPTION,
    CONF_NAME,
    CONF_PROVIDERS,
    PacketTransport,
    hash_encryption_key,
    new_packet_transport,
    transport_schema,
)
import esphome.codegen as cg
import esphome.config_validation as cv

from .. import CONF_SX1280_ID, SX1280, SX1280Listener, sx1280_ns


CONF_REPEAT_COUNT = "repeat_count"
CONF_REPEAT_JITTER = "repeat_jitter"
CONF_SEND_ON_UPDATE = "send_on_update"
CONF_TRANSMIT_ENABLED = "transmit_enabled"
CONF_ON_FIRST_PACKET = "on_first_packet"

SX1280Transport = sx1280_ns.class_(
    "SX1280Transport", PacketTransport, cg.PollingComponent, SX1280Listener
)

CONFIG_SCHEMA = transport_schema(SX1280Transport).extend(
    {
        cv.GenerateID(CONF_SX1280_ID): cv.use_id(SX1280),
        cv.Optional(CONF_SEND_ON_UPDATE, default=True): cv.boolean,
        cv.Optional(CONF_TRANSMIT_ENABLED, default=True): cv.boolean,
        cv.Optional(CONF_REPEAT_COUNT, default=3): cv.int_range(min=1, max=8),
        cv.Optional(CONF_REPEAT_JITTER, default="250ms"): cv.All(
            cv.positive_time_period_milliseconds,
            cv.Range(max=cv.TimePeriod(milliseconds=5000)),
        ),
        cv.Optional(CONF_ON_FIRST_PACKET): automation.validate_automation(single=True),
    }
)


async def to_code(config):
    var, _ = await new_packet_transport(config)
    radio = await cg.get_variable(config[CONF_SX1280_ID])
    cg.add(var.set_parent(radio))
    cg.add(var.set_send_on_update(config[CONF_SEND_ON_UPDATE]))
    cg.add(var.set_transmit_enabled(config[CONF_TRANSMIT_ENABLED]))
    cg.add(var.set_repeat_count(config[CONF_REPEAT_COUNT]))
    cg.add(var.set_repeat_jitter(config[CONF_REPEAT_JITTER].total_milliseconds))
    if CONF_ON_FIRST_PACKET in config:
        await automation.build_automation(
            var.get_first_packet_trigger(), [], config[CONF_ON_FIRST_PACKET]
        )
    for provider in config[CONF_PROVIDERS]:
        if encryption := provider.get(CONF_ENCRYPTION):
            cg.add(
                var.add_auth_provider(
                    provider[CONF_NAME], hash_encryption_key(encryption)
                )
            )
