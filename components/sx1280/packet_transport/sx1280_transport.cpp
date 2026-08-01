#include "sx1280_transport.h"

#include "esphome/components/xxtea/xxtea.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"

#include <algorithm>
#include <cstring>

namespace esphome::sx1280 {

static const char *const TAG = "sx1280.transport";
static constexpr uint16_t PACKET_MAGIC = 0x4553;
static constexpr uint8_t ROLLING_CODE_KEY = 5;

void SX1280Transport::setup() {
  PacketTransport::setup();
  this->parent_->register_listener(this);
}

void SX1280Transport::loop() {
  if (this->resend_ping_key_)
    this->send_ping_pong_request_();
  if (this->send_on_update_ && this->updated_)
    this->send_data_(false);
}

void SX1280Transport::send_packet(const std::vector<uint8_t> &buf) const {
  this->parent_->transmit_packet(buf, this->repeat_count_, this->repeat_jitter_ms_);
}

void SX1280Transport::add_auth_provider(const char *name, const std::vector<uint8_t> &key) {
  AuthProvider provider;
  provider.name = name;
  if (key.size() >= provider.key.size())
    std::copy_n(key.begin(), provider.key.size(), provider.key.begin());
  this->auth_providers_.push_back(std::move(provider));
}

static uint32_t read_u32(const uint8_t *data) {
  return static_cast<uint32_t>(data[0]) | static_cast<uint32_t>(data[1]) << 8 |
         static_cast<uint32_t>(data[2]) << 16 | static_cast<uint32_t>(data[3]) << 24;
}

bool SX1280Transport::authenticate_(const std::vector<uint8_t> &packet) {
  if (packet.size() < 16) {
    ESP_LOGD(TAG, "Authentication rejected: packet too short (%u bytes, minimum 16)",
             static_cast<unsigned>(packet.size()));
    return false;
  }
  if ((packet.size() & 3) != 0) {
    ESP_LOGD(TAG, "Authentication rejected: packet length is not 4-byte aligned (%u bytes)",
             static_cast<unsigned>(packet.size()));
    return false;
  }
  const uint16_t magic = packet[0] | static_cast<uint16_t>(packet[1]) << 8;
  if (magic != PACKET_MAGIC) {
    ESP_LOGD(TAG, "Authentication rejected: bad packet magic 0x%04X", magic);
    return false;
  }
  const size_t name_length = packet[2];
  if (name_length == 0) {
    ESP_LOGD(TAG, "Authentication rejected: sender name is empty");
    return false;
  }
  if (3 + name_length > packet.size()) {
    ESP_LOGD(TAG, "Authentication rejected: sender name length %u exceeds packet length %u",
             static_cast<unsigned>(name_length), static_cast<unsigned>(packet.size()));
    return false;
  }
  const std::string name(reinterpret_cast<const char *>(packet.data() + 3), name_length);
  size_t encrypted_offset = (3 + name_length + 3) & ~size_t(3);
  if (encrypted_offset >= packet.size()) {
    ESP_LOGD(TAG, "Authentication rejected: provider '%s' has no encrypted payload", name.c_str());
    return false;
  }
  if (((packet.size() - encrypted_offset) & 3) != 0) {
    ESP_LOGD(TAG, "Authentication rejected: provider '%s' has an unaligned encrypted payload", name.c_str());
    return false;
  }

  for (auto &provider : this->auth_providers_) {
    if (provider.name != name)
      continue;
    std::vector<uint8_t> decrypted(packet.begin() + encrypted_offset, packet.end());
    xxtea::decrypt(reinterpret_cast<uint32_t *>(decrypted.data()), decrypted.size() / 4,
                   reinterpret_cast<const uint32_t *>(provider.key.data()));
    if (decrypted.size() < 9) {
      ESP_LOGD(TAG, "Authentication rejected: provider '%s' payload is too short for a rolling code (%u bytes)",
               provider.name.c_str(), static_cast<unsigned>(decrypted.size()));
      return false;
    }
    if (decrypted[0] != ROLLING_CODE_KEY) {
      ESP_LOGD(TAG,
               "Authentication rejected: provider '%s' has invalid decrypted marker 0x%02X (wrong transport key "
               "or corrupt payload)",
               provider.name.c_str(), decrypted[0]);
      return false;
    }
    const uint32_t low = read_u32(decrypted.data() + 1);
    const uint32_t high = read_u32(decrypted.data() + 5);
    if (provider.has_code &&
        (high < provider.last_code_high || (high == provider.last_code_high && low <= provider.last_code_low))) {
      ESP_LOGD(TAG, "Authentication rejected: replayed rolling code from provider '%s'", provider.name.c_str());
      return false;
    }
    provider.last_code_low = low;
    provider.last_code_high = high;
    provider.has_code = true;
    return true;
  }
  ESP_LOGD(TAG, "Authentication rejected: unknown provider '%s'", name.c_str());
  return false;
}

void SX1280Transport::on_packet(const std::vector<uint8_t> &packet, float rssi, float snr) {
  if (!this->authenticate_(packet))
    return;
  const bool is_first_packet = !this->has_authenticated_packet_;
  this->process_(packet);
  this->last_authenticated_packet_ms_ = millis();
  this->has_authenticated_packet_ = true;
  ESP_LOGI(TAG, "📥 LoRa telemetry received — %u authenticated bytes, RSSI %.1f dBm, SNR %.1f dB",
           static_cast<unsigned>(packet.size()), rssi, snr);
  if (is_first_packet)
    this->first_packet_trigger_.trigger();
}

bool SX1280Transport::is_link_available(uint32_t timeout_ms) const {
  return this->last_authenticated_packet_ms_ != 0 && millis() - this->last_authenticated_packet_ms_ <= timeout_ms;
}

float SX1280Transport::authenticated_packet_age() const {
  return this->last_authenticated_packet_ms_ == 0 ? NAN : (millis() - this->last_authenticated_packet_ms_) / 1000.0f;
}

}  // namespace esphome::sx1280
