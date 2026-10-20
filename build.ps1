$ErrorActionPreference = 'Stop'
$vswherePath = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vsPath = & $vswherePath -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (!$vsPath) { throw 'Visual C++ build tools not found' }
$vcvars = Join-Path $vsPath 'VC\Auxiliary\Build\vcvars64.bat'
$buildEnvironment = & $env:ComSpec /d /c "`"$vcvars`" >nul && set"
if ($LASTEXITCODE -ne 0) { throw 'Compiler environment initialization failed' }
foreach ($line in $buildEnvironment) {
    if ($line -match '^([^=]+)=(.*)$') { [Environment]::SetEnvironmentVariable($matches[1], $matches[2], 'Process') }
}
$env:VSLANG = '1033'
cmake -S $PSScriptRoot -B (Join-Path $PSScriptRoot 'build') -G Ninja -DCMAKE_BUILD_TYPE=Release
if ($LASTEXITCODE -ne 0) { throw 'CMake configuration failed' }
# The localized MSVC /showIncludes prefix is decoded inconsistently by this
# bundled Ninja/CMake combination. Never retain objects with stale header ABIs.
cmake --build (Join-Path $PSScriptRoot 'build') --clean-first
if ($LASTEXITCODE -ne 0) { throw 'Native compilation failed' }
