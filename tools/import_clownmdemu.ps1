param([Parameter(Mandatory=$true)][string]$Source)
$ErrorActionPreference = 'Stop'
$sourceRoot = (Resolve-Path -LiteralPath $Source).Path
$repo = Split-Path $PSScriptRoot -Parent
$destination = Join-Path $repo 'lib/InkGenesis/clownmdemu'
$revisions = @{
    '.'='88ef45a6585556e2247dd26b6c937d6b9fb4a12d'
    'libraries/clown68000'='bb198386ae3ee0cd6d246ff917f9941b02b5be6a'
    'libraries/clownz80'='aa4cae3127a5893ba7066bcc2d9e9ba8e88e1c3b'
    'libraries/clowncommon'='ecee31fc78ee1eed901431d070239c9c6430607c'
    'libraries/clown68000/libraries/clowncommon'='ddeff174c2121b40284883e0e1e3520f39d99227'
    'libraries/clownz80/libraries/clowncommon'='ddeff174c2121b40284883e0e1e3520f39d99227'
}
foreach ($sub in $revisions.Keys) {
    $directory = Join-Path $sourceRoot $sub
    $sha = git -C $directory rev-parse HEAD
    if ($sha -ne $revisions[$sub]) { throw "Unexpected revision for $sub" }
    foreach ($file in (git -C $directory ls-files)) {
        if ($file -notmatch '\.(c|h|md|txt)$') { continue }
        $inputFile = Join-Path $directory $file
        if (!(Test-Path -LiteralPath $inputFile -PathType Leaf)) { continue }
        $outputFile = Join-Path (Join-Path $destination $sub) $file
        New-Item -ItemType Directory -Force -Path (Split-Path $outputFile -Parent) | Out-Null
        Copy-Item -LiteralPath $inputFile -Destination $outputFile
    }
}
Push-Location $repo
try {
    git apply --directory=lib/InkGenesis/clownmdemu tools/clownmdemu-vdp-memory.patch
    if ($LASTEXITCODE -ne 0) { throw 'Could not apply the InkDeck VDP allocation patch' }
} finally { Pop-Location }
Write-Output 'Imported pinned ClownMDEmu / Clown68000 / ClownZ80 sources and notices.'
