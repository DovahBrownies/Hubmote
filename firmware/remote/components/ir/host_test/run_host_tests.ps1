# run_host_tests.ps1 -- build and run the ir component's host tests (the
# pure logic in ir_codec.c) with a native gcc. Not part of the ESP-IDF
# build and never touches idf.py.
#
# Usage (from anywhere):
#   .\components\ir\host_test\run_host_tests.ps1
#   .\components\ir\host_test\run_host_tests.ps1 -Gcc C:\path\to\gcc.exe
#
# MSYS2's gcc is preferred and its bin directory is put first on PATH for
# this process: Git for Windows ships older libgmp/libzstd DLLs, and if
# those load first MSYS2's cc1.exe exits with status 1 and no message.

param([string]$Gcc = "")

$ErrorActionPreference = "Stop"

$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$comp = Split-Path -Parent $here
$out  = Join-Path $env:TEMP "ir_host_test"
New-Item -ItemType Directory -Force $out | Out-Null

if (-not $Gcc) {
    $msys = "C:\msys64\mingw64\bin\gcc.exe"
    if (Test-Path $msys) {
        $Gcc = $msys
    } else {
        $cmd = Get-Command gcc -ErrorAction SilentlyContinue
        if (-not $cmd) {
            Write-Error "No gcc found. Install MSYS2 (mingw64 gcc) or pass -Gcc."
        }
        $Gcc = $cmd.Source
    }
}
$env:PATH = (Split-Path -Parent $Gcc) + ";" + $env:PATH
$env:TMP  = $out
$env:TEMP = $out

$exe = Join-Path $out "ir_codec_test.exe"
$gccArgs = @(
    "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror", "-Wshadow", "-Wformat=2",
    "-Wmissing-prototypes", "-Wstrict-prototypes", "-Wconversion", "-Wno-sign-conversion",
    "-fsanitize=undefined", "-fsanitize-undefined-trap-on-error",
    "-I", (Join-Path $here "stub"), "-I", (Join-Path $comp "include"), "-I", $comp,
    (Join-Path $here "test_ir_codec.c"), (Join-Path $comp "ir_codec.c"),
    "-o", $exe
)
& $Gcc @gccArgs
if ($LASTEXITCODE -ne 0) {
    Write-Error "gcc failed with exit code $LASTEXITCODE"
}

& $exe
exit $LASTEXITCODE
