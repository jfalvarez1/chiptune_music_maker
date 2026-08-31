#pragma once

/*
 * ChiptuneTracker - Framebuffer capture
 *
 * Reads the rendered frame straight out of OpenGL rather than grabbing the
 * desktop. Screen capture is at the mercy of whatever else is on screen -
 * overlapping windows, focus changes, the taskbar, a different monitor
 * scale - none of which has anything to do with the app. glReadPixels sees
 * exactly what the app drew and nothing else.
 *
 * Output is a 24-bit BMP: no dependencies, a format every tool reads, and
 * the capture script converts it to a compressed PNG afterwards. Writing a
 * real PNG encoder here would mean a deflate implementation for no benefit,
 * since the conversion step exists anyway.
 *
 * Call captureFramebuffer() after rendering and *before* the buffer swap.
 * Reading after the swap gets undefined contents on most drivers.
 */

#include <GL/gl.h>

#include "Types.h"
#include "Macros.h"

#include <cstdint>
#include <cstdio>
#include <string>
#include <cstdlib>
#include <vector>

namespace ChiptuneTracker {

// ============================================================================
// 24-bit BMP writer
//
// BMP stores rows bottom-up and pads each row to a 4-byte boundary, which
// happens to match how glReadPixels hands back the framebuffer - so the
// rows go out in the order they arrive, with no flip needed.
// ============================================================================
inline bool writeBMP24(const std::string& path, int width, int height,
                       const std::vector<uint8_t>& rgb) {
    if (width <= 0 || height <= 0) return false;

    const size_t expected = static_cast<size_t>(width) * height * 3;
    if (rgb.size() < expected) return false;

    const int rowStride = width * 3;
    const int padding = (4 - (rowStride % 4)) % 4;
    const uint32_t imageBytes = static_cast<uint32_t>((rowStride + padding) * height);
    const uint32_t fileSize = 54u + imageBytes;

    std::FILE* file = std::fopen(path.c_str(), "wb");
    if (!file) return false;

    auto put16 = [&](uint16_t v) { std::fputc(v & 0xFF, file); std::fputc((v >> 8) & 0xFF, file); };
    auto put32 = [&](uint32_t v) {
        std::fputc(v & 0xFF, file);
        std::fputc((v >> 8) & 0xFF, file);
        std::fputc((v >> 16) & 0xFF, file);
        std::fputc((v >> 24) & 0xFF, file);
    };

    // BITMAPFILEHEADER
    std::fputc('B', file);
    std::fputc('M', file);
    put32(fileSize);
    put16(0);
    put16(0);
    put32(54);

    // BITMAPINFOHEADER
    put32(40);
    put32(static_cast<uint32_t>(width));
    put32(static_cast<uint32_t>(height));
    put16(1);       // planes
    put16(24);      // bits per pixel
    put32(0);       // BI_RGB, no compression
    put32(imageBytes);
    put32(2835);    // ~72 DPI
    put32(2835);
    put32(0);
    put32(0);

    const uint8_t padBytes[3] = {0, 0, 0};
    for (int y = 0; y < height; ++y) {
        const uint8_t* row = rgb.data() + static_cast<size_t>(y) * rowStride;
        // BMP wants BGR where OpenGL gave us RGB
        for (int x = 0; x < width; ++x) {
            std::fputc(row[x * 3 + 2], file);
            std::fputc(row[x * 3 + 1], file);
            std::fputc(row[x * 3 + 0], file);
        }
        if (padding > 0) std::fwrite(padBytes, 1, static_cast<size_t>(padding), file);
    }

    const bool ok = (std::ferror(file) == 0);
    std::fclose(file);
    return ok;
}

// ============================================================================
// Capture the current framebuffer
//
// Must be called after rendering and before SwapBuffers.
// ============================================================================
inline bool captureFramebuffer(const std::string& path, int width, int height) {
    if (width <= 0 || height <= 0) return false;

    std::vector<uint8_t> pixels(static_cast<size_t>(width) * height * 3);

    // Default alignment is 4; ask for tightly packed rows so the buffer
    // layout matches what writeBMP24 expects for any width.
    GLint previousAlignment = 4;
    glGetIntegerv(GL_PACK_ALIGNMENT, &previousAlignment);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);

    glReadBuffer(GL_BACK);
    glReadPixels(0, 0, width, height, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());

    glPixelStorei(GL_PACK_ALIGNMENT, previousAlignment);

    if (glGetError() != GL_NO_ERROR) return false;

    return writeBMP24(path, width, height, pixels);
}

// ============================================================================
// Command-line driven capture
//
// Reproducible gallery shots need the app to put itself into a known state
// - a given theme, a given view, a demo song loaded - render a few frames
// so the animated backgrounds and meters have settled, then capture and
// exit. Doing that from the command line means the screenshots can be
// regenerated after any UI change instead of being stale forever.
// ============================================================================
struct CaptureRequest {
    bool enabled = false;
    std::string outputPath;
    std::string themeName;
    std::string viewName;
    std::string workspaceName;
    std::string editMode;        // draw | select | erase
    std::string macroTab;        // volume | arpeggio | duty | pitch
    std::vector<std::string> showWindows;
    bool selectNotes = false;    // pre-select notes, so selection styling shows
    bool startPlaying = false;   // run the transport, so meters and playhead move

    // Capture normally ignores imgui.ini so a gallery shot looks the same
    // wherever it runs. This opts back in, so the layout-repair path - which
    // only triggers when a saved layout exists - can actually be tested.
    bool keepSavedLayout = false;
    int framesToWait = 90;      // ~1.5s at 60fps, enough for meters to settle
    int windowWidth = 1600;
    int windowHeight = 900;
    bool loadDemo = false;

    // A loop range drawn on the arrangement ruler, so the gallery can show
    // the feature rather than an empty strip. Negative means "not set".
    float loopStart = -1.0f;
    float loopEnd = -1.0f;

    // Grid snap division by name, so a shot can show triplets selected.
    std::string snapName;

    // Show the other channels' notes behind the edited pattern.
    bool showGhostNotes = false;

    // Open the Chip Accuracy section, which is collapsed by default.
    bool expandChipPanel = false;

    // Open the Effect Rack, which is collapsed by default.
    bool expandFxRack = false;

    // Put an audio clip on the timeline. The sample is synthesised in
    // memory rather than loaded from disk, so the shot does not depend
    // on a fixture file existing wherever this runs.
    bool addAudioClip = false;

    // Genre focus by name, so a shot can show the palette filtered.
    std::string genreFocus;

    // Window to bring to the front of its tab group. Several panels are
    // docked behind others and could not be photographed at all.
    std::string focusWindow;

    // Force the first-run welcome, which otherwise only appears once
    // ever and so could never be captured.
    bool forceWelcome = false;

    // Load a genre starter template instead of the demo song.
    std::string templateGenre;

    // Open the lesson at a given step (0-based; -1 = off).
    int tutorialStep = -1;
};

// Parses the flags this app understands and ignores anything else.
//
//   --capture <path>     write a BMP and exit
//   --theme <name>       stock|cyberpunk|synthwave|matrix|frutiger|minimal|
//                        vaporwave|terminal|gameboy|daylight
//   --view <name>        pianoroll|tracker|arrangement|mixer|pad
//   --workspace <name>   compose|sounddesign|mix
//   --show <name>        macros|spectrum|midi   (repeatable)
//   --loop <a> <b>       set a loop range on the arrangement ruler
//   --snap <name>        off|bar|1/2|1/4|1/8|1/16|1/32|1/4T|1/8T|1/16T
//   --ghosts             show cross-channel ghost notes
//   --chip-panel         open the Chip Accuracy section
//   --fx-rack            open the Effect Rack section
//   --audio-clip         put an audio clip on the arrangement
//   --focus <window>     bring a docked window to the front of its tabs
//   --welcome            force the first-run genre prompt
//   --template <genre>   load a genre starter template
//   --tutorial <step>    open the lesson at a 0-based step
//   --genre <name>       everything|chiptune|synthwave|hiphop|reggaeton|
//                        edm|rock|lofi
//   --frames <n>         frames to render before capturing
//   --size <w> <h>       window size
//   --demo               populate a short demo song first
inline CaptureRequest parseCaptureArgs(const std::vector<std::string>& args) {
    CaptureRequest request;

    for (size_t i = 0; i < args.size(); ++i) {
        const std::string& arg = args[i];
        auto next = [&](std::string& out) {
            if (i + 1 < args.size()) { out = args[++i]; return true; }
            return false;
        };

        if (arg == "--capture") {
            if (next(request.outputPath)) request.enabled = true;
        } else if (arg == "--theme") {
            next(request.themeName);
        } else if (arg == "--view") {
            next(request.viewName);
        } else if (arg == "--workspace") {
            next(request.workspaceName);
        } else if (arg == "--mode") {
            next(request.editMode);
        } else if (arg == "--macro-tab") {
            next(request.macroTab);
        } else if (arg == "--loop") {
            // --loop <start> <end>, in beats
            std::string startText, endText;
            if (next(startText) && next(endText)) {
                request.loopStart = std::strtof(startText.c_str(), nullptr);
                request.loopEnd = std::strtof(endText.c_str(), nullptr);
            }
        } else if (arg == "--snap") {
            next(request.snapName);
        } else if (arg == "--ghosts") {
            request.showGhostNotes = true;
        } else if (arg == "--chip-panel") {
            request.expandChipPanel = true;
        } else if (arg == "--fx-rack") {
            request.expandFxRack = true;
        } else if (arg == "--audio-clip") {
            request.addAudioClip = true;
        } else if (arg == "--genre") {
            next(request.genreFocus);
        } else if (arg == "--focus") {
            next(request.focusWindow);
        } else if (arg == "--welcome") {
            request.forceWelcome = true;
        } else if (arg == "--template") {
            next(request.templateGenre);
        } else if (arg == "--tutorial") {
            std::string stepText;
            request.tutorialStep = next(stepText)
                ? static_cast<int>(std::strtol(stepText.c_str(), nullptr, 10))
                : 0;
        } else if (arg == "--select") {
            request.selectNotes = true;
        } else if (arg == "--playing") {
            request.startPlaying = true;
        } else if (arg == "--keep-ini") {
            request.keepSavedLayout = true;
        } else if (arg == "--show") {
            std::string window;
            if (next(window)) request.showWindows.push_back(window);
        } else if (arg == "--frames") {
            std::string value;
            if (next(value)) {
                try { request.framesToWait = std::max(1, std::stoi(value)); } catch (...) {}
            }
        } else if (arg == "--size") {
            std::string w, h;
            if (next(w) && next(h)) {
                try {
                    request.windowWidth = std::max(320, std::stoi(w));
                    request.windowHeight = std::max(240, std::stoi(h));
                } catch (...) {}
            }
        } else if (arg == "--demo") {
            request.loadDemo = true;
        }
    }

    return request;
}

// Applies a CaptureRequest to the app state. Kept here beside the parser so
// the flag names and their effects stay in one place.
inline void applyCaptureState(const CaptureRequest& request,
                              Project& project, UIState& ui) {
    // --- Theme ---
    struct ThemeName { const char* name; Theme theme; };
    static const ThemeName themes[] = {
        {"stock",      Theme::Stock},
        {"cyberpunk",  Theme::Cyberpunk},
        {"synthwave",  Theme::Synthwave},
        {"matrix",     Theme::Matrix},
        {"frutiger",   Theme::FrutigerAero},
        {"minimal",    Theme::Minimal},
        {"vaporwave",  Theme::Vaporwave},
        {"terminal",   Theme::RetroTerminal},
        {"gameboy",    Theme::GameBoy},
        {"daylight",   Theme::Daylight},
    };
    for (const ThemeName& entry : themes) {
        if (request.themeName == entry.name) {
            ui.currentTheme = entry.theme;
            break;
        }
    }

    if (request.showGhostNotes) ui.showGhostNotes = true;
    if (request.expandChipPanel) g_ExpandChipAccuracy = true;
    if (request.expandFxRack) g_ExpandEffectRack = true;

    if (request.tutorialStep >= 0) {
        StartTutorial();
        g_Tutorial.step = request.tutorialStep;
    }

    // --- Genre focus ---
    //
    // Matched against the profile names folded to lowercase with spaces
    // dropped, so "Hip Hop" is reachable on a command line as "hiphop".
    if (!request.genreFocus.empty()) {
        auto fold = [](const char* text) {
            std::string out;
            for (const char* c = text; *c != '\0'; ++c) {
                if (*c == ' ') continue;
                out.push_back((*c >= 'A' && *c <= 'Z')
                              ? static_cast<char>(*c - 'A' + 'a') : *c);
            }
            return out;
        };

        for (int i = 0; i < static_cast<int>(Genre::Count); ++i) {
            const Genre candidate = static_cast<Genre>(i);
            if (fold(genreName(candidate)) == request.genreFocus) {
                ui.genre = candidate;
                ApplyGenrePanels(ui);
                break;
            }
        }
    }

    // --- Grid snap ---
    if (!request.snapName.empty()) {
        for (int i = 0; i < static_cast<int>(SnapDivision::Count); ++i) {
            const SnapDivision division = static_cast<SnapDivision>(i);
            if (request.snapName == snapLabel(division)) {
                ui.snapDivision = division;
                break;
            }
        }
    }

    // --- View ---
    struct ViewName { const char* name; ViewMode view; };
    static const ViewName views[] = {
        {"pianoroll",   ViewMode::PianoRoll},
        {"tracker",     ViewMode::Tracker},
        {"arrangement", ViewMode::Arrangement},
        {"mixer",       ViewMode::Mixer},
        {"pad",         ViewMode::PadController},
    };
    for (const ViewName& entry : views) {
        if (request.viewName == entry.name) {
            ui.currentView = entry.view;
            break;
        }
    }

    // --- Workspace ---
    struct WorkspaceName { const char* name; int index; };
    static const WorkspaceName workspaces[] = {
        {"compose", 0}, {"sounddesign", 1}, {"mix", 2},
    };
    for (const WorkspaceName& entry : workspaces) {
        if (request.workspaceName == entry.name) {
            ui.currentWorkspace = entry.index;
            break;
        }
    }

    // --- Piano roll edit mode ---
    if (request.editMode == "draw")   ui.pianoRollMode = PianoRollMode::Draw;
    if (request.editMode == "select") ui.pianoRollMode = PianoRollMode::Select;
    if (request.editMode == "erase")  ui.pianoRollMode = PianoRollMode::Erase;

    // --- Macro editor tab ---
    if (request.macroTab == "volume")   ui.macroEditorTab = 0;
    if (request.macroTab == "arpeggio") ui.macroEditorTab = 1;
    if (request.macroTab == "duty")     ui.macroEditorTab = 2;
    if (request.macroTab == "pitch")    ui.macroEditorTab = 3;

    // --- Extra windows ---
    for (const std::string& window : request.showWindows) {
        if (window == "macros")    ui.showMacroEditor = true;
        if (window == "spectrum")  ui.showSpectrumAnalyzer = true;
        if (window == "automation") ui.showAutomation = true;
        if (window == "midi")      ui.showMIDIInput = true;
        if (window == "wavetable") ui.showWavetableEditor = true;
    }

    // --- Demo content ---
    //
    // An empty piano roll makes for a poor screenshot. Lay down a short
    // chiptune phrase - a pulse lead, a bass line and a drum pattern - so
    // the gallery shows the app doing its job.
    if (request.loadDemo) {
        project.name = "Demo";
        project.bpm = 140.0f;
        project.patterns.clear();

        // One pattern per part, each on its own channel. This used to be a
        // single pattern holding the lead, the bass and the drums together,
        // placed on four channels - so every channel played everything,
        // which is not how the program is used and made the arrangement
        // screenshot meaningless. It also hid the ghost notes, since every
        // ghost sat exactly behind its own real note.
        auto addNote = [](Pattern& pattern, int pitch, float start,
                          float duration, OscillatorType osc, float velocity) {
            Note note;
            note.pitch = pitch;
            note.startTime = start;
            note.duration = duration;
            note.oscillatorType = osc;
            note.velocity = velocity;
            pattern.notes.push_back(note);
        };

        // Lead - a rising then falling arpeggio figure
        Pattern lead;
        lead.name = "Lead";
        lead.length = 16;
        const int leadPitches[] = {72, 76, 79, 84, 79, 76, 72, 69};
        for (int i = 0; i < 8; ++i) {
            addNote(lead, leadPitches[i], float(i) * 0.5f, 0.45f,
                    OscillatorType::SynthwaveLead, 0.85f);
        }

        // Harmony - a counter-line a third under the lead, on its own
        // channel. This is the case ghost notes exist for: writing one part
        // against another that lives in the same register, where the piano
        // roll can only show you one of them at a time.
        Pattern harmony;
        harmony.name = "Harmony";
        harmony.length = 16;
        const int harmonyPitches[] = {67, 72, 76, 79, 76, 72, 67, 64};
        for (int i = 0; i < 8; ++i) {
            addNote(harmony, harmonyPitches[i], float(i) * 0.5f, 0.45f,
                    OscillatorType::Pulse, 0.7f);
        }

        // Bass - root notes on the beat
        Pattern bass;
        bass.name = "Bass";
        bass.length = 16;
        const int bassPitches[] = {36, 36, 43, 41};
        for (int i = 0; i < 4; ++i) {
            addNote(bass, bassPitches[i], float(i), 0.9f,
                    OscillatorType::SynthBass, 0.9f);
        }

        // Drums - kick on the beat, hats on the offbeat, snare on 2 and 4
        Pattern drums;
        drums.name = "Drums";
        drums.length = 16;
        for (int i = 0; i < 4; ++i) {
            addNote(drums, 36, float(i), 0.25f, OscillatorType::Kick808, 1.0f);
            addNote(drums, 42, float(i) + 0.5f, 0.15f, OscillatorType::HiHat, 0.5f);
        }
        addNote(drums, 38, 1.0f, 0.25f, OscillatorType::Snare, 0.9f);
        addNote(drums, 38, 3.0f, 0.25f, OscillatorType::Snare, 0.9f);

        project.patterns.push_back(lead);
        project.patterns.push_back(harmony);
        project.patterns.push_back(bass);
        project.patterns.push_back(drums);

        project.arrangement.clear();
        project.arrangement.push_back(Clip{0, 0, 0.0f, 4.0f, 0xFF4488FFu});
        project.arrangement.push_back(Clip{1, 1, 0.0f, 4.0f, 0xFFAA66FFu});
        project.arrangement.push_back(Clip{2, 2, 0.0f, 4.0f, 0xFF44CC88u});
        project.arrangement.push_back(Clip{3, 4, 0.0f, 4.0f, 0xFFCC6644u});

        // The bass pattern again, an octave up on the free channel - one
        // pattern, two sounds. It puts the "+12" label in every arrangement
        // screenshot and means every smoke case plays a transposed clip.
        {
            Clip octave{2, 3, 0.0f, 4.0f, 0xFF66AACCu};
            octave.transpose = 12;
            project.arrangement.push_back(octave);
        }

        // Give the first channel a macro so the macro editor screenshot has
        // something in it
        project.channels[0].macros = makeMajorChordArp();
    }

    // --- An audio clip on the timeline ---
    //
    // The waveform drawing is the part the headless suite cannot reach:
    // there is no window. It is also the part most likely to go wrong, since
    // it walks screen pixels back into sample frames and could divide by
    // zero or index off the end. The sample is built here rather than read
    // from disk so the shot does not depend on a fixture file.
    if (request.addAudioClip) {
        Sample sample;
        sample.name = "take_01.wav";
        sample.filepath = "take_01.wav";
        sample.sampleRate = 48000;

        // Two seconds of a decaying tone with four transients in it, so the
        // drawn envelope has visible structure rather than a flat block -
        // a waveform that renders as a rectangle would pass a "did it draw"
        // check while telling the user nothing.
        const int frames = sample.sampleRate * 2;
        sample.audioData.resize(static_cast<size_t>(frames));
        for (int i = 0; i < frames; ++i) {
            const float t = float(i) / float(sample.sampleRate);
            const float intoHit = std::fmod(t, 0.5f);
            const float envelope = std::exp(-intoHit * 9.0f);
            sample.audioData[static_cast<size_t>(i)] =
                0.85f * envelope * std::sin(6.28318530718f * 180.0f * t);
        }
        sample.lengthSeconds = float(frames) / float(sample.sampleRate);
        sample.loopEndSeconds = sample.lengthSeconds;
        sample.isLoaded = true;

        const int id = project.samplePool.addSample(sample);
        if (id >= 0) {
            Clip clip;
            clip.type = ClipType::Audio;
            clip.sampleId = id;
            clip.channelIndex = std::min(5, project.activeChannelCount() - 1);
            clip.startBeat = 0.0f;
            clip.lengthBeats = 8.0f;
            clip.gain = 1.0f;
            clip.fadeInBeats = 1.0f;    // so the fade triangles draw too
            clip.fadeOutBeats = 1.5f;
            clip.loopClip = true;
            project.arrangement.push_back(clip);

            // And one whose sample is gone, which draws the placeholder
            // instead - the path a user hits after moving a folder, and the
            // one most likely to dereference a null Sample.
            Clip broken;
            broken.type = ClipType::Audio;
            broken.sampleId = -1;
            broken.channelIndex = std::min(6, project.activeChannelCount() - 1);
            broken.startBeat = 9.0f;
            broken.lengthBeats = 4.0f;
            project.arrangement.push_back(broken);
            project.missingSamples.push_back("D:/moved/vocal_take.wav");
        }
    }

    // Selecting a few notes makes the Note Editor populate and the selected
    // note styling visible - both invisible in an untouched screenshot.
    if (request.selectNotes && !project.patterns.empty()) {
        const int noteCount = static_cast<int>(project.patterns[0].notes.size());
        ui.selectedNoteIndices.clear();
        for (int i = 0; i < noteCount && i < 4; ++i) {
            ui.selectedNoteIndices.push_back(i);
        }
        if (noteCount > 0) ui.selectedNoteIndex = 0;
    }
}

// Splits a Windows command line on spaces, honouring double quotes so a
// path with a space in it survives.
inline std::vector<std::string> splitCommandLine(const char* commandLine) {
    std::vector<std::string> args;
    if (!commandLine) return args;

    std::string current;
    bool inQuotes = false;

    for (const char* p = commandLine; *p; ++p) {
        if (*p == '"') {
            inQuotes = !inQuotes;
        } else if (*p == ' ' && !inQuotes) {
            if (!current.empty()) { args.push_back(current); current.clear(); }
        } else {
            current += *p;
        }
    }
    if (!current.empty()) args.push_back(current);

    return args;
}

} // namespace ChiptuneTracker
