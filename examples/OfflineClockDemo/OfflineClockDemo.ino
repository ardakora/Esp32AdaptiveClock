#include <Arduino.h>
#include <Esp32AdaptiveClock.h>

using namespace esp32clock;

enum class Scene { Idle, Work };

// Change one setting at a time and rerun for comparable power measurements.
constexpr bool kAdaptive = true;  // false uses the fixed CPU frequency from Tools.
constexpr Scene kScene = Scene::Work;
constexpr IdleMode kIdleMode = IdleMode::Mhz80;
// Crystal, HalfCrystal and QuarterCrystal are experimental offline modes.
constexpr uint32_t kMeasureMs = 60000;
constexpr uint32_t kIntervalMs = 1000;
constexpr uint32_t kBudgetUs = 8000;
constexpr int8_t kMeasurePin = -1;  // Optional free GPIO. Share ground with the meter.

static AdaptiveClock clockControl;
static Job job(kBudgetUs, Gear::Mhz160);

struct Work {
  uint32_t iterations = 0;
  uint32_t checksum = 1;
  uint32_t cpuMhz = 0;
  uint32_t apbHz = 0;
};

struct Results {
  uint32_t workRuns = 0;
  uint32_t work80 = 0;
  uint32_t work160 = 0;
  uint32_t work240 = 0;
  uint32_t overBudget = 0;
  uint32_t lightRuns = 0;
  uint32_t heavyRuns = 0;
  uint32_t lightUs = 0;
  uint32_t heavyUs = 0;
  uint32_t workMaxUs = 0;
  uint32_t elapsedMs = 0;
  uint32_t idleCpuMhz = 0;
  uint32_t idleApbHz = 0;
};

static void doWork(void* context) {
  auto& work = *static_cast<Work*>(context);
  work.cpuMhz = getCpuFrequencyMhz();
  work.apbHz = getApbFrequency();
  uint32_t state = work.checksum;
  for (uint32_t i = 0; i < work.iterations; ++i) {
    state = state * 1664525u + 1013904223u;
    state ^= state >> 13;
    state ^= state << 7;
  }
  work.checksum = state;
}

static uint32_t gearMhz(Gear gear) {
  switch (gear) {
    case Gear::Mhz80: return 80;
    case Gear::Mhz160: return 160;
    case Gear::Mhz240: return 240;
  }
  return 0;
}

static bool measure(Results& result, Work& work) {
  result.idleCpuMhz = getCpuFrequencyMhz();
  result.idleApbHz = getApbFrequency();
  bool ok = true;
  if (kMeasurePin >= 0) digitalWrite(kMeasurePin, HIGH);
  const uint32_t started = millis();
  if (kScene == Scene::Work) {
    for (uint32_t due = 0; due < kMeasureMs; due += kIntervalMs) {
      const uint32_t elapsed = millis() - started;
      if (elapsed < due) delay(due - elapsed);
      if (millis() - started >= kMeasureMs) break;

      const bool heavy = due >= kMeasureMs / 2;
      work.iterations = heavy ? 160000 : 6000;
      uint32_t duration;
      if (kAdaptive) {
        ok = clockControl.run(job, doWork, &work);
        if (!ok) break;
        duration = job.lastMicros();
      } else {
        const uint32_t before = micros();
        doWork(&work);
        duration = micros() - before;
      }
      ++result.workRuns;
      if (work.cpuMhz == 80) ++result.work80;
      if (work.cpuMhz == 160) ++result.work160;
      if (work.cpuMhz == 240) ++result.work240;
      if (duration > kBudgetUs) ++result.overBudget;
      if (duration > result.workMaxUs) result.workMaxUs = duration;
      if (heavy) {
        ++result.heavyRuns;
        result.heavyUs += duration;
      } else {
        ++result.lightRuns;
        result.lightUs += duration;
      }
    }
  }
  const uint32_t elapsed = millis() - started;
  if (ok && elapsed < kMeasureMs) delay(kMeasureMs - elapsed);
  if (kMeasurePin >= 0) digitalWrite(kMeasurePin, LOW);
  result.elapsedMs = millis() - started;
  return ok;
}

void setup() {
  Serial.begin(115200);
  if (kMeasurePin >= 0) {
    pinMode(kMeasurePin, OUTPUT);
    digitalWrite(kMeasurePin, LOW);
  }
  // No Wi-Fi, Bluetooth, SPI or PWM in this test. Keep peripherals out of it.
  Serial.printf("mode=%s scene=%s start_cpu_mhz=%u xtal_mhz=%u budget_us=%u\n",
                kAdaptive ? "adaptive" : "fixed", kScene == Scene::Idle ? "idle" : "work",
                unsigned(getCpuFrequencyMhz()), unsigned(getXtalFrequencyMhz()), unsigned(kBudgetUs));
  Serial.printf("phase=settle measure_ms=%u\n", unsigned(kMeasureMs));
  Serial.flush();
  delay(3000);
  Serial.println("phase=steady");
  Serial.flush();
  if (kAdaptive && !clockControl.begin(kIdleMode)) {
    Serial.println("phase=done ok=0 error=clock_begin");
    return;
  }
  Results result;
  Work work;
  const bool measured = measure(result, work);
  const bool restored = !kAdaptive || clockControl.end();
  Serial.printf("phase=done ok=%u elapsed_ms=%u work_runs=%u checksum=%08x\n",
                unsigned(measured && restored), unsigned(result.elapsedMs),
                unsigned(result.workRuns), unsigned(work.checksum));
  Serial.printf("idle_cpu_mhz=%u idle_apb_hz=%u last_work_cpu_mhz=%u last_work_apb_hz=%u restored_cpu_mhz=%u\n",
                unsigned(result.idleCpuMhz), unsigned(result.idleApbHz), unsigned(work.cpuMhz),
                unsigned(work.apbHz), unsigned(getCpuFrequencyMhz()));
  Serial.printf("work_80=%u work_160=%u work_240=%u light_mean_us=%u heavy_mean_us=%u work_max_us=%u over_budget=%u\n",
                unsigned(result.work80), unsigned(result.work160), unsigned(result.work240),
                unsigned(result.lightRuns ? result.lightUs / result.lightRuns : 0),
                unsigned(result.heavyRuns ? result.heavyUs / result.heavyRuns : 0),
                unsigned(result.workMaxUs), unsigned(result.overBudget));
  if (kAdaptive && result.workRuns) {
    Serial.printf("last_gear_mhz=%u next_gear_mhz=%u\n",
                  unsigned(gearMhz(job.lastGear())), unsigned(gearMhz(job.nextGear())));
  }
}

void loop() {
  delay(1000);
}
