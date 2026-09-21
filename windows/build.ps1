# Requires Visual Studio 2022 C++ Build Tools and a Windows 10/11 SDK.
# Examples: .\windows\build.ps1 -Test
#           .\windows\build.ps1 -Architecture ARM64 -Target portable-tests -Test
#           .\windows\build.ps1 -Target renderer-probe
[CmdletBinding()]
param(
    [ValidateSet('Auto', 'ARM64', 'x64')][string]$Architecture = 'Auto',
    [ValidateSet('Debug', 'Release', 'RelWithDebInfo')][string]$Configuration = 'Release',
    [string]$BuildDirectory = '',
    [ValidateRange(1, 128)][int]$Jobs = 4,
    [string[]]$Target = @(),
    [switch]$Test,
    [switch]$ConfigureOnly,
    [switch]$Fresh,
    [switch]$NoApp
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot

if ($Architecture -eq 'Auto') {
    # Environment/process architecture can report AMD64 even on native ARM64.
    if (-not ('ScreamSeq.NativeBuildArchitecture' -as [type])) {
        Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
namespace ScreamSeq {
    public static class NativeBuildArchitecture {
        [DllImport("kernel32.dll", SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        public static extern bool IsWow64Process2(IntPtr process, out ushort processMachine, out ushort nativeMachine);
        [DllImport("kernel32.dll")]
        public static extern IntPtr GetCurrentProcess();
    }
}
'@
    }
    [UInt16]$processMachine = 0
    [UInt16]$nativeMachine = 0
    if (-not [ScreamSeq.NativeBuildArchitecture]::IsWow64Process2(
        [ScreamSeq.NativeBuildArchitecture]::GetCurrentProcess(), [ref]$processMachine, [ref]$nativeMachine)) {
        throw 'Cannot determine native Windows architecture; pass -Architecture ARM64 or x64.'
    }
    switch ($nativeMachine) {
        0xAA64 { $Architecture = 'ARM64' }
        0x8664 { $Architecture = 'x64' }
        default { throw "Unsupported native Windows architecture: $nativeMachine" }
    }
}
if (-not $BuildDirectory) {
    $BuildDirectory = Join-Path $root "bin/windows-$($Architecture.ToLowerInvariant())"
}
$BuildDirectory = [IO.Path]::GetFullPath($BuildDirectory)

$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
if (-not (Test-Path $vswhere)) {
    throw 'Visual Studio Installer not found. Install VS 2022 Build Tools with C++ tools, CMake and a Windows SDK.'
}
$component = if ($Architecture -eq 'ARM64') {
    'Microsoft.VisualStudio.Component.VC.Tools.ARM64'
} else { 'Microsoft.VisualStudio.Component.VC.Tools.x86.x64' }
$installations = @(& $vswhere -latest -products '*' -version '[17.0,18.0)' -requires $component -property installationPath)
if ($LASTEXITCODE -ne 0 -or $installations.Count -eq 0 -or -not $installations[0]) {
    throw "No completed Visual Studio 2022 installation with $component. If installation is running, retry after it finishes."
}
$vsPath = $installations[0]
$cmake = Join-Path $vsPath 'Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe'
if (-not (Test-Path $cmake)) {
    $command = Get-Command cmake.exe -ErrorAction SilentlyContinue
    if (-not $command) { throw 'CMake 3.24+ not found; install the Visual Studio C++ CMake component.' }
    $cmake = $command.Source
}
$ctest = Join-Path (Split-Path -Parent $cmake) 'ctest.exe'
if ($Test -and -not (Test-Path $ctest)) { throw "CTest not found next to $cmake" }
Write-Host "ScreamSeq: $Architecture / $Configuration -> $BuildDirectory"
$configureArguments = @('-S', $PSScriptRoot, '-B', $BuildDirectory, '-G', 'Visual Studio 17 2022',
    '-A', $Architecture, "-DCMAKE_GENERATOR_INSTANCE=$vsPath", '-DBUILD_TESTING=ON')
$configureArguments += if ($NoApp) { '-DSCREAMSEQ_BUILD_APP=OFF' } else { '-DSCREAMSEQ_BUILD_APP=ON' }
if ($Fresh) { $configureArguments += '--fresh' }
& $cmake @configureArguments
if ($LASTEXITCODE -ne 0) { throw "CMake configure failed ($LASTEXITCODE)." }
# A failed compiler-detection attempt can leave every flag initialized to an
# empty string. A later successful configure otherwise builds without Release
# optimization or C++ exception unwinding. Do not qualify that accidental cache.
$cache = Get-Content -LiteralPath (Join-Path $BuildDirectory 'CMakeCache.txt')
if (($cache -contains 'CMAKE_CXX_FLAGS:STRING=') -and
    ($cache -contains 'CMAKE_CXX_FLAGS_RELEASE:STRING=') -and
    ($cache -contains 'CMAKE_CXX_FLAGS_DEBUG:STRING=')) {
    throw 'Compiler flags are uninitialized, possibly after failed compiler detection. Re-run with -Fresh to restore CMake toolchain defaults.'
}
if ($ConfigureOnly) { return }
$buildArguments = @('--build', $BuildDirectory, '--config', $Configuration, '--parallel', "$Jobs")
# A test run must build every registered portable test, even with a narrow target.
$targets = @($Target)
if ($Test -and $targets.Count -gt 0 -and $targets -notcontains 'portable-tests') { $targets += 'portable-tests' }
if ($targets.Count -gt 0) { $buildArguments += '--target'; $buildArguments += $targets }
& $cmake @buildArguments
if ($LASTEXITCODE -ne 0) { throw "CMake build failed ($LASTEXITCODE)." }
if ($Test) {
    & $ctest --test-dir $BuildDirectory -C $Configuration --output-on-failure --no-tests=error -L portable
    if ($LASTEXITCODE -ne 0) { throw "Portable tests failed ($LASTEXITCODE)." }
    Write-Host 'Portable functional tests passed. Realtime allocation/lock auditing and sanitizers were not run.'
}
