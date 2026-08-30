<#
.SYNOPSIS
    Regenerate the screenshot gallery in docs/images.

.DESCRIPTION
    Drives the app's own --capture mode, which reads the rendered frame out
    of OpenGL with glReadPixels. That is deliberate: capturing the desktop
    instead would pick up whatever else is on screen, and would depend on
    window focus, overlapping windows and display scaling. The framebuffer
    sees exactly what the app drew and nothing else.

    Each shot puts the app into a known state from the command line, so the
    gallery can be regenerated after any UI change instead of going stale.

    Covers one image per feature - including the states you can only reach
    by clicking, like the three piano-roll edit modes and an active note
    selection - plus every theme.

    The app writes BMP (no image dependencies in C++); this converts to PNG.

.EXAMPLE
    ./tools/generate-gallery.ps1
    ./tools/generate-gallery.ps1 -Only piano-roll,mixer
#>
param(
    [string]$OutDir = "docs/images",
    [string]$Exe = "build/bin/Release/ChiptuneTracker.exe",
    [int]$Width = 1600,
    [int]$Height = 900,
    [int]$Frames = 150,
    [int]$TimeoutSeconds = 60,
    [string[]]$Only = @()
)

$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.Drawing

if (-not (Test-Path $Exe)) { throw "Executable not found: $Exe. Build first." }
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null

$temp = Join-Path $env:TEMP "chiptune_gallery"
New-Item -ItemType Directory -Force -Path $temp | Out-Null

# name, extra arguments. One entry per gallery image.
$shots = @(
    # --- Views -----------------------------------------------------------
    @{ name = "piano-roll";      args = "--view pianoroll --theme synthwave --demo" }
    @{ name = "tracker";         args = "--view tracker --theme stock --demo" }
    @{ name = "arrangement";     args = "--view arrangement --theme cyberpunk --demo" }
    @{ name = "mixer";           args = "--view mixer --theme cyberpunk --demo --playing" }
    @{ name = "pad";             args = "--view pad --theme vaporwave --demo" }

    # --- Piano roll edit modes -------------------------------------------
    @{ name = "mode-draw";       args = "--view pianoroll --theme stock --demo --mode draw" }
    @{ name = "mode-select";     args = "--view pianoroll --theme stock --demo --mode select --select" }
    @{ name = "mode-erase";      args = "--view pianoroll --theme stock --demo --mode erase" }

    # --- Feature panels ---------------------------------------------------
    @{ name = "macro-volume";    args = "--view pianoroll --theme stock --demo --show macros --macro-tab volume" }
    @{ name = "macro-arpeggio";  args = "--view pianoroll --theme stock --demo --show macros --macro-tab arpeggio" }
    @{ name = "wavetable";       args = "--view pianoroll --theme stock --demo --show wavetable" }
    @{ name = "spectrum";        args = "--view pianoroll --theme cyberpunk --demo --show spectrum --playing" }
    @{ name = "automation";      args = "--view pianoroll --theme stock --demo --show automation" }
    @{ name = "midi-input";      args = "--view pianoroll --theme stock --demo --show midi" }
    @{ name = "channel-editor";  args = "--view pianoroll --theme minimal --demo --select" }
    @{ name = "tools-euclidean"; args = "--workspace sounddesign --theme stock --demo" }

    # --- Workspaces -------------------------------------------------------
    @{ name = "workspace-sound"; args = "--workspace sounddesign --theme stock --demo --show macros --show wavetable" }
    @{ name = "workspace-mix";   args = "--workspace mix --theme stock --demo --show spectrum --playing" }

    # --- Themes -----------------------------------------------------------
    @{ name = "theme-stock";     args = "--view pianoroll --theme stock --demo" }
    @{ name = "theme-cyberpunk"; args = "--view pianoroll --theme cyberpunk --demo" }
    @{ name = "theme-synthwave"; args = "--view pianoroll --theme synthwave --demo" }
    @{ name = "theme-matrix";    args = "--view pianoroll --theme matrix --demo" }
    @{ name = "theme-frutiger";  args = "--view pianoroll --theme frutiger --demo" }
    @{ name = "theme-minimal";   args = "--view pianoroll --theme minimal --demo" }
    @{ name = "theme-vaporwave"; args = "--view pianoroll --theme vaporwave --demo" }
    @{ name = "theme-terminal";  args = "--view pianoroll --theme terminal --demo" }
    @{ name = "theme-gameboy";   args = "--view pianoroll --theme gameboy --demo" }
    @{ name = "theme-daylight";  args = "--view pianoroll --theme daylight --demo" }
)

if ($Only.Count -gt 0) {
    $shots = $shots | Where-Object { $Only -contains $_.name }
    if ($shots.Count -eq 0) { throw "No shots matched -Only" }
}

$failed = @()

foreach ($shot in $shots) {
    $bmp = Join-Path $temp "$($shot.name).bmp"
    $png = Join-Path $OutDir "$($shot.name).png"
    if (Test-Path $bmp) { Remove-Item $bmp -Force }

    $argList = "--capture `"$bmp`" --frames $Frames --size $Width $Height $($shot.args)"
    $process = Start-Process -FilePath $Exe -ArgumentList $argList -PassThru

    if (-not $process.WaitForExit($TimeoutSeconds * 1000)) {
        Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue
        Write-Warning "$($shot.name): timed out"
        $failed += $shot.name
        continue
    }
    if (-not (Test-Path $bmp)) {
        Write-Warning "$($shot.name): no capture produced"
        $failed += $shot.name
        continue
    }

    $image = [System.Drawing.Image]::FromFile($bmp)
    try { $image.Save($png, [System.Drawing.Imaging.ImageFormat]::Png) }
    finally { $image.Dispose() }
    Remove-Item $bmp -Force

    $size = [math]::Round((Get-Item $png).Length / 1KB)
    Write-Output "$($shot.name).png  (${size} KB)"
}

if ($failed.Count -gt 0) { throw "Failed to capture: $($failed -join ', ')" }

Write-Output ""
Write-Output "Wrote $($shots.Count) images to $OutDir"
