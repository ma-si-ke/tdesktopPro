# =====================================================================
#  Build machine env check -- tdesktop self-hosted runner
#  Usage (admin PowerShell):
#    powershell -ExecutionPolicy Bypass -File .\check-build-env.ps1
#  ASCII-only on purpose: avoids codepage/BOM issues in Windows PowerShell 5.1.
# =====================================================================

$ErrorActionPreference = "SilentlyContinue"
$script:fail = 0
$script:warn = 0

function Ok   ($n)    { Write-Host ("[ OK ] {0}" -f $n) -ForegroundColor Green }
function Bad  ($n,$h) { Write-Host ("[FAIL] {0}" -f $n) -ForegroundColor Red;    Write-Host ("        -> {0}" -f $h) -ForegroundColor DarkGray; $script:fail++ }
function Warn ($n,$h) { Write-Host ("[WARN] {0}" -f $n) -ForegroundColor Yellow; Write-Host ("        -> {0}" -f $h) -ForegroundColor DarkGray; $script:warn++ }
function Check($n,$cond,$hint) { if ($cond) { Ok $n } else { Bad $n $hint } }

Write-Host ""
Write-Host "=== tdesktop build-machine environment check ===" -ForegroundColor Cyan
Write-Host ""

# --- Administrator ---
$admin = ([Security.Principal.WindowsPrincipal] [Security.Principal.WindowsIdentity]::GetCurrent()
         ).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
if ($admin) { Ok "Running as Administrator" } else { Warn "Not running as Administrator" "Run in an elevated PowerShell for accurate checks" }

# --- Developer Mode (needed by mklink) ---
$dev = (Get-ItemProperty "HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\AppModelUnlock" `
        -Name AllowDevelopmentWithoutDevLicense).AllowDevelopmentWithoutDevLicense
Check "Developer Mode (mklink)" ($dev -eq 1) `
  'reg add "HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\AppModelUnlock" /t REG_DWORD /f /v AllowDevelopmentWithoutDevLicense /d 1'

# --- Git ---
$git = Get-Command git -ErrorAction SilentlyContinue
Check "Git" ($null -ne $git) "Install Git for Windows: https://git-scm.com/download/win"

# --- Git Bash tools (used by the workflow's bash steps) ---
$bash = "$env:ProgramFiles\Git\bin\bash.exe"
$bashOk = (Test-Path $bash) -and (& $bash -lc "command -v sha256sum >/dev/null && command -v sed >/dev/null && command -v base64 >/dev/null && command -v find >/dev/null && echo yes" 2>$null)
Check "Git Bash tools (sha256sum/sed/base64/find)" ($bashOk -eq "yes") "Make sure you installed Git for Windows (ships Git Bash)"

# --- Python 3.10 ---
$pyver = (python --version 2>&1)
$pyOk  = ($null -ne (Get-Command python -ErrorAction SilentlyContinue)) -and ($pyver -match "3\.10")
if ($pyOk) { Ok "Python 3.10  ($pyver)" }
elseif ($pyver -match "Python 3") { Warn "Python is not 3.10  ($pyver)" "Docs require 3.10; other 3.x may work but is not guaranteed" }
else { Bad "Python 3.10" "Install Python 3.10 and check Add to PATH: https://www.python.org/downloads/" }

# --- CMake in PATH ---
$cmake = Get-Command cmake -ErrorAction SilentlyContinue
Check "CMake in PATH" ($null -ne $cmake) "Install official CMake and add to PATH: https://cmake.org/download/"
if ($cmake) { Write-Host ("        {0}" -f ((cmake --version 2>&1)[0])) -ForegroundColor DarkGray }

# --- CMake at Program Files\CMake (the sed patch depends on this) ---
Check "CMake at C:\Program Files\CMake" (Test-Path "$env:ProgramFiles\CMake\share") `
  "Install the official CMake MSI to the default location; win.yml patches Windows-MSVC.cmake there"

# --- NuGet ---
$nuget = Get-Command nuget -ErrorAction SilentlyContinue
Check "NuGet CLI in PATH" ($null -ne $nuget) "Download nuget.exe into a PATH folder: https://dist.nuget.org/win-x86-commandline/latest/nuget.exe"

# --- Visual Studio + C++ ---
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vs = if (Test-Path $vswhere) {
  & $vswhere -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -latest -property installationPath
} else { $null }
Check "Visual Studio + C++ tools" (($null -ne $vs) -and (Test-Path "$vs\VC\Auxiliary\Build\vcvars64.bat")) `
  "Install VS 2026 Build Tools with the 'Desktop development with C++' workload"
if ($vs) { Write-Host ("        {0}" -f $vs) -ForegroundColor DarkGray }

# --- MSVC toolset 14.44 ---
$has1444 = $false
if ($vs) { $has1444 = [bool](Get-ChildItem "$vs\VC\Tools\MSVC" -ErrorAction SilentlyContinue | Where-Object { $_.Name -like "14.44*" }) }
Check "MSVC toolset 14.44 (v144.4)" $has1444 "VS Installer -> Individual components -> search 14.44 -> MSVC v144.4 x86/x64"

# --- Windows SDK 10.0.26100.0 ---
$sdk = Test-Path "${env:ProgramFiles(x86)}\Windows Kits\10\Include\10.0.26100.0"
Check "Windows SDK 10.0.26100.0" $sdk "VS Installer -> Individual components -> search 26100 -> Windows 11 SDK (10.0.26100.0)"

# --- C++ ATL (optional but the official build needs it) ---
$atl = $false
if ($vs) { $atl = [bool](Get-ChildItem "$vs\VC\Tools\MSVC\*\atlmfc\include\atlbase.h" -ErrorAction SilentlyContinue) }
if ($atl) { Ok "C++ ATL" } else { Warn "C++ ATL not detected" "VS Installer -> 'C++ ATL for latest v144 build tools'" }

# --- Disk space (any drive with >120GB free passes) ---
$maxFree = 0
Get-PSDrive -PSProvider FileSystem | ForEach-Object {
  if ($_.Free) { $gb = [math]::Round($_.Free / 1GB, 1); if ($gb -gt $maxFree) { $maxFree = $gb }
    Write-Host ("        drive {0}: {1} GB free" -f $_.Name, $gb) -ForegroundColor DarkGray }
}
if ($maxFree -ge 120) { Ok ("Disk space OK (max free {0} GB)" -f $maxFree) }
elseif ($maxFree -ge 60) { Warn ("Disk a bit tight (max free {0} GB)" -f $maxFree) "Recommend >=120GB; deps + Qt/WebRTC sources are large" }
else { Bad ("Not enough disk (max free {0} GB)" -f $maxFree) "Free up or expand; recommend >=120GB" }

# --- Summary ---
Write-Host ""
Write-Host "=== Summary ===" -ForegroundColor Cyan
if ($script:fail -eq 0 -and $script:warn -eq 0) {
  Write-Host "All checks passed. You can register the runner." -ForegroundColor Green
} elseif ($script:fail -eq 0) {
  Write-Host ("Passed with {0} warning(s). OK to continue, but review them." -f $script:warn) -ForegroundColor Yellow
} else {
  Write-Host ("{0} check(s) FAILED, {1} warning(s). Fix the FAIL items first." -f $script:fail, $script:warn) -ForegroundColor Red
}
Write-Host ""
exit $script:fail
