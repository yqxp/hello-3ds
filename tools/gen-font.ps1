# gen-font.ps1 - rebuild romfs/cjk.bcfnt from the characters actually used in source/main.c
#
# Usage (from the repo root):
#     powershell -ExecutionPolicy Bypass -File tools/gen-font.ps1
#
# Only Windows PowerShell is needed; mkbcfnt comes with devkitPro.

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot

# Read the source and STRIP COMMENTS FIRST.
# Code comments often contain prose in many languages. If we did not strip them
# the generated font would drag in hundreds of extra glyphs and balloon in size.
$txt = [System.IO.File]::ReadAllText((Join-Path $root 'source\main.c'), [System.Text.Encoding]::UTF8)
$txt = [regex]::Replace($txt, '/\*[\s\S]*?\*/', '')
$txt = [regex]::Replace($txt, '(?m)//[^\r\n]*', '')

$cps = New-Object System.Collections.Generic.HashSet[int]
for ($i = 0x20; $i -le 0x7E; $i++) { [void]$cps.Add($i) }
foreach ($ch in $txt.ToCharArray()) {
    $c = [int][char]$ch
    if ($c -gt 0x7F) { [void]$cps.Add($c) }
}

$list = ($cps | Sort-Object | ForEach-Object { '0x{0:X4}' -f $_ }) -join ' '
[System.IO.File]::WriteAllText((Join-Path $root 'gfx\whitelist.txt'), $list, (New-Object System.Text.ASCIIEncoding))
Write-Host "codepoints: $($cps.Count)"

$font = Join-Path $root 'fonts\NotoSansSC-Regular.otf'
if (!(Test-Path $font)) { Write-Host 'Missing fonts/NotoSansSC-Regular.otf - see README'; exit 1 }

# Locate mkbcfnt: env override, then PATH, then the usual devkitPro spots.
$cand = @()
if ($env:MKBCFNT) { $cand += $env:MKBCFNT }
$cmd = Get-Command mkbcfnt -ErrorAction SilentlyContinue
if ($cmd) { $cand += $cmd.Source }
$cand += 'C:\devkitPro\tools\bin\mkbcfnt.exe'
if ($env:DEVKITPRO) {
    $d = $env:DEVKITPRO
    if ($d -eq '/opt/devkitpro') { $d = 'C:\devkitPro' }
    $cand += (Join-Path $d 'tools\bin\mkbcfnt.exe')
}
$mk = $cand | Where-Object { $_ -and (Test-Path $_) } | Select-Object -First 1
if (!$mk) { Write-Host 'mkbcfnt not found - is devkitPro installed / on PATH?'; exit 1 }

& $mk -o (Join-Path $root 'romfs\cjk.bcfnt') -s 26 -w (Join-Path $root 'gfx\whitelist.txt') $font
Write-Host 'wrote romfs/cjk.bcfnt'
