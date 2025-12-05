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

#include <cstdio>
#include <memory>

// OpenGL function loading
typedef HGLRC (WINAPI* PFNWGLCREATECONTEXTATTRIBSARBPROC)(HDC, HGLRC, const int*);
PFNWGLCREATECONTEXTATTRIBSARBPROC wglCreateContextAttribsARB = nullptr;

// Forward declarations
LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// Global state
static bool g_Running = true;
static ChiptuneTracker::Sequencer* g_Sequencer = nullptr;

// ============================================================================
// Miniaudio Callback
// ============================================================================
void audioCallback(ma_device* pDevice, void* pOutput, const void* /*pInput*/, ma_uint32 frameCount) {
    float* output = static_cast<float*>(pOutput);

    if (g_Sequencer) {
        // Deinterleave for our sequencer
        std::vector<float> left(frameCount), right(frameCount);
        g_Sequencer->process(left.data(), right.data(), frameCount);

        // Interleave for miniaudio
        for (ma_uint32 i = 0; i < frameCount; ++i) {
            output[i * 2 + 0] = left[i];
            output[i * 2 + 1] = right[i];
        }
    } else {
        std::fill_n(output, frameCount * 2, 0.0f);
    }
}

// ============================================================================
// WinMain Entry Point
// ============================================================================
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE /*hPrevInstance*/, LPSTR /*lpCmdLine*/, int nCmdShow) {

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
        "Chiptune Tracker DAW v0.2",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT,
        1600, 900,
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

    // Dark tracker-style theme
    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 4.0f;
    style.FrameRounding = 2.0f;
    style.GrabRounding = 2.0f;
    style.ScrollbarRounding = 2.0f;
    style.WindowPadding = ImVec2(8, 8);
    style.FramePadding = ImVec2(4, 3);
    style.ItemSpacing = ImVec2(8, 4);

    // Custom colors
    ImVec4* colors = style.Colors;
    colors[ImGuiCol_WindowBg] = ImVec4(0.06f, 0.06f, 0.08f, 1.0f);
    colors[ImGuiCol_ChildBg] = ImVec4(0.05f, 0.05f, 0.07f, 1.0f);
    colors[ImGuiCol_PopupBg] = ImVec4(0.08f, 0.08f, 0.10f, 0.95f);
    colors[ImGuiCol_Border] = ImVec4(0.20f, 0.20f, 0.25f, 1.0f);
    colors[ImGuiCol_FrameBg] = ImVec4(0.12f, 0.12f, 0.15f, 1.0f);
    colors[ImGuiCol_FrameBgHovered] = ImVec4(0.18f, 0.18f, 0.22f, 1.0f);
    colors[ImGuiCol_FrameBgActive] = ImVec4(0.22f, 0.22f, 0.28f, 1.0f);
    colors[ImGuiCol_TitleBg] = ImVec4(0.08f, 0.08f, 0.10f, 1.0f);
    colors[ImGuiCol_TitleBgActive] = ImVec4(0.12f, 0.12f, 0.15f, 1.0f);
    colors[ImGuiCol_Header] = ImVec4(0.20f, 0.35f, 0.20f, 1.0f);
    colors[ImGuiCol_HeaderHovered] = ImVec4(0.25f, 0.45f, 0.25f, 1.0f);
    colors[ImGuiCol_HeaderActive] = ImVec4(0.30f, 0.55f, 0.30f, 1.0f);
    colors[ImGuiCol_Button] = ImVec4(0.20f, 0.35f, 0.20f, 1.0f);
    colors[ImGuiCol_ButtonHovered] = ImVec4(0.25f, 0.45f, 0.25f, 1.0f);
    colors[ImGuiCol_ButtonActive] = ImVec4(0.30f, 0.55f, 0.30f, 1.0f);
    colors[ImGuiCol_SliderGrab] = ImVec4(0.40f, 0.70f, 0.40f, 1.0f);
    colors[ImGuiCol_SliderGrabActive] = ImVec4(0.50f, 0.85f, 0.50f, 1.0f);
    colors[ImGuiCol_CheckMark] = ImVec4(0.50f, 0.90f, 0.50f, 1.0f);

    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplOpenGL3_Init("#version 130");
    printf("ImGui backends initialized\n");

    // ========================================================================
    // Initialize Audio
    // ========================================================================
    printf("Initializing audio...\n");
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
    printf("Creating project...\n");
    ChiptuneTracker::Project project;
    printf("Project created\n");
    printf("Creating sequencer...\n");
    ChiptuneTracker::Sequencer sequencer;
    printf("Sequencer created\n");
    ChiptuneTracker::UIState uiState;
    ChiptuneTracker::PlaybackState playbackState;

    sequencer.setSampleRate(44100.0f);
    sequencer.setProject(&project);
    sequencer.setLoop(false, 0.0f, 16.0f);  // Don't loop by default - stop at end

    g_Sequencer = &sequencer;

    // Start with empty pattern (no demo noise)
    uiState.selectedPattern = 0;
    uiState.selectedChannel = 0;

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
                if (ImGui::MenuItem("Save Project")) {
                    // TODO: Implement save
                }
                if (ImGui::MenuItem("Load Project")) {
                    // TODO: Implement load
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Export WAV")) {
                    // TODO: Implement export
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
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Help")) {
                if (ImGui::MenuItem("About")) {
                    // TODO: Show about dialog
                }
                ImGui::EndMenu();
            }
            ImGui::EndMainMenuBar();
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
        }

        // Keyboard shortcuts
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

        SwapBuffers(hdc);
    }

    // ========================================================================
    // Cleanup
    // ========================================================================
    g_Sequencer = nullptr;
    ma_device_uninit(&device);

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
