param([Parameter(Mandatory=$true)][string]$Version)
$ErrorActionPreference='Stop'
if ($Version -notmatch '^\d+\.\d+\.\d+$') { throw 'Expected semantic version' }
Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem
$repo=Split-Path $PSScriptRoot -Parent
$output=Join-Path $repo ".pio/inkdeck-source-v$Version.zip"
$stream=[IO.File]::Open($output,[IO.FileMode]::Create)
$archive=New-Object IO.Compression.ZipArchive($stream,[IO.Compression.ZipArchiveMode]::Create)
function Add-Source([string]$source,[string]$entry) {
    if (!(Test-Path -LiteralPath $source -PathType Leaf)) { throw "Missing source: $entry" }
    [IO.Compression.ZipFileExtensions]::CreateEntryFromFile($archive,$source,"InkDeck/$entry") | Out-Null
}
try {
    # Only explicitly tracked project files; never include private .pio checkouts,
    # device data, local logs, credentials, games, or generated firmware images.
    foreach($file in (git -C $repo ls-files)) {
        if($file -match '(^docs/firmware/|\.(bin|zip|elf)$)') { continue }
        Add-Source (Join-Path $repo $file) $file
    }
    foreach($target in @('seeed_xiao_esp32s3','seeed_reterminal_sticky','lilygo_t5s3_pro')) {
        $directory=Join-Path $repo ".pio/libdeps/$target"
        foreach($file in (Get-ChildItem -LiteralPath $directory -Recurse -File)) {
            $relative=$file.FullName.Substring($directory.Length+1).Replace('\','/')
            if($relative -match '(^|/)(\.git|__pycache__|\.pio)(/|$)|\.(pyc|bin|elf|o|a)$') { continue }
            Add-Source $file.FullName ".pio/libdeps/$target/$relative"
        }
    }
    $framework=Join-Path $env:USERPROFILE '.platformio/packages/framework-arduinoespressif32'
    foreach($part in @('cores','libraries','variants')) {
        foreach($file in (Get-ChildItem -LiteralPath (Join-Path $framework $part) -Recurse -File)) {
            $relative=$file.FullName.Substring($framework.Length+1).Replace('\','/')
            if($relative -match '(^|/)(\.git|__pycache__)(/|$)|\.(pyc|bin|elf|o|a)$') { continue }
            Add-Source $file.FullName "framework-arduinoespressif32/$relative"
        }
    }
    foreach($name in @('package.json','LICENSE.md','LICENSE','NOTICE')) {
        $file=Join-Path $framework $name
        if(Test-Path -LiteralPath $file -PathType Leaf) { Add-Source $file "framework-arduinoespressif32/$name" }
    }
} finally { $archive.Dispose();$stream.Dispose() }
Write-Output "Packaged corresponding source: $output"
