# Run a command inside the MSVC (x64) developer environment.
# Ninja + MSVC requires VS environment variables (cl / INCLUDE / LIB).
#
# Usage (from repository root):
#   .\tools\dev.ps1 cmake --preset x64-debug
#   .\tools\dev.ps1 cmake --build --preset x64-debug
#   .\tools\dev.ps1 cmake --build --preset x64-debug --target ZomboidLike
param(
    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]]$Command
)

$ErrorActionPreference = "Stop"

$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path $vswhere)) {
    throw "vswhere.exe not found. Please install Visual Studio 2022 with the C++ desktop workload."
}
$vsPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vsPath) {
    throw "No Visual Studio 2022 installation with the C++ toolset was found."
}

Import-Module "$vsPath\Common7\Tools\Microsoft.VisualStudio.DevShell.dll"
Enter-VsDevShell -VsInstallPath $vsPath -SkipAutomaticLocation -DevCmdArguments "-arch=x64 -host_arch=x64" | Out-Null

if (-not $env:VCPKG_ROOT) { $env:VCPKG_ROOT = "E:\Dev\vcpkg" }
if (-not (Test-Path "$env:VCPKG_ROOT\scripts\buildsystems\vcpkg.cmake")) {
    throw "VCPKG_ROOT is invalid ($env:VCPKG_ROOT). See README setup instructions."
}

if (-not $Command -or $Command.Count -eq 0) {
    Write-Host "MSVC x64 developer environment ready (cl / link / ninja / cmake)."
    Write-Host "Example: .\tools\dev.ps1 cmake --preset x64-debug"
    return
}

if ($Command.Count -eq 1) {
    & $Command[0]
} else {
    & $Command[0] $Command[1..($Command.Count - 1)]
}
exit $LASTEXITCODE
