#pragma once

#include <stdint.h>

namespace esp32clock {

enum class IdleMode : uint8_t { Mhz80, Crystal, HalfCrystal, QuarterCrystal };
enum class Gear : uint8_t { Mhz80 = 80, Mhz160 = 160, Mhz240 = 240 };

// Keep one Job per recurring workload. The first run uses 80 MHz.
class Job {
 public:
  explicit Job(uint32_t budgetMicros, Gear ceiling = Gear::Mhz160)
      : budgetMicros_(budgetMicros), ceiling_(ceiling) {}

  uint32_t lastMicros() const { return lastMicros_; }
  Gear lastGear() const { return lastGear_; }
  Gear nextGear() const { return nextGear_; }

 private:
  friend class AdaptiveClock;
  uint32_t budgetMicros_;
  uint32_t lastMicros_ = 0;
  Gear ceiling_;
  Gear lastGear_ = Gear::Mhz80;
  Gear nextGear_ = Gear::Mhz80;
};

// One controller, used from one task, with Wi-Fi and Bluetooth off.
// Run only at clock-safe boundaries for the application and its peripherals.
class AdaptiveClock {
 public:
  using Work = void (*)(void* context);

  AdaptiveClock() = default;
  AdaptiveClock(const AdaptiveClock&) = delete;
  AdaptiveClock& operator=(const AdaptiveClock&) = delete;

  bool begin(IdleMode mode = IdleMode::Mhz80);
  // Work must finish synchronous peripheral activity and return normally.
  // A false return can also mean that Work ran but the idle switch failed.
  bool run(Gear gear, Work work, void* context = nullptr);
  bool run(Job& job, Work work, void* context = nullptr);
  bool end();  // Restores the frequency recorded at begin().
  uint32_t idleMhz() const { return idleMhz_; }

 private:
  bool execute(Gear gear, Work work, void* context, uint32_t* elapsed);
  bool canSwitch() const;

  void* owner_ = nullptr;
  uint32_t originalMhz_ = 0;
  uint32_t idleMhz_ = 0;
  bool running_ = false;
};

}  // namespace esp32clock
