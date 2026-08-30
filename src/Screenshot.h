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
    std::vector<std::string> showWindows;
    int framesToWait = 90;      // ~1.5s at 60fps, enough for meters to settle
    int windowWidth = 1600;
    int windowHeight = 900;
    bool loadDemo = false;
};

// Parses the flags this app understands and ignores anything else.
//
//   --capture <path>     write a BMP and exit
//   --theme <name>       stock|cyberpunk|synthwave|matrix|frutiger|minimal|
//                        vaporwave|terminal|gameboy|daylight
//   --view <name>        pianoroll|tracker|arrangement|mixer|pad
//   --show <name>        macros|spectrum|midi   (repeatable)
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

    // --- Extra windows ---
    for (const std::string& window : request.showWindows) {
        if (window == "macros") ui.showMacroEditor = true;
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

        Pattern pattern;
        pattern.name = "Demo";
        pattern.length = 16;

        auto addNote = [&](int pitch, float start, float duration,
                           OscillatorType osc, float velocity) {
            Note note;
            note.pitch = pitch;
            note.startTime = start;
            note.duration = duration;
            note.oscillatorType = osc;
            note.velocity = velocity;
            pattern.notes.push_back(note);
        };

        // Lead - a rising then falling arpeggio figure
        const int leadPitches[] = {72, 76, 79, 84, 79, 76, 72, 69};
        for (int i = 0; i < 8; ++i) {
            addNote(leadPitches[i], float(i) * 0.5f, 0.45f,
                    OscillatorType::SynthwaveLead, 0.85f);
        }

        // Bass - root notes on the beat
        const int bassPitches[] = {36, 36, 43, 41};
        for (int i = 0; i < 4; ++i) {
            addNote(bassPitches[i], float(i), 0.9f, OscillatorType::SynthBass, 0.9f);
        }

        // Drums - kick on the beat, hats on the offbeat, snare on 2 and 4
        for (int i = 0; i < 4; ++i) {
            addNote(36, float(i), 0.25f, OscillatorType::Kick808, 1.0f);
            addNote(42, float(i) + 0.5f, 0.15f, OscillatorType::HiHat, 0.5f);
        }
        addNote(38, 1.0f, 0.25f, OscillatorType::Snare, 0.9f);
        addNote(38, 3.0f, 0.25f, OscillatorType::Snare, 0.9f);

        project.patterns.push_back(pattern);

        project.arrangement.clear();
        for (int ch = 0; ch < 4; ++ch) {
            project.arrangement.push_back(Clip{0, ch, 0.0f, 4.0f, 0xFF4488FFu});
        }

        // Give the first channel a macro so the macro editor screenshot has
        // something in it
        project.channels[0].macros = makeMajorChordArp();
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
