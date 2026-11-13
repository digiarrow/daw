# Build a Windows zip that contains WaveformStudio.exe.
# windows-latest ships Visual Studio 2026 (CMake generator "Visual Studio 18 2026").
# Fall back to Visual Studio 2022 when that is the installed instance.
$ErrorActionPreference = "Stop"
Set-Location (Join-Path $PSScriptRoot "..")

if (Test-Path build-package) {
    Remove-Item -Recurse -Force build-package
}

$configured = $false
foreach ($generator in @("Visual Studio 18 2026", "Visual Studio 17 2022")) {
    & cmake -S . -B build-package -G $generator -A x64 -DWAVEFORM_ENABLE_ASIO=OFF
    if ($LASTEXITCODE -eq 0) {
        $configured = $true
        break
    }
    if (Test-Path build-package) {
        Remove-Item -Recurse -Force build-package
    }
}
if (-not $configured) {
    throw "CMake could not find Visual Studio 2026 or Visual Studio 2022."
}

& cmake --build build-package --config Release
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& cmake --install build-package --config Release --prefix build-package/install
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& cpack --config build-package/CPackConfig.cmake -C Release -B build-package/package
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
Write-Host "Zip: build-package/package/"
