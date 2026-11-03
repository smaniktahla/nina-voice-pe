#include "ready_chime.h"
#include <algorithm>
#include <cassert>
#include <cstdlib>
#include <iostream>
using esphome::nina_voice::ReadyChime;
int main() {
  ReadyChime chime;
  assert(!chime.active() && chime.next() == 0);
  chime.start();
  int peak = 0;
  for (unsigned i = 0; i < ReadyChime::FRAMES; ++i) {
    assert(chime.active());
    const auto sample = chime.next();
    peak = std::max(peak, std::abs(int(sample)));
    if (i >= 2400 && i < 3360) assert(sample == 0);
  }
  assert(peak > 4000 && peak <= 5200);
  assert(!chime.active() && chime.remaining() == 0 && chime.next() == 0);
  chime.start(); chime.next(); chime.clear();
  assert(!chime.active() && chime.next() == 0);
  std::cout << "PASS ready chime duration, envelope, gap, bounds and cancellation\n";
}
