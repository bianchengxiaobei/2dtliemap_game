# Compile bgfx shaders (.sc) into embedded C headers (bin2c).
# Generated headers are committed so the project builds without shaderc.
# Usage: powershell -NoProfile -ExecutionPolicy Bypass -File tools\build_shaders.ps1
param(
    [string]$Shaderc = "",
    [string]$OutDir = ""
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot

if (-not $Shaderc) {
    $candidates = @(
        (Join-Path $root "build\x64-debug\vcpkg_installed\x64-windows\tools\bgfx\shaderc.exe"),
        (Join-Path $root "build\x64-release\vcpkg_installed\x64-windows\tools\bgfx\shaderc.exe")
    )
    foreach ($c in $candidates) {
        if (Test-Path $c) { $Shaderc = $c; break }
    }
}
if (-not $Shaderc -or -not (Test-Path $Shaderc)) {
    Write-Host "ERROR: shaderc.exe not found. Run 'cmake --preset x64-debug' first (installs bgfx[tools])."
    exit 1
}

# include dir for bgfx_shader.sh - prefer x64-debug, then x64-release
$incCandidates = @(
    (Join-Path $root "build\x64-debug\vcpkg_installed\x64-windows\include"),
    (Join-Path $root "build\x64-release\vcpkg_installed\x64-windows\include")
)
$inc = $null
foreach ($c in $incCandidates) {
    if (Test-Path (Join-Path $c "bgfx\bgfx_shader.sh")) { $inc = (Join-Path $c "bgfx"); break }
}
if (-not $inc) {
    Write-Host "ERROR: bgfx_shader.sh not found under vcpkg_installed include dir."
    exit 1
}

if (-not $OutDir) { $OutDir = Join-Path $root "src\render\shaders_generated" }
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null

$shaders = @(
    @{ name = "vs_sprite"; type = "vertex" },
    @{ name = "fs_sprite"; type = "fragment" }
)
$varyingDef = Join-Path $root "shaders\varying.def.sc"
foreach ($s in $shaders) {
    $name = $s.name
    $src = Join-Path $root "shaders\$name.sc"
    # Direct3D 11 (shader model 5.0)
    $out = Join-Path $OutDir "$name.bin.h"
    & $Shaderc -f $src -o $out --bin2c "_${name}_dx11" --type $s.type --varyingdef $varyingDef --platform windows -p s_5_0 -i $inc -O 3
    if ($LASTEXITCODE -ne 0) {
        Write-Host "ERROR: shaderc failed for $name (dx11)"
        exit 1
    }
    # OpenGL 2.1+ (GLSL 120) fallback
    $outGlsl = Join-Path $OutDir "$name.glsl.bin.h"
    & $Shaderc -f $src -o $outGlsl --bin2c "_${name}_glsl" --type $s.type --varyingdef $varyingDef --platform linux -p 120 -i $inc -O 3
    if ($LASTEXITCODE -ne 0) {
        Write-Host "ERROR: shaderc failed for $name (glsl)"
        exit 1
    }
    Write-Host "compiled $name -> $out, $outGlsl"
}
Write-Host "done."
