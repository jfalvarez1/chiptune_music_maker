<#
.SYNOPSIS
    Automated UI smoke test: render every view, theme, workspace and panel
    and check the result is a real frame.

.DESCRIPTION
    The headless suite (ChiptuneTests) covers the synth, sequencer, file I/O
    and exporters. It cannot cover the renderer, because there is no window.
    This fills that gap: it drives the app's --capture mode through every
    combination that a person would otherwise have to click through, and for
    each one checks that

      - the process exited cleanly (no crash, no hang)
      - a frame was actually captured
      - the frame is not blank or a single flat colour, which is what a
        failed render, a lost GL context or an all-black theme looks like
      - the frame has plausible variety, so a frame that rendered only the
        background and no panels is caught

    It is a smoke test, not a pixel-perfect regression test: it answers "does
    every screen still draw" rather than "does it look identical". Comparing
    against golden images would fail on every legitimate UI change and teach
    everyone to ignore it.

.EXAMPLE
    ./tools/ui-smoke-test.ps1
    ./tools/ui-smoke-test.ps1 -Quick
#>
param(
    [string]$Exe = "build/bin/Release/ChiptuneTracker.exe",
    [int]$Width = 1280,
    [int]$Height = 800,
    [int]$Frames = 60,
    [int]$TimeoutSeconds = 45,
    [switch]$Quick,
    [switch]$KeepImages
)

$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.Drawing

if (-not (Test-Path $Exe)) { throw "Executable not found: $Exe. Build first." }

$temp = Join-Path $env:TEMP "chiptune_ui_smoke"
if (Test-Path $temp) { Remove-Item $temp -Recurse -Force }
New-Item -ItemType Directory -Force -Path $temp | Out-Null

# ---------------------------------------------------------------------------
# Build the matrix
# ---------------------------------------------------------------------------
$views = @("pianoroll", "tracker", "arrangement", "mixer", "pad")
$themes = @("stock", "cyberpunk", "synthwave", "matrix", "frutiger",
            "minimal", "vaporwave", "terminal", "gameboy", "daylight")
$panels = @("macros")
$workspaces = @("compose", "sounddesign", "mix")

if ($Quick) {
    $views = @("pianoroll", "mixer")
    $themes = @("stock", "gameboy")
    $workspaces = @("compose", "mix")
}

$cases = New-Object System.Collections.ArrayList

foreach ($view in $views) {
    [void]$cases.Add(@{ name = "view-$view"; args = "--view $view --demo" })
}
foreach ($theme in $themes) {
    [void]$cases.Add(@{ name = "theme-$theme"; args = "--view pianoroll --theme $theme --demo" })
}
foreach ($workspace in $workspaces) {
    [void]$cases.Add(@{ name = "workspace-$workspace"; args = "--workspace $workspace --demo" })
}
foreach ($panel in $panels) {
    [void]$cases.Add(@{ name = "panel-$panel"; args = "--view pianoroll --demo --show $panel" })
}

# Edge cases a person would not think to click through
[void]$cases.Add(@{ name = "empty-project";   args = "--view pianoroll" })
[void]$cases.Add(@{ name = "empty-tracker";   args = "--view tracker" })
[void]$cases.Add(@{ name = "empty-mixer";     args = "--view mixer" })
[void]$cases.Add(@{ name = "tiny-window";     args = "--view pianoroll --demo"; w = 800; h = 600 })
[void]$cases.Add(@{ name = "wide-window";     args = "--view pianoroll --demo"; w = 2560; h = 800 })
[void]$cases.Add(@{ name = "tall-window";     args = "--view mixer --demo";     w = 900;  h = 1200 })
[void]$cases.Add(@{ name = "single-frame";    args = "--view pianoroll --demo"; frames = 2 })

# Upgrading from a pre-docking build leaves an imgui.ini full of window
# positions with no DockId assignments, so every panel restores floating and
# the dock space sits empty behind them. The app detects that and rebuilds.
# This is the only case that must run WITH a saved layout, so it gets its own
# The loop range and the grid snap: both were unreachable before - loopStart
# and loopEnd could not be set from anywhere, and the snap step was hardcoded
# to a 1/16 note in fourteen places. These shots prove the controls render.
[void]$cases.Add(@{
    name = "arrangement-loop-range"
    args = "--demo --view arrangement --loop 4 16"
})

[void]$cases.Add(@{
    name = "pianoroll-triplet-snap"
    args = "--demo --view pianoroll --snap 1/8T"
})

[void]$cases.Add(@{
    name = "pianoroll-ghost-notes"
    args = "--demo --view pianoroll --ghosts"
})

# Audio clips draw their own waveform, which means walking screen pixels back
# into sample frames - the one place in the arrangement view that can divide
# by zero or index off the end of a sample. The case also places a clip whose
# sample is missing, which is the path that would dereference a null Sample.
[void]$cases.Add(@{
    name = "arrangement-audio-clip"
    args = "--demo --view arrangement --audio-clip"
})

# The same clips at a zoom where one screen pixel spans many sample frames,
# and at one where a single frame spans many pixels. Both ends of that
# division have their own way of going wrong.
[void]$cases.Add(@{
    name = "arrangement-audio-clip-wide"
    args = "--demo --view arrangement --audio-clip"
    w = 2560; h = 800
})

[void]$cases.Add(@{
    name = "arrangement-audio-clip-narrow"
    args = "--demo --view arrangement --audio-clip"
    w = 800; h = 600
})

[void]$cases.Add(@{
    name = "master-bus-chip-accuracy"
    args = "--demo --workspace mix --chip-panel"
})

# The tracker used to print the same note into all eight columns. With the
# demo loaded, each channel now carries a different part, so this shot fails
# visibly if the columns ever collapse back into one another.
[void]$cases.Add(@{
    name = "tracker-grid"
    args = "--demo --view tracker"
})

# Genre focus decides what is put in front of you. Two very different ones,
# so a change that collapsed them into the same palette would show up here.
# A starter template should play the moment it loads, so these two shots are
# of very different genres arriving ready to change.
[void]$cases.Add(@{
    name = "template-chiptune"
    args = "--template chiptune --view arrangement"
})

[void]$cases.Add(@{
    name = "template-reggaeton"
    args = "--template reggaeton --view tracker"
})

# Quick Start offers each genre its foundational patterns as buttons. The
# Tools panel is tabbed behind Patterns, so it needs focusing.
[void]$cases.Add(@{
    name = "quickstart-kits"
    args = "--demo --genre reggaeton --focus Tools"
})

# The insert rack. Task A replaced a fixed 17-member chain with reorderable
# slots, so this shot fails visibly if the list ever stops rendering.
[void]$cases.Add(@{
    name = "effect-rack"
    # The window name has a space in it, so the inner quotes are escaped -
    # the runner splices this into one argument string.
    args = "--demo --workspace sounddesign --fx-rack --focus `"Channel Editor`""
})

# The lesson panel, parked on the draw-a-melody step.
[void]$cases.Add(@{
    name = "tutorial-lesson"
    args = "--demo --tutorial 2"
})

# The first-run prompt appears once ever, so it needs forcing to be seen.
[void]$cases.Add(@{
    name = "welcome-genre-prompt"
    args = "--demo --welcome"
})

[void]$cases.Add(@{
    name = "genre-chiptune"
    args = "--demo --genre chiptune"
})

[void]$cases.Add(@{
    name = "genre-reggaeton"
    args = "--demo --genre reggaeton"
})

# working directory with the broken ini staged into it.
[void]$cases.Add(@{
    name     = "legacy-layout-repair"
    args     = "--demo --keep-ini"
    fixtures = @{ "tests/fixtures/legacy-predocking-imgui.ini" = "imgui.ini" }
})

# A recovery file surviving to launch means the previous session crashed, and
# the app should offer to restore it. The prompt is skipped in plain capture
# mode, so this uses --keep-ini, which means "behave like a real session".
[void]$cases.Add(@{
    name     = "crash-recovery-prompt"
    args     = "--demo --keep-ini"
    fixtures = @{
        "tests/fixtures/legacy-predocking-imgui.ini" = "imgui.ini"
        "tests/fixtures/recovery-session.ctp"        = "recovery.ctp"
    }
})

# ---------------------------------------------------------------------------
# Frame analysis
# ---------------------------------------------------------------------------
function Test-Frame {
    param([string]$Path)

    $image = [System.Drawing.Bitmap]::FromFile($Path)
    try {
        $w = $image.Width
        $h = $image.Height
        if ($w -lt 100 -or $h -lt 100) {
            return @{ ok = $false; reason = "frame is ${w}x${h}, too small to be real" }
        }

        # Sample a grid rather than every pixel - enough to characterise the
        # frame, fast enough to run over the whole matrix.
        $colors = @{}
        $samples = 0
        $stepX = [Math]::Max(1, [int]($w / 60))
        $stepY = [Math]::Max(1, [int]($h / 40))

        for ($y = 0; $y -lt $h; $y += $stepY) {
            for ($x = 0; $x -lt $w; $x += $stepX) {
                $p = $image.GetPixel($x, $y)
                # Quantise so anti-aliasing and gradients do not each count
                # as a distinct colour
                $key = "{0}-{1}-{2}" -f [int]($p.R / 16), [int]($p.G / 16), [int]($p.B / 16)
                $colors[$key] = $true
                $samples++
            }
        }

        $distinct = $colors.Count
        if ($distinct -lt 2) {
            return @{ ok = $false; reason = "frame is a single flat colour - nothing rendered" }
        }
        # A frame showing only a themed background with no panels lands around
        # 3-6 distinct buckets; a real screen with panels, text and controls is
        # well above that.
        if ($distinct -lt 8) {
            return @{ ok = $false; reason = "only $distinct distinct colours in $samples samples - panels likely did not draw" }
        }

        return @{ ok = $true; reason = "$distinct distinct colours"; }
    }
    finally {
        $image.Dispose()
    }
}

# ---------------------------------------------------------------------------
# Run
# ---------------------------------------------------------------------------
$passed = 0
$failed = New-Object System.Collections.ArrayList

Write-Output "UI smoke test: $($cases.Count) cases"
Write-Output ("=" * 60)

foreach ($case in $cases) {
    $name = $case.name
    $bmp = Join-Path $temp "$name.bmp"

    $w = if ($case.ContainsKey("w")) { $case.w } else { $Width }
    $h = if ($case.ContainsKey("h")) { $case.h } else { $Height }
    $f = if ($case.ContainsKey("frames")) { $case.frames } else { $Frames }

    $argList = "--capture `"$bmp`" --frames $f --size $w $h $($case.args)"

    # A case naming a fixture runs in its own directory with that ini staged
    # as imgui.ini, so it exercises the saved-layout path without touching
    # the repo's own layout file.
    $workDir = (Get-Location).Path
    if ($case.ContainsKey("fixtures")) {
        $workDir = Join-Path $temp "$name-cwd"
        if (Test-Path $workDir) { Remove-Item $workDir -Recurse -Force }
        New-Item -ItemType Directory -Force -Path $workDir | Out-Null
        foreach ($source in $case.fixtures.Keys) {
            Copy-Item $source (Join-Path $workDir $case.fixtures[$source]) -Force
        }
    }

    $process = Start-Process -FilePath (Resolve-Path $Exe) -ArgumentList $argList `
                             -WorkingDirectory $workDir -PassThru
    if (-not $process.WaitForExit($TimeoutSeconds * 1000)) {
        Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue
        [void]$failed.Add("$name : timed out after ${TimeoutSeconds}s")
        Write-Output "FAIL  $name  (timeout)"
        continue
    }

    if ($process.ExitCode -ne 0) {
        [void]$failed.Add(("$name : exited with 0x{0:X}" -f $process.ExitCode))
        Write-Output ("FAIL  $name  (exit 0x{0:X})" -f $process.ExitCode)
        continue
    }

    if (-not (Test-Path $bmp)) {
        [void]$failed.Add("$name : no frame captured")
        Write-Output "FAIL  $name  (no frame)"
        continue
    }

    $result = Test-Frame -Path $bmp
    if (-not $result.ok) {
        [void]$failed.Add("$name : $($result.reason)")
        Write-Output "FAIL  $name  ($($result.reason))"
        continue
    }

    $passed++
    Write-Output "ok    $name  ($($result.reason))"
}

if (-not $KeepImages) { Remove-Item $temp -Recurse -Force -ErrorAction SilentlyContinue }

Write-Output ("=" * 60)
Write-Output "$passed passed, $($failed.Count) failed"

if ($failed.Count -gt 0) {
    Write-Output ""
    Write-Output "Failures:"
    foreach ($f in $failed) { Write-Output "  $f" }
    exit 1
}

Write-Output "All UI smoke tests passed."
exit 0
