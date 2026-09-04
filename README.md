# Failsafe Supervisor

Bir Linux edge cihazinda (Raspberry Pi 3 B+) calisan, isci proseslerini
paylasimli-bellek heartbeatleri uzerinden denetleyen, kacirilan deadlineleri
monoton saate gore tespit eden, butcelenmis bir politikayla yeniden baslatan ve
kurtarilamayan durumda sistemi tanimli bir **guvenli duruma** suren C++ daemonu.

Hedef: arizalari sistematik olarak enjekte edip her ariza sinifi icin
kurtarma gecikmesini olcmek.

## Durum
- [x] M0 — bring-up: toolchain, CMake, CI, ilk LED
- [ ] M1 — heartbeat + deadline tespiti
- [ ] M2 — restart politikasi + SIGCHLD hizli yol
- [ ] M3 — durum makineleri + gercek guvenli durum (GPIO)
- [ ] M4 — donanim watchdog
- [ ] M5 — ariza enjeksiyonu + olcum
- [ ] M6 — konteynerler

## Hedef donanim
Raspberry Pi 3 B+, Raspberry Pi OS (Debian 13 trixie, aarch64), GCC 14, libgpiod 2.

## Derleme
\`\`\`sh
cmake -S . -B build -G Ninja -DHAVE_GPIO=ON   # Pi uzerinde
cmake --build build
ctest --test-dir build --output-on-failure
\`\`\`
CI, donanimsiz (\`HAVE_GPIO=OFF\`) olarak x86_64 uzerinde derler ve test eder.

## Kapsam disi (bilincli)
Iki-dugum failover kapsam disidir: iki dugumle quorum kurulamaz, bu yuzden
ag bolunmesinde split-brain olusur — iki supervisor da aktuator sahipligini
iddia eder ki bu, onlemeye calisilan arizadan daha kotudur. Dogrusu ucuncu bir
dugum veya harici hakem + fencing gerektirir; gelecek is olarak not edilmistir.
