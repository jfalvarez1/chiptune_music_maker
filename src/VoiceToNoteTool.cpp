#define NOMINMAX
#include <iostream>
#include <vector>
#include <string>
#include <fstream>
#include <algorithm>
#include <cstring>

// Include ImGui
#include "../vendor/imgui/imgui.h"
#include "../vendor/imgui/backends/imgui_impl_win32.h"
#include "../vendor/imgui/backends/imgui_impl_opengl3.h"
#include <windows.h>
#include <commdlg.h>
#include <GL/gl.h>

#define STB_IMAGE_IMPLEMENTATION
#include "../vendor/miniaudio/miniaudio.h" // Implementation is in AudioRecorder.h or main.cpp, be careful with duplicate generic defs

#include "AudioRecorder.h"
#include "AudioAnalyzer.h"
#include "AudioPlayer.h"
#include "Types.h" // For Note/Pattern structs

// Simple WAV loader using miniaudio (decode to mono f32)
static bool loadWavFile(const std::string& filepath, int& sampleRateOut, std::vector<float>& samplesOut) {
    ma_decoder decoder;
    ma_decoder_config config = ma_decoder_config_init(ma_format_f32, 1, 44100);
    if (ma_decoder_init_file(filepath.c_str(), &config, &decoder) != MA_SUCCESS) return false;
    sampleRateOut = decoder.outputSampleRate;
    ma_uint64 frames = 0;
    if (ma_decoder_get_length_in_pcm_frames(&decoder, &frames) != MA_SUCCESS) { ma_decoder_uninit(&decoder); return false; }
    samplesOut.resize(frames);
    ma_uint64 read = 0;
    if (ma_decoder_read_pcm_frames(&decoder, samplesOut.data(), frames, &read) != MA_SUCCESS || read == 0) { ma_decoder_uninit(&decoder); return false; }
    samplesOut.resize(static_cast<size_t>(read));
    ma_decoder_uninit(&decoder);
    return true;
}

static bool browseForWav(char* buffer, DWORD bufferSize) {
    OPENFILENAMEA ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = NULL;
    ofn.lpstrFilter = "WAV Files\0*.wav\0All Files\0*.*\0";
    ofn.lpstrFile = buffer;
    ofn.nMaxFile = bufferSize;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    return GetOpenFileNameA(&ofn) == TRUE;
}

// Forward declarations
void RenderUI();
void ExportToCTP(const std::vector<DetectedNote>& notes, const std::string& filename, bool isDrums);
void pushUndo();
void auditionInstrument(int instrumentIndex, int sampleRate, AudioPlayer& player);

// Global state
AudioRecorder recorder;
AudioPlayer player;
std::vector<DetectedNote> detectedNotes;
std::vector<float> importedSamples;
int importedSampleRate = 0;
std::string importedPathStr;
int detectionMode = 0; // 0: Melodic, 1: Percussion
int drumFocus = 0;     // 0: Full Kit, 1: Kick, 2: Snare, 3: HiHat
const char* drumFocusNames[] = { "Full Kit (All)", "Kick Drum Only", "Snare Drum Only", "Hi-Hat Only" };
bool advancedPreview = false;
int selectedInstrument = 0; 
std::vector<AudioRecorder::InputDevice> inputDevices;
std::vector<std::string> inputDeviceNamesStr; 
std::vector<const char*> inputDeviceNames;
int currentInputDevice = 0;
float analysisThreshold = 0.02f; // RMS/Onset threshold
int targetBPM = 120;

int pitchFilter = 0; // 0: All, 1: Melody, 2: Bass
int harmonyMode = 0; // 0: None, 1: +12, 2: -12, 3: +7, 4: Power, 5: Smart 3rd
const char* harmonyNames[] = { "None", "Octave Up (+12)", "Octave Down (-12)", "Perfect Fifth (+7)", "Power Chord (+7, +12)", "Smart 3rd (Key)" };
DetectedKey currentKey = {0, false, 0.0f};
bool keyDetected = false;

// Post-Processing State
std::vector<DetectedNote> originalDetectedNotes;
bool ppHarmonics = false;
int ppHarmonicsCount = 1;
int ppHarmonicsInterval = 12; // 12=Octave
int ppHarmonicsDir = 0; // 0=Up, 1=Down, 2=Both
const char* ppHarmonicsDirNames[] = { "Up", "Down", "Both" };
const char* ppHarmonicsIntNames[] = { "Octave (12)", "Perfect 5th (7)", "Major 3rd (4)", "Minor 3rd (3)" };
int ppHarmonicsIntValues[] = { 12, 7, 4, 3 };
int ppHarmonicsIntIndex = 0;
bool ppAutoTune = false;
bool ppLegato = false; // "Chain Melody"
bool autoPreview = true;

// Drum Kit Mapping
int selectedKickType = 0; 
int selectedSnareType = 0; 
int selectedHiHatType = 0; 

const char* kickNames[] = { "Standard", "808", "Hard", "Soft" };
const char* snareNames[] = { "Standard", "808", "Rimshot", "Clap" };
const char* hihatNames[] = { "Closed", "Open", "Pedal" };

int kickIndices[] = { 27, 28, 52, 53 };
int snareIndices[] = { 29, 30, 54, 31 };
int hihatIndices[] = { 32, 33, 55 };

const char* instrumentNames[] = { 
    "Pulse 50%", "Pulse 25%", "Pulse 12.5%", "Triangle", "Sawtooth", "Sine", "Noise",
    "Synth Lead", "Synth Pad", "Synth Bass", "Synth Pluck", "Synth Arp", 
    "Synth Organ", "Synth Strings", "Synth Brass", "Synth Chip", "Synth Bell",
    "Reggaeton Bass", "Latin Brass", "Guira", "Bongo", "Timbale", "Dembow 808", "Dembow Snare",
    "Synthwave Bass", "Acid Bass", "Sub Bass 808",
    "Kick", "Kick 808", "Snare", "Snare 808", "Clap", "Hi-Hat", "Hi-Hat Open", "Tom", "Crash", "Ride",
    "Synthwave Lead", "Synthwave Pad", "Synthwave Arp", "Synthwave Chord", "Synthwave FM",
    "Techno Stab", "Hoover", "Rave Chord", "Reese",
    "Lo-Fi Keys", "Vinyl Noise", "Trap Lead",
    "Gated Pad", "Poly Synth", "Sync Lead",
    "Kick Hard", "Kick Soft", "Snare Rim", "Hi-Hat Pedal", 
    "Tom Low", "Tom High", "Cowbell", "Clave", "Conga", "Maracas", "Tambourine",
// New Bass-focused presets (map to existing engine oscillators)
    "Bass Punch", "Bass Sub", "Bass FM", "Bass Reese", "Bass Acid", "Bass Pluck"
};

// UX helpers / session state
float uiTimeScale = 50.0f; // pixels per second for piano roll
bool noiseGateEnabled = false;
float noiseGateThreshold = 0.01f;
int templateIndex = 0;
std::vector<DetectedNote> slotA;
std::vector<DetectedNote> slotB;
int activeSlot = 0; // 0 = live detectedNotes, 1 = slotA, 2 = slotB
bool useBeatsRuler = false;
bool useLightTheme = false;
float previewLatencyMs = 0.0f;
float autoDetectedBPM = 0.0f;
bool stickyControls = false;
bool showAdvancedDet = false;
bool lastAnalyzeRequested = false;
std::vector<std::vector<DetectedNote>> undoStack;
std::vector<std::vector<DetectedNote>> redoStack;
int selectedExample = 0;
bool playRequested = false;
float resizeGrabPx = 6.0f;

struct PadTarget {
    const char* label;
    int instrumentIndex;
};

PadTarget padTargets[] = {
    { "Kick", 27 }, { "Snare", 29 }, { "Hat", 32 }, { "Bass", 26 },
    { "Lead", 7 }, { "Chord", 8 }, { "Pluck", 10 }, { "FX", 47 }
};

struct SessionTemplate {
    const char* name;
    int mode;
    float sensitivity;
    int pitchFilterTweak;
    int drumFocusTweak;
    int instrument;
};

SessionTemplate sessionTemplates[] = {
    { "Beatbox to Drums", 1, 0.02f, 0, 0, kickIndices[0] },
    { "Hum to Bass", 2, 0.02f, 2, 0, 26 }, // Sub Bass 808
    { "Sing a Lead", 0, 0.02f, 1, 0, 7 }, // Synth Lead
    { "Tap Rhythm to Pluck", 4, 0.02f, 0, 0, 10 }, // Synth Pluck
    { "Chords to Pad", 3, 0.03f, 0, 0, 8 } // Synth Pad
};

// Window boilerplate
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int) {
    // Create application window
    WNDCLASSEXW wc = { sizeof(wc), CS_CLASSDC, WndProc, 0L, 0L, hInstance, NULL, NULL, NULL, NULL, L"VoiceToNote Tool", NULL };
    ::RegisterClassExW(&wc);
    HWND hwnd = ::CreateWindowW(wc.lpszClassName, L"Chiptune Voice-to-Note", WS_OVERLAPPEDWINDOW, 100, 100, 1280, 800, NULL, NULL, wc.hInstance, NULL);

    // Initialize OpenGL
    HDC hDc = ::GetDC(hwnd);
    PIXELFORMATDESCRIPTOR pfd = { 0 };
    pfd.nSize = sizeof(pfd);
    pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 32;
    int pf = ::ChoosePixelFormat(hDc, &pfd);
    ::SetPixelFormat(hDc, pf, &pfd);
    HGLRC hRc = ::wglCreateContext(hDc);
    ::wglMakeCurrent(hDc, hRc);

    ::ShowWindow(hwnd, SW_SHOWMAXIMIZED);
    ::UpdateWindow(hwnd);

    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;

    // Setup Dear ImGui style
    ImGui::StyleColorsDark();

    // Setup Platform/Renderer backends
    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplOpenGL3_Init("#version 130");

    // Initialize Input Devices List
    inputDevices = recorder.getAvailableDevices();
    inputDeviceNamesStr.push_back("Default Device");
    for(const auto& d : inputDevices) inputDeviceNamesStr.push_back(d.name);
    for(const auto& s : inputDeviceNamesStr) inputDeviceNames.push_back(s.c_str());

    // Initialize Recorder & Player
    if (!recorder.init()) {
        std::cerr << "Failed to initialize audio recorder!" << std::endl;
    }
    if (!player.init()) {
        std::cerr << "Failed to initialize audio player!" << std::endl;
    }

    // Main loop
    bool done = false;
    while (!done) {
        MSG msg;
        while (::PeekMessage(&msg, NULL, 0U, 0U, PM_REMOVE)) {
            ::TranslateMessage(&msg);
            ::DispatchMessage(&msg);
            if (msg.message == WM_QUIT) done = true;
        }
        if (done) break;

        // Update Audio Player state (safe stop from main thread)
        player.update();

        // Start the Dear ImGui frame
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        if (useLightTheme) ImGui::StyleColorsLight(); else ImGui::StyleColorsDark();
        RenderUI();

        // Rendering
        ImGui::Render();
        glViewport(0, 0, 1280, 800);
        glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        ::SwapBuffers(hDc);
    }

    // Cleanup
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    ::wglMakeCurrent(NULL, NULL);
    ::wglDeleteContext(hRc);
    ::ReleaseDC(hwnd, hDc);
    ::DestroyWindow(hwnd);
    ::UnregisterClassW(wc.lpszClassName, wc.hInstance);

    return 0;
}

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
        return true;

    switch (msg) {
    case WM_SIZE:
        return 0;
    case WM_DESTROY:
        ::PostQuitMessage(0);
        return 0;
    }
    return ::DefWindowProcW(hWnd, msg, wParam, lParam);
}

void DrawInstrumentSelector(const char* label, int* selected) {
    const char* preview = (*selected >= 0 && *selected < IM_ARRAYSIZE(instrumentNames)) ? instrumentNames[*selected] : "Unknown";
    if (ImGui::BeginCombo(label, preview, ImGuiComboFlags_HeightLarge)) {
        
        auto Item = [&](int i) {
            bool isSelected = (*selected == i);
            if (ImGui::Selectable(instrumentNames[i], isSelected)) *selected = i;
            if (isSelected) ImGui::SetItemDefaultFocus();
        };

        // Bass section (unique listing)
        ImGui::SeparatorText("Bass");
        int bassIdx[] = {9, 25, 26, 63, 64, 65, 66, 67, 68};
        for (int i : bassIdx) Item(i);

        ImGui::SeparatorText("Oscillators");
        for (int i = 0; i <= 6; i++) Item(i);

        ImGui::SeparatorText("Classic Synths");
        for (int i = 7; i <= 16; i++) {
            if (i == 9) continue; // Already in Bass
            Item(i);
        }

        ImGui::SeparatorText("Synthwave");
        int synthwaveIdx[] = {24, 37, 38, 39, 40, 41, 49, 50, 51};
        for (int i : synthwaveIdx) Item(i);

        ImGui::SeparatorText("Techno / Electronic");
        int technoIdx[] = {42, 43, 44, 45};
        for (int i : technoIdx) Item(i);

        ImGui::SeparatorText("Hip Hop");
        int hiphopIdx[] = {46, 47, 48};
        for (int i : hiphopIdx) Item(i);

        ImGui::SeparatorText("Reggaeton");
        for (int i = 17; i <= 23; i++) Item(i);

        ImGui::SeparatorText("Kick / Toms / Cymbals");
        int kickIdx[] = {27, 28, 52, 53, 34, 56, 57, 35, 36, 58, 59, 60, 61, 62};
        for (int i : kickIdx) Item(i);

        ImGui::SeparatorText("Snare");
        int snareIdx[] = {29, 30, 31, 54};
        for (int i : snareIdx) Item(i);

        ImGui::SeparatorText("Hi-Hat");
        int hatIdx[] = {32, 33, 55};
        for (int i : hatIdx) Item(i);

        ImGui::EndCombo();
    }
}

void RenderUI() {
    ImGui::Begin("Voice to Note Converter", nullptr, stickyControls ? ImGuiWindowFlags_NoMove : 0);
    ImGuiIO& io = ImGui::GetIO();

    // Current sample source (imported WAV takes precedence over live recording)
    std::vector<float> samples = importedSamples.empty() ? recorder.getSamples() : importedSamples;
    int currentSampleRate = importedSamples.empty() ? recorder.getSampleRate() : importedSampleRate;
    bool usingImported = !importedSamples.empty();

    // Progress markers
    ImGui::TextColored(ImVec4(0.6f,0.8f,1.0f,1.0f), "Steps: Capture > Mode > Analyze > Preview > Export");
    ImGui::Separator();
    ImGui::Checkbox("Light Theme", &useLightTheme);
    ImGui::SameLine();
    ImGui::Checkbox("Show Beats Ruler", &useBeatsRuler);
    ImGui::SameLine();
    ImGui::Checkbox("Sticky Controls", &stickyControls);
    ImGui::SameLine();
    ImGui::Checkbox("Advanced Detection Knobs", &showAdvancedDet);
    ImGui::Separator();
    // Sticky quick controls
    if (ImGui::Button("Analyze Now (A)")) {
        lastAnalyzeRequested = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Play/Stop (Space)")) {
        playRequested = true;
    }

    ImGui::Text("Session");
    ImGui::SameLine();
    if (ImGui::Button("New Session")) {
        importedSamples.clear();
        importedSampleRate = 0;
        importedPathStr.clear();
        recorder.stop();
        detectedNotes.clear();
        originalDetectedNotes.clear();
        slotA.clear(); slotB.clear(); activeSlot = 0;
    }

    if (ImGui::Combo("Input Device", &currentInputDevice, inputDeviceNames.data(), inputDeviceNames.size())) {
        // Index 0 is "Default", so device index is currentInputDevice - 1
        recorder.init(currentInputDevice - 1, inputDevices);
    }

    ImGui::Text("Sample Rate: %d Hz | Channels: %d", recorder.getSampleRate(), recorder.getChannels());
    float peak = recorder.getPeak();
    ImGui::ProgressBar(peak, ImVec2(-1, 0), "Input Level");
    if (peak > 0.95f) {
        ImGui::TextColored(ImVec4(1,0.6f,0.4f,1), "Input is clipping; lower mic gain.");
    } else if (peak < 0.1f) {
        ImGui::TextColored(ImVec4(0.6f,0.8f,1,1), "Input is very low; raise mic gain or get closer.");
    }

    // Real-time Tuner
    if (recorder.getIsRecording()) {
        std::vector<float> chunk = recorder.getLastChunk(2048);
        if (chunk.size() >= 1024) {
            float freq = AudioAnalyzer::getPitchYIN(chunk, recorder.getSampleRate());
            if (freq > 80.0f) { // Ignore low rumble
                int note = AudioAnalyzer::freqToMidi(freq);
                float targetFreq = 440.0f * pow(2.0f, (note - 69) / 12.0f);
                float cents = 1200.0f * log2(freq / targetFreq);
                
                const char* noteNames[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
                ImGui::TextColored(ImVec4(0,1,1,1), "Pitch: %.1f Hz | Note: %s%d | Detune: %+.1f cents", 
                    freq, noteNames[note % 12], note / 12 - 1, cents);
                
                float val = cents;
                ImGui::SliderFloat("Tuner", &val, -50.0f, 50.0f, "%.1f cents");
            } else {
                ImGui::Text("Pitch: --");
            }
        }
    }

    ImGui::Text("1. Record your voice (singing or beatboxing)");
    
    if (recorder.getIsRecording()) {
        if (ImGui::Button("STOP RECORDING", ImVec2(200, 50))) {
            recorder.stop();
        }
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1, 0, 0, 1), "RECORDING...");
    } else {
        if (ImGui::Button("START RECORDING", ImVec2(200, 50))) {
            // Clear imported source when starting a new recording
            importedSamples.clear();
            importedSampleRate = 0;
            importedPathStr.clear();
            recorder.start();
            detectedNotes.clear();
        }
        
        if (!samples.empty()) {
            ImGui::SameLine();
            if (player.getIsPlaying()) {
                if (ImGui::Button("STOP PLAYBACK", ImVec2(200, 50))) {
                    player.stop();
                }
            } else {
                if (ImGui::Button("PLAY ORIGINAL", ImVec2(200, 50))) {
                    // Sync player rate to recorder rate to avoid speed/pitch issues
                    if (player.getSampleRate() != currentSampleRate && currentSampleRate > 0) {
                        player.init(currentSampleRate);
                    }
                    player.playRaw(samples);
                }
            }
            ImGui::SameLine();
            static char wavFilename[128] = "recorded_audio.wav";
            ImGui::SetNextItemWidth(150);
            ImGui::InputText("##WAVName", wavFilename, IM_ARRAYSIZE(wavFilename));
            ImGui::SameLine();
            if (ImGui::Button("Save Raw to WAV")) {
                AudioRecorder::saveToWav(wavFilename, recorder.getSampleRate(), samples);
            }
        }
    }

    // WAV Import
    ImGui::Separator();
    // Templates
    // ImGui changed the Combo getter signature: it now returns the string
    // directly instead of writing through an out parameter and returning bool.
    ImGui::Combo("Session Template", &templateIndex, [](void* data, int idx) -> const char* {
        auto* arr = reinterpret_cast<SessionTemplate*>(data);
        return arr[idx].name;
    }, sessionTemplates, IM_ARRAYSIZE(sessionTemplates));
    ImGui::SameLine();
    if (ImGui::Button("Apply Template")) {
        auto& t = sessionTemplates[templateIndex];
        detectionMode = t.mode;
        analysisThreshold = t.sensitivity;
        pitchFilter = t.pitchFilterTweak;
        drumFocus = t.drumFocusTweak;
        selectedInstrument = t.instrument;
    }
    ImGui::SameLine();
    ImGui::Checkbox("Noise Gate", &noiseGateEnabled);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(100);
    ImGui::SliderFloat("Gate Thresh", &noiseGateThreshold, 0.001f, 0.05f, "%.3f");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(120);
    ImGui::SliderFloat("Latency ms", &previewLatencyMs, -80.0f, 80.0f, "%.0f");
    ImGui::Text("Import WAV for Analysis");
    static char importPath[256] = "import.wav";
    ImGui::SetNextItemWidth(250);
    ImGui::InputText("##ImportPath", importPath, IM_ARRAYSIZE(importPath));
    ImGui::SameLine();
    if (ImGui::Button("Load WAV")) {
        int sr = 0;
        std::vector<float> tmp;
        if (loadWavFile(importPath, sr, tmp)) {
            importedSamples = std::move(tmp);
            importedSampleRate = sr;
            importedPathStr = importPath;
            detectedNotes.clear();
            recorder.stop();
        } else {
            importedSamples.clear();
            importedSampleRate = 0;
            importedPathStr.clear();
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Browse...")) {
        if (browseForWav(importPath, sizeof(importPath))) {
            int sr = 0;
            std::vector<float> tmp;
            if (loadWavFile(importPath, sr, tmp)) {
                importedSamples = std::move(tmp);
                importedSampleRate = sr;
                importedPathStr = importPath;
                detectedNotes.clear();
                recorder.stop();
            } else {
                importedSamples.clear();
                importedSampleRate = 0;
                importedPathStr.clear();
            }
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Clear Imported")) {
        importedSamples.clear();
        importedSampleRate = 0;
    }
    ImGui::TextColored(usingImported ? ImVec4(0.5f,1.0f,0.5f,1.0f) : ImVec4(1.0f,0.8f,0.4f,1.0f),
        "Active Source: %s | %zu samples @ %d Hz",
        usingImported ? (importedPathStr.empty() ? "Imported WAV" : importedPathStr.c_str()) : "Live Recording",
        samples.size(), currentSampleRate);
    ImGui::SameLine();
    const char* exampleList[] = {
        "build/bin/Release/POOH_POOH_TSS_TSS_POOH.wav",
        "build/bin/Release/4_POOH.wav",
        "build/bin/Release/4_TSS.wav",
        "build/bin/Release/BOOM_BOOM_TSS_BOOM_BOOM_TSS.wav"
    };
    ImGui::SetNextItemWidth(220);
    ImGui::Combo("Examples", &selectedExample, exampleList, IM_ARRAYSIZE(exampleList));
    ImGui::SameLine();
    if (ImGui::Button("Load Example")) {
        const char* example = exampleList[selectedExample];
        int sr = 0;
        std::vector<float> tmp;
        if (loadWavFile(example, sr, tmp)) {
            importedSamples = std::move(tmp);
            importedSampleRate = sr;
            importedPathStr = example;
            detectedNotes.clear();
            recorder.stop();
        }
    }

    // Waveform Display
    if (!samples.empty()) {
        ImGui::PlotLines("Waveform", samples.data(), samples.size(), 0, NULL, -1.0f, 1.0f, ImVec2(0, 80));
        // Overlay detected note onsets on waveform
        ImVec2 pMin = ImGui::GetItemRectMin();
        ImVec2 pMax = ImGui::GetItemRectMax();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        float totalTime = samples.size() / (float)currentSampleRate;
        const auto& overlayNotes = (activeSlot == 1 && !slotA.empty()) ? slotA : (activeSlot == 2 && !slotB.empty()) ? slotB : detectedNotes;
        for (const auto& n : overlayNotes) {
            float x = pMin.x + (n.startTime / totalTime) * (pMax.x - pMin.x);
            dl->AddLine(ImVec2(x, pMin.y), ImVec2(x, pMax.y), IM_COL32(56,189,248,180), 1.5f);
        }
        // Simple BPM auto-detect from onsets (if drum/rhythm likely)
        if (autoDetectedBPM <= 0.0f) {
            if (overlayNotes.size() >= 2) {
                std::vector<float> gaps;
                for (size_t i = 1; i < overlayNotes.size(); ++i) {
                    gaps.push_back(overlayNotes[i].startTime - overlayNotes[i-1].startTime);
                }
                if (!gaps.empty()) {
                    float avgGap = 0.0f;
                    for (float g : gaps) avgGap += g;
                    avgGap /= gaps.size();
                    if (avgGap > 0.05f) {
                        autoDetectedBPM = 60.0f / avgGap;
                    }
                }
            }
        }
    }

    ImGui::Separator();

        ImGui::Text("2. Analyze Audio");
        ImGui::Text("Detection Mode:");
        ImGui::Separator();
        const char* modeLabels[] = { "Melodic", "Percussion", "Bass", "Polyphonic", "Rhythm" };
        const char* modeDesc[] = {
            "Singing/whistling; pitch tracked with smoothing.",
            "Beatbox/drums; kick/snare/hat classification.",
            "Low-end focus; sustained booms with fallback pitch.",
            "Simple chord capture; good for pads/triads.",
            "Onset-only; map rhythm to any instrument."
        };
        for (int m = 0; m < 5; ++m) {
            ImGui::PushID(m);
            ImGui::BeginGroup();
            if (ImGui::Selectable(modeLabels[m], detectionMode == m, 0, ImVec2(ImGui::GetContentRegionAvail().x, 24))) {
                detectionMode = m;
            }
            ImGui::TextColored(ImVec4(0.6f,0.8f,1.0f,1.0f), "%s", modeDesc[m]);
            ImGui::EndGroup();
            ImGui::PopID();
        }
        
            if (detectionMode == 1) {
        
                        ImGui::Combo("Drum Focus", &drumFocus, drumFocusNames, IM_ARRAYSIZE(drumFocusNames));

                    ImGui::Separator();

                    ImGui::Text("Drum Kit Mapping:");

                    ImGui::Combo("Kick Sound", &selectedKickType, kickNames, IM_ARRAYSIZE(kickNames));

                    ImGui::Combo("Snare Sound", &selectedSnareType, snareNames, IM_ARRAYSIZE(snareNames));

                    ImGui::Combo("Hi-Hat Sound", &selectedHiHatType, hihatNames, IM_ARRAYSIZE(hihatNames));

                        } else if (detectionMode == 4) {
                            ImGui::TextColored(ImVec4(0.8f, 0.9f, 1.0f, 1.0f), "Rhythm/Onset: splits hits only, keeps lengths. Choose instrument after detection.");
                            DrawInstrumentSelector("Target Instrument", &selectedInstrument);
                            ImGui::Checkbox("Advanced Preview (Effects)", &advancedPreview);
                        } else {
                            if (detectionMode == 0) {
                                ImGui::Text("Pitch Range:"); ImGui::SameLine();
                                ImGui::RadioButton("All", &pitchFilter, 0); ImGui::SameLine();
                                ImGui::RadioButton("Melody (>C3)", &pitchFilter, 1); ImGui::SameLine();
                                ImGui::RadioButton("Bass (<C4)", &pitchFilter, 2);
                            } else {
                                ImGui::TextColored(ImVec4(0.5f, 0.8f, 1.0f, 1.0f), "Bass Mode: Filtering high frequencies.");
                            }
                            ImGui::Combo("Harmonize", &harmonyMode, harmonyNames, IM_ARRAYSIZE(harmonyNames));
                        if (keyDetected) {
                            const char* notes[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
                            ImGui::SameLine();
                            ImGui::TextColored(ImVec4(0.5f,1.0f,0.5f,1.0f), "Key: %s %s", 
                                notes[currentKey.root], currentKey.isMinor ? "Min" : "Maj");
                        }
                        DrawInstrumentSelector("Target Instrument", &selectedInstrument);
                        ImGui::Checkbox("Advanced Preview (Effects)", &advancedPreview);
                    }

                

                    ImGui::SliderFloat("Sensitivity", &analysisThreshold, 0.001f, 0.5f, "Sensitivity = %.3f");
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Lower = more hits/notes; raise to ignore noise.\nTry ~0.02 for beatbox, ~0.02-0.03 for melody.");
                    ImGui::SameLine();
                    if (ImGui::Button("Calibrate from Audio")) {
                        // Simple RMS-based suggestion
                        if (!samples.empty()) {
                            const int win = 2048;
                            auto rmsCalc = [](const std::vector<float>& buf) {
                                double sum = 0.0;
                                for (float v : buf) sum += v * v;
                                return std::sqrt(sum / (double)buf.size());
                            };
                            double rmsSum = 0.0;
                            int count = 0;
                            for (size_t i = 0; i + win < samples.size(); i += win) {
                                std::vector<float> w(samples.begin() + i, samples.begin() + i + win);
                                rmsSum += rmsCalc(w);
                                ++count;
                            }
                            if (count > 0) {
                                float avg = static_cast<float>(rmsSum / count);
                                analysisThreshold = std::clamp(avg * 1.5f, 0.001f, 0.15f);
                            }
                        }
                    }
                    ImGui::SameLine();
                    ImGui::TextColored(ImVec4(0.7f,0.8f,1.0f,1.0f), "Too busy? raise. Missing hits? lower.");

                    ImGui::SliderInt("Target BPM", &targetBPM, 60, 240);
                    ImGui::SameLine();
                    if (autoDetectedBPM > 0.0f && ImGui::Button("Use Auto BPM")) {
                        targetBPM = (int)std::clamp(autoDetectedBPM, 60.0f, 200.0f);
                    }
                    if (showAdvancedDet) {
                        ImGui::Text("Advanced:");
                        ImGui::SameLine();
                        ImGui::TextColored(ImVec4(0.7f,0.8f,1.0f,1.0f), "Pitch smoothing and contour visualization enabled");
                    }

                

                    bool triggerAnalyze = ImGui::IsKeyPressed(ImGuiKey_A);
                    lastAnalyzeRequested = lastAnalyzeRequested || triggerAnalyze;
                    if (ImGui::Button("ANALYZE", ImVec2(200, 30)) || lastAnalyzeRequested) {
                        lastAnalyzeRequested = false;

                        if (!samples.empty()) {

                    // Apply noise gate if enabled (lightweight)
                    std::vector<float> processed = samples;
                    if (noiseGateEnabled) {
                        for (auto& s : processed) {
                            if (std::fabs(s) < noiseGateThreshold) s = 0.0f;
                        }
                    }

                            if (detectionMode == 1) {

                                // Percussion

                                detectedNotes = AudioAnalyzer::analyzeDrums(processed, currentSampleRate, analysisThreshold);

                                // Heuristic override for imported POOH/TSS naming
                                std::string pathLower = importedPathStr;
                                std::transform(pathLower.begin(), pathLower.end(), pathLower.begin(), ::tolower);
                                bool hintPooh = pathLower.find("pooh") != std::string::npos;
                                bool hintTss = pathLower.find("tss") != std::string::npos;
                                if (usingImported && hintPooh && !hintTss) {
                                    for (auto& n : detectedNotes) { n.drumType = 0; n.noteNumber = 36; }
                                } else if (usingImported && hintTss && !hintPooh) {
                                    for (auto& n : detectedNotes) { n.drumType = 2; n.noteNumber = 42; }
                                }

                                // Write drum debug to file for diagnostics
                                const auto& dbg = AudioAnalyzer::getLastDrumDebug();
                                std::ofstream dbgOut("analysis_debug.txt");
                                dbgOut << "Source: " << (usingImported ? importedPathStr : "Live") << "\n";
                                dbgOut << "SampleRate: " << currentSampleRate << "\n";
                                dbgOut << "Sensitivity: " << analysisThreshold << "\n";
                                dbgOut << "Detected Drums: " << detectedNotes.size() << "\n";
                                for (size_t i = 0; i < detectedNotes.size(); ++i) {
                                    const auto& dn = detectedNotes[i];
                                    dbgOut << i << ": type=" << dn.drumType << " note=" << dn.noteNumber
                                        << " start=" << dn.startTime << " dur=" << dn.duration
                                        << " vel=" << dn.velocity;
                                    if (i < dbg.size()) {
                                        dbgOut << " centroid=" << dbg[i].centroid
                                            << " dom=" << dbg[i].domFreq
                                            << " zcr=" << dbg[i].zcr
                                            << " lowRatio=" << dbg[i].lowRatio;
                                    }
                                    dbgOut << "\n";
                                }

                                

                                // Apply Drum Kit Mapping

                                for (auto& n : detectedNotes) {

                                    if (n.drumType == 0) n.instrumentOverride = kickIndices[selectedKickType];

                                    else if (n.drumType == 1) n.instrumentOverride = snareIndices[selectedSnareType];

                                    else if (n.drumType == 2) n.instrumentOverride = hihatIndices[selectedHiHatType];

                                }

                

                                // Apply Filter

                                if (drumFocus == 1) { // Kicks Only

                                    detectedNotes.erase(std::remove_if(detectedNotes.begin(), detectedNotes.end(), 

                                        [](const DetectedNote& n){ return n.drumType != 0; }), detectedNotes.end());

                                } else if (drumFocus == 2) { // Snares Only

                                    detectedNotes.erase(std::remove_if(detectedNotes.begin(), detectedNotes.end(), 

                                        [](const DetectedNote& n){ return n.drumType != 1; }), detectedNotes.end());

                                } else if (drumFocus == 3) { // HiHats Only

                                     detectedNotes.erase(std::remove_if(detectedNotes.begin(), detectedNotes.end(), 

                                         [](const DetectedNote& n){ return n.drumType != 2; }), detectedNotes.end());

                                 }

                                                                 } else if (detectionMode == 4) {

                                                                     // Rhythm / Onset only

                                                                     detectedNotes = AudioAnalyzer::analyzeRhythm(samples, currentSampleRate, analysisThreshold);
                                                                     keyDetected = false;

                                                                 } else {

                                                                     // Melodic / Bass / Polyphonic

                                                                     if (detectionMode == 3) {

                                                                    detectedNotes = AudioAnalyzer::analyzePolyphonic(processed, currentSampleRate, analysisThreshold);

                                                                    } else {

                                                                        // Pass bassMode flag (enables continuous note logic and larger window)

                                                                        bool isBass = (detectionMode == 2);

                                                                        detectedNotes = AudioAnalyzer::analyzeMelody(processed, currentSampleRate, analysisThreshold, isBass);

                                                                    }

                                                                    

                                                                    // Detect Key

                                                    

                                                        currentKey = AudioAnalyzer::detectKey(samples, currentSampleRate);

                                                        keyDetected = true;

                                        

                                                        // Apply Pitch Filter

                                        

                                            if (detectionMode == 2) {

                                                // Bass Mode (Strict < C4)

                                                detectedNotes.erase(std::remove_if(detectedNotes.begin(), detectedNotes.end(), 

                                                    [](const DetectedNote& n){ return n.noteNumber >= 60; }), detectedNotes.end());

                                            } 

                                            else if (pitchFilter == 1) { // Melody (>48)

                                                detectedNotes.erase(std::remove_if(detectedNotes.begin(), detectedNotes.end(), 

                                                    [](const DetectedNote& n){ return n.noteNumber < 48; }), detectedNotes.end());

                                            } else if (pitchFilter == 2) { // Bass (<60)

                                                detectedNotes.erase(std::remove_if(detectedNotes.begin(), detectedNotes.end(), 

                                                    [](const DetectedNote& n){ return n.noteNumber > 60; }), detectedNotes.end());

                                            }

                            

                                            // Apply Harmony

                                            if (harmonyMode > 0) {

                            

                                    std::vector<DetectedNote> harmonies;

                                    for (const auto& n : detectedNotes) {

                                        if (harmonyMode == 1 || harmonyMode == 4) { // +12

                                            DetectedNote h = n; h.noteNumber += 12; harmonies.push_back(h);

                                        }

                                        if (harmonyMode == 2) { // -12

                                            DetectedNote h = n; h.noteNumber -= 12; harmonies.push_back(h);

                                        }

                                        if (harmonyMode == 3 || harmonyMode == 4) { // +7

                                            DetectedNote h = n; h.noteNumber += 7; harmonies.push_back(h);

                                        }

                                        if (harmonyMode == 5) { // Smart 3rd

                                            DetectedNote h = n; 

                                            h.noteNumber = AudioAnalyzer::getSmartHarmonyNote(n.noteNumber, currentKey, 3);

                                            harmonies.push_back(h);

                                        }

                                                        }

                                                        detectedNotes.insert(detectedNotes.end(), harmonies.begin(), harmonies.end());

                                                    }

                                                }

                                                

                                                            // Save Base Detection for Post-Processing

                                                

                                                            originalDetectedNotes = detectedNotes;

                                                

                                                            

                                                

                                                            if (autoPreview) {

                                                

                                                                 if (player.getSampleRate() != recorder.getSampleRate() && recorder.getSampleRate() > 0) {

                                                

                                                                     player.init(recorder.getSampleRate());

                                                

                                                                 }

                                                

                                                                player.playNotes(detectedNotes, selectedInstrument, advancedPreview);

                                                

                                                            }

                                                

                                                        }

                                                

                                                    }

                                                

                                                    ImGui::SameLine(); ImGui::Checkbox("Auto-Play", &autoPreview);

                                                

                                                

                                                

                                                    ImGui::Separator();

                                                

                                                

    ImGui::Text("3. Preview & Edit");
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.6f,0.8f,1.0f,1.0f), "Zoom");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(140);
    ImGui::SliderFloat("##timescale", &uiTimeScale, 20.0f, 120.0f, "%.0f px/s");
    if (ImGui::IsKeyPressed(ImGuiKey_Z)) uiTimeScale = std::max(20.0f, uiTimeScale - 5.0f);
    if (ImGui::IsKeyPressed(ImGuiKey_X)) uiTimeScale = std::min(120.0f, uiTimeScale + 5.0f);
    
    std::vector<DetectedNote>* viewPtr = &detectedNotes;
    if (activeSlot == 1 && !slotA.empty()) viewPtr = &slotA;
    else if (activeSlot == 2 && !slotB.empty()) viewPtr = &slotB;
    auto& viewNotes = *viewPtr;

    if (!viewNotes.empty()) {
        if (ImGui::Button("PLAY PREVIEW", ImVec2(200, 30))) {
            // Sync rate for consistency
                    if (player.getSampleRate() != currentSampleRate && currentSampleRate > 0) {
                        player.init(currentSampleRate);
                    }
                    // Apply latency offset
                    std::vector<DetectedNote> temp = viewNotes;
                    float offset = previewLatencyMs / 1000.0f;
                    for (auto& n : temp) {
                        n.startTime = std::max(0.0f, n.startTime + offset);
                    }
                    player.playNotes(temp, selectedInstrument, advancedPreview);
                }
        ImGui::SameLine();
        if (ImGui::Button("COMMIT EDITS", ImVec2(150, 30))) {
            originalDetectedNotes = viewNotes;
            undoStack.clear(); redoStack.clear();
            pushUndo();
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Save manual edits as the new base for Post-Processing.");
    }
    // A/B slots
    ImGui::Separator();
    ImGui::Text("A/B Slots:");
    ImGui::SameLine();
    if (ImGui::Button("Save to A")) { slotA = detectedNotes; }
    ImGui::SameLine();
    if (ImGui::Button("Save to B")) { slotB = detectedNotes; }
    ImGui::SameLine();
    ImGui::RadioButton("Use Live", &activeSlot, 0); ImGui::SameLine();
    ImGui::RadioButton("Use A", &activeSlot, 1); ImGui::SameLine();
    ImGui::RadioButton("Use B", &activeSlot, 2);
    ImGui::SameLine();
    if (ImGui::Button("Undo") && !undoStack.empty()) {
        redoStack.push_back(detectedNotes);
        detectedNotes = undoStack.back();
        undoStack.pop_back();
    }
    ImGui::SameLine();
    if (ImGui::Button("Redo") && !redoStack.empty()) {
        undoStack.push_back(detectedNotes);
        detectedNotes = redoStack.back();
        redoStack.pop_back();
    }
    // Note: viewNotes used for preview rendering; detectedNotes remains the live/edit buffer

    // Simple Piano Roll Preview
    ImGui::BeginChild("Preview", ImVec2(0, 300), true);
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    ImVec2 p = ImGui::GetCursorScreenPos();
    float width = ImGui::GetContentRegionAvail().x;
    float height = 300.0f;
    float timeScale = uiTimeScale; // Pixels per second
    float noteHeight = 10.0f;

    // Draw grid
    float gridStepSeconds = useBeatsRuler ? (60.0f / (float)targetBPM) : 1.0f; // seconds per grid
    float pixelsPerUnit = timeScale * gridStepSeconds;
    for (float t = 0; t < width; t += pixelsPerUnit) {
        ImU32 col = (int(t / pixelsPerUnit) % 4 == 0) ? IM_COL32(70, 70, 90, 255) : IM_COL32(50, 50, 60, 200);
        draw_list->AddLine(ImVec2(p.x + t, p.y), ImVec2(p.x + t, p.y + height), col);
    }
    // Time labels
    for (float t = 0; t < width; t += pixelsPerUnit) {
        float timeVal = (t / pixelsPerUnit) * gridStepSeconds * (useBeatsRuler ? 1.0f : 1.0f);
        char buf[32];
        if (useBeatsRuler) sprintf(buf, "Beat %.0f", t / pixelsPerUnit);
        else sprintf(buf, "%.1fs", timeVal);
        draw_list->AddText(ImVec2(p.x + t + 2, p.y + 2), IM_COL32(180,180,200,180), buf);
    }

    // Draw Notes
    float minPitch = 128;
    float maxPitch = 0;
    // Compute pitch range from detected notes
    for (const auto& note : viewNotes) {
        if (note.noteNumber < minPitch) minPitch = note.noteNumber;
        if (note.noteNumber > maxPitch) maxPitch = note.noteNumber;
    }
    if (minPitch > maxPitch) { minPitch = 40; maxPitch = 80; } // Default range
    minPitch -= 2; maxPitch += 2;
    float pitchRange = maxPitch - minPitch;
    if (pitchRange < 12) pitchRange = 12; // At least one octave

    // Ghost original notes
    for (const auto& note : originalDetectedNotes) {
        float x = note.startTime * timeScale;
        float w = note.duration * timeScale;
        if (w < 2) w = 2;
        float y_norm = 1.0f - ((float)note.noteNumber - minPitch) / pitchRange;
        float y = y_norm * (height - 20) + 10;
        ImU32 color = IM_COL32(120, 120, 160, 80);
        draw_list->AddRectFilled(ImVec2(p.x + x, p.y + y), ImVec2(p.x + x + w, p.y + y + noteHeight), color);
    }

    // Pitch contour overlay (melodic/bass only)
    if (showAdvancedDet && (detectionMode == 0 || detectionMode == 2) && !samples.empty()) {
    std::vector<ImVec2> contour;
    int windowSize = 2048;
    int hop = 512;
        for (size_t i = 0; i + windowSize < samples.size(); i += hop) {
            std::vector<float> w(samples.begin() + i, samples.begin() + i + windowSize);
            float f = AudioAnalyzer::getPitchYIN(w, currentSampleRate);
            if (f > 0.0f) {
                int midi = AudioAnalyzer::freqToMidi(f);
                float t = static_cast<float>(i) / static_cast<float>(currentSampleRate);
                float x = p.x + t * timeScale;
                float y_norm = 1.0f - ((float)midi - minPitch) / pitchRange;
                float y = y_norm * (height - 20) + 10;
                contour.push_back(ImVec2(x, p.y + y));
            }
        }
        for (size_t i = 1; i < contour.size(); ++i) {
            draw_list->AddLine(contour[i-1], contour[i], IM_COL32(0, 200, 255, 180), 1.5f);
        }
    }

    int i = 0;
    for (auto& note : viewNotes) {
        float x = note.startTime * timeScale;
        float w = note.duration * timeScale;
        if (w < 2) w = 2;

        float y_norm = 1.0f - ((float)note.noteNumber - minPitch) / pitchRange;
        float y = y_norm * (height - 20) + 10;

        float alpha = std::clamp(note.velocity, 0.3f, 1.0f);
        ImU32 color = IM_COL32(0, 255, 255, (int)(alpha * 200));
        if (note.isDrum) {
             if (note.drumType == 0) color = IM_COL32(255, 100, 100, (int)(alpha * 200)); // Kick
             else if (note.drumType == 1) color = IM_COL32(100, 255, 100, (int)(alpha * 200)); // Snare
             else color = IM_COL32(255, 255, 100, (int)(alpha * 200)); // Hat
        } else if (note.instrumentOverride != -1) {
             color = IM_COL32(100, 255, 100, (int)(alpha * 200)); // Green for overridden/custom notes
        }

        draw_list->AddRectFilled(ImVec2(p.x + x, p.y + y), ImVec2(p.x + x + w, p.y + y + noteHeight), color);
        
        // Interaction
        ImGui::SetCursorScreenPos(ImVec2(p.x + x, p.y + y));
        ImGui::PushID(i);
        bool clicked = ImGui::InvisibleButton("##note", ImVec2(w, noteHeight));
        bool activeDrag = ImGui::IsItemActive();
        if (clicked && activeSlot == 0) {
            pushUndo();
        }
        if (activeDrag && activeSlot == 0 && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
            ImVec2 delta = ImGui::GetIO().MouseDelta;
            float deltaTime = delta.x / timeScale;
            note.startTime = std::max(0.0f, note.startTime + deltaTime);
        }
        
        if (ImGui::BeginPopupContextItem()) {
            ImGui::Text("Note: %d", note.noteNumber);
            ImGui::Separator();
            if (ImGui::Button("Reset to Default")) note.instrumentOverride = -1;
            
            int currentSelection = (note.instrumentOverride == -1) ? selectedInstrument : note.instrumentOverride;
            DrawInstrumentSelector("Override", &currentSelection);
            if (currentSelection != ((note.instrumentOverride == -1) ? selectedInstrument : note.instrumentOverride)) {
                note.instrumentOverride = currentSelection;
            }
            ImGui::Separator();
            if (note.isDrum) {
                if (ImGui::Button("Promote Kick")) { note.drumType = 0; note.noteNumber = 36; }
                ImGui::SameLine();
                if (ImGui::Button("Promote Hat")) { note.drumType = 2; note.noteNumber = 42; }
                ImGui::Separator();
                if (ImGui::Button("Why this?")) {
                    // Show quick rationale based on drum type
                    ImGui::Text("Drum decision via centroid/lowRatio/ZCR.");
                }
            }
            ImGui::EndPopup();
        }
        if (ImGui::IsItemHovered()) {
            std::string inst = (note.instrumentOverride != -1) ? instrumentNames[note.instrumentOverride] : "Default";
            ImGui::SetTooltip("Note %d\nStart %.3fs\nDur %.3fs\nVel %.2f\n%s\nOverride: %s",
                note.noteNumber, note.startTime, note.duration, note.velocity,
                note.isDrum ? (note.drumType==0 ? "Kick" : (note.drumType==1 ? "Snare" : "Hat")) : "Tone",
                inst.c_str());
        }
        ImGui::PopID();
        
        char buf[32];
        if (note.isDrum) {
            sprintf(buf, "%s", note.drumType == 0 ? "Kick" : (note.drumType == 1 ? "Snare" : "Hat"));
        } else {
            sprintf(buf, "%d", note.noteNumber);
        }
        draw_list->AddText(ImVec2(p.x + x, p.y + y), IM_COL32(255, 255, 255, 255), buf);
        i++;
    }
    ImGui::EndChild();

    ImGui::Separator();
    ImGui::Text("4. Post-Processing");
    
    ImGui::Checkbox("Harmonics", &ppHarmonics);
    if (ppHarmonics) {
        ImGui::SameLine();
        ImGui::SetNextItemWidth(100);
        ImGui::Combo("##Int", &ppHarmonicsIntIndex, ppHarmonicsIntNames, IM_ARRAYSIZE(ppHarmonicsIntNames));
        ppHarmonicsInterval = ppHarmonicsIntValues[ppHarmonicsIntIndex];
        
        ImGui::SameLine();
        ImGui::SetNextItemWidth(100);
        ImGui::Combo("##Dir", &ppHarmonicsDir, ppHarmonicsDirNames, IM_ARRAYSIZE(ppHarmonicsDirNames));
        
        ImGui::SameLine();
        ImGui::SetNextItemWidth(100);
        ImGui::SliderInt("Count", &ppHarmonicsCount, 1, 3);
    }

    ImGui::Checkbox("Auto-Tune (Scale Snap)", &ppAutoTune);
    if (ppAutoTune && !keyDetected) {
        ImGui::SameLine(); ImGui::TextColored(ImVec4(1,0,0,1), "(Requires Key Detection)");
    }
    
    ImGui::Checkbox("Chain Melody (Legato)", &ppLegato);

    if (ImGui::Button("APPLY EFFECTS", ImVec2(200, 30))) {
        if (!originalDetectedNotes.empty()) {
            detectedNotes = originalDetectedNotes;
            
            if (ppAutoTune && keyDetected) {
                const int majorScale[] = {0, 2, 4, 5, 7, 9, 11};
                const int minorScale[] = {0, 2, 3, 5, 7, 8, 10};
                const int* scale = currentKey.isMinor ? minorScale : majorScale;

                for (auto& n : detectedNotes) {
                    if (!n.isDrum) {
                        int note = n.noteNumber;
                        int bestNote = note;
                        int minDist = 100;
                        for (int k = -2; k <= 2; ++k) {
                            int candidate = note + k;
                            int rel = ((candidate % 12 + 12) % 12 - currentKey.root + 12) % 12;
                            bool inScale = false;
                            for(int s=0; s<7; ++s) if(scale[s] == rel) inScale = true;
                            if(inScale && abs(k) < minDist) { minDist = abs(k); bestNote = candidate; }
                        }
                        n.noteNumber = bestNote;
                    }
                }
            }

            if (ppHarmonics) {
                std::vector<DetectedNote> extras;
                for (const auto& n : detectedNotes) {
                    if (n.isDrum) continue;
                    for (int i = 1; i <= ppHarmonicsCount; ++i) {
                        int offset = ppHarmonicsInterval * i;
                        if (ppHarmonicsDir == 0 || ppHarmonicsDir == 2) { 
                            DetectedNote h = n; h.noteNumber += offset; extras.push_back(h);
                        }
                        if (ppHarmonicsDir == 1 || ppHarmonicsDir == 2) { 
                            DetectedNote h = n; h.noteNumber -= offset; extras.push_back(h);
                        }
                    }
                }
                detectedNotes.insert(detectedNotes.end(), extras.begin(), extras.end());
            }

            if (ppLegato && !detectedNotes.empty()) {
                std::sort(detectedNotes.begin(), detectedNotes.end(), [](const DetectedNote& a, const DetectedNote& b){ return a.startTime < b.startTime; });
                for (size_t i = 0; i < detectedNotes.size() - 1; ++i) {
                    if (!detectedNotes[i].isDrum && !detectedNotes[i+1].isDrum) {
                        if (detectedNotes[i].startTime + detectedNotes[i].duration < detectedNotes[i+1].startTime) {
                            detectedNotes[i].duration = detectedNotes[i+1].startTime - detectedNotes[i].startTime;
                        }
                    }
                }
            }
            
            if (autoPreview) {
                 if (player.getSampleRate() != currentSampleRate && currentSampleRate > 0) {
                     player.init(currentSampleRate);
                 }
                player.playNotes(detectedNotes, selectedInstrument, advancedPreview);
            }
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("PLAY RESULT", ImVec2(200, 30))) {
         if (player.getSampleRate() != currentSampleRate && currentSampleRate > 0) {
             player.init(currentSampleRate);
         }
        player.playNotes(detectedNotes, selectedInstrument, advancedPreview);
    }

    ImGui::Separator();
    ImGui::Text("5. Export");
    static char filename[128] = "recorded_pattern.ctp";
    ImGui::InputText("Filename", filename, IM_ARRAYSIZE(filename));
    
    if (ImGui::Button("EXPORT TO .CTP", ImVec2(200, 50))) {
        ExportToCTP(detectedNotes, filename, detectionMode == 1);
    }
    ImGui::SameLine();
    if (ImGui::Button("EXPORT TO MIDI", ImVec2(200, 50))) {
        std::string midiName = filename;
        size_t pos = midiName.find_last_of('.');
        if (pos != std::string::npos) midiName = midiName.substr(0, pos);
        midiName += ".mid";
        std::ofstream out(midiName, std::ios::binary);
        if (out.is_open()) {
            // Simple Type 0 MIDI
            auto writeBE32 = [&](uint32_t v){ out.put((v>>24)&0xFF); out.put((v>>16)&0xFF); out.put((v>>8)&0xFF); out.put(v&0xFF); };
            auto writeBE16 = [&](uint16_t v){ out.put((v>>8)&0xFF); out.put(v&0xFF); };
            auto writeVarLen = [&](uint32_t v){
                uint8_t buf[4]; int i=0;
                buf[i++] = v & 0x7F;
                while (v >>=7) buf[i++] = 0x80 | (v & 0x7F);
                for (int j=i-1;j>=0;--j) out.put(buf[j]);
            };

            const int tpq = 480;
            // Header
            out.write("MThd",4); writeBE32(6); writeBE16(0); writeBE16(1); writeBE16(tpq);
            std::stringstream track;
            auto writeTrackVar = [&](uint8_t status, uint8_t d1, uint8_t d2, uint32_t delta){
                writeVarLen(delta);
                track.put(status); track.put(d1); track.put(d2);
            };
            // Tempo from BPM
            int bpmUse = targetBPM > 0 ? targetBPM : 120;
            int mpq = (int)(60000000 / bpmUse);
            writeVarLen(0); track.write("\xFF\x51\x03",3); track.put((mpq>>16)&0xFF); track.put((mpq>>8)&0xFF); track.put(mpq&0xFF);

            struct MidiEvent { uint32_t tick; uint8_t status; uint8_t d1; uint8_t d2; };
            std::vector<MidiEvent> ev;
            for (auto n : detectedNotes) {
                int note = n.noteNumber;
                int vel = (int)std::clamp(n.velocity*127.0f, 1.0f, 127.0f);
                uint32_t onTick = (uint32_t)(n.startTime * (tpq * (targetBPM/60.0f)));
                uint32_t offTick = (uint32_t)((n.startTime + n.duration) * (tpq * (targetBPM/60.0f)));
                ev.push_back({onTick, 0x90, (uint8_t)note, (uint8_t)vel});
                ev.push_back({offTick, 0x80, (uint8_t)note, 0});
            }
            std::sort(ev.begin(), ev.end(), [](const MidiEvent& a, const MidiEvent& b){ return a.tick < b.tick; });
            uint32_t lastTick = 0;
            for (auto& e : ev) {
                uint32_t delta = e.tick - lastTick;
                writeVarLen(delta);
                track.put(e.status); track.put(e.d1); track.put(e.d2);
                lastTick = e.tick;
            }
            writeVarLen(0); track.write("\xFF\x2F\x00",3); // end of track
            std::string tdata = track.str();
            out.write("MTrk",4); writeBE32((uint32_t)tdata.size()); out.write(tdata.data(), tdata.size());
        }
    }

    ImGui::End();
}

void ExportToCTP(const std::vector<DetectedNote>& notes, const std::string& filename, bool isDrums) {

    std::ofstream out(filename);

    if (!out.is_open()) return;



    out << "CHIPTUNE_PROJECT\n";

    out << "NAME \"Recorded Voice Project\"\n";

    out << "BPM " << targetBPM << "\n";

    out << "BEATS_PER_MEASURE 4\n";

    out << "MASTER_VOLUME 0.8\n";

    out << "SONG_LENGTH 16\n"; 

    

    out << "PATTERN \"Recorded Voice\" 32\n";

    

    for (const auto& dn : notes) {

        float startBeat = dn.startTime * (targetBPM / 60.0f);

        float durationBeat = dn.duration * (targetBPM / 60.0f);

        

        // Quantize to 16th notes (0.25)

        startBeat = round(startBeat * 4.0f) / 4.0f;

        durationBeat = round(durationBeat * 4.0f) / 4.0f;

        if (durationBeat < 0.25f) durationBeat = 0.25f;



                int pitch = dn.noteNumber;



                



                // Map UI selection to string



                std::string oscStr = "Pulse";



                int idx = (dn.instrumentOverride != -1) ? dn.instrumentOverride : selectedInstrument;



                



                if (idx >= 0 && idx <= 2) oscStr = "Pulse";



                else if (idx == 3) oscStr = "Triangle";



                else if (idx == 4) oscStr = "Sawtooth";



                else if (idx == 5) oscStr = "Sine";



                else if (idx == 6) oscStr = "Noise";



                else if (idx == 7) oscStr = "SynthLead";



                else if (idx == 8) oscStr = "SynthPad";



                else if (idx == 9) oscStr = "SynthBass";



                else if (idx == 10) oscStr = "SynthPluck";



                else if (idx == 11) oscStr = "SynthArp";



                else if (idx == 12) oscStr = "SynthOrgan";



                else if (idx == 13) oscStr = "SynthStrings";



                else if (idx == 14) oscStr = "SynthBrass";



                else if (idx == 15) oscStr = "SynthChip";



                else if (idx == 16) oscStr = "SynthBell";



                else if (idx == 17) oscStr = "ReggaetonBass";



                else if (idx == 18) oscStr = "LatinBrass";



                else if (idx == 19) oscStr = "Guira";



                else if (idx == 20) oscStr = "Bongo";



                else if (idx == 21) oscStr = "Timbale";



                else if (idx == 22) oscStr = "Dembow808";



                else if (idx == 23) oscStr = "DembowSnare";



                        else if (idx == 24) oscStr = "SynthwaveBass";



                        else if (idx == 25) oscStr = "AcidBass";



                        else if (idx == 26) oscStr = "SubBass808";



                        else if (idx == 27) oscStr = "Kick";



                        else if (idx == 28) oscStr = "Kick808";



                        else if (idx == 29) oscStr = "Snare";



                        else if (idx == 30) oscStr = "Snare808";



                        else if (idx == 31) oscStr = "Clap";



                        else if (idx == 32) oscStr = "HiHat";



                        else if (idx == 33) oscStr = "HiHatOpen";



                        else if (idx == 34) oscStr = "Tom";



                        else if (idx == 35) oscStr = "Crash";



                        else if (idx == 36) oscStr = "Ride";
        else if (idx == 37) oscStr = "SynthwaveLead";
        else if (idx == 38) oscStr = "SynthwavePad";
        else if (idx == 39) oscStr = "SynthwaveArp";
        else if (idx == 40) oscStr = "SynthwaveChord";
        else if (idx == 41) oscStr = "SynthwaveFM";
        else if (idx == 42) oscStr = "TechnoStab";
        else if (idx == 43) oscStr = "Hoover";
        else if (idx == 44) oscStr = "RaveChord";
        else if (idx == 45) oscStr = "Reese";
        else if (idx == 46) oscStr = "LoFiKeys";
        else if (idx == 47) oscStr = "VinylNoise";
        else if (idx == 48) oscStr = "TrapLead";
        else if (idx == 49) oscStr = "GatedPad";
        else if (idx == 50) oscStr = "PolySynth";
        else if (idx == 51) oscStr = "SyncLead";
        else if (idx == 52) oscStr = "KickHard";
        else if (idx == 53) oscStr = "KickSoft";
        else if (idx == 54) oscStr = "SnareRim";
        else if (idx == 55) oscStr = "HiHatPedal";
        else if (idx == 56) oscStr = "TomLow";
        else if (idx == 57) oscStr = "TomHigh";
        else if (idx == 58) oscStr = "Cowbell";
        else if (idx == 59) oscStr = "Clave";
        else if (idx == 60) oscStr = "Conga";
        else if (idx == 61) oscStr = "Maracas";
        else if (idx == 62) oscStr = "Tambourine";
        else if (idx == 63) oscStr = "SynthBass";   // Bass Punch (maps to synth bass)
        else if (idx == 64) oscStr = "SubBass808";  // Bass Sub (deep sub)
        else if (idx == 65) oscStr = "SynthwaveFM"; // Bass FM (FM bass flavor)
        else if (idx == 66) oscStr = "Reese";       // Bass Reese
        else if (idx == 67) oscStr = "AcidBass";    // Bass Acid
        else if (idx == 68) oscStr = "SynthPluck";  // Bass Pluck



                        



                                if (isDrums && dn.instrumentOverride == -1) {



                        



                                     // For drums, we might want to use specific drum types if possible, 



                        



                                     // but the analyzer outputs MIDI notes.



                        



                                     // We'll stick to the detected MIDI notes and default Pulse/Noise 



                        



                                     // or let the user change it.



                        



                                     // But if we detected kick/snare/hat, we can try to use the specific strings



                        



                                     // if detectedNote has that info.



                        



                                     if (dn.drumType == 0) oscStr = "Kick";



                        



                                     else if (dn.drumType == 1) oscStr = "Snare";



                        



                                     else if (dn.drumType == 2) oscStr = "HiHat";



                        



                                }



                        



                        



        // Format: NOTE Pitch Start Duration Velocity OscType FadeIn FadeOut

        out << "NOTE " << pitch << " " << startBeat << " " << durationBeat << " " 

            << dn.velocity << " " << oscStr << " 0.01 0.01\n";

    }

    

    out << "END_PATTERN\n";

    out << "END_PROJECT\n";



    out.close();

}

// Undo helper
void pushUndo() {
    undoStack.push_back(detectedNotes);
    redoStack.clear();
}
