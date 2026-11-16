# Download the AMD ADLX SDK headers into third_party/adlx (git-ignored).
#
# The ADLX SDK is NOT open source. It is licensed under AMD's "ADLX SDK License
# Agreement" (a PDF in the SDK root). Its header files may not be redistributed,
# so they must never be committed to this repository (see docs/decisions/D-033.md).
# By running this script with -AcceptAmdLicense you confirm you have read and
# accept that agreement.
#
# Without these headers Pitot still builds; only the optional PitotAdlx.dll
# (hotspot / VRAM temperature, board power, GPU fan) is skipped.
#
# Usage:
#   powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\get-adlx.ps1 -AcceptAmdLicense

param(
    [switch]$AcceptAmdLicense,
    [switch]$Force
)

$ErrorActionPreference = 'Stop'

# Pinned to v1.5 (runtime 1.5.0.124 ships with the driver on the development PC).
# Newer drivers accept older SDK versions; an older SDK keeps more drivers working.
$Version = '1.5'
$Url = "https://github.com/GPUOpen-LibrariesAndSDKs/ADLX/archive/refs/tags/v$Version.zip"
$Sha256 = '19015F29EAAC36B13515AD8BE0BC7CAB3C26C9A1881D6DFBC7DC762CBACD58E9'

$root = Split-Path -Parent $PSScriptRoot
$dest = Join-Path $root 'third_party\adlx'

if (-not $AcceptAmdLicense) {
    Write-Host 'The ADLX SDK is licensed under the AMD ADLX SDK License Agreement, not an open source license.'
    Write-Host 'Read it at https://github.com/GPUOpen-LibrariesAndSDKs/ADLX (ADLX SDK License Agreement.pdf),'
    Write-Host 'then run this script again with -AcceptAmdLicense.'
    exit 1
}

if ((Test-Path (Join-Path $dest 'include\ADLX.h')) -and -not $Force) {
    Write-Host "ADLX SDK already present in $dest (use -Force to download again)."
    exit 0
}

$tmp = Join-Path ([IO.Path]::GetTempPath()) ("pitot-adlx-" + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $tmp | Out-Null
try {
    $zip = Join-Path $tmp 'adlx.zip'
    Write-Host "Downloading $Url"
    Invoke-WebRequest -UseBasicParsing -Uri $Url -OutFile $zip -TimeoutSec 300
    $hash = (Get-FileHash $zip -Algorithm SHA256).Hash
    if ($hash -ne $Sha256) {
        throw "SHA-256 mismatch: expected $Sha256, got $hash"
    }

    # Only the public headers and the license are needed. The SDK's sample code
    # (ADLXHelper etc.) is not used; Pitot has its own loader.
    $prefix = "ADLX-$Version"
    & tar -xf $zip -C $tmp "$prefix/SDK/Include" "$prefix/ADLX SDK License Agreement.pdf"
    if ($LASTEXITCODE -ne 0) { throw 'tar failed' }

    if (Test-Path $dest) { Remove-Item -Recurse -Force $dest }
    New-Item -ItemType Directory -Path $dest | Out-Null
    Move-Item (Join-Path $tmp "$prefix\SDK\Include") (Join-Path $dest 'include')
    Move-Item (Join-Path $tmp "$prefix\ADLX SDK License Agreement.pdf") $dest
    Set-Content -Path (Join-Path $dest 'VERSION.txt') -Value "ADLX SDK v$Version, $Url, SHA-256 $Sha256" -Encoding ascii
    Write-Host "ADLX SDK v$Version headers installed to $dest"
    Write-Host 'Re-run the CMake configure step (build.ps1 does this) to build PitotAdlx.dll.'
}
finally {
    Remove-Item -Recurse -Force $tmp -ErrorAction SilentlyContinue
}
