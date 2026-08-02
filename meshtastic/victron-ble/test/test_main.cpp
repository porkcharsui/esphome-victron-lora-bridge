#include "VictronDecoder.cpp"
#include "VictronDecoder.h"
#include <array>
#include <unity.h>

void setUp(void) {}
void tearDown(void) {}

void test_parses_victron_product_advertisement(void) {
  uint8_t data[25] = {0xe1, 0x02, 0x10, 0x16, 0x8a,
                      0xa3, 0x02, 0x34, 0x12, 0xab};
  victron::Advertisement advertisement;
  TEST_ASSERT_TRUE(
      victron::parseAdvertisement(data, sizeof(data), 0xab, advertisement));
  TEST_ASSERT_EQUAL_UINT8(victron::BATTERY_MONITOR, advertisement.recordType);
  TEST_ASSERT_EQUAL_HEX16(0x1234, advertisement.counter);
  TEST_ASSERT_EQUAL_UINT(15, advertisement.encryptedSize);
  TEST_ASSERT_EQUAL_PTR(data + 10, advertisement.encrypted);
}

void test_rejects_wrong_manufacturer_and_key(void) {
  uint8_t data[25] = {0xe0, 0x02, 0x10, 0x16, 0x8a,
                      0xa3, 0x02, 0x34, 0x12, 0xab};
  victron::Advertisement advertisement;
  TEST_ASSERT_FALSE(
      victron::parseAdvertisement(data, sizeof(data), 0xab, advertisement));
  data[0] = 0xe1;
  TEST_ASSERT_FALSE(
      victron::parseAdvertisement(data, sizeof(data), 0xcd, advertisement));
}

void test_rejects_malformed_advertisements(void) {
  uint8_t data[27] = {0xe1, 0x02, 0x10, 0x16, 0x8a,
                      0xa3, 0x02, 0x34, 0x12, 0xab};
  victron::Advertisement advertisement;

  TEST_ASSERT_FALSE(
      victron::parseAdvertisement(nullptr, 25, 0xab, advertisement));
  TEST_ASSERT_FALSE(victron::parseAdvertisement(data, 10, 0xab, advertisement));
  TEST_ASSERT_FALSE(
      victron::parseAdvertisement(data, sizeof(data), 0xab, advertisement));
  data[2] = 0x00;
  TEST_ASSERT_FALSE(victron::parseAdvertisement(data, 25, 0xab, advertisement));
}

void test_rejects_truncated_metric_records(void) {
  const uint8_t data[15] = {};
  victron::BatteryMetrics metrics;
  TEST_ASSERT_FALSE(victron::decodeBatteryMonitor(data, 14, metrics));
  TEST_ASSERT_FALSE(victron::decodeSolarCharger(data, 5, metrics));
  TEST_ASSERT_FALSE(
      victron::decodeBatteryMonitor(nullptr, sizeof(data), metrics));
}

void test_decodes_smartshunt_metrics(void) {
  const uint8_t data[15] = {0x00, 0x00, 0x2c, 0x05, 0x00, 0x00, 0x00, 0x00,
                            0x1f, 0x3f, 0xff, 0x00, 0x00, 0x00, 0x31};
  victron::BatteryMetrics metrics;
  TEST_ASSERT_TRUE(victron::decodeBatteryMonitor(data, sizeof(data), metrics));
  TEST_ASSERT_TRUE(metrics.hasVoltage);
  TEST_ASSERT_TRUE(metrics.hasCurrent);
  TEST_ASSERT_TRUE(metrics.hasSoc);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 13.24f, metrics.voltage);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, -12.345f, metrics.current);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 78.4f, metrics.soc);
}

void test_decodes_smartsolar_metrics(void) {
  const uint8_t data[6] = {0x03, 0x00, 0x46, 0x05, 0x2a, 0x00};
  victron::BatteryMetrics metrics;
  TEST_ASSERT_TRUE(victron::decodeSolarCharger(data, sizeof(data), metrics));
  TEST_ASSERT_TRUE(metrics.hasVoltage);
  TEST_ASSERT_TRUE(metrics.hasCurrent);
  TEST_ASSERT_FALSE(metrics.hasSoc);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 13.50f, metrics.voltage);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 4.2f, metrics.current);
}

void test_ignores_victron_unavailable_sentinels(void) {
  const uint8_t data[15] = {0x00, 0x00, 0xff, 0x7f, 0x00, 0x00, 0x00, 0x00,
                            0xff, 0xff, 0xff, 0x00, 0x00, 0xf0, 0x3f};
  victron::BatteryMetrics metrics;
  TEST_ASSERT_FALSE(victron::decodeBatteryMonitor(data, sizeof(data), metrics));
  TEST_ASSERT_FALSE(metrics.hasVoltage);
  TEST_ASSERT_FALSE(metrics.hasCurrent);
  TEST_ASSERT_FALSE(metrics.hasSoc);
}

void test_decrypts_aes_ctr_payload(void) {
  const std::array<uint8_t, 16> key = {};
  const uint8_t encrypted[] = {0x67, 0xeb, 0x48, 0xd0};
  const victron::Advertisement advertisement = {
      victron::BATTERY_MONITOR,
      0,
      encrypted,
      sizeof(encrypted),
  };
  std::array<uint8_t, victron::MAX_ENCRYPTED_SIZE> plaintext = {};

  TEST_ASSERT_TRUE(victron::decrypt(advertisement, key, plaintext));
  TEST_ASSERT_EQUAL_HEX8(0x01, plaintext[0]);
  TEST_ASSERT_EQUAL_HEX8(0x02, plaintext[1]);
  TEST_ASSERT_EQUAL_HEX8(0x03, plaintext[2]);
  TEST_ASSERT_EQUAL_HEX8(0x04, plaintext[3]);
}

int main(int argc, char **argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_parses_victron_product_advertisement);
  RUN_TEST(test_rejects_wrong_manufacturer_and_key);
  RUN_TEST(test_rejects_malformed_advertisements);
  RUN_TEST(test_rejects_truncated_metric_records);
  RUN_TEST(test_decodes_smartshunt_metrics);
  RUN_TEST(test_decodes_smartsolar_metrics);
  RUN_TEST(test_ignores_victron_unavailable_sentinels);
  RUN_TEST(test_decrypts_aes_ctr_payload);
  return UNITY_END();
}
