# VcDex Loader

Oyun hilelerini tek tikla yukleyen, GitHub uzerinden otomatik guncellenen Windows loader.

- **Yapimci:** Laynex
- **Surum:** 1.3.0
- **Platform:** Windows 10 / 11 (x64)

## Yenilikler (1.3.0)

- **Tamamen yeni arayuz:** likit turkuaz / mavi / beyaz tema, GPU uzerinde calisan dalgali arka plan, cam (glass) paneller, yumusak gecisler ve mikro animasyonlar.
- **5 secilebilir tema:** Lagoon (varsayilan), Aurora, Ember, Matcha ve Ice. Tema secimi aninda onizlenir ve `data/settings.json` icinde saklanir.
- **Ayarlar sayfasi:** tema kartlari, animasyon / partikul / parallax anahtarlari, arka plan siddeti, cam blur kalitesi (LOW / HIGH) ve reduce motion secenegi.
- **Kalici tercihler:** tema, efektler ve pencere boyutu atomik olarak `data/settings.json` dosyasina yazilir; guncellemeler bu dosyaya dokunmaz.
- **Yeni kutuphane duzeni:** one cikan oyun icin buyuk hero karti, kapak gorselleri, detay paneli, arama (`Ctrl+K`), guncelleme bandi ve durum cubugu.
- **Animasyonlu splash korundu:** acilista surum kontrolu, otomatik guncelleme ve dosya dogrulama akisi aynen calisir.
- **Guvenli geri donus:** GPU arka plani veya blur olusturulamazsa arayuz gradient tabanli yedek gorunume otomatik gecer.
- **Guncelleme / enjeksiyon mantigi degismedi:** otomatik guncelleme, SHA-256 onarimi, self-heal ve DLL enjeksiyonu onceki surumlerle ayni sekilde calisir.

## Yenilikler (1.2.1)

- **Update butonu duzeltildi:** `RESTART & UPDATE` butonu artik gercekten guncellemeyi baslatiyor (onceki surumlerde tiklama yok sayiliyordu).
- Butona basildiginda aninda "Preparing update" geri bildirimi ve islem surerken "UPDATING..." etiketi.
- `F5` davranisi korundu: guncelleme varsa baslatir, yoksa dosya dogrulama yapar.

## Yenilikler (1.2.0)

- **Tam otomatik guncelleme:** her acilista once manifest alinir, ardindan tum dosyalar SHA-256 ile dogrulanir. Eksik, bozuk veya eski dosya varsa sessizce yeniden indirilir.
- **Exe hash kontrolu:** loader kendi SHA-256'sini manifest ile karsilastirir. Surum ayni olsa bile eski/degistirilmis exe otomatik yenilenir (test klasorundeki eski kopyalar dahil).
- **Coklu sunucu:** GitHub raw basarisiz olursa jsDelivr ve GitHub raw yedekleri otomatik denenir.
- **Dayanikli indirme:** 3 deneme, yedek sunucu gecisi, `.part` gecici dosya, hash + boyut dogrulama, atomik degistirme ve kilitli dosya icin tekrar deneme.
- **Yedekli self-update:** eski exe `*.old` olarak saklanir; degistirme basarisiz olursa geri alinir. Yardimci bat PID bekler, 80 kez dener ve sonucu loglar.
- **Arka plan kontrolu:** uygulama acikken 10 dakikada bir otomatik kontrol; yeni dosyalar sessizce indirilir, yeni loader surumu bildirilir.
- **Kurulum onarimi:** `CHECK FILES` / `F5` ile tum kurulumu elle dogrulayip onarabilirsiniz.
- **Guncelleme gunlugu:** `data/logs/update.log` (1 MB'ta devirli) tum adimlari kaydeder.
- **Eski dosya temizligi:** manifestten kaldirilan dosyalar ve yarim kalan `.part`/`.tmp` dosyalari otomatik silinir.
- **Yazilamayan klasor destegi:** exe klasoru yazilamazsa veriler `%LOCALAPPDATA%\VcDex` altinda tutulur.

## Yenilikler (1.1.0)

- Animasyonlu acilis ekrani: donen VcDex logosu, yumusak progress ve durum metni.
- Guncelleme varsa acilista otomatik uygulanir (splash sirasinda indirilir ve yeniden baslatilir).
- Oyun kartlarinda gercek kapak gorselleri: Redmatch 2 ve One Armed Robber.
- Kart hover animasyonu, aurora arka plan, toast slide-in ve yumusak gecisler.
- Gorseller exe icine gomuldu (`res/vc.png`, `res/rm2.png`, `res/oar.png`); harici dosya gerekmez.

## Kullanim

1. `VcDexLoader.exe` dosyasini calistir.
2. Listeden oyunu sec.
3. **LOAD GAME** butonuna bas (veya `Enter`).

Loader oyunu gerekirse Steam uzerinden baslatir, DLL'i enjekte eder ve sonucu bildirir.

Guncelleme varsa acilista splash sirasinda otomatik uygulanir. Uygulama acikken yeni surum bulunursa ustte "Update available" bandi cikar; **RESTART & UPDATE** veya `F5` ile yenilenir. `CHECK FILES` butonu tum dosyalari dogrulayip onarir.

Elle kontrol: `F5` (guncelleme yoksa dosya dogrulama yapar). Guncelleme gunlugu: `data/logs/update.log`.

Kisayollar: `Ctrl+K` arama alanina odaklanir, `Enter` secili oyunu yukler, `Esc` aramayi temizler / ayarlardan kutuphaneye doner. Tema ve efektler **Settings -> Appearance** bolumunden degistirilir.

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
  covers/
    rm2.png
    oar.png
  manifest.json        <- son bilinen manifest (offline yedek)
  settings.json        <- tema, efektler ve pencere tercihleri
  logs/update.log      <- guncelleme gunlugu
  .managed             <- yonetilen dosya listesi (eski dosya temizligi)
```

Loader her acilista manifesti alir, tum dosyalari hash ile dogrular ve eksik/bozuk/eski olanlari indirir. Ayni exe farkli bir klasorden calistirilsa bile kendi klasorundeki `data` dizinini kurar ve guncel hale getirir.

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
