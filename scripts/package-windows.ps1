# Build a Windows zip that contains WaveformStudio.exe.
# Run from a Visual Studio 2022 developer shell, or any shell where cmake finds MSVC.
$ErrorActionPreference = "Stop"
Set-Location (Join-Path $PSScriptRoot "..")
cmake -S . -B build-package -G "Visual Studio 17 2022" -A x64 -DWAVEFORM_ENABLE_ASIO=OFF
cmake --build build-package --config Release
cmake --install build-package --config Release --prefix build-package/install
cpack --config build-package/CPackConfig.cmake -C Release -B build-package/package
Write-Host "Zip: build-package/package/"
