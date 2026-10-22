#include "volume_setting.h"
#include <cassert>
#include <cmath>
#include <iostream>
#include <limits>
using esphome::nina_voice::VolumeSetting;
int main() {
  VolumeSetting volume;
  assert(std::abs(volume.value() - 0.45f) < 0.001f && !volume.pending());
  assert(volume.restore(0.63f));
  assert(std::abs(volume.fraction() - 0.9f) < 0.001f && !volume.pending());
  for (float bad : {-1.0f, 0.71f, std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()}) {
    assert(!volume.restore(bad));
    assert(std::abs(volume.value() - 0.63f) < 0.001f);
  }
  assert(volume.step(0.03f, 100));
  assert(!volume.save_due(1099) && volume.save_due(1100));
  assert(volume.step(0.03f, 600));
  assert(!volume.save_due(1100) && volume.save_due(1600));
  const float saved = volume.value();
  volume.saved();
  assert(!volume.pending());
  VolumeSetting reboot;
  assert(reboot.restore(saved) && std::abs(reboot.value() - saved) < 0.0001f);
  assert(reboot.step(1, 2000));
  assert(reboot.value() == VolumeSetting::MAXIMUM);
  reboot.saved();
  assert(!reboot.step(1, 2100) && !reboot.pending());
  assert(!reboot.step(std::numeric_limits<float>::quiet_NaN(), 2200));
  assert(reboot.step(-2, UINT32_MAX - 500));
  assert(reboot.value() == 0 && !reboot.save_due(100) && reboot.save_due(500));
  reboot.retry_later(500);
  assert(!reboot.save_due(1000) && reboot.save_due(1500));
  std::cout << "PASS volume restore, limits, debouncing, persistence model and rollover\n";
}
