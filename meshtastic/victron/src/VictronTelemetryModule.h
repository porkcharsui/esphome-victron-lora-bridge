#pragma once

// Injected by the external PlatformIO overlay; not part of Meshtastic core.

#include "configuration.h"

#if defined(HAS_VICTRON_BLE) && (defined(ARCH_ESP32) || defined(ARCH_NRF52))

#include "ProtobufModule.h"
#include "VictronDecoder.h"
#include "concurrency/Lock.h"
#include "concurrency/OSThread.h"
#include "mesh/generated/meshtastic/telemetry.pb.h"
#include <array>

#if defined(ARCH_ESP32)
#include <host/ble_gap.h>
#elif defined(ARCH_NRF52)
#include "BluetoothStatus.h"
#include <bluefruit.h>
#endif

class VictronTelemetryModule : private concurrency::OSThread,
                               public ProtobufModule<meshtastic_Telemetry> {
public:
  VictronTelemetryModule();

protected:
  int32_t runOnce() override;
  bool handleReceivedProtobuf(const meshtastic_MeshPacket &mp,
                              meshtastic_Telemetry *telemetry) override;
  meshtastic_MeshPacket *allocReply() override;

private:
  struct Reading {
    victron::BatteryMetrics metrics;
    uint32_t receivedAt = 0;
    uint16_t counter = 0;
    bool hasCounter = false;
    bool valid = false;
  };

#if defined(ARCH_ESP32)
  static int handleGapEvent(ble_gap_event *event, void *context);
#elif defined(ARCH_NRF52)
  static void handleAdvertisement(ble_gap_evt_adv_report_t *report);
  static VictronTelemetryModule *instance;
  int handleBluetoothStatus(const meshtastic::Status *status);
  CallbackObserver<VictronTelemetryModule, const meshtastic::Status *>
      bluetoothStatusObserver =
          CallbackObserver<VictronTelemetryModule, const meshtastic::Status *>(
              this, &VictronTelemetryModule::handleBluetoothStatus);
#endif
  static bool addressMatches(const uint8_t *nimbleAddress,
                             const std::array<uint8_t, 6> &configuredAddress);
  void processDiscovery(const uint8_t *address, const uint8_t *data,
                        size_t size);
  void processAdvertisement(bool solar, const uint8_t *manufacturerData,
                            size_t size);
  bool startScan();
  bool sendTelemetry(const Reading &shunt, const Reading &solar);
  bool sendTelemetryPacket(const meshtastic_Telemetry &telemetry,
                           bool updateLocalNode);
  bool sendLowSocAlert(const Reading &shunt);
  bool isFresh(const Reading &reading, uint32_t now) const;
  bool hasMeaningfulChange(const Reading &reading) const;

  concurrency::Lock readingsMutex;
  Reading shuntReading;
  Reading solarReading;
  Reading lastPublishedShunt;
  uint32_t lastMeshSend = 0;
  bool lowSocAlerted = false;
  bool bluetoothWarningLogged = false;
  bool scanFailureLogged = false;
};

#endif
