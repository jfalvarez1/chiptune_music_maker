<#
.SYNOPSIS
    Regenerate the screenshot gallery in docs/images.

.DESCRIPTION
    Drives the app's own --capture mode, which reads the rendered frame out
    of OpenGL with glReadPixels. That is deliberate: capturing the desktop
    instead would pick up whatever else happens to be on screen, and would
    depend on window focus, overlapping windows and display scaling. The
    framebuffer sees exactly what the app drew and nothing else.

    Each shot puts the app into a known state from the command line, so the
    gallery can be regenerated after any UI change instead of going stale.

    The app writes BMP (no dependencies in C++); this converts to PNG.

.EXAMPLE
    ./tools/generate-gallery.ps1
    ./tools/generate-gallery.ps1 -OutDir docs/images -Width 1600 -Height 900
#>
param(
    [string]$OutDir = "docs/images",
    [string]$Exe = "build/bin/Release/ChiptuneTracker.exe",
    [int]$Width = 1600,
    [int]$Height = 900,
    [int]$Frames = 150,
    [int]$TimeoutSeconds = 60
)

$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.Drawing

if (-not (Test-Path $Exe)) { throw "Executable not found: $Exe. Build first." }
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null

$temp = Join-Path $env:TEMP "chiptune_gallery"
New-Item -ItemType Directory -Force -Path $temp | Out-Null

# name, extra arguments. Each entry is one gallery image.
$shots = @(
    @{ name = "piano-roll";    args = "--view pianoroll --theme synthwave --demo" }
    @{ name = "tracker";       args = "--view tracker --theme stock --demo" }
    @{ name = "arrangement";   args = "--view arrangement --theme cyberpunk --demo" }
    @{ name = "mixer";         args = "--view mixer --theme cyberpunk --demo" }
    @{ name = "pad";           args = "--view pad --theme vaporwave --demo" }
    @{ name = "macro-editor";  args = "--view pianoroll --theme stock --demo --show macros" }
    @{ name = "theme-gameboy"; args = "--view pianoroll --theme gameboy --demo" }
    @{ name = "theme-matrix";  args = "--view pianoroll --theme matrix --demo" }
    @{ name = "theme-daylight"; args = "--view pianoroll --theme daylight --demo" }
    @{ name = "theme-terminal"; args = "--view pianoroll --theme terminal --demo" }
)

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
    try {
        $image.Save($png, [System.Drawing.Imaging.ImageFormat]::Png)
    } finally {
        $image.Dispose()
    }
    Remove-Item $bmp -Force

    $size = [math]::Round((Get-Item $png).Length / 1KB)
    Write-Output "$($shot.name).png  (${size} KB)"
}

if ($failed.Count -gt 0) {
    throw "Failed to capture: $($failed -join ', ')"
}

Write-Output ""
Write-Output "Wrote $($shots.Count) images to $OutDir"
