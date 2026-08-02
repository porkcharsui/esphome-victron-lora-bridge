#include "VictronDecoder.h"

#include <AES.h>

namespace victron {
namespace {
uint16_t readU16(const uint8_t *value) {
  return static_cast<uint16_t>(value[0]) |
         (static_cast<uint16_t>(value[1]) << 8);
}

int16_t readI16(const uint8_t *value) {
  return static_cast<int16_t>(readU16(value));
}
} // namespace

bool parseAdvertisement(const uint8_t *data, size_t size, uint8_t keyByteZero,
                        Advertisement &advertisement) {
  constexpr size_t headerSize = 10;
  if (!data || size <= headerSize || size > headerSize + MAX_ENCRYPTED_SIZE ||
      readU16(data) != MANUFACTURER_ID || data[2] != PRODUCT_ADVERTISEMENT ||
      data[9] != keyByteZero) {
    return false;
  }

  advertisement.recordType = data[6];
  advertisement.counter = readU16(data + 7);
  advertisement.encrypted = data + headerSize;
  advertisement.encryptedSize = size - headerSize;
  return true;
}

bool decrypt(const Advertisement &advertisement,
             const std::array<uint8_t, 16> &key,
             std::array<uint8_t, MAX_ENCRYPTED_SIZE> &plaintext) {
  if (!advertisement.encrypted || advertisement.encryptedSize == 0 ||
      advertisement.encryptedSize > plaintext.size()) {
    return false;
  }

  AES128 cipher;
  if (!cipher.setKey(key.data(), key.size())) {
    return false;
  }

  std::array<uint8_t, 16> nonce = {
      static_cast<uint8_t>(advertisement.counter & 0xff),
      static_cast<uint8_t>(advertisement.counter >> 8)};
  std::array<uint8_t, 16> stream = {};
  cipher.encryptBlock(stream.data(), nonce.data());
  cipher.clear();
  for (size_t index = 0; index < advertisement.encryptedSize; ++index) {
    plaintext[index] = advertisement.encrypted[index] ^ stream[index];
  }
  return true;
}

bool decodeBatteryMonitor(const uint8_t *data, size_t size,
                          BatteryMetrics &metrics) {
  if (!data || size < 15) {
    return false;
  }

  metrics = {};
  int16_t voltage = readI16(data + 2);
  if (voltage != 0x7fff) {
    metrics.hasVoltage = true;
    metrics.voltage = voltage * 0.01f;
  }

  uint32_t currentField = static_cast<uint32_t>(data[8]) |
                          (static_cast<uint32_t>(data[9]) << 8) |
                          (static_cast<uint32_t>(data[10]) << 16);
  uint32_t currentRaw = currentField >> 2;
  if (currentRaw != 0x3fffff) {
    int32_t current = static_cast<int32_t>(currentRaw);
    if (currentRaw & 0x200000) {
      current |= ~0x3fffff;
    }
    metrics.hasCurrent = true;
    metrics.current = current * 0.001f;
  }

  uint32_t socField = static_cast<uint32_t>(data[11]) |
                      (static_cast<uint32_t>(data[12]) << 8) |
                      (static_cast<uint32_t>(data[13]) << 16) |
                      (static_cast<uint32_t>(data[14]) << 24);
  uint16_t socRaw = (socField >> 20) & 0x03ff;
  if (socRaw != 0x03ff) {
    metrics.hasSoc = true;
    metrics.soc = socRaw * 0.1f;
  }
  return metrics.hasVoltage || metrics.hasCurrent || metrics.hasSoc;
}

bool decodeSolarCharger(const uint8_t *data, size_t size,
                        BatteryMetrics &metrics) {
  if (!data || size < 6) {
    return false;
  }

  metrics = {};
  int16_t voltage = readI16(data + 2);
  if (voltage != 0x7fff) {
    metrics.hasVoltage = true;
    metrics.voltage = voltage * 0.01f;
  }
  int16_t current = readI16(data + 4);
  if (current != 0x7fff) {
    metrics.hasCurrent = true;
    metrics.current = current * 0.1f;
  }
  return metrics.hasVoltage || metrics.hasCurrent;
}
} // namespace victron
