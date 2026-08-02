#pragma once

// External Victron Instant Readout decoder shared by Meshtastic targets.

#include <array>
#include <cstddef>
#include <cstdint>

namespace victron {
constexpr uint16_t MANUFACTURER_ID = 0x02e1;
constexpr uint8_t PRODUCT_ADVERTISEMENT = 0x10;
constexpr uint8_t SOLAR_CHARGER = 0x01;
constexpr uint8_t BATTERY_MONITOR = 0x02;
constexpr size_t MAX_ENCRYPTED_SIZE = 16;

struct BatteryMetrics {
  bool hasVoltage = false;
  bool hasCurrent = false;
  bool hasSoc = false;
  float voltage = 0.0f;
  float current = 0.0f;
  float soc = 0.0f;
};

struct Advertisement {
  uint8_t recordType = 0;
  uint16_t counter = 0;
  const uint8_t *encrypted = nullptr;
  size_t encryptedSize = 0;
};

bool parseAdvertisement(const uint8_t *manufacturerData, size_t size,
                        uint8_t keyByteZero, Advertisement &advertisement);
bool decrypt(const Advertisement &advertisement,
             const std::array<uint8_t, 16> &key,
             std::array<uint8_t, MAX_ENCRYPTED_SIZE> &plaintext);
bool decodeBatteryMonitor(const uint8_t *plaintext, size_t size,
                          BatteryMetrics &metrics);
bool decodeSolarCharger(const uint8_t *plaintext, size_t size,
                        BatteryMetrics &metrics);
} // namespace victron
