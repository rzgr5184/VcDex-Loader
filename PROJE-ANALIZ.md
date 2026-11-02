# VcDex Loader — Teknik Analiz ve Geliştirme Yol Haritası

> Bu belge, projenin mevcut durumunu, kod seviyesindeki bulguları ve
> UI/UX/görsel/mimari açıdan kapsamlı bir geliştirme planını içerir.
>
> **Kapsam notu:** UI/UX, mimari, derleme, yayın ve kod kalitesi konularında
> yardımcı olunabilir. Anti-cheat bypass / tespit edilmeme (undetected) /
> enjeksiyon gizleme tarafını güçlendirmeye yönelik çalışmalar bu kapsamın
> dışındadır.

---

## 1. Proje Özeti

| Alan | Değer |
| --- | --- |
| Dil | C++17 |
| Platform | Windows 10/11 x64 |
| Arayüz | Win32 + DirectX 11 + Dear ImGui 1.93.1 WIP |
| Derleme | CMake 3.20+, MSVC 2022, statik CRT |
| Bağımlılık | Sadece `vendor/imgui` (harici kütüphane yok) |
| Güncelleme | GitHub `raw.githubusercontent.com` üzerinden `manifest.json` + SHA-256 |
| Boyut | ~63 KB kaynak kod, ~940 KB exe |

### Klasör yapısı

```
src/
  main.cpp      Win32 pencere, D3D11, ImGui kurulumu, tüm UI çizimi (~1000 satır)
  app.cpp       Manifest, indirme, dosya senkronizasyonu, enjeksiyon akışı, self-update
  http.cpp      WinHTTP GET / Download
  inject.cpp    Process bulma, Steam yolu, LoadLibrary enjeksiyonu, injector çalıştırma
  json.hpp      Bağımlılıksız minimal JSON parser
  sha256.cpp    SHA-256
  config.hpp    Ürün adı, sürüm, GitHub repo bilgisi
  app.hpp       UI state ve public API
res/
  icon.ico      Tek görsel varlık
  resources.rc  İkon + VERSIONINFO (sürüm elle sabit)
vendor/imgui    Gömülü ImGui
data/           Yerel DLL/injector + manifest cache (git'e dahil değil)
dist/           Yayınlanan exe + DLL'ler (git'e dahil)
tools/
  package.ps1   Build + dist kopyalama + SHA-256 + manifest üretimi
```

### Akış

1. `wWinMain` → D3D11 cihazı + ImGui + fontlar → `app::Init` → `StartupWorker` thread.
2. `manifest.json` indirilir → JSON parse → `data/manifest.json` cache'lenir.
3. Her oyun için eksik/hash'i tutmayan dosyalar `data/<oyun>/` altına indirilir.
4. `LOAD GAME`:
   - Injector varsa: oyun kapalıysa `--auto` ile erken enjeksiyon, açıksa mevcut sürece bağlanma.
   - Injector yoksa: Steam ile başlat → process bekle → `LoadLibraryW` remote thread.
5. Self-update: yeni exe indir → SHA-256 doğrula → geçici `.bat` ile dosyayı değiştir → yeniden başlat.

---

## 2. Kritik Bulgular (öncelik sırasıyla)

### P0 — Güvenlik ve yayın

1. **Güncelleme güvenliği yalnızca SHA-256'ya dayanıyor.**
   Manifest de aynı repodan geldiği için repo/hesap ele geçirilirse saldırgan
   kendi exe'sini ve hash'ini yayınlayabilir. Kod imzalama (Authenticode) ve
   manifest imzası (Ed25519/minisign) gerekir; public key exe içine gömülmeli.

2. **`raw.githubusercontent.com` binary dağıtımı için uygun değil.**
   Rate limit, Cache-Control, büyük dosya ve `text/plain` content-type sorunları
   var. GitHub **Releases** + `browser_download_url` kullanılmalı.

3. **`dist/` git'e commit ediliyor.**
   Her sürümde ~2.5 MB binary Git geçmişinde kalıcı olarak birikiyor. `dist/`
   `.gitignore`'a alınmalı, çıktılar Release asset olarak yüklenmeli.

4. **Self-update `.bat` riskli.**
   `ping` ile bekleme, `move /y`, `%~f0` silme; başarısızlıkta geri dönüş yok.
   Yedek exe tutulmalı, `MoveFileEx` + rollback uygulanmalı, `.bat` yerine
   yardımcı bir updater exe tercih edilmeli.

5. **İmzasız binary'ler.**
   SmartScreen "bilinmeyen yayıncı" uyarısı, Defender/AV false-positive ve
   GitHub malware politikası riski. Kod imzalama sertifikası + VirusTotal
   şeffaflığı önerilir.

6. **Hukuki / ToS riski.**
   Anti-cheat bypass ve DLL enjeksiyonu oyun ToS ihlali, hesap banı ve bazı
   ülkelerde hukuki sorumluluk doğurabilir. Kullanıcı sözleşmesi/sorumluluk
   reddi ve yaş doğrulama eklenmeli.

### P1 — Doğruluk / dayanıklılık

7. **`resources.rc` sürümü elle sabit (`1,0,0,0`).**
   `package.ps1` sadece `config.hpp`'yi güncelliyor; dosya özelliklerinde sürüm
   eski kalıyor. Script `.rc` dosyasını da güncellemeli.

8. **`SyncAllFiles` her açılışta tüm dosyaların SHA-256'sını hesaplıyor.**
   Büyük DLL'lerde açılışı yavaşlatır. `(boyut, mtime)` cache'i + sadece
   değişince hash kontrolü yapılmalı.

9. **`FindFileRecursively` tüm Steam kütüphanesini tarıyor.**
   `appmanifest_<appid>.acf` dosyasındaki `installdir` doğrudan okunmalı;
   recursive tarama son çare olmalı.

10. **Thread'ler `detach()` ediliyor.**
    Kapanışta join/iptal yok; yarım indirme veya çökme riski. Thread pool +
    `std::jthread`/iptal bayrağı + kapanışta bekleme gerekir.

11. **Her frame `Snapshot()` tüm state'i (oyun listesi dahil) kopyalıyor.**
    Küçük ama gereksiz; sürüm sayacı veya `const&` snapshot ile iyileştirilebilir.

12. **Fontlar `C:\Windows\Fonts`'tan yükleniyor.**
    Taşınabilirlik yok; `seguisb.ttf` yoksa fallback var ama tasarım bozulabilir.
    Inter/Manrope gibi fontlar `res/fonts/` altına gömülmeli.

13. **DPI yalnızca açılışta hesaplanıyor.**
    Monitör değişince ölçek güncellenmiyor. `WM_DPICHANGED` işlenmeli.

14. **Win10'da DWM rounded corner yok, gölge yok.**
    `DWMWA_WINDOW_CORNER_PREFERENCE` sadece Win11'de çalışır. Win10 için
    özel gölge/rounded region gerekir.

15. **Loglama ve crash handler yok.**
    Sahada teşhis imkânsız. Dosya log + `MiniDumpWriteDump` eklenmeli.

16. **Türkçe/İngilizce string'ler karışık.**
    UI metinleri çoğunlukla İngilizce, hata/toast mesajları Türkçe. i18n
    altyapısı (TR/EN) kurulmalı.

17. **Tek örnek (single instance) koruması yok.**
    İki loader aynı anda açılırsa dosya çakışması olabilir. Named mutex eklenmeli.

18. **CLI argümanı yok.**
    `--silent`, `--update`, `--game <id>` gibi seçenekler otomasyon ve
    kısayol için faydalı olur.

19. **`config::RemoteConfigured()` yalnızca `OWNER_TODO` kontrol ediyor.**
    Boş owner/repo durumunu da yakalamalı.

20. **`json.hpp` accent alanını `AsNumber(i)` ile okuyor.**
    Şema netleştirilmeli: `accent` string anahtar (`violet`, `orange`) olsa
    manifest daha okunur ve ileriye dönük uyumlu olur.

### P2 — Küçük kod kokuları

- `std::size(levels)` için `<iterator>` include'u var, iyi; ancak `#pragma comment(lib)` ile CMake link'leri tekrar ediyor.
- `main.cpp` tek dosyada ~1000 satır; UI bileşenleri ayrılmalı.
- Renk paleti `const ImU32` global; tema sistemi için yapıya taşınmalı.
- `DrawSpaced` karakter karakter çiziyor; Türkçe/Unicode karakterlerde ve
  ligature'larda sorun çıkarabilir.
- `Ellipsize` byte bazlı; kelime ortasında kesebilir. Unicode-aware olmalı.
- Manifest şema sürümü yok (`schema_version` eklenmeli).
- İndirme sırasında hız/kalan süre gösterilmiyor.
- Hata durumunda `Retry` butonu yok.

---

## 3. UI / UX ve Görsel Geliştirme Listesi

### A. Marka ve kimlik

- [ ] Logo tasarımı: SVG ana kaynak + 16/32/48/256 çok boyutlu `.ico`
- [ ] Wordmark (VcDex yazı logosu) ve koyu/açık varyantlar
- [ ] Renk paleti ve token'lar: arka plan, yüzey, kenarlık, metin, accent, durum renkleri
- [ ] Tema sistemi: `Midnight` (mevcut), `Ember`, `Arctic`, `Solarized` + JSON tema dosyası
- [ ] Gömülü fontlar: Inter / Manrope / Space Grotesk (başlık + gövde + mono)
- [ ] Splash / boot ekranı (logo + ince progress, 1–2 sn)
- [ ] GitHub sosyal önizleme (1280×640), README GIF'i, installer banner

### B. Ana ekran ve oyun kartları

- [ ] **Gerçek oyun kapak görselleri** (Steam CDN `header.jpg` / `capsule_616x353.jpg`),
      indirme + yerel cache, çevrimdışı fallback gradient
- [ ] Kart hover: hafif scale, kapak zoom, gradient kenarlık, glow
- [ ] Seçili kart: animasyonlu accent ring + spring checkmark
- [ ] Arama kutusu (anlık filtre), sıralama (isim / son oynanan / durum)
- [ ] Kategori sekmeleri: `All`, `Ready`, `Favorites`
- [ ] Favoriler (yıldız) ve "son oynanan" kaydı
- [ ] Grid / list görünüm değiştirici
- [ ] Yükleme iskeleti (shimmer skeleton)
- [ ] Boş durum illüstrasyonu ("No games found")
- [ ] Kart üzerinde: boyut, sürüm, son oynama, durum geçmişi
- [ ] Sağdan açılan detay paneli: açıklama, tuşlar, changelog, ekran görüntüleri
- [ ] Oyun durumu rozetleri: `Undetected`, `Updating`, `Maintenance`, `Offline`

### C. Animasyonlar (ImGui'de elle çizilecek)

- [ ] Sayfa geçişleri: fade + slide (150–250 ms, ease-out)
- [ ] Kart stagger animasyonu (liste yüklenirken sırayla belirme)
- [ ] Buton basma spring'i ve ripple efekti
- [ ] Progress bar: shimmer, yumuşak easing, parçacık izi
- [ ] Spinner: mevcut arc + dot pulse / orbit alternatifi
- [ ] Toast: slide-in, otomatik kapanma progress'i, kuyruk yönetimi
- [ ] Update banner: nabız efekti + changelog marquee
- [ ] Arka plan: yavaş aurora / starfield / noise + fare parallax
- [ ] Logo: glow pulse, hover tilt
- [ ] Sidebar aktif göstergesi: lerp ile kayan çubuk
- [ ] Başarılı yüklemede confetti / başarı patlaması
- [ ] Sayaç animasyonları (sürüm, dosya sayısı)
- [ ] "Reduced motion" ayarı (erişilebilirlik)

### D. Varlıklar (assets)

- [ ] `res/logo.png`, `res/wordmark.png`, `res/background.png`, `res/noise.png`
- [ ] Oyun kapakları (cache'lenmiş) + placeholder gradient
- [ ] İkon seti: Lucide/Feather SVG → PNG atlas veya icon font
      (mevcut elle çizilen 2x2 grid yerine gerçek ikonlar)
- [ ] Steam / platform ikonu, durum ikonları
- [ ] Ekran görüntüsü carousel'i
- [ ] Installer (Inno Setup / NSIS) banner ve ikonları
- [ ] Windows uygulama ikonu (çok boyutlu)

### E. Özellikler

- [ ] **Settings sayfası**: tema, dil (TR/EN), indirme klasörü, otomatik güncelleme,
      proxy, reduced motion, ses aç/kapa
- [ ] Sistem tepsisi (tray) + arka planda çalışma
- [ ] Discord Rich Presence ("VcDex Loader'da")
- [ ] Otomatik oyun algılama: Steam `appmanifest` taraması, "Install" durumu
- [ ] İndirme kuyruğu: duraklat/devam, hız ve kalan süre
- [ ] Update changelog ekranı
- [ ] Hata merkezi + log görüntüleyici + "Retry"
- [ ] Çevrimdışı mod göstergesi ve yeniden bağlanma
- [ ] Kullanıcı sözleşmesi / sorumluluk reddi ekranı
- [ ] Klavye navigasyonu, yüksek kontrast, ekran okuyucu etiketleri
- [ ] Opsiyonel ses efektleri (hover/click/success)

### F. Teknik UI altyapısı

- [ ] `stb_image` ile PNG/JPG yükleme + texture cache
- [ ] Asenkron görsel indirme (UI thread'ini bloklamadan)
- [ ] Animasyon yardımcıları: `Spring`, `Tween`, `Easing`, `AnimatedValue`
- [ ] Tema/token sistemi (`Theme` struct + JSON)
- [ ] Responsive layout: minimum 900×600, ölçekli kart grid'i
- [ ] `WM_DPICHANGED` ile çoklu monitör DPI
- [ ] Win10 için özel gölge / rounded region
- [ ] UI bileşenlerini ayrı dosyalara bölme (`ui/sidebar.cpp`, `ui/cards.cpp`, ...)

---

## 4. Mimari İyileştirme

- [ ] Katmanlı yapı: `core/`, `net/`, `update/`, `inject/`, `ui/`
- [ ] State machine: `Idle → Checking → Syncing → Ready → Loading → Error`
- [ ] Thread pool + iptal (kapanışta güvenli join)
- [ ] Kullanıcı ayarları: `%APPDATA%\VcDex\settings.json`
- [ ] Loglama: `%APPDATA%\VcDex\logs\` + rotasyon
- [ ] Crash handler: `MiniDumpWriteDump` + rapor yükleme (opsiyonel)
- [ ] Tek örnek mutex'i
- [ ] CLI argümanları: `--silent`, `--update`, `--game <id>`
- [ ] Manifest `schema_version` + geriye dönük uyumluluk
- [ ] Delta / sıkıştırılmış güncelleme (zstd) — ileride
- [ ] Unit test: JSON parser, sürüm karşılaştırma, hash doğrulama
- [ ] GitHub Actions CI: build → Release → manifest üretimi → imza → yayın

---

## 5. Yayın / CI Önerisi

```yaml
# .github/workflows/release.yml (özet)
- cmake -S . -B build -G "Visual Studio 17 2022" -A x64
- cmake --build build --config Release
- signtool sign /fd SHA256 /tr <timestamp> /td SHA256 dist/VcDexLoader.exe
- gh release create v$VERSION dist/* --notes-file CHANGELOG.md
- tools/package.ps1 -Version $VERSION -SkipBuild
```

- Sürüm numarası tek kaynaktan (`config.hpp` veya `VERSION` dosyası) üretilmeli.
- `manifest.json` Release asset URL'lerini içermeli, `raw` kullanılmamalı.
- Release notları otomatik changelog'a dönüşmeli.

---

## 6. Öncelikli Yol Haritası

| Öncelik | İş | Kazanç |
| --- | --- | --- |
| P0 | Releases + imzalama + manifest imzası | Güvenlik, SmartScreen, güven |
| P0 | Loglama + crash handler + single instance | Teşhis ve kararlılık |
| P0 | `resources.rc` sürüm senkronizasyonu | Doğru dosya bilgisi |
| P1 | Ayarlar sayfası + TR/EN i18n | Kullanılabilirlik |
| P1 | Oyun kapak görselleri + gerçek ikonlar | Görsel kalite |
| P1 | Arama / favoriler / filtre / detay paneli | Kullanılabilirlik |
| P1 | Animasyon altyapısı + geçişler | Premium his |
| P2 | Tray, Discord RPC, tema sistemi | Ürünleşme |
| P2 | İndirme kuyruğu + hız/retry | Güvenilirlik |
| P3 | CI/CD, delta update, overlay | Ölçeklenebilirlik |

---

## 7. Derleme

Gerekli: Visual Studio 2022 Build Tools (C++ workload) + CMake 3.20+.

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
# çıktı: dist/VcDexLoader.exe
```

Paketleme:

```powershell
powershell -ExecutionPolicy Bypass -File tools/package.ps1
```

---

## 8. Hızlı Kazançlar (1–2 günde yapılabilir)

1. `.gitignore`'a `dist/` ekle, eski binary'leri geçmişten temizle (BFG/filter-repo).
2. `resources.rc` sürümünü `package.ps1` içinde otomatik güncelle.
3. `manifest.json`'a `schema_version` ve oyun `cover` URL'i ekle.
4. Steam kapak görselini indir + cache'le + gradient fallback koy.
5. Toast/ilerleme animasyonlarını yumuşat; buton spring'i ekle.
6. Arama kutusu + favoriler + `Ready` filtresi.
7. `%APPDATA%\VcDex\settings.json` ve log dosyası.
8. Tek örnek mutex'i + `--silent` / `--game` argümanları.
