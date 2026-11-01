# VcDex Loader

Oyun hilelerini tek tikla yukleyen, GitHub uzerinden otomatik guncellenen Windows loader.

- **Yapimci:** Laynex
- **Surum:** 1.0.0
- **Platform:** Windows 10 / 11 (x64)

## Kullanim

1. `VcDexLoader.exe` dosyasini calistir.
2. Listeden oyunu sec.
3. **LOAD GAME** butonuna bas (veya `Enter`).

Loader oyunu gerekirse Steam uzerinden baslatir, DLL'i enjekte eder ve sonucu bildirir.

| Oyun | Enjeksiyon | Tuslar |
| --- | --- | --- |
| Redmatch 2 | `LaynexRm2Injector.exe` (bypass, erken enjeksiyon) | `INSERT` menu / `END` unload |
| One Armed Robber | Loader'in dahili `LoadLibraryW` enjeksiyonu | - |

> Redmatch 2 icin orijinal bypass injector kullanilir. Oyun kapaliyken injector oyunu askida baslatip DLL'i anti-cheat devreye girmeden once yukler; oyun zaten acikken dogrudan mevcut surece enjekte eder.

## Klasor yapisi

```
VcDexLoader.exe        <- ana loader
data/
  redmatch2/
    LaynexRm2.dll
    LaynexRm2Injector.exe
  onearmedrobber/
    one_armed_robber_int.dll
```

Loader ilk acilista `manifest.json` uzerinden eksik/guncel olmayan dosyalari `data` klasorune indirir.

## Guncelleme yayinlama (gelistirici)

1. `src/config.hpp` icindeki `kVersion` degerini artir (ornek `1.0.1`).
2. Paketle:

   ```powershell
   powershell -ExecutionPolicy Bypass -File tools/package.ps1
   ```

   Script sunlari yapar: Release derleme, `data` -> `dist` kopyalama, SHA-256 hesaplama, `manifest.json` yazma, yerel `VcDexLoader.exe` kopyasi.

3. Yayinla:

   ```powershell
   git add -A
   git commit -m "release 1.0.1"
   git push
   ```

Tum kullanicilar bir sonraki acilista yeni manifest'i alir; loader guncellemesi varsa "Update available" bandi cikar ve tek tikla kendini degistirip yeniden baslar. Hile DLL'leri de ayni sekilde otomatik yenilenir.

## Derleme

Gereksinim: Visual Studio 2022 (C++ is yuku) + CMake 3.20+.

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

Cikti: `dist/VcDexLoader.exe` (statik CRT, ek runtime gerekmez).

## Notlar

- Loader kendini guncellerken `VcDexLoader.update.exe` indirir, kucuk bir `.bat` yardimcisi ile dosyayi degistirip yeniden baslar.
- Manifest imzasi/hash dogrulamasi SHA-256 ile yapilir; indirilen dosya hash tutmazsa yuklenmez.
- Internet yoksa loader son bilinen manifest'i (`data/manifest.json`) ve yerel dosyalari kullanir.
