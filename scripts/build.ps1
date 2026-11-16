# Build Pitot from any PowerShell window: enters the MSVC dev environment if needed,
# then runs the CMake presets. Kept ASCII-only because Windows PowerShell 5.1 reads
# BOM-less files in the ANSI code page.
#
#   scripts\build.ps1                    # Debug
#   scripts\build.ps1 -Config release
#   scripts\build.ps1 -Config all -Test  # Debug + Release, run tests
#   scripts\build.ps1 -Config release -Target PitotAdlx  # one target, leaves a running Pitot.exe alone

param(
    [ValidateSet('debug', 'release', 'all')]
    [string]$Config = 'debug',
    [string]$Target,
    [switch]$Test
)

$ErrorActionPreference = 'Stop'

# Pick up tools installed after this shell was opened.
$env:Path = [Environment]::GetEnvironmentVariable('Path', 'Machine') + ';' +
            [Environment]::GetEnvironmentVariable('Path', 'User')

if (-not (Get-Command cl.exe -ErrorAction SilentlyContinue)) {
    $installer = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer'
    $vs = & (Join-Path $installer 'vswhere.exe') -latest -products * `
        -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if (-not $vs) { throw 'No Visual Studio with C++ tools found. See docs/environment.md.' }
    # VsDevCmd calls vswhere by name; without this it prints a harmless "not recognized" error.
    $env:Path = "$installer;$env:Path"
    Import-Module (Join-Path $vs 'Common7\Tools\Microsoft.VisualStudio.DevShell.dll')
    Enter-VsDevShell -VsInstallPath $vs -SkipAutomaticLocation -DevCmdArguments '-arch=x64 -host_arch=x64' | Out-Null
}

$root = Split-Path $PSScriptRoot -Parent
$configs = if ($Config -eq 'all') { @('debug', 'release') } else { @($Config) }

Push-Location $root
try {
    foreach ($c in $configs) {
        $preset = "x64-$c"
        cmake --preset $preset
        if ($LASTEXITCODE) { throw "Configure failed: $preset" }
        $buildArgs = @('--build', '--preset', $preset)
        if ($Target) { $buildArgs += @('--target', $Target) }
        cmake @buildArgs
        if ($LASTEXITCODE) { throw "Build failed: $preset" }
        if ($Test) {
            ctest --preset $preset
            if ($LASTEXITCODE) { throw "Tests failed: $preset" }
        }
    }
}
finally {
    Pop-Location
}
