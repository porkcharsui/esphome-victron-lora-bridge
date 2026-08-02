#include "VictronTelemetryModule.h"

// The platform-specific scanner backends share the decoder and mesh behavior.

#if defined(HAS_VICTRON_BLE) && (defined(ARCH_ESP32) || defined(ARCH_NRF52))

#include "MeshService.h"
#include "NodeDB.h"
#include "airtime.h"
#include "concurrency/LockGuard.h"
#include "gps/RTC.h"
#include "main.h"
#include "mesh/Throttle.h"
#include "victron_secrets.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <meshUtils.h>

#if defined(ARCH_ESP32)
#include "nimble/NimbleBluetooth.h"
#include <host/ble_gap.h>
#include <host/ble_hs.h>
#include <host/ble_hs_id.h>
#endif

#if defined(ARCH_NRF52)
VictronTelemetryModule *VictronTelemetryModule::instance = nullptr;
#endif

VictronTelemetryModule::VictronTelemetryModule()
    : concurrency::OSThread("VictronTelemetry"),
      ProtobufModule("VictronTelemetry", meshtastic_PortNum_TELEMETRY_APP,
                     &meshtastic_Telemetry_msg) {
#if defined(ARCH_NRF52)
  instance = this;
  bluetoothStatusObserver.observe(&bluetoothStatus->onNewStatus);
#endif
  setIntervalFromNow(10 * 1000);
}

bool VictronTelemetryModule::handleReceivedProtobuf(
    const meshtastic_MeshPacket &packet, meshtastic_Telemetry *telemetry) {
  if (telemetry->which_variant == meshtastic_Telemetry_device_metrics_tag) {
    nodeDB->updateTelemetry(getFrom(&packet), *telemetry, RX_SRC_RADIO);
  }
  return false;
}

meshtastic_MeshPacket *VictronTelemetryModule::allocReply() { return nullptr; }

#if defined(ARCH_ESP32)
int VictronTelemetryModule::handleGapEvent(ble_gap_event *event,
                                           void *context) {
  if (event->type == BLE_GAP_EVENT_DISC) {
    static_cast<VictronTelemetryModule *>(context)->processDiscovery(
        event->disc.addr.val, event->disc.data, event->disc.length_data);
  }
  return 0;
}
#elif defined(ARCH_NRF52)
void VictronTelemetryModule::handleAdvertisement(
    ble_gap_evt_adv_report_t *report) {
  if (instance) {
    instance->processDiscovery(report->peer_addr.addr, report->data.p_data,
                               report->data.len);
  }
  Bluefruit.Scanner.resume();
}

int VictronTelemetryModule::handleBluetoothStatus(
    const meshtastic::Status *status) {
  if (status->getStatusType() == STATUS_TYPE_BLUETOOTH &&
      bluetoothStatus->getConnectionState() !=
          meshtastic::BluetoothStatus::ConnectionState::DISCONNECTED &&
      Bluefruit.Scanner.isRunning()) {
    Bluefruit.Scanner.stop();
    LOG_DEBUG("Victron BLE scan paused for phone connection");
  }
  return 0;
}
#endif

bool VictronTelemetryModule::addressMatches(
    const uint8_t *nimbleAddress,
    const std::array<uint8_t, 6> &configuredAddress) {
  for (size_t index = 0; index < configuredAddress.size(); ++index) {
    if (nimbleAddress[configuredAddress.size() - 1 - index] !=
        configuredAddress[index]) {
      return false;
    }
  }
  return true;
}

void VictronTelemetryModule::processDiscovery(const uint8_t *address,
                                              const uint8_t *data,
                                              size_t size) {
  bool solar = false;
  if (!addressMatches(address, VictronConfig::SHUNT_MAC)) {
    if (!VictronConfig::SOLAR_ENABLED ||
        !addressMatches(address, VictronConfig::SOLAR_MAC)) {
      return;
    }
    solar = true;
  }

  size_t offset = 0;
  while (offset < size) {
    uint8_t fieldLength = data[offset];
    if (fieldLength == 0 || offset + fieldLength + 1 > size) {
      return;
    }
    uint8_t fieldType = data[offset + 1];
    if (fieldType == 0xff && fieldLength > 1) {
      processAdvertisement(solar, data + offset + 2, fieldLength - 1);
      return;
    }
    offset += fieldLength + 1;
  }
}

void VictronTelemetryModule::processAdvertisement(
    bool solar, const uint8_t *manufacturerData, size_t size) {
  const auto &key = solar ? VictronConfig::SOLAR_KEY : VictronConfig::SHUNT_KEY;
  Reading *destination = solar ? &solarReading : &shuntReading;
  uint8_t requiredRecordType =
      solar ? victron::SOLAR_CHARGER : victron::BATTERY_MONITOR;

  victron::Advertisement advertisement;
  if (!victron::parseAdvertisement(manufacturerData, size, key[0],
                                   advertisement) ||
      advertisement.recordType != requiredRecordType) {
    return;
  }

  {
    concurrency::LockGuard lock(&readingsMutex);
    if (destination->hasCounter &&
        destination->counter == advertisement.counter) {
      return;
    }
  }

  std::array<uint8_t, victron::MAX_ENCRYPTED_SIZE> plaintext = {};
  victron::BatteryMetrics decoded;
  if (!victron::decrypt(advertisement, key, plaintext)) {
    return;
  }
  bool valid =
      requiredRecordType == victron::BATTERY_MONITOR
          ? victron::decodeBatteryMonitor(plaintext.data(),
                                          advertisement.encryptedSize, decoded)
          : victron::decodeSolarCharger(plaintext.data(),
                                        advertisement.encryptedSize, decoded);
  if (!valid) {
    return;
  }

  concurrency::LockGuard lock(&readingsMutex);
  destination->metrics = decoded;
  destination->receivedAt = millis();
  destination->counter = advertisement.counter;
  destination->hasCounter = true;
  destination->valid = true;
  if (solar) {
    LOG_DEBUG("Victron BLE valid SmartSolar packet counter=%u voltage=%.2fV "
              "current=%.3fA",
              advertisement.counter, decoded.voltage, decoded.current);
  } else if (decoded.hasSoc) {
    LOG_DEBUG("Victron BLE valid SmartShunt packet counter=%u SOC=%.1f%% "
              "voltage=%.2fV current=%.3fA",
              advertisement.counter, decoded.soc, decoded.voltage,
              decoded.current);
  } else {
    LOG_DEBUG("Victron BLE valid SmartShunt packet counter=%u "
              "SOC=unavailable voltage=%.2fV current=%.3fA",
              advertisement.counter, decoded.voltage, decoded.current);
  }
}

bool VictronTelemetryModule::startScan() {
#if defined(ARCH_ESP32)
  if (!nimbleBluetooth || !nimbleBluetooth->isActive() || !ble_hs_synced()) {
    if (!bluetoothWarningLogged && !config.bluetooth.enabled) {
      LOG_WARN("Victron BLE requires Meshtastic Bluetooth to remain enabled");
      bluetoothWarningLogged = true;
    }
    return false;
  }

  if (ble_gap_disc_active()) {
    return true;
  }

  uint8_t ownAddressType = 0;
  int result = ble_hs_id_infer_auto(0, &ownAddressType);
  if (result == 0) {
    ble_gap_disc_params parameters = {};
    parameters.itvl = 1600;
    parameters.window = 1440;
    parameters.passive = 1;
    parameters.filter_duplicates = 0;
    result = ble_gap_disc(ownAddressType, BLE_HS_FOREVER, &parameters,
                          handleGapEvent, this);
  }
  if (result != 0 && result != BLE_HS_EALREADY) {
    if (!scanFailureLogged) {
      LOG_WARN("Unable to start Victron BLE scan, rc=%d", result);
      scanFailureLogged = true;
    }
    return false;
  }
  scanFailureLogged = false;
  LOG_INFO("Victron BLE passive scan started");
  return true;
#elif defined(ARCH_NRF52)
  if (!config.bluetooth.enabled) {
    if (!bluetoothWarningLogged) {
      LOG_WARN("Victron BLE requires Meshtastic Bluetooth to remain enabled");
      bluetoothWarningLogged = true;
    }
    return false;
  }

  if (bluetoothStatus->getConnectionState() !=
      meshtastic::BluetoothStatus::ConnectionState::DISCONNECTED) {
    if (Bluefruit.Scanner.isRunning()) {
      Bluefruit.Scanner.stop();
    }
    return true;
  }

  if (Bluefruit.Scanner.isRunning()) {
    return true;
  }

  Bluefruit.Scanner.setRxCallback(handleAdvertisement);
  Bluefruit.Scanner.restartOnDisconnect(false);
  Bluefruit.Scanner.setInterval(1600, 400);
  Bluefruit.Scanner.useActiveScan(false);
  if (!Bluefruit.Scanner.start(0)) {
    if (!scanFailureLogged) {
      LOG_WARN("Unable to start Victron BLE scan");
      scanFailureLogged = true;
    }
    return false;
  }
  scanFailureLogged = false;
  LOG_INFO("Victron BLE passive scan started");
  return true;
#endif
}

bool VictronTelemetryModule::isFresh(const Reading &reading,
                                     uint32_t now) const {
  return reading.valid &&
         (now - reading.receivedAt) <= VictronConfig::STALE_AFTER_MS;
}

bool VictronTelemetryModule::hasMeaningfulChange(const Reading &reading) const {
  if (!lastPublishedShunt.valid) {
    return true;
  }
  const auto &current = reading.metrics;
  const auto &previous = lastPublishedShunt.metrics;
  return (current.hasSoc && previous.hasSoc &&
          std::fabs(current.soc - previous.soc) >=
              VictronConfig::SOC_DELTA_PERCENT) ||
         (current.hasVoltage && previous.hasVoltage &&
          std::fabs(current.voltage - previous.voltage) >=
              VictronConfig::VOLTAGE_DELTA) ||
         (current.hasCurrent && previous.hasCurrent &&
          std::fabs(current.current - previous.current) >=
              VictronConfig::CURRENT_DELTA) ||
         current.hasSoc != previous.hasSoc ||
         current.hasVoltage != previous.hasVoltage ||
         current.hasCurrent != previous.hasCurrent;
}

int32_t VictronTelemetryModule::runOnce() {
  startScan();

  Reading shunt;
  Reading solar;
  {
    concurrency::LockGuard lock(&readingsMutex);
    shunt = shuntReading;
    solar = solarReading;
  }

  uint32_t now = millis();
  if (!isFresh(shunt, now)) {
    return 1000;
  }

  if (shunt.metrics.hasSoc) {
    if (lowSocAlerted &&
        shunt.metrics.soc >= VictronConfig::LOW_SOC_REARM_PERCENT) {
      lowSocAlerted = false;
    } else if (!lowSocAlerted &&
               shunt.metrics.soc <= VictronConfig::LOW_SOC_PERCENT &&
               sendLowSocAlert(shunt)) {
      lowSocAlerted = true;
    }
  }

  bool firstSend = lastMeshSend == 0;
  bool periodicSend =
      !firstSend && !Throttle::isWithinTimespanMs(
                        lastMeshSend, VictronConfig::TELEMETRY_INTERVAL_MS);
  bool deltaSend =
      hasMeaningfulChange(shunt) &&
      (firstSend || !Throttle::isWithinTimespanMs(
                        lastMeshSend, VictronConfig::MIN_SEND_INTERVAL_MS));
  if ((firstSend || periodicSend || deltaSend) &&
      airTime->isTxAllowedAirUtil() &&
      config.device.role != meshtastic_Config_DeviceConfig_Role_CLIENT_HIDDEN &&
      sendTelemetry(shunt, solar)) {
    lastMeshSend = now;
    lastPublishedShunt = shunt;
  }
  return 1000;
}

bool VictronTelemetryModule::sendTelemetry(const Reading &shunt,
                                           const Reading &solar) {
  meshtastic_Telemetry device = meshtastic_Telemetry_init_zero;
  device.time = getTime();
  device.which_variant = meshtastic_Telemetry_device_metrics_tag;
  device.variant.device_metrics = meshtastic_DeviceMetrics_init_zero;
  if (shunt.metrics.hasSoc) {
    device.variant.device_metrics.has_battery_level = true;
    device.variant.device_metrics.battery_level = static_cast<uint32_t>(
        std::clamp(std::lround(shunt.metrics.soc), 0L, 100L));
  }
  if (shunt.metrics.hasVoltage) {
    device.variant.device_metrics.has_voltage = true;
    device.variant.device_metrics.voltage = shunt.metrics.voltage;
  }

  meshtastic_Telemetry power = meshtastic_Telemetry_init_zero;
  power.time = device.time;
  power.which_variant = meshtastic_Telemetry_power_metrics_tag;
  power.variant.power_metrics = meshtastic_PowerMetrics_init_zero;
  if (shunt.metrics.hasVoltage) {
    power.variant.power_metrics.has_ch1_voltage = true;
    power.variant.power_metrics.ch1_voltage = shunt.metrics.voltage;
  }
  if (shunt.metrics.hasCurrent) {
    power.variant.power_metrics.has_ch1_current = true;
    power.variant.power_metrics.ch1_current = shunt.metrics.current * 1000.0f;
  }
  if (VictronConfig::SOLAR_ENABLED && isFresh(solar, millis())) {
    if (solar.metrics.hasVoltage) {
      power.variant.power_metrics.has_ch2_voltage = true;
      power.variant.power_metrics.ch2_voltage = solar.metrics.voltage;
    }
    if (solar.metrics.hasCurrent) {
      power.variant.power_metrics.has_ch2_current = true;
      power.variant.power_metrics.ch2_current = solar.metrics.current * 1000.0f;
    }
  }

  bool deviceSent = sendTelemetryPacket(device, true);
  bool powerSent = sendTelemetryPacket(power, false);
  if (deviceSent && powerSent && shunt.metrics.hasSoc) {
    LOG_INFO("Victron telemetry sent: SOC=%.1f%% voltage=%.2fV current=%.3fA",
             shunt.metrics.soc, shunt.metrics.voltage, shunt.metrics.current);
  } else if (deviceSent && powerSent) {
    LOG_INFO("Victron telemetry sent: SOC=unavailable voltage=%.2fV "
             "current=%.3fA",
             shunt.metrics.voltage, shunt.metrics.current);
  }
  return deviceSent && powerSent;
}

bool VictronTelemetryModule::sendTelemetryPacket(
    const meshtastic_Telemetry &telemetry, bool updateLocalNode) {
  meshtastic_MeshPacket *packet = allocDataProtobuf(telemetry);
  if (!packet) {
    return false;
  }
  packet->to = NODENUM_BROADCAST;
  packet->decoded.want_response = false;
  packet->priority =
      config.device.role == meshtastic_Config_DeviceConfig_Role_SENSOR
          ? meshtastic_MeshPacket_Priority_RELIABLE
          : meshtastic_MeshPacket_Priority_BACKGROUND;
  if (updateLocalNode) {
    nodeDB->updateTelemetry(nodeDB->getNodeNum(), telemetry, RX_SRC_LOCAL);
  }
  service->sendToMesh(packet, RX_SRC_LOCAL, true);
  return true;
}

bool VictronTelemetryModule::sendLowSocAlert(const Reading &shunt) {
  if (!airTime->isTxAllowedAirUtil()) {
    return false;
  }
  meshtastic_MeshPacket *packet = allocDataPacket();
  if (!packet) {
    return false;
  }
  char message[96];
  int length = snprintf(
      message, sizeof(message), "Victron battery low: %.1f%% (%.2f V, %.2f A)",
      shunt.metrics.soc, shunt.metrics.voltage, shunt.metrics.current);
  if (length < 0) {
    packetPool.release(packet);
    return false;
  }
  packet->decoded.portnum = meshtastic_PortNum_TEXT_MESSAGE_APP;
  packet->decoded.payload.size = std::min(
      static_cast<size_t>(length), sizeof(packet->decoded.payload.bytes));
  memcpy(packet->decoded.payload.bytes, message, packet->decoded.payload.size);
  packet->to = NODENUM_BROADCAST;
  packet->decoded.want_response = false;
  packet->priority = meshtastic_MeshPacket_Priority_RELIABLE;
  service->sendToMesh(packet, RX_SRC_LOCAL, true);
  LOG_WARN("Victron low SOC alert sent at %.1f%%", shunt.metrics.soc);
  return true;
}

#endif
