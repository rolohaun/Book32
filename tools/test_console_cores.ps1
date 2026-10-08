param([string]$Optimization = '-O0', [switch]$Wasi)
$ErrorActionPreference = 'Stop'
$consoleSources = @(Get-ChildItem lib/InkNes -Recurse -Filter '*.c' | ForEach-Object FullName)
$testSources = @(Get-ChildItem test/console_cores -Filter '*.c' | ForEach-Object FullName)
$testTarget = 'x86_64-windows-gnu'
$testOutput = '.pio/test_console_cores.exe'
$extraFlags = @()
if ($Wasi) {
    $testTarget = 'wasm32-wasi'
    $testOutput = '.pio/test_console_cores.wasm'
    $extraFlags = @('-Itest/crankboy_host/wasm', '-Wl,-z,stack-size=2097152', '-D_WASI_EMULATED_SIGNAL', '-lwasi-emulated-signal')
}
& .pio/host-tools/ziglang/zig.exe cc -target $testTarget -std=gnu11 $Optimization -UNDEBUG @extraFlags `
    -DBOARD_LILYGO_T5S3_PRO -Itest/crankboy_host -Ilib/InkNes `
    -Wno-incompatible-pointer-types -Wno-pointer-sign @testSources `
    lib/Apps/AppPaperboy/NesCore.c `
    lib/Apps/AppPaperboy/GameCore.c lib/Apps/AppPaperboy/ConsoleCore.c `
    lib/Apps/AppPaperboy/crankboy/minigb_apu/minigb_apu.c @consoleSources -lm -o $testOutput `
    *> .pio/test-console-build.log
if ($LASTEXITCODE -ne 0) { Get-Content .pio/test-console-build.log -Tail 30; exit $LASTEXITCODE }
if ($Wasi) { node tools/run_crankboy_wasm.cjs $testOutput *> .pio/test-console-run.log }
else { & $testOutput *> .pio/test-console-run.log }
$consoleTestExit = $LASTEXITCODE
Get-Content .pio/test-console-run.log -Tail 6
exit $consoleTestExit
