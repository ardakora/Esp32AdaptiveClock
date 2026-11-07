#include "Esp32AdaptiveClock.h"

#include <Arduino.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#if !defined(ARDUINO_ARCH_ESP32) || !defined(CONFIG_IDF_TARGET_ESP32)
#error "Esp32AdaptiveClock supports classic ESP32 on Arduino."
#endif

namespace esp32clock {
namespace {

uint32_t mhz(Gear gear) {
  return static_cast<uint32_t>(gear);
}

bool validGear(Gear gear) {
  return gear == Gear::Mhz80 || gear == Gear::Mhz160 || gear == Gear::Mhz240;
}

uint32_t idleFrequency(IdleMode mode) {
  if (mode == IdleMode::Mhz80) return 80;

  const uint32_t crystal = getXtalFrequencyMhz();
  if (crystal != 40 && crystal != 26) return 0;
  switch (mode) {
    case IdleMode::Crystal: return crystal;
    case IdleMode::HalfCrystal: return crystal / 2;
    case IdleMode::QuarterCrystal: return crystal == 40 ? 10 : 0;
    default: return 0;
  }
}

Gear higher(Gear gear) {
  return gear == Gear::Mhz80 ? Gear::Mhz160 : Gear::Mhz240;
}

Gear lower(Gear gear) {
  return gear == Gear::Mhz240 ? Gear::Mhz160 : Gear::Mhz80;
}

bool apply(uint32_t frequency) {
  return getCpuFrequencyMhz() == frequency || setCpuFrequencyMhz(frequency);
}

}  // namespace

bool AdaptiveClock::begin(IdleMode mode) {
#if defined(CONFIG_PM_ENABLE) && CONFIG_PM_ENABLE
  (void)mode;
  return false;  // Keep a single owner of the global CPU clock.
#else
  if (owner_ || xPortInIsrContext()) return false;
  const auto caller = xTaskGetCurrentTaskHandle();
  const uint32_t idle = idleFrequency(mode);
  if (!caller || !idle) return false;

  const uint32_t original = getCpuFrequencyMhz();
  if (!apply(idle)) return false;

  originalMhz_ = original;
  idleMhz_ = idle;
  owner_ = caller;
  return true;
#endif
}

bool AdaptiveClock::canSwitch() const {
  return !xPortInIsrContext() && owner_ && owner_ == xTaskGetCurrentTaskHandle() && !running_;
}

bool AdaptiveClock::end() {
  if (!canSwitch() || !apply(originalMhz_)) return false;
  owner_ = nullptr;
  idleMhz_ = 0;
  return true;
}

bool AdaptiveClock::run(Gear gear, Work work, void* context) {
  return execute(gear, work, context, nullptr);
}

bool AdaptiveClock::run(Job& job, Work work, void* context) {
  if (!job.budgetMicros_ || !validGear(job.ceiling_)) return false;
  const Gear limit = job.ceiling_ == Gear::Mhz240 && originalMhz_ < 240
                         ? Gear::Mhz160 : job.ceiling_;
  const Gear gear = mhz(job.nextGear_) > mhz(limit) ? limit : job.nextGear_;
  uint32_t elapsed = 0;
  if (!execute(gear, work, context, &elapsed)) return false;

  job.lastMicros_ = elapsed;
  job.lastGear_ = gear;
  job.nextGear_ = gear;
  if (elapsed > job.budgetMicros_ && mhz(gear) < mhz(limit)) {
    job.nextGear_ = higher(gear);
  } else if (elapsed <= job.budgetMicros_ / 3) {
    job.nextGear_ = lower(gear);
  }
  return true;
}

bool AdaptiveClock::execute(Gear gear, Work work, void* context, uint32_t* elapsed) {
  if (!canSwitch() || !work || !validGear(gear) ||
      (gear == Gear::Mhz240 && originalMhz_ < 240)) return false;
  if (!apply(mhz(gear))) return false;

  running_ = true;
  const int64_t started = elapsed ? esp_timer_get_time() : 0;
  work(context);
  if (elapsed) {
    const int64_t duration = esp_timer_get_time() - started;
    *elapsed = duration > UINT32_MAX ? UINT32_MAX : static_cast<uint32_t>(duration);
  }
  const bool restored = apply(idleMhz_);
  running_ = false;
  return restored;
}

}  // namespace esp32clock
