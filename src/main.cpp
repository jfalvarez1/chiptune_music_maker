/*
 * ChiptuneTracker - Main Entry Point
 *
 * Creates a native Win32 window with:
 *   - Dear ImGui for immediate-mode UI
 *   - OpenGL 3.3 rendering backend
 *   - miniaudio-powered audio engine
 *
 * This is a minimal "Hello World" that proves the stack works.
 */

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>
#include <GL/gl.h>

#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_opengl3.h"

#include "AudioEngine.h"

#include <cstdio>

// OpenGL function loading (minimal for OpenGL 3.3)
typedef HGLRC (WINAPI* PFNWGLCREATECONTEXTATTRIBSARBPROC)(HDC, HGLRC, const int*);
PFNWGLCREATECONTEXTATTRIBSARBPROC wglCreateContextAttribsARB = nullptr;

// Forward declarations
LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// Global state (for simplicity in this demo)
static bool g_Running = true;

// ============================================================================
// WinMain Entry Point
// ============================================================================
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE /*hPrevInstance*/, LPSTR /*lpCmdLine*/, int nCmdShow) {

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

    // ========================================================================
    // Create Window
    // ========================================================================
    HWND hwnd = CreateWindowExA(
        0,
        CLASS_NAME,
        "Chiptune Tracker v0.1",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT,
        1280, 720,
        nullptr,
        nullptr,
        hInstance,
        nullptr
    );

    if (!hwnd) {
        MessageBoxA(nullptr, "Failed to create window", "Error", MB_OK | MB_ICONERROR);
        return 1;
    }

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

    // Create temporary context to load WGL extensions
    HGLRC tempContext = wglCreateContext(hdc);
    wglMakeCurrent(hdc, tempContext);

    wglCreateContextAttribsARB = (PFNWGLCREATECONTEXTATTRIBSARBPROC)
        wglGetProcAddress("wglCreateContextAttribsARB");

    HGLRC glContext = nullptr;
    if (wglCreateContextAttribsARB) {
        // Create OpenGL 3.3 Core context
        int attribs[] = {
            0x2091, 3, // WGL_CONTEXT_MAJOR_VERSION_ARB
            0x2092, 3, // WGL_CONTEXT_MINOR_VERSION_ARB
            0x9126, 0x00000001, // WGL_CONTEXT_PROFILE_MASK_ARB, CORE
            0
        };
        glContext = wglCreateContextAttribsARB(hdc, nullptr, attribs);
    }

    if (!glContext) {
        // Fallback to legacy context
        glContext = tempContext;
    } else {
        wglMakeCurrent(nullptr, nullptr);
        wglDeleteContext(tempContext);
        wglMakeCurrent(hdc, glContext);
    }

    // ========================================================================
    // Initialize Dear ImGui
    // ========================================================================
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    // Dark theme
    ImGui::StyleColorsDark();

    // Custom tracker-style colors
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 2.0f;
    style.FrameRounding = 2.0f;
    style.Colors[ImGuiCol_WindowBg] = ImVec4(0.05f, 0.05f, 0.08f, 1.0f);
    style.Colors[ImGuiCol_FrameBg] = ImVec4(0.10f, 0.10f, 0.15f, 1.0f);
    style.Colors[ImGuiCol_SliderGrab] = ImVec4(0.40f, 0.80f, 0.40f, 1.0f);
    style.Colors[ImGuiCol_SliderGrabActive] = ImVec4(0.50f, 1.00f, 0.50f, 1.0f);

    // Initialize ImGui backends
    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplOpenGL3_Init("#version 130");

    // ========================================================================
    // Initialize Audio Engine
    // ========================================================================
    ChiptuneTracker::AudioEngine audioEngine;

    if (!audioEngine.initialize()) {
        MessageBoxA(nullptr, "Failed to initialize audio engine", "Error", MB_OK | MB_ICONERROR);
        return 1;
    }

    if (!audioEngine.start()) {
        MessageBoxA(nullptr, "Failed to start audio engine", "Error", MB_OK | MB_ICONERROR);
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
    float frequency = 440.0f;
    float volume = 0.5f;
    int waveformIndex = 0;
    const char* waveformNames[] = { "Sine", "Square", "Sawtooth", "Triangle", "Noise" };

    while (g_Running) {
        // Process Windows messages
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

        // ====================================================================
        // Audio Control Panel
        // ====================================================================
        ImGui::Begin("Chiptune Tracker - Audio Test", nullptr, ImGuiWindowFlags_AlwaysAutoResize);

        ImGui::Text("Audio Engine Status: %s",
            audioEngine.isRunning() ? "Running" : "Stopped");

        ImGui::Separator();
        ImGui::Text("Oscillator Controls");

        // Frequency slider (logarithmic for musical control)
        if (ImGui::SliderFloat("Frequency", &frequency, 20.0f, 2000.0f, "%.1f Hz", ImGuiSliderFlags_Logarithmic)) {
            audioEngine.setFrequency(frequency);
        }

        // Volume slider
        if (ImGui::SliderFloat("Volume", &volume, 0.0f, 1.0f, "%.2f")) {
            audioEngine.setVolume(volume);
        }

        // Waveform selector
        if (ImGui::Combo("Waveform", &waveformIndex, waveformNames, IM_ARRAYSIZE(waveformNames))) {
            audioEngine.setWaveform(static_cast<ChiptuneTracker::WaveformType>(waveformIndex));
        }

        ImGui::Separator();

        // Quick note buttons (C4 to B4)
        ImGui::Text("Quick Notes:");
        const float notes[] = { 261.63f, 293.66f, 329.63f, 349.23f, 392.00f, 440.00f, 493.88f, 523.25f };
        const char* noteNames[] = { "C4", "D4", "E4", "F4", "G4", "A4", "B4", "C5" };

        for (int i = 0; i < 8; ++i) {
            if (i > 0) ImGui::SameLine();
            if (ImGui::Button(noteNames[i], ImVec2(40, 30))) {
                frequency = notes[i];
                audioEngine.setFrequency(frequency);
            }
        }

        ImGui::Separator();
        ImGui::TextColored(ImVec4(0.5f, 0.8f, 0.5f, 1.0f), "Chiptune Tracker v0.1 - Audio Stack Working!");

        ImGui::End();

        // ====================================================================
        // Render
        // ====================================================================
        ImGui::Render();

        RECT rect;
        GetClientRect(hwnd, &rect);
        glViewport(0, 0, rect.right - rect.left, rect.bottom - rect.top);
        glClearColor(0.02f, 0.02f, 0.04f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        SwapBuffers(hdc);
    }

    // ========================================================================
    // Cleanup
    // ========================================================================
    audioEngine.shutdown();

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
    // Let ImGui process input first
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
            // Handle resize if needed
            return 0;
    }

    return DefWindowProcA(hwnd, uMsg, wParam, lParam);
}
