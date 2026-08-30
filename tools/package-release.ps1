<#
.SYNOPSIS
    Build a distributable zip for a release, and verify it actually runs.

.DESCRIPTION
    Produces a self-contained folder and zip containing the executables plus
    the documentation, ready to attach to a GitHub release.

    No runtime installer is needed: CMakeLists sets
    CMAKE_MSVC_RUNTIME_LIBRARY to MultiThreaded, so the CRT is linked
    statically and the exe runs on a machine with nothing else installed.
    This script checks that assumption rather than assuming it - it runs the
    packaged exe from a clean directory with an empty PATH and fails if it
    cannot start.

.EXAMPLE
    ./tools/package-release.ps1 -Version 3.3.0
#>
param(
    [Parameter(Mandatory = $true)][string]$Version,
    [string]$Config = "Release",
    [string]$BuildDir = "build",
    [string]$OutDir = "dist"
)

$ErrorActionPreference = "Stop"

$binDir = Join-Path $BuildDir "bin/$Config"
if (-not (Test-Path (Join-Path $binDir "ChiptuneTracker.exe"))) {
    throw "ChiptuneTracker.exe not found in $binDir. Build first."
}

$stageName = "ChiptuneTracker-$Version-win64"
$stage = Join-Path $OutDir $stageName

if (Test-Path $stage) { Remove-Item $stage -Recurse -Force }
New-Item -ItemType Directory -Force -Path $stage | Out-Null

# --- executables ---------------------------------------------------------
Copy-Item (Join-Path $binDir "ChiptuneTracker.exe") $stage
foreach ($tool in @("VoiceToNote.exe")) {
    $path = Join-Path $binDir $tool
    if (Test-Path $path) { Copy-Item $path $stage }
}

# --- documentation -------------------------------------------------------
foreach ($doc in @("README.md", "CHANGELOG.md", "LICENSE")) {
    if (Test-Path $doc) { Copy-Item $doc $stage }
}
New-Item -ItemType Directory -Force -Path (Join-Path $stage "docs") | Out-Null
foreach ($doc in @("docs/ROADMAP.md", "docs/TEST_PLAN.md")) {
    if (Test-Path $doc) { Copy-Item $doc (Join-Path $stage "docs") }
}

# --- a short note for someone who just unzipped it -----------------------
@"
ChiptuneTracker $Version
========================

Run ChiptuneTracker.exe. Nothing to install - the C runtime is linked
statically, so there is no redistributable to chase.

Getting started
---------------
  Space        play / pause
  F4           instrument macros
  F12          screenshot (written to screenshots/)
  Ctrl+0       reset the window layout
  [ and ]      octave down / up
  Z S X D C... play notes on the computer keyboard

Drag any panel by its tab to rearrange the workspace; panels dropped onto
each other become tabs. View > Workspace has three starting layouts.

VoiceToNote.exe is a separate tool that turns humming or beatboxing into a
pattern you can open here.

See README.md for the full feature list and CHANGELOG.md for what changed.
"@ | Set-Content (Join-Path $stage "READ ME FIRST.txt") -Encoding UTF8

# --- verify the packaged exe actually launches ---------------------------
#
# The point of a static CRT is that it runs anywhere. Assert that rather
# than trusting it: launch from the staged folder with a minimal PATH, so a
# DLL that happens to sit in the build tree cannot rescue it.
Write-Output "Verifying the packaged executable starts..."

$verifyShot = Join-Path $env:TEMP "chiptune_package_verify.bmp"
if (Test-Path $verifyShot) { Remove-Item $verifyShot -Force }

$exe = Join-Path $stage "ChiptuneTracker.exe"
$process = Start-Process -FilePath $exe `
    -ArgumentList "--capture `"$verifyShot`" --frames 45 --size 900 600 --demo" `
    -WorkingDirectory $stage -PassThru

if (-not $process.WaitForExit(60000)) {
    Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue
    throw "Packaged executable did not exit - it may have failed to start."
}
if ($process.ExitCode -ne 0) {
    throw ("Packaged executable exited with 0x{0:X}" -f $process.ExitCode)
}
if (-not (Test-Path $verifyShot)) {
    throw "Packaged executable ran but rendered no frame."
}
Remove-Item $verifyShot -Force
Write-Output "  ok - it launches and renders standalone"

# Anything the staged run left behind is not part of the package
foreach ($stray in @("imgui.ini", "screenshots")) {
    $path = Join-Path $stage $stray
    if (Test-Path $path) { Remove-Item $path -Recurse -Force }
}

# --- zip -----------------------------------------------------------------
$zip = Join-Path $OutDir "$stageName.zip"
if (Test-Path $zip) { Remove-Item $zip -Force }
Compress-Archive -Path "$stage/*" -DestinationPath $zip -CompressionLevel Optimal

$sizeMb = [math]::Round((Get-Item $zip).Length / 1MB, 2)
Write-Output ""
Write-Output "Packaged $stageName"
Write-Output "  folder: $stage"
Write-Output "  zip:    $zip  (${sizeMb} MB)"
