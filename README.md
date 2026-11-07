# Esp32AdaptiveClock

Klasik çift çekirdekli ESP32 ve Arduino için çevrimdışı CPU frekans deneyi.
İki çekirdek aynı CPU saatini kullanır. Arduino-ESP32 3.3.8 paketindeki
`setCpuFrequencyMhz()` ile çalışır. Tekrarlanan işler için gerekli hızı
işin ölçülen süresine göre seçer.

## Arduino IDE

1. **Taslak → Kütüphane Ekle → .ZIP Kitaplığı Ekle…** ile
   `Esp32AdaptiveClock.zip` dosyasını ekle.
2. **Dosya → Örnekler → Esp32AdaptiveClock → OfflineClockDemo** örneğini
   aç. Klasik ESP32 kartını seç ve Seri Monitörü **115200 baud** aç.
3. İlk koşuyu `kIdleMode = IdleMode::Mhz80` ile yap. Seri çıktı 60
   saniye sonunda saatleri, tamamlanan işleri, süreleri ve işlerin
   hangi frekanslarda çalıştığını gösterir.

## Kullanım

```cpp
#include <Esp32AdaptiveClock.h>
using namespace esp32clock;

AdaptiveClock cpu;
Job drawJob(8000);  // 8 ms süre hedefi, en fazla 160 MHz.

// setup(), aynı görevden bir kere:
// if (!cpu.begin()) { /* Hata */ }

// Ekranda değişiklik olduğunda, aynı görevden:
// if (!cpu.run(drawJob, drawFrame, context)) { /* Hata */ }
// drawFrame imzası: void drawFrame(void* context)

// Frekans yönetimini bırakırken: cpu.end();
```

İlk çalışma 80 MHz'de başlar. İş hedefi aşarsa aynı işin bir sonraki
çağrısı bir üst frekansı dener. Hedef sürenin üçte biri içinde biterse
bir alt frekansı dener. Orta aralıkta aynı hız kalır. Farklı iş türleri
için ayrı `Job` nesneleri kullan. Bir iş için frekansı baştan biliyorsan
`cpu.run(Gear::Mhz160, callback, context)` ile doğrudan seç.
Ölçülen süre SPI gibi beklemeleri de içerir. PixBand'in mevcut 1 Hz
sayaç örneğinde süreyi aktarım belirlediği için ilk denemede
`cpu.run(Gear::Mhz80, callback, context)` uygun.
240 MHz'e çıkmak için başlangıçta kartın 240 MHz'te çalışıyor olması
gerekir. İş dönünce kütüphane boşta frekansına geçer.

`begin()`, `run()` ve `end()` aynı Arduino görevinden çağrılmalı.
Tek denetleyici kullan. Uygulama, hangi işlerin çalışacağını bildirir.
Başka görevlerin yükü frekans kararına dahil değildir. Geri çağırım
tamamlanmadan başlattığı SPI/DMA gibi işlemleri bitirmeli. `run()`
`false` dönerse saat geçişlerinden biri başarısız olmuştur. Boşta
frekansına dönüş başarısızsa iş zaten çalışmış olabilir. `end()` başarılı
olduğunda `begin()` öncesindeki frekansı geri getirir.

## Boşta frekans seçenekleri

| Seçenek | 40 MHz kristalli klasik ESP32 |
| --- | ---: |
| `IdleMode::Mhz80` (varsayılan) | 80 MHz |
| `IdleMode::Crystal` (deneysel) | 40 MHz |
| `IdleMode::HalfCrystal` (deneysel) | 20 MHz |
| `IdleMode::QuarterCrystal` (deneysel) | 10 MHz |

26 MHz kristalli desteklenen kartta `Crystal` ve `HalfCrystal` sırasıyla
26 ve 13 MHz seçer. Çeyrek kristal seçeneği 40 MHz kristal gerektirir.
Bu sürümde Wi‑Fi ve Bluetooth uygulama tarafından kapalı tutulmalı.
Frekans değişikliği kesme içinde yapılmaz. ESP-IDF güç yönetimi etkin bir
derlemede frekansı onun yönetmesi için `begin()` devreye girmez.

80 MHz altında klasik ESP32'nin çevre birimi saati (APB) de düşer.
SPI, UART, PWM ve zamanlayıcıların gerçek davranışını ayrı doğrula.
Özellikle PixBand'in 5 kHz arka ışık PWM'i için önce 80 MHz'i kullan.
20 veya 10 MHz'i denemeden önce çevre birimsiz örnekte 40 MHz ile
süreyi ve seri çıktıyı kontrol et.
[Espressif güç yönetimi](https://docs.espressif.com/projects/esp-idf/en/v5.5.2/esp32/api-reference/system/power_management.html)
ve [LEDC saat kaynakları](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/peripherals/ledc.html#characteristics-of-esp32-ledc-source-clocks).

## Akım karşılaştırması

`kAdaptive = false` sabit frekanslı kontrol koşusunu seçer. Sabit
80 MHz için IDE'de **Araçlar → CPU Frequency → 80 MHz (WiFi/BT)**
seç. Sonra uyarlanabilir modu 80 ve deneysel 40 MHz boşta ayarlarıyla
karşılaştır.
`Scene::Idle` boşta akımını, `Scene::Work` aynı hafif ve ağır iş dizisinin
akımını ölçmek içindir. Ölçeri aynı besleme noktasında kullan ve hem
ortalama akımı hem iş sürelerini kaydet. `kMeasurePin` ayarlanırsa
ölçüm penceresi boyunca GPIO yüksek kalır. Yazılım süre ve frekansı
raporlar. Akım ve enerji harici ölçüm gerektirir.
