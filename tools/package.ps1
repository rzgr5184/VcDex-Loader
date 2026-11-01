param(
    [string]$Version = "",
    [switch]$SkipBuild
)

$ErrorActionPreference = "Stop"

$root = Split-Path -Parent (Split-Path -Parent $PSCommandPath)
$dist = Join-Path $root "dist"
$data = Join-Path $root "data"
$configPath = Join-Path $root "src\config.hpp"

# ---------------------------------------------------------------- version sync
if (-not $Version) {
    $match = Select-String -Path $configPath -Pattern 'kVersion\s*=\s*"([^"]+)"' | Select-Object -First 1
    if (-not $match) { throw "kVersion bulunamadi: $configPath" }
    $Version = $match.Matches[0].Groups[1].Value
} else {
    $content = Get-Content $configPath -Raw
    $content = [regex]::Replace($content, '(kVersion\s*=\s*")[^"]+(")', ('${1}' + $Version + '${2}'))
    [IO.File]::WriteAllText($configPath, $content, (New-Object Text.UTF8Encoding($false)))
}

Write-Host "VcDex Loader paketleniyor - surum $Version" -ForegroundColor Cyan

function Get-Sha256([string]$Path) {
    $sha = [System.Security.Cryptography.SHA256]::Create()
    $stream = [System.IO.File]::OpenRead($Path)
    try {
        $bytes = $sha.ComputeHash($stream)
    } finally {
        $stream.Dispose()
        $sha.Dispose()
    }
    return ([BitConverter]::ToString($bytes) -replace '-', '').ToLowerInvariant()
}

# ---------------------------------------------------------------- build
if (-not $SkipBuild) {
    cmake --build (Join-Path $root "build") --config Release | Out-Host
    if ($LASTEXITCODE -ne 0) { throw "Derleme basarisiz." }
}

New-Item -ItemType Directory -Force -Path $dist | Out-Null

$exe = Join-Path $dist "VcDexLoader.exe"
if (-not (Test-Path $exe)) { throw "Derlenmis exe bulunamadi: $exe" }

# ---------------------------------------------------------------- dist dosyalari
$games = @(
    @{
        id       = "redmatch2"
        name     = "Redmatch 2"
        subtitle = "Unity Engine  -  FPS"
        status   = "Undetected"
        tag      = "Bypass Injector"
        process  = "Redmatch 2.exe"
        appid    = "1280770"
        installdir = "Redmatch 2"
        exe_rel  = "Redmatch 2.exe"
        dll      = "redmatch2/LaynexRm2.dll"
        injector = "redmatch2/LaynexRm2Injector.exe"
        accent   = 0
        files    = @("redmatch2/LaynexRm2.dll", "redmatch2/LaynexRm2Injector.exe")
    },
    @{
        id       = "onearmedrobber"
        name     = "One Armed Robber"
        subtitle = "Unreal Engine 4  -  FPS"
        status   = "Undetected"
        tag      = "Internal DLL"
        process  = "OAR-Win64-Shipping.exe"
        appid    = "2551020"
        installdir = "One-armed robber"
        exe_rel  = "OAR\Binaries\Win64\OAR-Win64-Shipping.exe"
        dll      = "onearmedrobber/one_armed_robber_int.dll"
        injector = ""
        accent   = 1
        files    = @("onearmedrobber/one_armed_robber_int.dll")
    }
)

$manifestGames = @()

foreach ($game in $games) {
    $fileEntries = @()

    foreach ($relative in $game.files) {
        $source = Join-Path $data ($relative -replace '/', '\')
        if (-not (Test-Path $source)) { throw "Kaynak dosya yok: $source" }

        $target = Join-Path $dist ($relative -replace '/', '\')
        New-Item -ItemType Directory -Force -Path (Split-Path -Parent $target) | Out-Null
        Copy-Item $source $target -Force

        $hash = (Get-Sha256 $target)
        $fileEntries += @{
            path   = $relative
            url    = "dist/$relative"
            sha256 = $hash
        }
        Write-Host ("  + dist/{0}  [{1}]" -f $relative, $hash.Substring(0, 12))
    }

    $manifestGames += @{
        id         = $game.id
        name       = $game.name
        subtitle   = $game.subtitle
        status     = $game.status
        tag        = $game.tag
        process    = $game.process
        appid      = $game.appid
        installdir = $game.installdir
        exe_rel    = $game.exe_rel
        dll        = $game.dll
        injector   = $game.injector
        accent     = $game.accent
        files      = $fileEntries
    }
}

# ---------------------------------------------------------------- manifest
$loaderHash = (Get-Sha256 $exe)

$manifest = [ordered]@{
    loader_version = $Version
    loader_url     = "dist/VcDexLoader.exe"
    loader_sha256  = $loaderHash
    games          = $manifestGames
}

$json = $manifest | ConvertTo-Json -Depth 12
$manifestPath = Join-Path $root "manifest.json"
[IO.File]::WriteAllText($manifestPath, $json, (New-Object Text.UTF8Encoding($false)))

# ---------------------------------------------------------------- yerel kopya
Copy-Item $exe (Join-Path $root "VcDexLoader.exe") -Force

Write-Host ""
Write-Host "Tamamlandi." -ForegroundColor Green
Write-Host ("  manifest.json      : {0}" -f $manifestPath)
Write-Host ("  loader sha256      : {0}" -f $loaderHash)
Write-Host ("  yerel calistirilabilir: {0}" -f (Join-Path $root "VcDexLoader.exe"))
Write-Host ""
Write-Host "Yayinlamak icin:  git add -A ; git commit -m \"release $Version\" ; git push" -ForegroundColor Yellow

