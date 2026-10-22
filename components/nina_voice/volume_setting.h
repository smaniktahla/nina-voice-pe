#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace esphome::nina_voice {
class VolumeSetting {
 public:
  static constexpr float MAXIMUM = 0.70f;
  bool restore(float value) {
    if (!std::isfinite(value) || value < 0.0f || value > MAXIMUM) return false;
    value_ = value;
    pending_ = false;
    return true;
  }
  bool step(float delta, uint32_t now) {
    if (!std::isfinite(delta)) return false;
    const float next = std::clamp(value_ + delta, 0.0f, MAXIMUM);
    if (next == value_) return false;
    value_ = next;
    changed_at_ = now;
    pending_ = true;
    return true;
  }
  float value() const { return value_; }
  float fraction() const { return value_ / MAXIMUM; }
  bool pending() const { return pending_; }
  bool save_due(uint32_t now) const { return pending_ && uint32_t(now - changed_at_) >= 1000; }
  void saved() { pending_ = false; }
  void retry_later(uint32_t now) { changed_at_ = now; }
 private:
  float value_{0.45f};
  uint32_t changed_at_{0};
  bool pending_{false};
};
}
