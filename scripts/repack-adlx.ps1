# Rebuilds a published release's installer and zip with the locally built PitotAdlx.dll and its
# end-user agreement inside, then replaces them, and SHA256SUMS.txt, on the GitHub release. Run it
# after the release workflow has published the tag. Pitot.exe and PitotNvidia.dll come from the
# release's own zip, so only the AMD DLL is taken from this machine. Kept ASCII-only because
# Windows PowerShell 5.1 reads BOM-less files in the ANSI code page.
#
#   scripts\get-adlx.ps1 -AcceptAmdLicense   (once)
#   scripts\get-innosetup.ps1                (once)
#   git checkout 0.1.0                       (the DLL must be built from the tagged source)
#   scripts\build.ps1 -Config release -Target PitotAdlx
#   scripts\repack-adlx.ps1 -Tag 0.1.0
#   git checkout main
#
# The DLL is built on the maintainer's machine, not in CI, because the AMD ADLX SDK license is
# accepted there (D-033).

param(
    [Parameter(Mandatory)]
    [string]$Tag
)

$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$dll = Join-Path $root 'build\x64-release\PitotAdlx.dll'
if (-not (Test-Path -LiteralPath $dll)) {
    throw "Missing $dll. Run scripts\get-adlx.ps1 -AcceptAmdLicense, then scripts\build.ps1 -Config release -Target PitotAdlx."
}

# A DLL from another commit would not match the release it is packaged into.
$tagCommit = (git -C $root rev-parse "$Tag^{commit}" 2>$null)
if ($LASTEXITCODE -ne 0) { throw "Tag $Tag not found locally. Run git fetch --tags." }
if ($tagCommit -ne (git -C $root rev-parse HEAD)) { throw "HEAD is not at tag $Tag. Check it out and rebuild first." }
if (git -C $root status --porcelain) { throw 'The working tree has uncommitted changes.' }

$zipName = "Pitot-$Tag-win-x64.zip"
$tmp = Join-Path ([IO.Path]::GetTempPath()) ('pitot-repack-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $tmp | Out-Null
Push-Location $root
try {
    gh release download $Tag --pattern $zipName --dir $tmp
    if ($LASTEXITCODE -ne 0) { throw "Could not download $zipName from release $Tag" }
    $bin = Join-Path $tmp 'bin'
    Expand-Archive -LiteralPath (Join-Path $tmp $zipName) -DestinationPath $bin

    & (Join-Path $PSScriptRoot 'package.ps1') -Version $Tag -BinDir $bin -AdlxDll $dll

    $assets = "Pitot-$Tag-setup.exe", $zipName, 'SHA256SUMS.txt' | ForEach-Object { Join-Path $root "dist\$_" }
    gh release upload $Tag @assets --clobber
    if ($LASTEXITCODE -ne 0) { throw "gh release upload failed: $LASTEXITCODE" }
}
finally {
    Pop-Location
    Remove-Item -LiteralPath $tmp -Recurse -Force -ErrorAction SilentlyContinue
}
