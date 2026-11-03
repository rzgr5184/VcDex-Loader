# VcDex Loader — UI Redesign Plan (v1.3.0)

> **Durum:** Plan / uygulama bekliyor
> **Karar:** C++ + ImGui kalacak. Güncelleme motoru, enjeksiyon, manifest, self-heal, kapak indirme **aynen korunacak**. Sadece arayüz baştan yazılacak.
> **Ana stil:** Liquid Turquoise (turkuaz / mavi / beyaz), bol efekt + animasyon.
> **Tema:** 5 adet seçilebilir tema, Settings içinden değiştirilebilir.
> **Hedef sürüm:** 1.3.0

---

## 0. Kısa özet

Mevcut menü çalışıyor ama görsel olarak zayıf: düz koyu arka plan, sıkışık 2 sütun kart, küçük tipografi, sabit renkler, boş "Settings" alanı, efekt yok. Yeni sürümde:

- Animasyonlu **liquid/caustic arka plan** (GPU shader).
- Gerçek **cam panel** hissi (blur + tint + kenar ışığı).
- Sol **nav rail**, üst **header**, büyük **hero kart**, **oyun kartları**, sağ **detay paneli**, alt **durum çubuğu**.
- **Splash**, **update banner**, **toast**, **busy overlay** aynı kimlikle yeniden çizilecek.
- **5 tema** + efekt ayarları + kalıcı `settings.json`.
- Tek exe kalacak; yeni runtime/DLL bağımlılığı yok.
- Tüm güncelleme ve enjeksiyon davranışı birebir korunacak.

---

## 1. Hedefler ve kapsam

### 1.1 Hedefler

1. "Yazılım gibi" görünen, premium, akıcı bir loader.
2. Ana tema: **liquid turquoise / mavi / beyaz**.
3. 5 tema: aynı layout, farklı renk/atmosfer.
4. Bol ama abartısız efekt: akışkan arka plan, cam paneller, hover animasyonları, ripple, shimmer, yumuşak geçişler.
5. Ayarlar sayfası: tema, efekt yoğunluğu, blur kalitesi, reduce motion, güncelleme/onarım, yollar, hakkında.
6. Aynı tek exe, aynı otomatik güncelleme altyapısı.
7. Düşük donanımda da 60 FPS'e yakın; efektler kapatılabilir.
8. Türkçe karakter desteği (ı, ş, ğ, İ, ç, ö, ü) — font atlası buna göre.

### 1.2 Kapsam dışı

- Enjeksiyon / anti-cheat / bypass mantığı değişmeyecek.
- Yeni oyun eklemek bu planın parçası değil (ama layout 3+ oyunu destekleyecek).
- Online hesap / login / cloud sync yok.
- Ses efektleri yok (ileride opsiyonel).
- DLL/runtime kurulumu gerektiren framework yok (Electron/.NET/Qt yok).

---

## 2. Neden aynı framework?

| Konu | Mevcut | Yeni |
|---|---|---|
| Dil | C++17 | C++17 |
| UI | ImGui (custom draw) | ImGui (custom draw + D3D11 shader) |
| Render | D3D11 | D3D11 |
| Exe boyutu | ~1.15 MB | ~1.6–2.0 MB (gömülü font + shader) |
| Runtime bağımlılığı | Yok | Yok |
| Güncelleme | Hash + manifest | Aynı, değişmiyor |
| Risk | — | Düşük |

Framework değiştirmek (C#/Tauri/Qt) görsel olarak daha kolay olsa da updater + injection + tek exe dengesini bozar. Asıl eksik olan **tasarım sistemi**; onu ImGui üzerinde kuruyoruz.

---

## 3. Tasarım vizyonu — "Liquid Turquoise"

**Hissiyat:** derin okyanus altı, cam yüzeyler, yavaş hareket eden turkuaz ışık dalgaları, beyaz tipografi, ince cyan kenar ışıkları, yumuşak ama net.

**Ana prensipler:**

1. **Derinlik:** arka plan → cam panel → içerik → vurgu katmanı.
2. **Akış:** her geçiş 120–500 ms; ani zıplama yok.
3. **Nefes:** bol boşluk, büyük başlıklar, az ama güçlü vurgu rengi.
4. **Işık:** seçili öğe ve butonlar glow alır; pasif alanlar sakin kalır.
5. **Okunabilirlik:** cam efektleri metnin arkasında; kontrast her temada test edilir.

**Yasaklar:**

- Gökkuşağı gradyanlar, aşırı neon, her yeri parlayan butonlar.
- 3'ten fazla font ağırlığı.
- 500 ms üzeri zorunlu animasyon.
- Metin üzerinde blur.

---

## 4. Genel yerleşim

### 4.1 Ana pencere (Library)

```
┌────────────────────────────────────────────────────────────────────────────┐
│ [logo] VcDex Loader            [ search games... ]        [—] [□] [×]      │ 64px
├──────┬─────────────────────────────────────────────────────────────────────┤
│      │  Library                                      [CHECK FILES] [gear] │
│  ◈   │  Pick a game, then press Load Game.                                 │
│      │                                                                     │
│ Lib  │  ┌───────────────────────────────────────────────┬───────────────┐  │
│      │  │                                               │  DETAILS      │  │
│ Set  │  │              HERO / FEATURED GAME             │  cover thumb  │  │
│      │  │   big cover, title, status, LOAD GAME         │  status pill  │  │
│      │  │                                               │  process      │  │
│      │  └───────────────────────────────────────────────┤  appid        │  │
│      │  ┌──────────────┐  ┌──────────────┐             │  install dir  │  │
│      │  │ game card 1  │  │ game card 2  │             │  [OPEN] [LOAD]│  │
│      │  └──────────────┘  └──────────────┘             └───────────────┘  │
│      │                                                                     │
│      │                                                                     │
├──────┴─────────────────────────────────────────────────────────────────────┤
│ ● Online   v1.3.0   All files up to date        [progress bar / sync]     │ 40px
└────────────────────────────────────────────────────────────────────────────┘
```

- Varsayılan boyut: **1180 × 760**
- Minimum: **1024 × 680**
- Resizable, DPI-aware.
- 100% ölçekte sidebar 84 px, sağ panel 320 px.
- Genişlik < 1080 ise sağ panel alta iner (responsive).
- Genişlik < 900 ise oyun kartları tek sütun (min width'e kadar).

### 4.2 Settings sayfası

```
┌────────────────────────────────────────────────────────────────────────────┐
│ [logo] VcDex Loader                               [ back to library ]      │
├──────┬─────────────────────────────────────────────────────────────────────┤
│  ◈   │  Settings                                                           │
│      │  ┌───────────────────────────────────────────────────────────────┐  │
│ Lib  │  │  Appearance                                                   │  │
│ Set  │  │  Theme:  [Lagoon] [Aurora] [Ember] [Matcha] [Ice]             │  │
│      │  │          live animated preview cards                          │  │
│      │  │  Effects:  [x] Animations  [x] Particles  [x] Parallax        │  │
│      │  │  Background intensity:  [———●———]                             │  │
│      │  │  Blur quality:  ( ) Low   (•) High                            │  │
│      │  │  [x] Reduce motion                                            │  │
│      │  ├───────────────────────────────────────────────────────────────┤  │
│      │  │  Updates                                                      │  │
│      │  │  Installed: v1.3.0     Latest: v1.3.0     Last check: 2m ago  │  │
│      │  │  [CHECK NOW]  [REPAIR FILES]  [OPEN LOG]                      │  │
│      │  ├───────────────────────────────────────────────────────────────┤  │
│      │  │  Paths / About                                                │  │
│      │  └───────────────────────────────────────────────────────────────┘  │
└──────┴─────────────────────────────────────────────────────────────────────┘
```

Settings ayrı bir "sayfa" olarak açılır (modal değil), geçiş animasyonlu.

---

## 5. Bileşen spesifikasyonları

### 5.1 Pencere / title bar

- Borderless, custom title bar (mevcut drag mekanizması korunur: `WM_NCLBUTTONDOWN`, `HTCAPTION`).
- Win11'de `DwmSetWindowAttribute(DWMWA_WINDOW_CORNER_PREFERENCE)` ile yuvarlatılmış köşe (desteklenmiyorsa görmezden gel).
- Yükseklik 64 px.
- Sol: 28×28 logo mark + "VcDex Loader" (15 px semibold) + küçük "by Laynex" (10.5 px, dim).
- Orta: search alanı (240×38, cam, icon + placeholder + kısayol `Ctrl+K`).
- Sağ: minimize / maximize (opsiyonel) / close; 36×36, radius 10, hover'da `error` rengi.
- Sürükleme alanı: title bar'ın boş kısmı.

**Buton durumları:** normal (şeffaf) → hover (beyaz %8) → press (%14).
**Kapatma hover:** `error` %18 dolgu + kırmızı icon.

### 5.2 Sol nav rail (84 px)

- Üstte logo.
- Öğeler: **Library**, **Settings**; altta **Discord** (harici link), **GitHub**.
- Öğe: 56×56, radius 16, icon 22 px + altında 9.5 px label.
- Aktif öğe: cam dolgu + 3 px sol accent pill + icon glow.
- Aktif pill, sayfa geçişinde **260 ms** kayarak hareket eder.
- Alt kısımda sürüm `v1.3.0` (10 px, faint) + güncelleme varsa küçük turkuaz nokta.
- Hover: hafif yukarı kalkma (1 px) + icon parlaklığı.

### 5.3 Header

- Sol: sayfa başlığı "Library" (26 px semibold) + alt açıklama (12.5 px, dim).
- Sağ: `CHECK FILES` ghost butonu + settings icon butonu + opsiyonel refresh icon.
- `CHECK FILES` tıklanınca `app::CheckFiles()` — mevcut davranış aynı.
- Sayfa geçişinde başlık 12 px sağdan kayarak + fade girer (220 ms).

### 5.4 Hero / featured game

Seçili oyunu büyük gösteren ana kart.

- Boyut: genişlik = içerik alanı, yükseklik 260–280 px.
- Radius 24.
- Arka plan: oyun kapağı `object-fit: cover` + üstüne koyu gradyan scrim (soldan sağa) + cam overlay.
- Sol alt: oyun adı (30 px bold), altında subtitle (13 px), status pill.
- Sağ alt: `LOAD GAME` primary butonu (44 px) + `OPEN FOLDER` ghost.
- Hover: kapak 1.02 scale + paralaks (mouse'a göre ±4 px), kenar glow artar.
- Seçim değişince hero içeriği **crossfade + 8 px slide** ile geçer (300 ms).
- Kapak yoksa: temaya uygun gradyan + büyük baş harf monogram.

### 5.5 Oyun kartları

- Grid: genişlik ≥ 1080 → 2 sütun; altında hero.
- Kart: radius 20, cam panel.
- Üst: kapak (16:9 veya 4:3, radius 16) + sağ üst status chip.
- Alt: oyun adı (15 px semibold), subtitle (11.5 px dim), tag chip.
- Seçili: 2 px accent border + 16 px dış glow + sağ üstte küçük checkmark.
- Hover: yukarı 2 px, border parlaklığı +%30, kapakta shine sweep (6 sn'de bir).
- Tıklama: seçer; çift tıklama: `LoadSelectedGame()`.
- Kart giriş animasyonu: 40 ms stagger, 320 ms ease-out, y +16 → 0, opacity 0 → 1.

### 5.6 Sağ detay paneli (320 px)

- Cam panel, radius 20, iç padding 20.
- Kapak thumbnail (radius 14) + altında oyun adı.
- Bilgi satırları (36 px yükseklik):
  - Status (pill)
  - Process
  - AppID
  - Install dir (truncate + tooltip)
  - Injection type
- Alt butonlar: `OPEN FOLDER` (ghost), `LOAD GAME` (primary, full width).
- En altta "Last verified: ..." + dosya durumu (5/5 ready).
- Panel, seçim değişince 220 ms fade + 10 px slide.

### 5.7 Alt durum çubuğu (40 px)

- Sol: online/offline dot + metin ("Online", "Offline mode").
- Orta: `v1.3.0`.
- Sağ: dosya durumu ("All files up to date", "Updating 2/5...") + ince progress bar.
- Arka plan: cam, üst kenar 1 px border.
- Güncelleme sırasında progress bar accent → primary gradient, shimmer.
- Background sync sırasında küçük spinner + "Checking in background".

### 5.8 Splash

- Tam ekran liquid arka plan.
- Ortada:
  - Dönen `vc.png` logo (96 px) + çevresinde caustic halka.
  - Altında "VcDex Loader" (28 px bold) + "by Laynex" (11 px).
  - Progress bar veya halka (mevcut `state.progress` / `indeterminate` ile beslenir).
  - Durum metni: `state.phase` / `state.detail`.
- Logo sürekli yavaş döner (mevcut davranış korunur), ek olarak nefes alan glow.
- Splash çıkışı: 400 ms fade + 1.02 scale.
- Boot sırasında self-update akışı aynen çalışır; splash bunu kapatmaz.

### 5.9 Update banner

- Üstte 60 px cam bar (hero'nun üstünde).
- Sol: pulse eden accent dot + "Update available" / "Repair available".
- Orta: `v1.2.1 → v1.3.0` veya "Local executable differs from the official build".
- Sağ: `RESTART & UPDATE` primary butonu (**tıklama bağlı**, StartSelfUpdate).
- İşlem sırasında buton `UPDATING...`, altta ince progress.
- Banner girişi: yukarıdan 12 px slide + fade (260 ms).
- Tüm temalarda okunabilir kontrast.

### 5.10 Toast

- Sağ üst, 320×64, radius 14, cam.
- Sol accent bar (3 px) + icon (success/warn/error).
- Başlık + tek satır mesaj.
- Giriş: sağdan 24 px slide + fade (260 ms); çıkış 200 ms.
- 4.5 sn sonra otomatik kapanır; hover'da süre durur.
- Aynı anda max 3 toast; yenisi gelince eskiler yukarı kayar.

### 5.11 Busy overlay

- Tüm ekranı kaplayan cam dim (%55).
- Ortada 360×150 kart: spinner + başlık (`state.phase`) + detay (`state.detail`) + progress.
- `state.busy` true olduğunda 180 ms fade-in.
- İşlem bitince otomatik kapanır.
- İptal butonu yok (mevcut davranış).

### 5.12 Settings sayfası

Bölümler:

1. **Appearance**
   - 5 tema kartı (canlı animasyonlu mini preview): Lagoon, Aurora, Ember, Matcha, Ice.
   - Seçili tema 2 px accent border + check.
   - Tema değişimi 500 ms renk crossfade.
   - Efekt toggles: Animations, Particles, Parallax.
   - Background intensity slider (0–100).
   - Blur quality: Low / High.
   - Reduce motion toggle.
2. **Updates**
   - Installed version, latest version, last check time.
   - `CHECK NOW` → `app::CheckFiles()`.
   - `REPAIR FILES` → `app::CheckFiles()` (aynı, ama metin net).
   - `OPEN LOG` → `data/logs/update.log`.
3. **Paths**
   - Exe path, data path, `OPEN DATA FOLDER`, `COPY` butonları.
4. **About**
   - VcDex Loader, sürüm, yapımcı, GitHub linki, font lisansları.

Ayarlar `data/settings.json` dosyasına **atomik** yazılır (`.tmp` + `MoveFileExW`).

### 5.13 Diğer

- **Tooltip:** cam, 8 radius, 10/6 padding, 12 px.
- **Scrollbar:** 6 px, radius 3, primary %40; hover %65.
- **Empty state:** "No games configured" + cam kart + "Check files" butonu.
- **Offline state:** arka plan daha yavaş/dim, status "Offline mode", cached games gösterilir.
- **Focus ring:** klavye ile gezinmede 2 px accent outline.
- **Text truncation:** uzun oyun adları `...` ile; tooltip'te tam ad.

---

## 6. Tema sistemi

### 6.1 Mimari

- `Theme` struct: tüm renkler + shader renkleri + yoğunluklar.
- `ThemeManager`: aktif tema, önceki tema, geçiş süresi, interpolasyon.
- Renk geçişi `ImGui::ColorConvert` ile doğrusal uzayda lerp (500 ms, ease-out).
- Shader uniformları her frame aktif (interpolate edilmiş) renklerle güncellenir.
- `settings.json` içinde `"theme": "lagoon"`.

### 6.2 5 tema

#### 1) Lagoon — varsayılan (liquid turquoise / blue / white)

| Token | Hex |
|---|---|
| bg0 | `#03141B` |
| bg1 | `#062430` |
| bg2 | `#0A3340` |
| primary | `#2DD4BF` |
| primarySoft | `#5EEAD4` |
| secondary | `#38BDF8` |
| accent | `#EAFDFF` |
| glow | `#22D3EE` |
| blob1 | `#14B8A6` |
| blob2 | `#0EA5E9` |
| blob3 | `#67E8F9` |
| text | `#F1FDFF` |
| textDim | `#A6C4CE` |
| textFaint | `#62828D` |
| panelTint | `rgba(210,255,252,0.055)` |
| panelBorder | `rgba(160,240,255,0.18)` |
| success | `#34D399` |
| warning | `#FBBF24` |
| error | `#FB7185` |

#### 2) Aurora (mor / cyan / pembe)

| Token | Hex |
|---|---|
| bg0 | `#080512` |
| bg1 | `#120A24` |
| bg2 | `#1B1035` |
| primary | `#A78BFA` |
| primarySoft | `#C4B5FD` |
| secondary | `#22D3EE` |
| accent | `#F5D0FE` |
| glow | `#8B5CF6` |
| blob1 | `#7C3AED` |
| blob2 | `#22D3EE` |
| blob3 | `#EC4899` |
| text | `#F7F3FF` |
| textDim | `#B5A8CF` |
| textFaint | `#6E6288` |
| panelTint | `rgba(230,220,255,0.055)` |
| panelBorder | `rgba(196,181,253,0.20)` |

#### 3) Ember (turuncu / kırmızı / altın)

| Token | Hex |
|---|---|
| bg0 | `#140805` |
| bg1 | `#23100A` |
| bg2 | `#33160D` |
| primary | `#FB923C` |
| primarySoft | `#FDBA74` |
| secondary | `#F43F5E` |
| accent | `#FDE68A` |
| glow | `#F97316` |
| blob1 | `#F97316` |
| blob2 | `#F43F5E` |
| blob3 | `#FACC15` |
| text | `#FFF6EF` |
| textDim | `#CCAB9B` |
| textFaint | `#8A6A5C` |
| panelTint | `rgba(255,230,210,0.05)` |
| panelBorder | `rgba(253,186,116,0.20)` |

#### 4) Matcha (yeşil / lime / mint)

| Token | Hex |
|---|---|
| bg0 | `#04120C` |
| bg1 | `#082018` |
| bg2 | `#0C2B1F` |
| primary | `#34D399` |
| primarySoft | `#6EE7B7` |
| secondary | `#A3E635` |
| accent | `#D9F99D` |
| glow | `#10B981` |
| blob1 | `#10B981` |
| blob2 | `#84CC16` |
| blob3 | `#5EEAD4` |
| text | `#F0FFF7` |
| textDim | `#9FC6B3` |
| textFaint | `#5F8574` |
| panelTint | `rgba(220,255,235,0.05)` |
| panelBorder | `rgba(110,231,183,0.18)` |

#### 5) Ice (buz mavisi / çelik / beyaz — minimal)

| Token | Hex |
|---|---|
| bg0 | `#070B12` |
| bg1 | `#0C1420` |
| bg2 | `#121D2E` |
| primary | `#93C5FD` |
| primarySoft | `#BFDBFE` |
| secondary | `#E2E8F0` |
| accent | `#FFFFFF` |
| glow | `#60A5FA` |
| blob1 | `#60A5FA` |
| blob2 | `#94A3B8` |
| blob3 | `#E2E8F0` |
| text | `#F8FAFC` |
| textDim | `#A9B8CA` |
| textFaint | `#66788D` |
| panelTint | `rgba(235,245,255,0.05)` |
| panelBorder | `rgba(191,219,254,0.18)` |

> **Not:** ImGui `IM_COL32` formatı `0xAABBGGRR` (alpha en üst byte, sonra B, G, R). Hex tabloları `#RRGGBB`; kodda dönüştürülecek.

### 6.3 Tema kartı preview

Her tema kartı 150×90:
- Mini liquid arka plan (aynı shader, küçük RT veya statik gradient).
- 2–3 mini panel + accent çizgi.
- Seçiliyken canlı animasyon; değilken statik (performans).
- Hover'da hafif scale 1.02.

---

## 7. Efekt ve animasyon kataloğu

| # | Efekt | Tetik | Süre / eğri | Not |
|---|---|---|---|---|
| 1 | Liquid arka plan blob hareketi | sürekli | 18–40 sn döngü | shader, mouse paralaks |
| 2 | Caustic shimmer | sürekli | 12–20 sn | düşük yoğunluk |
| 3 | Grain | sürekli | 12 fps stepped | %3 opaklık |
| 4 | Cursor light | mouse | lerp 0.15 | %4 beyaz radial |
| 5 | Sayfa geçişi | nav | 220 ms ease-out | 12 px slide + fade |
| 6 | Kart girişi | sayfa açılışı | 320 ms, 40 ms stagger | y+16, fade |
| 7 | Kart hover | hover | 140 ms | y-2, border +30% |
| 8 | Kapak shine | hover (6 sn'de bir) | 900 ms | diagonal highlight |
| 9 | Kapak paralaks | mouse | lerp 0.12 | ±4 px |
| 10 | Seçim ring | seçim | 220 ms | accent + glow |
| 11 | Buton hover | hover | 120 ms | parlaklık +6%, y-1 |
| 12 | Buton press | click | 80 ms | scale 0.98 |
| 13 | Ripple | click | 700 ms | genişleyen halka |
| 14 | Nav indicator | sayfa | 260 ms | pill slide |
| 15 | Tema geçişi | tema seçimi | 500 ms | renk crossfade |
| 16 | Toast giriş/çıkış | toast | 260/200 ms | slide + fade |
| 17 | Progress shimmer | busy | 1.6 sn loop | accent şerit |
| 18 | Status pulse | update/online | 2.4 sn loop | dot glow |
| 19 | Splash logo dönüşü | boot | sürekli | mevcut + glow |
| 20 | Splash çıkışı | ready | 400 ms | fade + 1.02 scale |
| 21 | Window açılışı | launch | 240 ms | 0.98 → 1, fade |
| 22 | Sayaç animasyonu | dosya sayısı | 500 ms | number roll |

**Easing yardımcıları:** `EaseOutCubic`, `EaseInOutQuad`, `EaseOutBack` (sadece buton press), spring (hover).

**Reduce motion açıksa:** 1, 2, 4, 8, 9, 13, 18, 19 kapanır; kalanlar 120 ms fade'e düşer.

---

## 8. Teknik mimari

### 8.1 Mevcut yapı (korunacak)

- `src/main.cpp` — Win32 + D3D11 + ImGui + tüm UI (≈54 KB, monolit).
- `src/app.cpp` — updater, sync, self-update, injection çağrıları.
- `src/images.cpp` — PNG yükleme (stb), embedded + `data/covers`.
- `src/http.*`, `src/sha256.*`, `src/inject.*`, `src/json.hpp`.
- `src/config.hpp` — sürüm, mirror'lar.
- `tools/package.ps1` — dist + hash + manifest.

**Kural:** `app.cpp` içindeki güncelleme/enjeksiyon mantığı değişmeyecek. UI yalnızca mevcut `app::Snapshot()` / `app::CheckFiles()` / `app::StartSelfUpdate()` / `app::LoadSelectedGame()` API'lerini kullanacak.

### 8.2 Yeni modüller

```
src/
  theme.hpp / theme.cpp        -> 5 tema, renk interpolasyonu, token'lar
  backdrop.hpp / backdrop.cpp  -> RT'ler, shader compile, liquid bg, blur
  ui.hpp / ui.cpp              -> ortak widget'lar (glass, button, card, pill...)
  ui_pages.hpp / ui_pages.cpp  -> Library + Settings sayfaları
  ui_splash.hpp / ui_splash.cpp-> splash + busy overlay + toast
  settings.hpp / settings.cpp  -> settings.json oku/yaz, default'lar
  main.cpp                     -> Win32/D3D/ImGui kurulum + sayfa yönlendirme
res/
  fonts/Inter-*.ttf            -> gömülü (RCDATA)
  fonts/lucide-subset.ttf      -> ikonlar (RCDATA)
  shaders/background.hlsl      -> gömülü (RCDATA)
  shaders/blur.hlsl            -> gömülü (RCDATA)
  licenses/                    -> OFL / MIT metinleri
```

### 8.3 Render pipeline

```
Frame:
 1. Background pass
    - RT0 (yarım çözünürlük): liquid shader (blobs + caustic + grain)
 2. Blur passes
    - RT0 -> RT1 (1/4 çözünürlük)
    - RT1 --H--> RT2 --V--> RT3   (High: 2 iterasyon)
 3. ImGui draw
    - RT0 fullscreen background olarak çizilir
    - Cam paneller: RT3'ten `AddImageRounded` + tint + border
    - İçerik, metin, ikonlar
 4. Present
```

- RT'ler `WM_SIZE` ve DPI değişiminde yeniden oluşturulur.
- High kalite: blur 2 iterasyon; Low: 1 iterasyon, RT 1/6 çözünürlük.
- Shader derleme: build sırasında `fxc` varsa önceden derlenmiş bytecode gömülür; yoksa runtime `D3DCompile` (dinamik yüklenen `d3dcompiler_47.dll`). Başarısızsa fallback.

### 8.4 HLSL — background shader

**Uniform'lar:**

| Uniform | Tip | Açıklama |
|---|---|---|
| `uResolution` | float2 | piksel boyutu |
| `uTime` | float | saniye |
| `uMouse` | float2 | 0..1 normalize mouse |
| `uBg0/uBg1/uBg2` | float4 | tema arka plan renkleri |
| `uBlob0/uBlob1/uBlob2` | float4 | blob renkleri |
| `uIntensity` | float | 0..1 efekt yoğunluğu |
| `uCaustic` | float | caustic miktarı |
| `uGrain` | float | grain miktarı |
| `uRipplePos` | float2 | tıklama noktası |
| `uRippleTime` | float | ripple başlangıcından beri süre |

**Algoritma (özet):**
1. Ekran UV + hafif mouse parallax.
2. 2 katman domain-warped fbm noise (4 oktav) → liquid vein.
3. 3 adet hareketli radial blob (farklı periyod/faz), additive blend.
4. Caustic: iki yönlü `sin` + noise, düşük kontrast.
5. Ripple: merkezden genişleyen halka, `smoothstep`, sönümlü.
6. Vignette + grain + gamma.
7. Çıktı: tema `bg0..2` ile renklendirilmiş, `primary` hafif karışım.

**Performans:** yarım çözünürlükte tek pass, hedef < 1.5 ms (iGPU).

### 8.5 HLSL — blur shader

- Separable Gaussian, 9 tap.
- `dir` uniform (1,0) / (0,1), `texelSize`, `radius`.
- Yumuşak cam için yeterli; high kalitede 2 iterasyon.

### 8.6 Cam panel çizimi

`ui::GlassPanel(draw, min, max, rounding, opts)`:

1. `draw->AddImageRounded(blurSRV, min, max, uvMin, uvMax, tint, rounding)` — blur'lu arka planı kes.
2. Üstte `AddRectFilled` çok düşük alpha beyaz (cam parlaklığı).
3. `AddRect` 1 px panel border.
4. Üst kenara ince highlight çizgisi (1 px, beyaz %6).
5. Opsiyonel dış glow (accent %10, 16 px).

`uv` hesabı: panelin ekran koordinatı / ekran boyutu; blur RT 1/4 olduğu için aynı normalize UV kullanılır.

**Fallback:** blur yoksa `AddRectFilled` + hafif gradient + border.

### 8.7 Font ve ikon pipeline

- **Fontlar:** Inter (Regular 400, Medium 500, SemiBold 600, Bold 700), OFL lisansı.
- **İkonlar:** Lucide subset (sadece kullanılan ~20 ikon), MIT.
- Fontlar `res/fonts/` altında, `.rc` ile `RCDATA` olarak exe'ye gömülür.
- `AddFontFromMemoryTTF` ile yüklenir.
- Glyph range: `GetGlyphRangesTurkish()` + icon range birleştirilir.
- Boyutlar (100% ölçek): 10.5, 12, 13, 14, 16, 18, 24, 30, 34.
- Font yoksa Segoe UI fallback.
- DPI değişiminde atlas yeniden kurulur (`WM_DPICHANGED`).

### 8.8 Settings kalıcılığı

`data/settings.json`:

```json
{
  "schema": 1,
  "theme": "lagoon",
  "effects": {
    "animations": true,
    "particles": true,
    "parallax": true,
    "blur": "high",
    "intensity": 70,
    "reduceMotion": false
  },
  "window": { "width": 1180, "height": 760 },
  "lastGame": "redmatch2"
}
```

- İlk açılışta default'lar.
- Bozuk/eksik dosyada default'a dön + log.
- Atomik yazma: `settings.json.tmp` → `MoveFileExW`.
- `data/` yazılamıyorsa mevcut `%LOCALAPPDATA%\VcDex` fallback'i kullanılır.
- **Önemli:** updater'ın `.managed` listesine eklenmez; güncelleme settings'i ezmez.

### 8.9 DPI ve resize

- `WM_DPICHANGED`: `g_scale` güncelle, font atlasını yeniden kur, RT'leri yeniden boyutla.
- `WM_SIZE`: swapchain + RT resize; layout responsive.
- Minimum boyut `WM_GETMINMAXINFO` ile 1024×680.
- 100/125/150/200% test edilecek.

### 8.10 Performans bütçesi

| Bileşen | Bütçe |
|---|---|
| Background pass (0.5×) | < 1.5 ms |
| Blur (0.25×, high) | < 0.8 ms |
| ImGui draw + UI | < 3 ms |
| Toplam frame | < 8 ms (hedef 60 FPS) |
| Bellek (RT + font atlas) | < 40 MB |
| Exe artışı | < +700 KB |

- Efektler kapalıyken arka plan statik gradient (0 ms shader) → pil/düşük GPU dostu.
- `FPS` 60'a sabit; pencere arkada/minimize iken 15 FPS.

### 8.11 Fallback stratejisi

Her katman ayrı ayrı düşebilir:

1. Shader derlenemez → statik gradient + cam panel (blur yok).
2. Blur RT oluşturulamaz → düz cam panel.
3. Font yüklenemez → Segoe UI.
4. Kapak yüklenemez → monogram/gradient.
5. `settings.json` bozuk → default.
6. DWM yuvarlatma yok → kare köşe (Win10).

Hiçbir durumda loader açılmamazlık etmez.

---

## 9. Mevcut özelliklerin korunması (regression checklist)

| Özellik | Mevcut kaynak | Yeni UI'da |
|---|---|---|
| Splash + dönen logo | `main.cpp` | Aynı akış, yeni görsel |
| Açılışta otomatik self-update | `main.cpp` + `app.cpp` | Dokunulmaz |
| Update banner | `DrawUpdateBanner` | Yeniden çizilir, buton bağlı kalır |
| `RESTART & UPDATE` | `StartSelfUpdate` | Aynı |
| `F5` = update/check | `main.cpp` | Aynı |
| `Enter` = load | `main.cpp` | Aynı |
| `CHECK FILES` | `CheckFiles` | Header + detay panel + settings |
| `OPEN FOLDER` | `OpenSelectedFolder` | Aynı |
| `LOAD GAME` | `LoadSelectedGame` | Aynı |
| Toast | `SetToast` / `Snapshot` | Yeniden çizilir |
| Busy overlay | `state.busy` | Yeniden çizilir |
| Background sync | `state.backgroundSync` | Durum çubuğu + spinner |
| Kapaklar | `images.cpp` | Aynı + rounded + blur |
| Offline/cache | `app.cpp` | Aynı, yeni offline görünüm |
| Güncelleme logu | `data/logs/update.log` | Settings'ten açılır |
| Self-update marker toast | `app.cpp` | Aynı |
| Mirror / retry / hash | `app.cpp` | Dokunulmaz |

---

## 10. Dosya bazlı değişiklik planı

### Yeni dosyalar

| Dosya | İçerik |
|---|---|
| `src/theme.hpp/.cpp` | Theme struct, 5 palet, ThemeManager, interpolasyon |
| `src/backdrop.hpp/.cpp` | D3D11 RT yönetimi, shader derleme, blur, resize |
| `src/ui.hpp/.cpp` | GlassPanel, Button, IconButton, Card, Pill, Toast, Progress, Tooltip |
| `src/ui_pages.hpp/.cpp` | Library + Settings sayfaları, layout |
| `src/ui_splash.hpp/.cpp` | Splash, busy overlay, toast çizimi |
| `src/settings.hpp/.cpp` | JSON oku/yaz, default, migration |
| `res/fonts/*.ttf` | Inter + Lucide subset |
| `res/shaders/*.hlsl` | background + blur |
| `res/licenses/*.txt` | OFL / MIT |

### Değişecek dosyalar

| Dosya | Değişiklik |
|---|---|
| `src/main.cpp` | D3D/ImGui kurulumu korunur; UI çizimi `ui_pages`'e taşınır; DPI/resize/shader init eklenir |
| `src/app.hpp` | Gerekirse sadece UI için salt-okunur alanlar (ör. `lastChecked`, `filesTotal`) |
| `src/app.cpp` | **Mümkünse hiç dokunulmaz.** Sadece yeni alanları doldurmak için minimal ekleme |
| `src/images.cpp` | Kapak texture'ları için blur/thumbnail opsiyonel yardımcı |
| `CMakeLists.txt` | Yeni `.cpp` dosyaları; `d3dcompiler` opsiyonel |
| `res/resources.rc` | Font + shader `RCDATA`; sürüm 1.3.0 |
| `src/config.hpp` | `kVersion = "1.3.0"` |
| `tools/package.ps1` | Değişiklik gerekmez (hash/size otomatik) |
| `README.md` | Yeni UI, temalar, ekran görüntüleri |

### Dokunulmayacaklar

- `src/inject.cpp/.hpp`
- `src/http.cpp/.hpp`
- `src/sha256.cpp/.hpp`
- `src/json.hpp`
- `app.cpp` içindeki update/sync/self-update fonksiyonları

---

## 11. Uygulama fazları

> Toplam tahmini: **10–14 odaklı gün**. Her faz sonunda derlenebilir + çalışır durum.

### Faz 0 — Tasarım sistemi temeli (1 gün)

- [ ] `theme.hpp/.cpp`: token'lar, 5 palet, interpolasyon.
- [ ] `ui.hpp`: spacing/radius/font ölçekleri, renk yardımcıları.
- [ ] `main.cpp` içindeki hardcoded renkleri tema token'larına bağla (görsel değişiklik yok).
- [ ] Mevcut UI'ın screenshot'ını al (regression baseline).

**Çıktı:** Aynı görünüm, ama tüm renkler tema sisteminden geliyor.

### Faz 1 — Render foundation (2–3 gün)

- [ ] `backdrop.cpp`: RT yönetimi, resize.
- [ ] `background.hlsl` + `blur.hlsl`, gömme + derleme + fallback.
- [ ] Liquid arka plan ekrana bas.
- [ ] `GlassPanel` helper (blur + tint + border).
- [ ] Performans ölçümü (iGPU).

**Çıktı:** Hareketli liquid arka plan + cam paneller, eski UI üstünde test.

### Faz 2 — Layout shell (2 gün)

- [ ] Yeni pencere boyutu, title bar, nav rail, header, footer.
- [ ] Sayfa yönlendirme (Library / Settings) + geçiş animasyonu.
- [ ] Responsive breakpoint'ler.
- [ ] Drag/minimize/close korunur.

**Çıktı:** Boş ama tam iskelet, gezinilebilir.

### Faz 3 — Bileşenler (2–3 gün)

- [ ] Button / IconButton / Card / Pill / Progress / Tooltip / Toast.
- [ ] Hero kart + oyun kartları + detay paneli.
- [ ] Update banner (buton bağlı).
- [ ] Busy overlay.
- [ ] Klavye odağı ve kısayollar.

**Çıktı:** Tüm işlevler yeni görünümle çalışır.

### Faz 4 — Settings + temalar (2 gün)

- [ ] `settings.cpp` + `settings.json`.
- [ ] Settings sayfası tüm bölümler.
- [ ] 5 tema kartı + canlı preview + geçiş.
- [ ] Efekt toggles, intensity, blur quality, reduce motion.

**Çıktı:** Tema değiştirilebilir, ayarlar kalıcı.

### Faz 5 — Splash + polish (1–2 gün)

- [ ] Yeni splash (logo, caustic, progress).
- [ ] Tüm animasyon kataloğu.
- [ ] Reduced motion.
- [ ] Win10/Win11 farkları, DWM köşeler.

**Çıktı:** Tam deneyim.

### Faz 6 — Test + performans (1–2 gün)

- [ ] Test matrisi (aşağıda).
- [ ] Ekran görüntüleri (5 tema × ana ekran + settings + splash).
- [ ] Düşük GPU testi.
- [ ] Updater regression (bozuk DLL, eski exe, hash onarımı).

### Faz 7 — Release (0.5 gün)

- [ ] Sürüm 1.3.0, `resources.rc`, README.
- [ ] Build + package + commit + push.
- [ ] Remote manifest doğrulama (3 mirror).
- [ ] Eski sürümden otomatik yükseltme testi.

---

## 12. Kabul kriterleri (Definition of Done)

- [ ] 5 tema sorunsuz değişiyor ve kalıcı.
- [ ] Liquid arka plan + cam paneller 60 FPS'e yakın çalışıyor.
- [ ] Tüm mevcut işlevler bozulmadan çalışıyor (load, check, update, folder, toast).
- [ ] Update banner butonu gerçekten güncelleme başlatıyor.
- [ ] Splash → Library geçişi akıcı; self-update splash sırasında çalışıyor.
- [ ] 100/125/150% DPI'da bozulma yok.
- [ ] Türkçe karakterler doğru render ediliyor.
- [ ] Reduce motion açıkken rahatsız edici animasyon yok.
- [ ] Shader/font yüklenemezse loader yine açılıyor (fallback).
- [ ] `settings.json` bozulsa bile default ile açılıyor.
- [ ] Updater regression testleri geçiyor (bozuk DLL, eski exe, hash onarımı).
- [ ] Exe tek dosya; yeni runtime bağımlılığı yok.
- [ ] Remote manifest 1.3.0 üç mirror'da doğru.

---

## 13. Test matrisi

| Senaryo | Beklenen |
|---|---|
| Temiz kurulum (data yok) | Splash → dosyalar iner → Library |
| Offline açılış | Cached oyunlar + offline durumu |
| Güncelleme var | Banner + otomatik self-update |
| Banner butonuna tıkla | Update başlar, restart, toast |
| Bozuk DLL | Açılışta sessiz onarım |
| Bozuk exe (aynı sürüm) | Hash onarımı |
| Eski exe (1.0.0) | F5 veya yeni exe ile yükseltme |
| Background sync | Durum çubuğunda spinner, oyun kesilmez |
| 5 tema × 3 ekran | Görsel bozulma yok |
| 100/125/150/200% DPI | Keskin metin, doğru layout |
| Win10 | Yuvarlatma fallback, efektler çalışır |
| Win11 | Yuvarlatılmış köşe |
| iGPU / düşük GPU | 60 FPS veya efekt otomatik düşer |
| Reduce motion | Animasyonlar sadeleşir |
| Uzun oyun adı | Truncate + tooltip |
| Kapak yok | Monogram fallback |
| Klavye only | Tüm butonlara erişim, focus ring |
| Pencere resize | Layout bozulmaz |
| Minimize/restore | RT'ler doğru yeniden boyutlanır |

---

## 14. Riskler ve azaltma

| Risk | Olasılık | Etki | Azaltma |
|---|---|---|---|
| Shader derleme sorunu | Orta | Yüksek | Build-time fxc + runtime fallback + statik gradient |
| Düşük GPU'da blur pahalı | Orta | Orta | Low kalite, 1/4 RT, efekt toggle |
| Font lisansı | Düşük | Orta | Inter OFL, Lucide MIT; lisans dosyaları repo'da |
| Türkçe glyph eksikliği | Düşük | Yüksek | `GetGlyphRangesTurkish()` + test |
| DPI'da layout kayması | Orta | Orta | Tüm ölçüler `g_scale` ile; DPI testleri |
| Exe boyutu artışı | Düşük | Düşük | Font subset, shader küçük; +700 KB kabul |
| Updater'ın settings'i ezmesi | Düşük | Yüksek | Settings `.managed` dışında |
| Tema geçişinde okunabilirlik | Orta | Orta | Kontrast testi, metin hep açık renk |
| Win10'da DWM farkı | Orta | Düşük | Custom liquid ana görsel; DWM opsiyonel |
| Bakım maliyeti (monolit) | Orta | Orta | Faz 0'da modüllere ayır |
| AV false positive | Düşük | Orta | Yeni DLL yok, runtime compile opsiyonel |

---

## 15. Sürüm / paketleme / push

1. `src/config.hpp` → `kVersion = "1.3.0"`.
2. `res/resources.rc` → dosya sürümü 1.3.0.
3. `README.md` → yeni ekran görüntüleri + tema listesi.
4. `cmake --build build --config Release`.
5. `tools/package.ps1 -SkipBuild`.
6. `git add -A && git commit -m "release 1.3.0 - liquid UI redesign"`.
7. `git push origin main`.
8. 3 mirror'da manifest doğrula.
9. Eski 1.2.1 exe ile otomatik yükseltme testi.

**Manifest şeması değişmez.** `package.ps1` hash/size'ı otomatik üretir. Ekstra dosya yok (font/shader exe içinde).

---

## 16. Uygulama günlüğü (implement ederken işaretle)

```
[ ] Faz 0  Tasarım sistemi
[ ] Faz 1  Render foundation
[ ] Faz 2  Layout shell
[ ] Faz 3  Bileşenler
[ ] Faz 4  Settings + temalar
[ ] Faz 5  Splash + polish
[ ] Faz 6  Test + performans
[ ] Faz 7  Release 1.3.0
```

Her faz sonunda:

```
- derleme: OK / FAIL
- ekran görüntüsü: docs/screenshots/<faz>.png
- notlar:
```

---

## 17. Referanslar / ilham

- Liquid glass / glassmorphism: yumuşak blur, ince border, düşük opaklık tint.
- Okyanus/caustic shader'ları: domain-warped fbm + additive blob'lar.
- Modern launcher layout: sol rail + hero + detay paneli (Steam/Riot/Epic modern varyasyonları).
- Motion: 120–320 ms aralığı, ease-out; sadece vurgu yerlerde spring.
- Erişilebilirlik: reduced motion, kontrast, klavye odağı.

> Not: İnternet araştırması bu oturumda araç tarafından iptal edildi; yukarıdaki referanslar genel tasarım pratiğine dayanıyor. Uygulama sırasında spesifik ilham linkleri eklenebilir.

---

## 18. Uygulama sırasında uyulacak kurallar

1. `app.cpp` updater/enjeksiyon mantığına dokunma.
2. Tek exe kalsın; yeni runtime DLL'i ekleme.
3. Tüm ölçüler `g_scale` ile çarpılsın.
4. Renkler sadece `ThemeManager`'dan gelsin; hardcoded renk bırakma.
5. Yeni metinler UTF-8; Türkçe karakterler test edilsin.
6. Her frame allocation yapma; RT ve font atlasını cache'le.
7. Efektler kapatılabilir olmalı.
8. Fallback'siz özellik ekleme.
9. Her faz sonunda çalışan build + screenshot.
10. Sürüm numarası ve manifest ancak release fazında değişsin.

---

## 19. Açık kararlar (implement öncesi netleştirilecek)

- [ ] Varsayılan pencere boyutu 1180×760 mı kalsın, yoksa mevcut 1060×700 mü?
- [ ] Arayüz dili İngilizce mi kalsın, yoksa TR/EN dil seçeneği de eklensin mi?
- [ ] Maximize butonu olsun mu? (Şu an yok)
- [ ] İlk açılışta tema seçim ekranı gösterilsin mi, yoksa Lagoon default mu?
- [ ] `vc.png` splash logosu korunsun mu, yoksa yeni bir logo mark mı çizilsin?
- [ ] Oyun sayısı 2; hero + grid düzeni 3+ oyunda nasıl davransın? (öneri: ilk 1 hero, kalanlar grid)
