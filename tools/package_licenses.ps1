$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem
$repo = Split-Path $PSScriptRoot -Parent
$output = Join-Path $repo '.pio/inkdeck-licenses.zip'
$sources = @(
    'THIRD_PARTY_NOTICES.md',
    'LICENSE.md',
    'COPYING',
    'lib/FastEPD/LICENSE',
    'lib/FastEPD/MSG-LICENSE',
    'lib/Apps/AppPaperboy/crankboy/LICENSE',
    'lib/Apps/AppPaperboy/crankboy/minigb_apu/LICENSE',
    'lib/InkNes/COPYING',
    'lib/InkNes/COPYING.LGPL2',
    'lib/InkNes/CREDITS',
    'lib/InkGenesis/README.md',
    'lib/InkGenesis/clownmdemu/LICENCE.txt',
    'lib/InkGenesis/clownmdemu/libraries/clown68000/LICENCE.txt',
    'lib/InkGenesis/clownmdemu/libraries/clownz80/LICENCE.txt',
    'lib/InkGenesis/clownmdemu/libraries/clowncommon/licence.txt',
    'lib/InkGenesis/clownmdemu/libraries/clown68000/libraries/clowncommon/licence.txt',
    'lib/InkGenesis/clownmdemu/libraries/clownz80/libraries/clowncommon/licence.txt'
)
foreach ($source in $sources) {
    if (!(Test-Path -LiteralPath (Join-Path $repo $source))) { throw "Missing notice: $source" }
}
$stream = [IO.File]::Open($output, [IO.FileMode]::Create)
$archive = New-Object IO.Compression.ZipArchive($stream, [IO.Compression.ZipArchiveMode]::Create)
try {
    foreach ($source in $sources) {
        [IO.Compression.ZipFileExtensions]::CreateEntryFromFile($archive, (Join-Path $repo $source), $source) | Out-Null
    }
    # Include dependency notices installed by PlatformIO, retaining their
    # library names. Source versions/URLs are recorded in platformio.ini.
    $dependencies = Join-Path $repo '.pio/libdeps/lilygo_t5s3_pro'
    foreach ($file in (Get-ChildItem -LiteralPath $dependencies -Recurse -File | Where-Object { $_.Name -match '^(LICENSE|COPYING|NOTICE)(\..*)?$' })) {
        $entry = 'dependencies/' + $file.FullName.Substring($dependencies.Length + 1).Replace('\', '/')
        [IO.Compression.ZipFileExtensions]::CreateEntryFromFile($archive, $file.FullName, $entry) | Out-Null
    }
} finally { $archive.Dispose(); $stream.Dispose() }
Write-Output "Packaged license notices: $output"
