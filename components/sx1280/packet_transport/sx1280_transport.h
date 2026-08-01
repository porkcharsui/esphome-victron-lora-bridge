#pragma once

#include "esphome/components/packet_transport/packet_transport.h"
#include "esphome/components/sx1280/sx1280.h"
#include "esphome/core/automation.h"
#include "esphome/core/component.h"

#include <array>
#include <string>
#include <vector>

namespace esphome::sx1280 {

class SX1280Transport : public packet_transport::PacketTransport,
                        public Parented<SX1280>,
                        public SX1280Listener {
 public:
  void setup() override;
  void loop() override;
  void on_packet(const std::vector<uint8_t> &packet, float rssi, float snr) override;
  float get_setup_priority() const override { return setup_priority::AFTER_WIFI; }

  void set_repeat_count(uint8_t value) { this->repeat_count_ = value; }
  void set_repeat_jitter(uint32_t value) { this->repeat_jitter_ms_ = value; }
  void set_send_on_update(bool enabled) { this->send_on_update_ = enabled; }
  void set_transmit_enabled(bool enabled) { this->transmit_enabled_ = enabled; }
  void add_auth_provider(const char *name, const std::vector<uint8_t> &key);
  bool is_link_available(uint32_t timeout_ms = 300000) const;
  float authenticated_packet_age() const;
  Trigger<> *get_first_packet_trigger() { return &this->first_packet_trigger_; }

 protected:
  struct AuthProvider {
    std::string name;
    // ESPHome's XXTEA implementation uses eight 32-bit key words.
    std::array<uint8_t, 32> key{};
    uint32_t last_code_low{0};
    uint32_t last_code_high{0};
    bool has_code{false};
  };

  void send_packet(const std::vector<uint8_t> &buf) const override;
  bool should_send() override { return this->transmit_enabled_; }
  size_t get_max_packet_size() override { return this->parent_->get_max_packet_size(); }
  bool authenticate_(const std::vector<uint8_t> &packet);

  uint8_t repeat_count_{3};
  uint32_t repeat_jitter_ms_{250};
  bool send_on_update_{true};
  bool transmit_enabled_{true};
  bool has_authenticated_packet_{false};
  uint32_t last_authenticated_packet_ms_{0};
  Trigger<> first_packet_trigger_;
  std::vector<AuthProvider> auth_providers_;
};

}  // namespace esphome::sx1280
