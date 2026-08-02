#pragma once

// Minimal host-test compatibility for the Crypto library's unused RNG object.
#include <chrono>
#include <cstdint>

inline uint32_t micros() {
  using namespace std::chrono;
  return static_cast<uint32_t>(
      duration_cast<microseconds>(steady_clock::now().time_since_epoch())
          .count());
}

inline uint32_t millis() { return micros() / 1000U; }
