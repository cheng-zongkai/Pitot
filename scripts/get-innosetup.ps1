# Installs Inno Setup for the current user, which scripts\package.ps1 needs to build the Windows
# installer, plus the Simplified Chinese messages file the installer uses. Versions and SHA-256
# hashes are pinned so CI and local builds use the same compiler. The windows-2025 GitHub runner
# image does not include Inno Setup, so the release workflow runs this script too. Kept ASCII-only
# because Windows PowerShell 5.1 reads BOM-less files in the ANSI code page.
#
#   scripts\get-innosetup.ps1

$ErrorActionPreference = 'Stop'
$version = '6.7.3'
$tag = 'is-' + $version.Replace('.', '_')
$setupUrl = "https://github.com/jrsoftware/issrc/releases/download/$tag/innosetup-$version.exe"
$setupSha256 = '9c73c3bae7ed48d44112a0f48e66742c00090bdb5bef71d9d3c056c66e97b732'
# Unofficial translation from the same Inno Setup source tag; the installer does not ship it.
$islUrl = "https://raw.githubusercontent.com/jrsoftware/issrc/$tag/Files/Languages/Unofficial/ChineseSimplified.isl"
$islSha256 = '7d544b9bb1d142cfa11f2e5d3cc8abe2e55f8e066c5124e3772675aa236e1278'

$dir = Join-Path $env:LOCALAPPDATA 'Programs\Inno Setup 6'
$iscc = Join-Path $dir 'ISCC.exe'
$isl = Join-Path $dir 'Languages\ChineseSimplified.isl'
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12

function Get-Verified([string]$url, [string]$sha256, [string]$path) {
    Invoke-WebRequest -Uri $url -OutFile $path -UseBasicParsing
    $actual = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($actual -ne $sha256) {
        Remove-Item -LiteralPath $path -Force
        throw "SHA-256 mismatch for $url`: $actual"
    }
}

# ISCC.exe has no version resource; the per-user uninstall entry records the installed version.
$installed = Get-ItemProperty -LiteralPath 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\Inno Setup 6_is1' -ErrorAction SilentlyContinue
if ((Test-Path -LiteralPath $iscc) -and $installed -and $installed.DisplayVersion -eq $version) {
    Write-Host "Inno Setup $version is already installed: $iscc"
} else {
    $setup = Join-Path ([IO.Path]::GetTempPath()) "innosetup-$version.exe"
    try {
        Get-Verified $setupUrl $setupSha256 $setup
        $p = Start-Process -FilePath $setup -Wait -PassThru -ArgumentList @(
            '/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART', '/SP-', '/CURRENTUSER', "/DIR=`"$dir`"")
        if ($p.ExitCode -ne 0) { throw "Inno Setup installer exited with $($p.ExitCode)" }
    }
    finally {
        Remove-Item -LiteralPath $setup -Force -ErrorAction SilentlyContinue
    }
    if (-not (Test-Path -LiteralPath $iscc)) { throw "ISCC.exe not found at $iscc after installing" }
    Write-Host "Installed Inno Setup $version`: $iscc"
}

$tmp = Join-Path ([IO.Path]::GetTempPath()) ('ChineseSimplified-' + [Guid]::NewGuid().ToString('N') + '.isl')
try {
    Get-Verified $islUrl $islSha256 $tmp
    # The upstream file is UTF-8 without a BOM; ISCC reads BOM-less files as ANSI.
    $content = [IO.File]::ReadAllText($tmp, (New-Object Text.UTF8Encoding($false)))
    [IO.File]::WriteAllText($isl, $content, (New-Object Text.UTF8Encoding($true)))
}
finally {
    Remove-Item -LiteralPath $tmp -Force -ErrorAction SilentlyContinue
}
Write-Host "Installed $isl"
