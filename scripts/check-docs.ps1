# Checks that repository documentation stays navigable and consistent. ASCII only:
# Windows PowerShell 5.1 reads BOM-less files as ANSI. Run from anywhere:
#   powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\check-docs.ps1
# Exit code 0 = clean, 1 = problems found.

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$errors = New-Object System.Collections.Generic.List[string]

function Add-Problem([string]$msg) { $script:errors.Add($msg) }
function Rel([string]$path) { $path.Substring($root.Length + 1).Replace('\', '/') }
function Read-Lines([string]$path) { @(Get-Content -LiteralPath $path -Encoding UTF8) }

# 1. Size budgets and required collaboration files.
$requiredFiles = @(
    'README.md',
    'CONTRIBUTING.md',
    'SECURITY.md',
    '.github/pull_request_template.md',
    '.github/workflows/ci.yml',
    '.github/workflows/release.yml',
    'docs/README.md',
    'docs/architecture.md',
    'docs/configuration.md',
    'docs/design.md',
    'docs/environment.md',
    'docs/gotchas.md',
    'docs/hardware.md'
)
foreach ($req in $requiredFiles) {
    if (-not (Test-Path -LiteralPath (Join-Path $root $req))) {
        Add-Problem "missing required doc/workflow file: $req"
    }
}

$budgets = @(
    @{ Path = 'AGENTS.md'; Max = 150 },
    @{ Path = 'docs/progress.md'; Max = 100 }
)
Get-ChildItem -Path (Join-Path $root 'src') -Recurse -Filter 'AGENTS.md' | ForEach-Object {
    $budgets += @{ Path = (Rel $_.FullName); Max = 60 }
}
foreach ($b in $budgets) {
    $full = Join-Path $root $b.Path
    if (-not (Test-Path -LiteralPath $full)) { Add-Problem "missing file: $($b.Path)"; continue }
    $n = (Read-Lines $full).Count
    if ($n -gt $b.Max) { Add-Problem "$($b.Path) has $n lines, budget is $($b.Max)" }
}

# 2. Every AGENTS.md has a sibling CLAUDE.md containing @AGENTS.md.
Get-ChildItem -Path $root -Recurse -Filter 'AGENTS.md' |
    Where-Object { $_.FullName -notmatch '\\(build|third_party|\.git)\\' } | ForEach-Object {
    $claude = Join-Path $_.DirectoryName 'CLAUDE.md'
    if (-not (Test-Path -LiteralPath $claude)) {
        Add-Problem "$(Rel $_.FullName): no sibling CLAUDE.md"
    } elseif (-not ((Read-Lines $claude) -contains '@AGENTS.md')) {
        Add-Problem "$(Rel $claude): must contain the line @AGENTS.md"
    }
}

# 3. decisions/: active ADR files and index rows match one to one.
$decDir = Join-Path $root 'docs/decisions'
$indexLines = Read-Lines (Join-Path $decDir 'README.md')
$indexed = @{}
foreach ($line in $indexLines) {
    if ($line -match '^\| \[(D-\d{3})\]\((D-\d{3})\.md\) \|') {
        if ($Matches[1] -ne $Matches[2]) { Add-Problem "decisions/README.md: label $($Matches[1]) links to $($Matches[2]).md" }
        if ($indexed.ContainsKey($Matches[1])) { Add-Problem "decisions/README.md: $($Matches[1]) listed twice" }
        $indexed[$Matches[1]] = $true
    }
}
$files = @{}
Get-ChildItem -Path $decDir -Filter 'D-*.md' | ForEach-Object {
    $id = $_.BaseName
    $files[$id] = $true
    if (-not $indexed.ContainsKey($id)) { Add-Problem "decisions/$($_.Name): not listed in decisions/README.md" }
    $first = (Read-Lines $_.FullName)[0]
    if ($first -notmatch "^# $id ") { Add-Problem "decisions/$($_.Name): first line must start with '# $id '" }
}
foreach ($id in $indexed.Keys) {
    if (-not $files.ContainsKey($id)) { Add-Problem "decisions/README.md: $id has no file" }
}

# 4. Relative markdown links resolve.
$mdFiles = @(Get-ChildItem -Path $root -Recurse -Filter '*.md' | Where-Object {
    $_.FullName -notmatch '\\(build|third_party|\.git|tmp|outputs)\\'
})
foreach ($f in $mdFiles) {
    $text = (Read-Lines $f.FullName) -join "`n"
    foreach ($m in [regex]::Matches($text, '\]\(([^)\s]+)\)')) {
        $target = $m.Groups[1].Value
        if ($target -match '^(https?:|mailto:|#)') { continue }
        $target = ($target -split '#')[0]
        if ($target -eq '') { continue }
        $resolved = Join-Path $f.DirectoryName $target
        if (-not (Test-Path -LiteralPath $resolved)) { Add-Problem "$(Rel $f.FullName): broken link -> $($m.Groups[1].Value)" }
    }
}

# 5. Documentation is English. Chinese UI strings may be quoted, so allow a small share
#    of CJK characters; a larger share means a paragraph was written in Chinese.
$cjkPattern = '[' + [char]0x3400 + '-' + [char]0x9fff + ']'
foreach ($f in $mdFiles) {
    $text = (Read-Lines $f.FullName) -join "`n"
    $cjk = [regex]::Matches($text, $cjkPattern).Count
    $all = ($text -replace '\s', '').Length
    if ($all -gt 0 -and ($cjk / $all) -gt 0.08) {
        Add-Problem ("{0}: {1:P0} of the text is Chinese; documentation must be written in English" -f (Rel $f.FullName), ($cjk / $all))
    }
}

if ($errors.Count -eq 0) {
    Write-Host 'docs OK'
    exit 0
}
$errors | ForEach-Object { Write-Host "  $_" }
Write-Host "$($errors.Count) problem(s)"
exit 1
