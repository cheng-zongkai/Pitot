# Rebuild src/app/pitot.ico from the SVG sources next to it (D-082, D-088).
# 16/20/24/32 px use the pixel-aligned drawings pitot-<size>.svg; larger sizes use pitot.svg.
# The drawings are the black probe with a white keyline on a transparent ground.
# The SVGs are rendered by Edge (or Chrome) in headless mode with a transparent background,
# and every size is stored as a PNG entry in the ICO.
# ASCII only: Windows PowerShell 5.1 reads scripts in the ANSI code page.
param(
    [string]$Browser = ''
)
$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$appDir = Join-Path $root 'src\app'
$output = Join-Path $appDir 'pitot.ico'

if (-not $Browser) {
    $candidates = @(
        "${env:ProgramFiles(x86)}\Microsoft\Edge\Application\msedge.exe",
        "$env:ProgramFiles\Microsoft\Edge\Application\msedge.exe",
        "$env:ProgramFiles\Google\Chrome\Application\chrome.exe"
    )
    $Browser = $candidates | Where-Object { Test-Path $_ } | Select-Object -First 1
}
if (-not $Browser) {
    throw 'Edge or Chrome not found; pass -Browser <path to msedge.exe or chrome.exe>.'
}

$sizes = @(
    @{ Size = 16; Svg = 'pitot-16.svg' },
    @{ Size = 20; Svg = 'pitot-20.svg' },
    @{ Size = 24; Svg = 'pitot-24.svg' },
    @{ Size = 32; Svg = 'pitot-32.svg' },
    @{ Size = 40; Svg = 'pitot.svg' },
    @{ Size = 48; Svg = 'pitot.svg' },
    @{ Size = 64; Svg = 'pitot.svg' },
    @{ Size = 256; Svg = 'pitot.svg' }
)

$work = Join-Path ([IO.Path]::GetTempPath()) ('pitot-icon-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $work | Out-Null
Add-Type -AssemblyName System.Drawing

try {
    $profileDir = Join-Path $work 'profile'
    $images = @()
    foreach ($entry in $sizes) {
        $n = $entry.Size
        $svgPath = Join-Path $appDir $entry.Svg
        if (-not (Test-Path $svgPath)) {
            throw "Missing $svgPath"
        }
        $svgUrl = ([Uri]$svgPath).AbsoluteUri
        $html = Join-Path $work "icon-$n.html"
        $png = Join-Path $work "icon-$n.png"
        $page = '<!doctype html><html><body style="margin:0"><img src="' + $svgUrl + '" width="' + $n +
            '" height="' + $n + '" style="display:block"></body></html>'
        [IO.File]::WriteAllText($html, $page, [Text.Encoding]::ASCII)

        $arguments = @(
            '--headless=new', '--disable-gpu', '--hide-scrollbars', '--no-first-run',
            '--force-device-scale-factor=1', '--default-background-color=00000000',
            "--user-data-dir=$profileDir", "--window-size=$n,$n", "--screenshot=$png",
            ([Uri]$html).AbsoluteUri
        )
        $process = Start-Process -FilePath $Browser -ArgumentList $arguments -Wait -PassThru -WindowStyle Hidden
        if (-not (Test-Path $png)) {
            throw "Rendering $n px failed (exit $($process.ExitCode))"
        }
        $bitmap = [System.Drawing.Bitmap]::new($png)
        try {
            if ($bitmap.Width -ne $n -or $bitmap.Height -ne $n) {
                throw "Rendered $n px came out $($bitmap.Width)x$($bitmap.Height)"
            }
            if ($bitmap.GetPixel(0, 0).A -ne 0) {
                throw "Rendered $n px has no transparent background"
            }
        } finally {
            $bitmap.Dispose()
        }
        $images += , @{ Size = $n; Bytes = [IO.File]::ReadAllBytes($png) }
    }

    # ICONDIR + ICONDIRENTRY[] + PNG data
    $stream = [IO.MemoryStream]::new()
    $writer = [IO.BinaryWriter]::new($stream)
    $writer.Write([UInt16]0)
    $writer.Write([UInt16]1)
    $writer.Write([UInt16]$images.Count)
    $offset = 6 + 16 * $images.Count
    foreach ($image in $images) {
        $dimension = if ($image.Size -ge 256) { 0 } else { $image.Size }
        $writer.Write([Byte]$dimension)
        $writer.Write([Byte]$dimension)
        $writer.Write([Byte]0)
        $writer.Write([Byte]0)
        $writer.Write([UInt16]1)
        $writer.Write([UInt16]32)
        $writer.Write([UInt32]$image.Bytes.Length)
        $writer.Write([UInt32]$offset)
        $offset += $image.Bytes.Length
    }
    foreach ($image in $images) {
        $writer.Write($image.Bytes)
    }
    $writer.Flush()
    [IO.File]::WriteAllBytes($output, $stream.ToArray())
    $writer.Dispose()
    Write-Host "Wrote $output ($($images.Count) sizes, $((Get-Item $output).Length) bytes)"
} finally {
    Remove-Item -Recurse -Force $work -ErrorAction SilentlyContinue
}
