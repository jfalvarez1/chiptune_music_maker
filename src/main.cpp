/*
 * ChiptuneTracker - Main Entry Point
 *
 * Full DAW application with:
 *   - Piano Roll Editor
 *   - Tracker View
 *   - Arrangement Timeline
 *   - 8-Channel Mixer
 *   - Comprehensive Effects Suite
 */

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <GL/gl.h>

#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_opengl3.h"

#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

#include "Types.h"
#include "Sequencer.h"
#include "UI.h"
#include "MacroEditorUI.h"
#include "Version.h"
#include "Layout.h"
#include "Screenshot.h"
#include "CrashHandler.h"
#include "Autosave.h"

#include <cstdio>
#include <memory>
#include <atomic>
#include <vector>

// OpenGL function loading
typedef HGLRC (WINAPI* PFNWGLCREATECONTEXTATTRIBSARBPROC)(HDC, HGLRC, const int*);
PFNWGLCREATECONTEXTATTRIBSARBPROC wglCreateContextAttribsARB = nullptr;

// Forward declarations
LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// Global state
static bool g_Running = true;

// Read by the audio thread, written by the main thread. Atomic because a
// plain pointer shared across threads is a data race regardless of how
// benign it looks on x86.
static std::atomic<ChiptuneTracker::Sequencer*> g_Sequencer{nullptr};

// Deinterleave scratch for the audio callback. Allocated once at startup:
// the callback used to build two std::vectors per call, which is roughly 86
// heap allocations a second on the real-time thread, and malloc can block.
static constexpr ma_uint32 MAX_AUDIO_FRAMES = 4096;
static std::vector<float> g_AudioLeft;
static std::vector<float> g_AudioRight;

// Set when a layout was restored from imgui.ini, so it is worth checking
// whether that layout actually docks anything. See the health check below.
static bool g_CheckSavedLayout = false;

// Screenshot state. F12 captures the rendered frame to screenshots/.
static bool g_CaptureRequested = false;
static int g_CaptureCounter = 0;

// ============================================================================
// Miniaudio Callback
// ============================================================================
void audioCallback(ma_device* pDevice, void* pOutput, const void* /*pInput*/, ma_uint32 frameCount) {
    (void)pDevice;
    float* output = static_cast<float*>(pOutput);

    ChiptuneTracker::Sequencer* sequencer = g_Sequencer.load(std::memory_order_acquire);

    // Silence if we have no sequencer, or if the device asked for a block
    // larger than the scratch buffers - better a gap than a heap allocation
    // on this thread or a write past the end of the buffer.
    if (sequencer == nullptr || frameCount > g_AudioLeft.size()) {
        std::fill_n(output, static_cast<size_t>(frameCount) * 2, 0.0f);
        return;
    }

    sequencer->process(g_AudioLeft.data(), g_AudioRight.data(), frameCount);

    for (ma_uint32 i = 0; i < frameCount; ++i) {
        output[i * 2 + 0] = g_AudioLeft[i];
        output[i * 2 + 1] = g_AudioRight[i];
    }
}

// ============================================================================
// WinMain Entry Point
// ============================================================================
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE /*hPrevInstance*/, LPSTR lpCmdLine, int nCmdShow) {

    // First thing, so a crash during startup still reports itself.
    ChiptuneTracker::installCrashHandler();

    // Screenshot automation. Parsed first because it decides the window size.
    // See Screenshot.h for the flags.
    const ChiptuneTracker::CaptureRequest captureRequest =
        ChiptuneTracker::parseCaptureArgs(ChiptuneTracker::splitCommandLine(lpCmdLine));
    int captureFrameCounter = 0;

    // Allocate console for debugging
    AllocConsole();
    freopen("CONOUT$", "w", stdout);
    freopen("CONOUT$", "w", stderr);
    printf("ChiptuneTracker starting...\n");

    // ========================================================================
    // Create Window Class
    // ========================================================================
    const char* CLASS_NAME = "ChiptuneTrackerWindowClass";

    WNDCLASSA wc = {};
    wc.lpfnWndProc   = WindowProc;
    wc.hInstance     = hInstance;
    wc.lpszClassName = CLASS_NAME;
    wc.style         = CS_OWNDC | CS_HREDRAW | CS_VREDRAW;
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);

    if (!RegisterClassA(&wc)) {
        MessageBoxA(nullptr, "Failed to register window class", "Error", MB_OK | MB_ICONERROR);
        return 1;
    }
    printf("Window class registered\n");

    // ========================================================================
    // Create Window
    // ========================================================================
    HWND hwnd = CreateWindowExA(
        0,
        CLASS_NAME,
        ChiptuneTracker::windowTitle().c_str(),
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT,
        captureRequest.enabled ? captureRequest.windowWidth : 1600,
        captureRequest.enabled ? captureRequest.windowHeight : 900,
        nullptr,
        nullptr,
        hInstance,
        nullptr
    );

    if (!hwnd) {
        MessageBoxA(nullptr, "Failed to create window", "Error", MB_OK | MB_ICONERROR);
        return 1;
    }
    printf("Window created\n");

    // ========================================================================
    // Initialize OpenGL Context
    // ========================================================================
    HDC hdc = GetDC(hwnd);

    PIXELFORMATDESCRIPTOR pfd = {};
    pfd.nSize        = sizeof(pfd);
    pfd.nVersion     = 1;
    pfd.dwFlags      = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType   = PFD_TYPE_RGBA;
    pfd.cColorBits   = 32;
    pfd.cDepthBits   = 24;
    pfd.cStencilBits = 8;

    int pixelFormat = ChoosePixelFormat(hdc, &pfd);
    if (!pixelFormat) {
        MessageBoxA(nullptr, "Failed to choose pixel format", "Error", MB_OK | MB_ICONERROR);
        return 1;
    }
    SetPixelFormat(hdc, pixelFormat, &pfd);

    HGLRC tempContext = wglCreateContext(hdc);
    wglMakeCurrent(hdc, tempContext);

    wglCreateContextAttribsARB = (PFNWGLCREATECONTEXTATTRIBSARBPROC)
        wglGetProcAddress("wglCreateContextAttribsARB");

    HGLRC glContext = nullptr;
    if (wglCreateContextAttribsARB) {
        int attribs[] = {
            0x2091, 3,  // WGL_CONTEXT_MAJOR_VERSION_ARB
            0x2092, 3,  // WGL_CONTEXT_MINOR_VERSION_ARB
            0x9126, 0x00000001,  // WGL_CONTEXT_PROFILE_MASK_ARB, CORE
            0
        };
        glContext = wglCreateContextAttribsARB(hdc, nullptr, attribs);
    }

    if (!glContext) {
        glContext = tempContext;
    } else {
        wglMakeCurrent(nullptr, nullptr);
        wglDeleteContext(tempContext);
        wglMakeCurrent(hdc, glContext);
    }
    printf("OpenGL context created\n");

    // ========================================================================
    // Initialize Dear ImGui
    // ========================================================================
    printf("Creating ImGui context...\n");
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    printf("ImGui context created\n");
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    // Docking. Panels dock into shared regions and become tabs when they
    // share one - the model Reaper's Docker and Furnace both use, and the
    // reason panels can no longer overlap or fall off screen.
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.ConfigDockingWithShift = false;

    // Screenshots must not inherit whatever window layout this machine
    // happens to have saved. Disabling the ini file makes capture mode use
    // the app's own default positions, so a gallery shot looks the same
    // wherever it is generated.
    if (captureRequest.enabled && !captureRequest.keepSavedLayout) {
        io.IniFilename = nullptr;
    }

    // Apply default theme
    ImGui::StyleColorsDark();

    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplOpenGL3_Init("#version 130");
    printf("ImGui backends initialized\n");

    // ========================================================================
    // Initialize Audio
    // ========================================================================
    printf("Initializing audio...\n");

    // Sized before the device exists, so the first callback already has it.
    g_AudioLeft.assign(MAX_AUDIO_FRAMES, 0.0f);
    g_AudioRight.assign(MAX_AUDIO_FRAMES, 0.0f);

    ma_device_config config = ma_device_config_init(ma_device_type_playback);
    config.playback.format   = ma_format_f32;
    config.playback.channels = 2;
    config.sampleRate        = 44100;
    config.dataCallback      = audioCallback;
    config.periodSizeInFrames = 512;

    ma_device device;
    if (ma_device_init(nullptr, &config, &device) != MA_SUCCESS) {
        MessageBoxA(nullptr, "Failed to initialize audio device", "Error", MB_OK | MB_ICONERROR);
        return 1;
    }
    printf("Audio device initialized\n");

    // ========================================================================
    // Initialize Tracker
    // ========================================================================
    // These two are large (the Sequencer holds eight synths, each with a full
    // effects chain and its delay buffers). Give them static storage so they
    // never sit on WinMain's 1MB stack, no matter how the DSP grows.
    printf("Creating project...\n");
    static ChiptuneTracker::Project project;
    printf("Project created\n");
    printf("Creating sequencer...\n");
    static ChiptuneTracker::Sequencer sequencer;
    printf("Sequencer created\n");
    ChiptuneTracker::UIState uiState;
    bool showAboutDialog = false;

    // Autosave writes beside the user's file, never over it. A recovery file
    // surviving to the next launch is exactly the signal that the last
    // session ended badly - a clean exit deletes it.
    static ChiptuneTracker::Autosave autosave;
    autosave.setDirectory(".");
    bool showRecoveryPrompt = false;
    std::string recoveryAge;
    if (!captureRequest.enabled && autosave.hasRecoverableSession()) {
        showRecoveryPrompt = true;
        recoveryAge = autosave.describeRecoveryAge();
        printf("Recovery file found from %s\n", recoveryAge.c_str());
    }
    ChiptuneTracker::PlaybackState playbackState;

    sequencer.setSampleRate(44100.0f);
    sequencer.setProject(&project);
    sequencer.setLoop(false, 0.0f, 16.0f);  // Don't loop by default - stop at end

    g_Sequencer.store(&sequencer, std::memory_order_release);

    // Start with empty pattern (no demo noise)
    uiState.selectedPattern = 0;
    uiState.selectedChannel = 0;

    // Lay the windows out on first run. Without this the app opens with
    // nineteen windows at their individual defaults, overlapping each other
    // and spilling off the bottom of the display.
    {
        const bool hasSavedLayout = (io.IniFilename != nullptr) &&
                                    (GetFileAttributesA(io.IniFilename) != INVALID_FILE_ATTRIBUTES);
        // --keep-ini deliberately keeps whatever layout is on disk, so the
        // repair path has something broken to repair.
        if (!hasSavedLayout || (captureRequest.enabled && !captureRequest.keepSavedLayout)) {
            uiState.pendingLayoutFrames = 3;
        } else {
            // A layout was loaded from disk and we are not overriding it, so
            // it is worth checking whether it actually contains one. This is
            // the only case the repair exists for; running it after we built
            // the tree ourselves would be inspecting our own work.
            g_CheckSavedLayout = true;
        }
    }

    // Screenshot automation: put the app into the requested state before the
    // first frame, so a gallery shot is reproducible from the command line
    // rather than depending on someone clicking the right menus.
    if (captureRequest.enabled) {
        ChiptuneTracker::applyCaptureState(captureRequest, project, uiState);
        // The demo song is built after setProject(), so the synths need
        // re-syncing before anything is rendered or heard.
        sequencer.updateChannelConfigs();
        if (captureRequest.startPlaying) {
            sequencer.play();
        }
    }

    // Apply the current visual theme (Stock by default)
    ChiptuneTracker::ApplyTheme(uiState.currentTheme);

    // Start audio
    if (ma_device_start(&device) != MA_SUCCESS) {
        MessageBoxA(nullptr, "Failed to start audio device", "Error", MB_OK | MB_ICONERROR);
        ma_device_uninit(&device);
        return 1;
    }

    // ========================================================================
    // Show Window
    // ========================================================================
    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    // ========================================================================
    // Main Loop
    // ========================================================================
    while (g_Running) {
        MSG msg;
        while (PeekMessageA(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                g_Running = false;
            }
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }

        if (!g_Running) break;

        // Start ImGui frame
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        // Draw theme background effects (Matrix rain, Synthwave chasers, etc.)
        ChiptuneTracker::DrawThemeBackground(uiState.currentTheme, io.DeltaTime);

        // Get playback state
        playbackState = sequencer.getState();

        // ====================================================================
        // Main Menu Bar
        // ====================================================================
        if (ImGui::BeginMainMenuBar()) {
            if (ImGui::BeginMenu("File")) {
                if (ImGui::MenuItem("New Project")) {
                    project = ChiptuneTracker::Project();
                    sequencer.setProject(&project);
                }
                if (ImGui::MenuItem("Save Project", "Ctrl+S")) {
                    if (uiState.projectFilePath.empty()) {
                        std::string path = ChiptuneTracker::saveFileDialog(
                            "Chiptune Projects (*.ctp)\0*.ctp\0",
                            "ctp");
                        if (!path.empty()) {
                            uiState.projectFilePath = path;
                            ChiptuneTracker::saveProject(project, path);
                        }
                    } else {
                        ChiptuneTracker::saveProject(project, uiState.projectFilePath);
                    }
                }
                if (ImGui::MenuItem("Load Project", "Ctrl+O")) {
                    std::string path = ChiptuneTracker::openFileDialog(
                        "Chiptune Projects (*.ctp)\0*.ctp\0All Files (*.*)\0*.*\0",
                        "ctp");
                    if (!path.empty()) {
                        if (ChiptuneTracker::loadProject(project, path)) {
                            uiState.projectFilePath = path;
                            uiState.selectedPattern = 0;
                            uiState.selectedNoteIndex = -1;
                            uiState.selectedNoteIndices.clear();
                            sequencer.updateChannelConfigs();
                        }
                    }
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Exit")) {
                    g_Running = false;
                }
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Edit")) {
                if (ImGui::MenuItem("Undo", "Ctrl+Z")) {}
                if (ImGui::MenuItem("Redo", "Ctrl+Y")) {}
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("View")) {
                if (ImGui::MenuItem("Piano Roll", nullptr, uiState.currentView == ChiptuneTracker::ViewMode::PianoRoll)) {
                    uiState.currentView = ChiptuneTracker::ViewMode::PianoRoll;
                }
                if (ImGui::MenuItem("Tracker", nullptr, uiState.currentView == ChiptuneTracker::ViewMode::Tracker)) {
                    uiState.currentView = ChiptuneTracker::ViewMode::Tracker;
                }
                if (ImGui::MenuItem("Arrangement", nullptr, uiState.currentView == ChiptuneTracker::ViewMode::Arrangement)) {
                    uiState.currentView = ChiptuneTracker::ViewMode::Arrangement;
                }
                if (ImGui::MenuItem("Mixer", nullptr, uiState.currentView == ChiptuneTracker::ViewMode::Mixer)) {
                    uiState.currentView = ChiptuneTracker::ViewMode::Mixer;
                }
                ImGui::Separator();
                if (ImGui::BeginMenu("Workspace")) {
                    const char* names[] = { "Compose", "Sound Design", "Mix" };
                    for (int i = 0; i < 3; ++i) {
                        if (ImGui::MenuItem(names[i], nullptr, uiState.currentWorkspace == i)) {
                            uiState.currentWorkspace = i;
                            uiState.pendingLayoutFrames = 2;
                        }
                    }
                    ImGui::Separator();
                    if (ImGui::MenuItem("Reset layout", "Ctrl+0")) {
                        uiState.pendingLayoutFrames = 2;
                    }
                    ImGui::EndMenu();
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Instrument Macros", "F4", uiState.showMacroEditor)) {
                    uiState.showMacroEditor = !uiState.showMacroEditor;
                }
                if (ImGui::MenuItem("Wavetable Editor", nullptr, uiState.showWavetableEditor)) {
                    uiState.showWavetableEditor = !uiState.showWavetableEditor;
                }
                if (ImGui::MenuItem("Spectrum Analyzer", nullptr, uiState.showSpectrumAnalyzer)) {
                    uiState.showSpectrumAnalyzer = !uiState.showSpectrumAnalyzer;
                }
                if (ImGui::MenuItem("Automation", nullptr, uiState.showAutomation)) {
                    uiState.showAutomation = !uiState.showAutomation;
                }
                if (ImGui::MenuItem("MIDI Input", nullptr, uiState.showMIDIInput)) {
                    uiState.showMIDIInput = !uiState.showMIDIInput;
                }
                ImGui::Separator();
                if (ImGui::BeginMenu("Theme")) {
                    if (ImGui::MenuItem("Stock (Default)", nullptr, uiState.currentTheme == ChiptuneTracker::Theme::Stock)) {
                        uiState.currentTheme = ChiptuneTracker::Theme::Stock;
                        ChiptuneTracker::ApplyTheme(uiState.currentTheme);
                    }
                    if (ImGui::MenuItem("Cyberpunk", nullptr, uiState.currentTheme == ChiptuneTracker::Theme::Cyberpunk)) {
                        uiState.currentTheme = ChiptuneTracker::Theme::Cyberpunk;
                        ChiptuneTracker::ApplyTheme(uiState.currentTheme);
                    }
                    if (ImGui::MenuItem("Synthwave (80s)", nullptr, uiState.currentTheme == ChiptuneTracker::Theme::Synthwave)) {
                        uiState.currentTheme = ChiptuneTracker::Theme::Synthwave;
                        ChiptuneTracker::ApplyTheme(uiState.currentTheme);
                    }
                    if (ImGui::MenuItem("Matrix", nullptr, uiState.currentTheme == ChiptuneTracker::Theme::Matrix)) {
                        uiState.currentTheme = ChiptuneTracker::Theme::Matrix;
                        ChiptuneTracker::ApplyTheme(uiState.currentTheme);
                    }
                    ImGui::Separator();
                    if (ImGui::MenuItem("Frutiger Aero", nullptr, uiState.currentTheme == ChiptuneTracker::Theme::FrutigerAero)) {
                        uiState.currentTheme = ChiptuneTracker::Theme::FrutigerAero;
                        ChiptuneTracker::ApplyTheme(uiState.currentTheme);
                    }
                    if (ImGui::MenuItem("Minimal", nullptr, uiState.currentTheme == ChiptuneTracker::Theme::Minimal)) {
                        uiState.currentTheme = ChiptuneTracker::Theme::Minimal;
                        ChiptuneTracker::ApplyTheme(uiState.currentTheme);
                    }
                    if (ImGui::MenuItem("Vaporwave", nullptr, uiState.currentTheme == ChiptuneTracker::Theme::Vaporwave)) {
                        uiState.currentTheme = ChiptuneTracker::Theme::Vaporwave;
                        ChiptuneTracker::ApplyTheme(uiState.currentTheme);
                    }
                    if (ImGui::MenuItem("Retro Terminal", nullptr, uiState.currentTheme == ChiptuneTracker::Theme::RetroTerminal)) {
                        uiState.currentTheme = ChiptuneTracker::Theme::RetroTerminal;
                        ChiptuneTracker::ApplyTheme(uiState.currentTheme);
                    }
                    if (ImGui::MenuItem("Game Boy (DMG)", nullptr, uiState.currentTheme == ChiptuneTracker::Theme::GameBoy)) {
                        uiState.currentTheme = ChiptuneTracker::Theme::GameBoy;
                        ChiptuneTracker::ApplyTheme(uiState.currentTheme);
                    }
                    ImGui::Separator();
                    if (ImGui::MenuItem("Daylight (light)", nullptr, uiState.currentTheme == ChiptuneTracker::Theme::Daylight)) {
                        uiState.currentTheme = ChiptuneTracker::Theme::Daylight;
                        ChiptuneTracker::ApplyTheme(uiState.currentTheme);
                    }
                    ImGui::EndMenu();
                }
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Help")) {
                if (ImGui::MenuItem("About")) {
                    showAboutDialog = true;
                }
                ImGui::EndMenu();
            }
            ImGui::EndMainMenuBar();
        }

        // The dock space must be submitted before any dockable panel, so the
        // panels have somewhere to dock into on the frame they first appear.
        const ImGuiID dockspaceId = ChiptuneTracker::BeginDockSpace();

        // Repair an unusable saved layout, once, a couple of frames in.
        //
        // Upgrading from a pre-docking version leaves an imgui.ini full of
        // window positions but with no DockId assignments, so every panel
        // is restored floating at its old coordinates and the dock space
        // sits empty behind them. Merely having an ini used to be enough to
        // skip the default layout, so nothing corrected it.
        //
        // Frame 2, because ImGui applies the ini during the first NewFrame
        // and the windows have to have been submitted once to be counted.
        static int layoutHealthCheckFrame = 0;
        if (g_CheckSavedLayout && layoutHealthCheckFrame >= 0) {
            if (++layoutHealthCheckFrame > 2) {
                if (ChiptuneTracker::IsDockSpaceEmpty(dockspaceId)) {
                    printf("Saved layout has no docked panels - rebuilding.\n");
                    uiState.pendingLayoutFrames = 2;
                }
                layoutHealthCheckFrame = -1;   // check once, never again
            }
        }

        // Rebuilding the dock tree is a one-shot operation, not per-frame.
        if (uiState.pendingLayoutFrames > 0) {
            ChiptuneTracker::BuildWorkspaceLayout(
                dockspaceId,
                static_cast<ChiptuneTracker::Workspace>(uiState.currentWorkspace),
                ImGui::GetMainViewport()->WorkSize);
            uiState.pendingLayoutFrames = 0;
        }

        // Autosave. Only ticks when something changed, so an idle session
        // does not churn the disk.
        //
        // Change detection is a cheap fingerprint rather than hooking every
        // edit path: note counts, pattern and clip counts, and the tempo.
        // It misses an edit that swaps one note for another in the same
        // frame, which is a fair trade against threading a dirty flag
        // through several hundred call sites in UI.h.
        {
            size_t fingerprint = project.patterns.size() * 1000003u +
                                 project.arrangement.size() * 10007u +
                                 static_cast<size_t>(project.bpm * 100.0f);
            for (const auto& pattern : project.patterns) {
                fingerprint = fingerprint * 31u + pattern.notes.size();
            }
            static size_t lastFingerprint = 0;
            if (fingerprint != lastFingerprint) {
                lastFingerprint = fingerprint;
                autosave.markDirty();
            }
        }
        autosave.update(project, io.DeltaTime);

        if (showRecoveryPrompt) {
            ImGui::OpenPopup("Recover unsaved work?");
            showRecoveryPrompt = false;
        }
        ImGui::SetNextWindowSize(ImVec2(460, 0), ImGuiCond_Appearing);
        if (ImGui::BeginPopupModal("Recover unsaved work?", nullptr,
                                   ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextWrapped(
                "The last session did not close normally. An autosave from %s "
                "is available.",
                recoveryAge.c_str());
            ImGui::Spacing();
            ImGui::TextDisabled("Restoring replaces the current project. "
                                "Nothing is written to your own file either way.");
            ImGui::Spacing();
            ImGui::Separator();

            if (ImGui::Button("Restore it", ImVec2(150, 0))) {
                const std::string path = autosave.bestRecoveryPath();
                if (!path.empty() && ChiptuneTracker::loadProject(project, path)) {
                    sequencer.setProject(&project);
                    sequencer.updateChannelConfigs();
                    uiState.selectedPattern = 0;
                    printf("Restored autosave from %s\n", path.c_str());
                }
                autosave.clearOnCleanExit();
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Discard", ImVec2(150, 0))) {
                autosave.clearOnCleanExit();
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        // About dialog
        if (showAboutDialog) {
            ImGui::OpenPopup("About ChiptuneTracker");
            showAboutDialog = false;
        }
        ImGui::SetNextWindowSize(ImVec2(420, 0), ImGuiCond_Appearing);
        if (ImGui::BeginPopupModal("About ChiptuneTracker", nullptr,
                                   ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextUnformatted(ChiptuneTracker::aboutText().c_str());
            ImGui::Separator();
            if (ImGui::Button("Close", ImVec2(120, 0))) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }

        // ====================================================================
        // UI Windows
        // ====================================================================

        // File menu (always visible)
        ChiptuneTracker::DrawFileMenu(project, uiState, sequencer);

        // Transport bar (always visible)
        ChiptuneTracker::DrawTransportBar(sequencer, project, playbackState, uiState);

        // View tabs
        ChiptuneTracker::DrawViewTabs(uiState);

        // Pattern list (always visible)
        ChiptuneTracker::DrawPatternList(project, uiState);

        // Channel editor (always visible)
        ChiptuneTracker::DrawChannelEditor(project, uiState, sequencer);

        // Sound palette (always visible)
        ChiptuneTracker::DrawSoundPalette(project, uiState, sequencer);

        // Note editor (always visible when in piano roll mode)
        ChiptuneTracker::DrawNoteEditor(project, uiState);

        // Tools panel (always visible)
        ChiptuneTracker::DrawToolsPanel(project, uiState, sequencer);

        // Main editor view (based on current mode)
        switch (uiState.currentView) {
            case ChiptuneTracker::ViewMode::PianoRoll:
                ChiptuneTracker::DrawPianoRoll(project, uiState, sequencer);
                break;
            case ChiptuneTracker::ViewMode::Tracker:
                ChiptuneTracker::DrawTrackerView(project, uiState, sequencer);
                break;
            case ChiptuneTracker::ViewMode::Arrangement:
                ChiptuneTracker::DrawArrangement(project, uiState, sequencer);
                break;
            case ChiptuneTracker::ViewMode::Mixer:
                ChiptuneTracker::DrawMixer(project, uiState, sequencer);
                break;
            case ChiptuneTracker::ViewMode::PadController:
                ChiptuneTracker::DrawPadController(project, uiState, sequencer);
                break;
        }

        // Floating tool windows are submitted AFTER the main editor view.
        // ImGui draw order is z-order: submitted first means drawn behind,
        // and a tool window that opens behind the editor looks like it did
        // not open at all.
        //
        // These are opened from the View menu rather than shown
        // unconditionally - four extra windows stacked over the editor on
        // launch is not a workspace.
        ChiptuneTracker::DrawMacroEditor(project, uiState, sequencer);

        if (uiState.showSpectrumAnalyzer) {
            ChiptuneTracker::renderSpectrumAnalyzer(sequencer);
        }
        if (uiState.showMIDIInput) {
            ChiptuneTracker::renderMIDIInput(sequencer, uiState);
        }
        if (uiState.showAutomation) {
            ChiptuneTracker::renderAutomation(project, uiState, sequencer.getState());
        }
        if (uiState.showWavetableEditor) {
            ChiptuneTracker::renderWavetableEditor(project, uiState);
        }

        // Keyboard shortcuts
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_0)) {
            uiState.pendingLayoutFrames = 2;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_F12) && !ImGui::GetIO().WantTextInput) {
            g_CaptureRequested = true;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_F4) && !ImGui::GetIO().WantTextInput) {
            uiState.showMacroEditor = !uiState.showMacroEditor;
        }
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Space)) {
            if (playbackState.isPlaying) sequencer.pause();
            else sequencer.play();
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Space) && !io.KeyCtrl && !ImGui::GetIO().WantTextInput) {
            if (playbackState.isPlaying) sequencer.pause();
            else sequencer.play();
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Home)) {
            sequencer.stop();
        }

        // Virtual keyboard (play notes with computer keyboard)
        if (!ImGui::GetIO().WantTextInput) {
            const int keyMap[] = {
                ImGuiKey_Z, ImGuiKey_S, ImGuiKey_X, ImGuiKey_D, ImGuiKey_C,
                ImGuiKey_V, ImGuiKey_G, ImGuiKey_B, ImGuiKey_H, ImGuiKey_N,
                ImGuiKey_J, ImGuiKey_M
            };
            int baseNote = 48 + uiState.pianoRollOctaveOffset * 12;  // C3

            for (int i = 0; i < 12; ++i) {
                if (ImGui::IsKeyPressed(static_cast<ImGuiKey>(keyMap[i]))) {
                    sequencer.triggerNote(uiState.selectedChannel, baseNote + i, 0.8f);
                }
                if (ImGui::IsKeyReleased(static_cast<ImGuiKey>(keyMap[i]))) {
                    sequencer.releaseNote(uiState.selectedChannel, baseNote + i);
                }
            }

            // Octave up/down
            if (ImGui::IsKeyPressed(ImGuiKey_LeftBracket)) {
                uiState.pianoRollOctaveOffset = std::max(0, uiState.pianoRollOctaveOffset - 1);
            }
            if (ImGui::IsKeyPressed(ImGuiKey_RightBracket)) {
                uiState.pianoRollOctaveOffset = std::min(6, uiState.pianoRollOctaveOffset + 1);
            }
        }

        // Reset layout update flag after all windows have been positioned
        uiState.needsLayoutUpdate = false;

        // ====================================================================
        // Render
        // ====================================================================
        ImGui::Render();

        RECT rect;
        GetClientRect(hwnd, &rect);
        glViewport(0, 0, rect.right - rect.left, rect.bottom - rect.top);
        glClearColor(0.04f, 0.04f, 0.06f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        // Framebuffer capture has to happen after rendering and before the
        // swap - reading the back buffer after a swap gets undefined
        // contents on most drivers.
        const int frameWidth = rect.right - rect.left;
        const int frameHeight = rect.bottom - rect.top;

        if (g_CaptureRequested) {
            g_CaptureRequested = false;
            char path[512];
            std::snprintf(path, sizeof(path), "screenshots/shot_%03d.bmp", g_CaptureCounter++);
            CreateDirectoryA("screenshots", nullptr);
            if (ChiptuneTracker::captureFramebuffer(path, frameWidth, frameHeight)) {
                printf("Screenshot written to %s\n", path);
            } else {
                printf("Screenshot failed\n");
            }
        }

        if (captureRequest.enabled) {
            // Render a few frames first so animated backgrounds, meters and
            // the spectrum analyser look like themselves rather than like a
            // cold start.
            if (++captureFrameCounter >= captureRequest.framesToWait) {
                if (ChiptuneTracker::captureFramebuffer(captureRequest.outputPath,
                                                        frameWidth, frameHeight)) {
                    printf("Captured %dx%d to %s\n", frameWidth, frameHeight,
                           captureRequest.outputPath.c_str());
                } else {
                    printf("Capture failed for %s\n", captureRequest.outputPath.c_str());
                }
                g_Running = false;
            }
        }

        SwapBuffers(hdc);
    }

    // ========================================================================
    // Cleanup
    // ========================================================================
    // A clean shutdown clears the recovery files. Their presence at the
    // next launch is what tells us the previous session crashed.
    if (!captureRequest.enabled) {
        autosave.clearOnCleanExit();
    }

    // Order matters: stop the device before retiring the pointer, so no
    // callback can be in flight against state the main thread is dismantling.
    ma_device_uninit(&device);
    g_Sequencer.store(nullptr, std::memory_order_release);

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    wglMakeCurrent(nullptr, nullptr);
    wglDeleteContext(glContext);
    ReleaseDC(hwnd, hdc);
    DestroyWindow(hwnd);

    return 0;
}

// ============================================================================
// Window Procedure
// ============================================================================
LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    if (ImGui_ImplWin32_WndProcHandler(hwnd, uMsg, wParam, lParam)) {
        return true;
    }

    switch (uMsg) {
        case WM_CLOSE:
            g_Running = false;
            return 0;

        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;

        case WM_SIZE:
            return 0;
    }

    return DefWindowProcA(hwnd, uMsg, wParam, lParam);
}
