param([Parameter(Mandatory=$true)][string]$Version,
      [ValidateSet('all','book32','book32-sticky','inkdeck-lilygo')][string]$Target = 'all')
$ErrorActionPreference = 'Stop'
if ($Version -notmatch '^\d+\.\d+\.\d+$') { throw 'Expected a semantic version, such as 1.2.14' }
$repo = Split-Path $PSScriptRoot -Parent
$python = Join-Path $env:USERPROFILE '.platformio/penv/Scripts/python.exe'
$esptool = Join-Path $env:USERPROFILE '.platformio/packages/tool-esptoolpy/esptool.py'
$bootApp = Join-Path $env:USERPROFILE '.platformio/packages/framework-arduinoespressif32/tools/partitions/boot_app0.bin'
$docs = Join-Path $repo 'docs'
$firmwareDir = Join-Path $docs 'firmware'
$profiles = @(
    @{ Env='seeed_xiao_esp32s3'; Prefix='book32'; App='firmware'; FS='littlefs'; Manifest='manifest'; Name='InkDeck for Book32'; Offset=5308416; Flash='16MB' },
    @{ Env='seeed_reterminal_sticky'; Prefix='book32-sticky'; App='book32-sticky-firmware'; FS='book32-sticky-littlefs'; Manifest='manifest-sticky'; Name='InkDeck for Seeed Studio Sticky'; Offset=8454144; Flash='32MB' },
    @{ Env='lilygo_t5s3_pro'; Prefix='inkdeck-lilygo'; App='inkdeck-lilygo-firmware'; FS='inkdeck-lilygo-littlefs'; Manifest='manifest-lilygo'; Name='InkDeck for LILYGO T5 S3 Pro H752-01 (experimental)'; Offset=8454144; Flash='16MB' }
)
foreach ($profile in $profiles) {
    if ($Target -ne 'all' -and $Target -ne $profile.Prefix) { continue }
    $build = Join-Path $repo ".pio/build/$($profile.Env)"
    $app = "$($profile.App)-v$Version.bin"
    $fs = "$($profile.FS)-v$Version.bin"
    $boot = "$($profile.Prefix)-bootloader-v$Version.bin"
    $partitions = "$($profile.Prefix)-partitions-v$Version.bin"
    $bootSelect = "$($profile.Prefix)-boot-app-v$Version.bin"
    $factory = "$($profile.Prefix)-factory-v$Version.bin"
    Copy-Item "$build/firmware.bin" "$firmwareDir/$app"
    Copy-Item "$build/littlefs.bin" "$firmwareDir/$fs"
    Copy-Item "$build/bootloader.bin" "$firmwareDir/$boot"
    Copy-Item "$build/partitions.bin" "$firmwareDir/$partitions"
    Copy-Item $bootApp "$firmwareDir/$bootSelect"
    & $python $esptool --chip esp32s3 merge_bin -o "$firmwareDir/$factory" --flash_mode dio --flash_size $profile.Flash 0x0 "$build/bootloader.bin" 0x8000 "$build/partitions.bin" 0xe000 $bootApp 0x10000 "$build/firmware.bin"
    if ($LASTEXITCODE -ne 0) { throw "Factory merge failed for $($profile.Env)" }
    Copy-Item "$firmwareDir/$app" "$firmwareDir/$($profile.App).bin"
    Copy-Item "$firmwareDir/$app" "$repo/$($profile.App).bin"
    Copy-Item "$firmwareDir/$fs" "$firmwareDir/$($profile.FS).bin"
    Copy-Item "$firmwareDir/$fs" "$repo/$($profile.FS).bin"
    Copy-Item "$firmwareDir/$factory" "$firmwareDir/$($profile.Prefix)-factory.bin"
    foreach ($kind in @('update','factory')) {
        $parts = if ($kind -eq 'factory') {
            @(@{path="firmware/$factory";offset=0}, @{path="firmware/$fs";offset=$profile.Offset})
        } else {
            @(@{path="firmware/$boot";offset=0}, @{path="firmware/$partitions";offset=32768},
              @{path="firmware/$bootSelect";offset=57344}, @{path="firmware/$app";offset=65536},
              @{path="firmware/$fs";offset=$profile.Offset})
        }
        foreach ($part in $parts) {
            if (!(Test-Path (Join-Path $docs $part.path))) { throw "Missing asset: $($part.path)" }
        }
        $manifest = [ordered]@{
            name=$profile.Name; version=$Version; new_install_prompt_erase=$true; new_install_improv_wait_time=0
            builds=@(@{chipFamily='ESP32-S3';improv=$false;parts=$parts})
        } | ConvertTo-Json -Depth 8
        [IO.File]::WriteAllText("$docs/$($profile.Manifest)-$kind-v$Version.json", $manifest)
        [IO.File]::WriteAllText("$docs/$($profile.Manifest)-$kind.json", $manifest)
    }
}
Write-Output "Packaged InkDeck $Version ($Target)."
