#pragma once
#include <cmath>
#include <cstdint>
namespace esphome::nina_voice {
class ReadyChime {
 public:
  static constexpr uint32_t FRAMES = 5760;  // Two 100 ms notes and a 40 ms gap, at 24 kHz.
  void start() { position_ = 0; }
  void clear() { position_ = FRAMES; }
  bool active() const { return position_ < FRAMES; }
  uint32_t remaining() const { return FRAMES - position_; }
  int16_t next() {
    if (!active()) return 0;
    const uint32_t pos = position_++;
    if (pos >= 2400 && pos < 3360) return 0;
    const uint32_t note_pos = pos < 2400 ? pos : pos - 3360;
    const float hz = pos < 2400 ? 660.0f : 880.0f;
    const float envelope = std::sin(3.14159265f * float(note_pos) / 2400.0f);
    return int16_t(5200.0f * envelope * std::sin(2.0f * 3.14159265f * hz * float(note_pos) / 24000.0f));
  }
 private:
  uint32_t position_{FRAMES};
};
}
