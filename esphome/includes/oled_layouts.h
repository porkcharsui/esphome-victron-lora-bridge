#pragma once

#include <algorithm>
#include <cmath>

#include "esphome/components/display/display.h"
#include "esphome/components/font/font.h"

namespace oled_layouts {

inline void draw_battery_data(esphome::display::Display &it, esphome::font::Font *tiny_font,
                              esphome::font::Font *value_font,
                              esphome::font::Font *soc_font, float soc, float voltage,
                              float current, bool shunt_fresh, bool solar_fresh) {
  it.printf(64, 0, tiny_font, esphome::display::TextAlign::TOP_CENTER,
            "SHUNT:%s  SOLAR:%s", shunt_fresh ? "OK" : "--", solar_fresh ? "OK" : "--");

  constexpr int left_column_center = 39;
  constexpr int right_column_center = 103;
  // Roboto Mono 32's visible pixels begin 11 rows below a TOP anchor. At y=2,
  // the 23-pixel SOC glyph is centered in the open rows between the header
  // (ending at y=8) and the battery outline (starting at y=40).
  constexpr int soc_anchor_y = 2;
  if (!std::isnan(soc)) {
    it.printf(left_column_center, soc_anchor_y, soc_font,
              esphome::display::TextAlign::TOP_CENTER, "%.0f%%", soc);
  } else {
    it.print(left_column_center, soc_anchor_y, soc_font,
             esphome::display::TextAlign::TOP_CENTER, "--%");
  }

  if (!std::isnan(voltage)) {
    it.printf(right_column_center, 10, value_font, esphome::display::TextAlign::TOP_CENTER, "%.1fV",
              voltage);
  } else {
    it.print(right_column_center, 10, value_font, esphome::display::TextAlign::TOP_CENTER, "--.-V");
  }
  if (!std::isnan(current)) {
    it.printf(right_column_center, 29, value_font, esphome::display::TextAlign::TOP_CENTER, "%+.1fA",
              current);
  } else {
    it.print(right_column_center, 29, value_font, esphome::display::TextAlign::TOP_CENTER, "--.-A");
  }
  it.print(right_column_center, 53, tiny_font, esphome::display::TextAlign::TOP_CENTER,
           shunt_fresh ? "FRESH" : "STALE");

  constexpr int battery_top = 40;
  constexpr int battery_inner_width = 70;
  it.rectangle(0, battery_top, 74, 24);
  it.filled_rectangle(74, battery_top + 7, 4, 10);
  if (!std::isnan(soc)) {
    const float gauge_soc = std::max(0.0f, std::min(100.0f, soc));
    const int gauge_width =
        static_cast<int>(std::round(gauge_soc * battery_inner_width / 100.0f));
    if (gauge_width > 0) {
      it.filled_rectangle(2, battery_top + 2, gauge_width, 20);
    }
  }
}

inline void draw_uplink_home(esphome::display::Display &it,
                             esphome::font::Font *status_font, bool wifi_connected,
                             const char *ip_address, bool packet_seen, bool link_connected,
                             float packet_age, bool radio_available, float rssi, float snr) {
  it.print(0, 0, status_font, "VICTRON LORA BRIDGE");
  it.horizontal_line(0, 13, 128);

  if (wifi_connected) {
    it.printf(0, 16, status_font, "WiFi  %s", ip_address);
  } else {
    it.print(0, 16, status_font, "WiFi  OFFLINE");
  }

  if (!packet_seen) {
    it.print(0, 28, status_font, "LoRa  WAITING");
    it.print(0, 40, status_font, "Packet  never");
  } else {
    it.printf(0, 28, status_font, "LoRa  %s", link_connected ? "OK" : "STALE");
    if (packet_age < 120.0f) {
      it.printf(0, 40, status_font, "Packet  %.0f sec ago", packet_age);
    } else {
      it.printf(0, 40, status_font, "Packet  %.0f min ago", packet_age / 60.0f);
    }
  }

  if (radio_available) {
    it.printf(0, 52, status_font, "RF %.0fdBm %.0fdB", rssi, snr);

    const int rssi_bars = rssi >= -75.0f ? 4 : rssi >= -90.0f ? 3 : rssi >= -105.0f ? 2 : 1;
    const int snr_bars = snr >= 7.0f ? 4 : snr >= 0.0f ? 3 : snr >= -5.0f ? 2 : 1;
    const int signal_bars = std::min(rssi_bars, snr_bars);
    for (int bar = 0; bar < signal_bars; bar++) {
      const int height = 3 + bar * 3;
      it.filled_rectangle(110 + bar * 4, 64 - height, 3, height);
    }
  } else {
    it.print(0, 52, status_font, "RF  listening");
  }
}

}  // namespace oled_layouts
