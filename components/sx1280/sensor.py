from esphome.components import sensor
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import (
    CONF_TYPE,
    ENTITY_CATEGORY_DIAGNOSTIC,
    ICON_COUNTER,
    STATE_CLASS_MEASUREMENT,
    UNIT_DECIBEL,
    UNIT_SECOND,
)

from . import CONF_SX1280_ID, SX1280


TYPES = {
    "tx_count": sensor.sensor_schema(entity_category=ENTITY_CATEGORY_DIAGNOSTIC, icon=ICON_COUNTER),
    "rx_count": sensor.sensor_schema(entity_category=ENTITY_CATEGORY_DIAGNOSTIC, icon=ICON_COUNTER),
    "valid_packets": sensor.sensor_schema(entity_category=ENTITY_CATEGORY_DIAGNOSTIC, icon=ICON_COUNTER),
    "radio_failures": sensor.sensor_schema(entity_category=ENTITY_CATEGORY_DIAGNOSTIC, icon=ICON_COUNTER),
    "dropped_packets": sensor.sensor_schema(entity_category=ENTITY_CATEGORY_DIAGNOSTIC, icon=ICON_COUNTER),
    "oversized_packets": sensor.sensor_schema(entity_category=ENTITY_CATEGORY_DIAGNOSTIC, icon=ICON_COUNTER),
    "recovery_count": sensor.sensor_schema(entity_category=ENTITY_CATEGORY_DIAGNOSTIC, icon=ICON_COUNTER),
    "last_error": sensor.sensor_schema(entity_category=ENTITY_CATEGORY_DIAGNOSTIC),
    "last_packet_age": sensor.sensor_schema(
        entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
        unit_of_measurement=UNIT_SECOND,
        accuracy_decimals=0,
        state_class=STATE_CLASS_MEASUREMENT,
    ),
    "rssi": sensor.sensor_schema(
        entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
        unit_of_measurement="dBm",
        accuracy_decimals=1,
        state_class=STATE_CLASS_MEASUREMENT,
    ),
    "snr": sensor.sensor_schema(
        entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
        unit_of_measurement=UNIT_DECIBEL,
        accuracy_decimals=1,
        state_class=STATE_CLASS_MEASUREMENT,
    ),
}

CONFIG_SCHEMA = cv.typed_schema(
    {
        key: schema.extend({cv.GenerateID(CONF_SX1280_ID): cv.use_id(SX1280)})
        for key, schema in TYPES.items()
    },
    key=CONF_TYPE,
)


async def to_code(config):
    var = await sensor.new_sensor(config)
    parent = await cg.get_variable(config[CONF_SX1280_ID])
    cg.add(getattr(parent, f"set_{config[CONF_TYPE]}_sensor")(var))
