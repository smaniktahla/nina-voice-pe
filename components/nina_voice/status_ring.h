#pragma once

#include <algorithm>
#include <array>
#include <cstdint>

namespace esphome::nina_voice {

struct RingColor { uint8_t red{0}, green{0}, blue{0}; };
using RingFrame = std::array<RingColor, 12>;

struct RingInputs {
  bool wifi{false}, clock_synced{false}, connected{false}, session_ready{false};
  bool muted{false}, awake{false}, failed{false}, showing_volume{false};
  float volume{0};
};

enum class RingStage { POWER_ON, BOOTING, NO_WIFI, WAITING_CLOCK, NO_SERVER,
                       WAITING_SESSION, READY, WAKING, MUTED, AWAKE, DORMANT, FAILED, VOLUME };

// No network/audio work, sleeps or LED-driver dependencies. All timing is wrap-safe.
class StatusRing {
 public:
  void wake(uint32_t now) { wake_seen_ = true; wake_at_ = now; }

  RingStage stage(uint32_t now, const RingInputs &in) {
    if (!started_) { started_ = true; boot_at_ = now; }
    if (!in.session_ready || !in.connected || !in.wifi || in.failed) ready_seen_ = false;
    if (in.muted) return RingStage::MUTED;
    if (in.failed) return RingStage::FAILED;
    if (!boot_complete_) {
      const uint32_t boot_age = now - boot_at_;
      if (boot_age < 800) return RingStage::POWER_ON;
      if (boot_age < 2500) return RingStage::BOOTING;
      boot_complete_ = true;
    }
    if (!in.wifi) return RingStage::NO_WIFI;
    if (!in.clock_synced) return RingStage::WAITING_CLOCK;
    if (!in.connected) return RingStage::NO_SERVER;
    if (!in.session_ready) return RingStage::WAITING_SESSION;
    if (!ready_seen_) { ready_seen_ = true; ready_at_ = now; }
    if (wake_seen_ && uint32_t(now - wake_at_) < 300) return RingStage::WAKING;
    if (uint32_t(now - ready_at_) < 1800) return RingStage::READY;
    if (in.showing_volume) return RingStage::VOLUME;
    return in.awake ? RingStage::AWAKE : RingStage::DORMANT;
  }

  RingFrame frame(uint32_t now, const RingInputs &in) {
    RingFrame pixels{};
    const auto visible = stage(now, in);
    switch (visible) {
      case RingStage::POWER_ON:
      case RingStage::WAKING:
        pixels.fill({96, 96, 96}); break;
      case RingStage::BOOTING:
      case RingStage::WAITING_CLOCK: {
        const bool clock = visible == RingStage::WAITING_CLOCK;
        const unsigned head = (now / 100) % 12;
        for (unsigned tail = 0; tail < 4; ++tail) {
          const uint8_t level = 96 >> tail;
          pixels[(head + 12 - tail) % 12] = clock ? RingColor{level, uint8_t(level / 2), 0}
                                                          : RingColor{0, level, level};
        }
        break;
      }
      case RingStage::NO_WIFI:
        // Four flashes per second: Wi-Fi is unavailable, including reconnects.
        if ((now / 125) % 2 == 0) pixels.fill({64, 0, 0});
        break;
      case RingStage::NO_SERVER:
        pixels.fill({64, 0, 0}); break;
      case RingStage::WAITING_SESSION:
      case RingStage::FAILED:
        pixels.fill({pulse(now, 2000, 64), 0, 0}); break;
      case RingStage::READY:
        pixels.fill({0, pulse(now - ready_at_, 900, 96), 0}); break;
      case RingStage::MUTED:
        // Two steady red marks distinguish hardware mute from a solid red ring.
        pixels[0] = pixels[6] = {40, 0, 0}; break;
      case RingStage::AWAKE:
        pixels.fill({0, 8, 20}); break;
      case RingStage::DORMANT:
        pixels.fill({5, 0, 10}); break;
      case RingStage::VOLUME: {
        const float segments = std::clamp(in.volume, 0.0f, 1.0f) * 12.0f;
        for (unsigned i = 0; i < 12; ++i) {
          const float level = std::clamp(segments - float(i), 0.0f, 1.0f);
          pixels[(i + 7) % 12] = {0, uint8_t(45 * level), uint8_t(70 * level)};
        }
        break;
      }
    }
    return pixels;
  }

 private:
  static uint8_t pulse(uint32_t age, uint32_t period, uint8_t maximum) {
    const uint32_t half = period / 2, phase = age % period;
    const uint32_t ramp = phase < half ? phase : period - phase;
    return uint8_t(4 + (maximum - 4) * ramp / half);
  }
  bool started_{false}, boot_complete_{false}, ready_seen_{false}, wake_seen_{false};
  uint32_t boot_at_{0}, ready_at_{0}, wake_at_{0};
};

}  // namespace esphome::nina_voice
