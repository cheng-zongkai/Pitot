# Packages a Release build into the files attached to a GitHub release, in dist\:
#   Pitot-<version>-setup.exe        Windows installer, compiled from installer\Pitot.iss
#   Pitot-<version>-win-x64.zip      portable copy: the same files the installer installs
#   SHA256SUMS.txt                   sha256sum-compatible hashes of the two files above
# Both hold Pitot.exe, PitotNvidia.dll and the license files. With -AdlxDll they also hold
# PitotAdlx.dll and its end-user agreement PitotAdlx-EULA.txt (D-033). The release workflow never
# passes -AdlxDll; scripts\repack-adlx.ps1 passes the DLL built on the maintainer's machine.
# Kept ASCII-only because Windows PowerShell 5.1 reads BOM-less files in the ANSI code page.
#
#   scripts\get-innosetup.ps1                (once)
#   scripts\build.ps1 -Config release
#   scripts\package.ps1 -Version 0.1.0-dev [-AdlxDll build\x64-release\PitotAdlx.dll]

param(
    [Parameter(Mandatory)]
    [ValidatePattern('^\d+\.\d+\.\d+(-[0-9A-Za-z.]+)?$')]
    [string]$Version,
    # Folder holding Pitot.exe and PitotNvidia.dll
    [string]$BinDir = 'build\x64-release',
    [string]$AdlxDll,
    [string]$OutDir = 'dist'
)

$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
function Resolve-RepoPath([string]$p) { if ([IO.Path]::IsPathRooted($p)) { $p } else { Join-Path $root $p } }
$bin = Resolve-RepoPath $BinDir
$out = Resolve-RepoPath $OutDir

# The numeric part of the tag must match what the binary reports (PITOT_VERSION comes from project()).
$cmake = Get-Content -LiteralPath (Join-Path $root 'CMakeLists.txt') -Raw
if ($cmake -notmatch 'project\(Pitot VERSION (\d+\.\d+\.\d+)') { throw 'project(Pitot VERSION ...) not found in CMakeLists.txt' }
$base = ($Version -split '-')[0]
if ($Matches[1] -ne $base) {
    throw "Version $Version does not match project VERSION $($Matches[1]) in CMakeLists.txt"
}

$iscc = @(
    (Join-Path $env:LOCALAPPDATA 'Programs\Inno Setup 6\ISCC.exe'),
    (Join-Path ${env:ProgramFiles(x86)} 'Inno Setup 6\ISCC.exe')
) | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
if (-not $iscc -or -not (Test-Path -LiteralPath (Join-Path (Split-Path $iscc) 'Languages\ChineseSimplified.isl'))) {
    throw 'Inno Setup or its Chinese messages file is missing. Run scripts\get-innosetup.ps1 first.'
}

# To is the path inside the zip and below the install folder.
$files = @(
    @{ From = (Join-Path $bin 'Pitot.exe');          To = 'Pitot.exe' },
    @{ From = (Join-Path $bin 'PitotNvidia.dll');    To = 'PitotNvidia.dll' },
    @{ From = (Join-Path $root 'LICENSE');           To = 'LICENSE' },
    @{ From = (Join-Path $root 'THIRD_PARTY_NOTICES.md'); To = 'THIRD_PARTY_NOTICES.md' },
    # Same relative path as the link in THIRD_PARTY_NOTICES.md
    @{ From = (Join-Path $root 'third_party\pawnio\COPYING'); To = 'third_party/pawnio/COPYING' }
)
if ($AdlxDll) {
    # The AMD license requires the agreement to travel with the DLL.
    $files += @{ From = (Resolve-RepoPath $AdlxDll); To = 'PitotAdlx.dll' }
    $files += @{ From = (Join-Path $root 'PitotAdlx-EULA.txt'); To = 'PitotAdlx-EULA.txt' }
}
foreach ($f in $files) {
    if (-not (Test-Path -LiteralPath $f.From)) { throw "Missing $($f.From). Run scripts\build.ps1 -Config release first." }
}

if (Test-Path -LiteralPath $out) { Remove-Item -LiteralPath $out -Recurse -Force }
New-Item -ItemType Directory -Force -Path $out | Out-Null
$stage = Join-Path ([IO.Path]::GetTempPath()) ('pitot-stage-' + [Guid]::NewGuid().ToString('N'))
try {
    foreach ($f in $files) {
        $dest = Join-Path $stage ($f.To.Replace('/', '\'))
        New-Item -ItemType Directory -Force -Path (Split-Path $dest) | Out-Null
        Copy-Item -LiteralPath $f.From -Destination $dest
    }

    # Entries are added one by one with "/" separators: CreateFromDirectory and Compress-Archive
    # write backslashes on Windows PowerShell 5.1, which breaks unzip on other platforms.
    Add-Type -AssemblyName System.IO.Compression, System.IO.Compression.FileSystem
    $zipName = "Pitot-$Version-win-x64.zip"
    $zip = [IO.Compression.ZipFile]::Open((Join-Path $out $zipName), [IO.Compression.ZipArchiveMode]::Create)
    try {
        foreach ($f in $files) {
            [void][IO.Compression.ZipFileExtensions]::CreateEntryFromFile($zip, $f.From, $f.To, [IO.Compression.CompressionLevel]::Optimal)
        }
    }
    finally {
        $zip.Dispose()
    }

    $setupName = "Pitot-$Version-setup"
    & $iscc /Q "/DAppVersion=$Version" "/DVersionNumber=$base" "/DStageDir=$stage" "/DOutputDir=$out" `
        "/DOutputName=$setupName" (Join-Path $root 'installer\Pitot.iss')
    if ($LASTEXITCODE -ne 0) { throw "ISCC failed: $LASTEXITCODE" }
}
finally {
    Remove-Item -LiteralPath $stage -Recurse -Force -ErrorAction SilentlyContinue
}

$lines = foreach ($name in "$setupName.exe", $zipName) {
    $hash = (Get-FileHash -LiteralPath (Join-Path $out $name) -Algorithm SHA256).Hash.ToLowerInvariant()
    "$hash  $name"
}
[IO.File]::WriteAllText((Join-Path $out 'SHA256SUMS.txt'), (($lines -join "`n") + "`n"), (New-Object Text.UTF8Encoding($false)))

Get-ChildItem -LiteralPath $out -File | Select-Object Name, Length
