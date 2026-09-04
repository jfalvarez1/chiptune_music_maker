cmake_minimum_required(VERSION 3.20)
project(ChiptuneTracker VERSION 0.1.0 LANGUAGES CXX C)

# ============================================================================
# C++ Standard
# ============================================================================
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ============================================================================
# Build Type (Default to Release)
# ============================================================================
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# ============================================================================
# Compiler Flags
# ============================================================================
if(MSVC)
    add_compile_options(/W4 /permissive-)

    # Debug info in Release too. Costs nothing at runtime - the PDB sits
    # beside the exe and is not loaded unless something asks for it - and it
    # is the difference between a crash report naming a function and naming
    # a hex address.
    add_compile_options(/Zi)
    add_link_options(/DEBUG /OPT:REF /OPT:ICF)
    # Fast floating point for audio performance
    add_compile_options(/fp:fast)

    # Static runtime linking - eliminates need for VC++ Redistributable
    # Users can run the .exe without installing anything else
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
    add_compile_options(-ffast-math)
endif()

# ============================================================================
# Find OpenGL (Required for ImGui)
# ============================================================================
find_package(OpenGL REQUIRED)

# ============================================================================
# miniaudio (Header-only library)
# ============================================================================
add_library(miniaudio INTERFACE)
target_include_directories(miniaudio INTERFACE ${CMAKE_SOURCE_DIR}/vendor/miniaudio)

# ============================================================================
# midifile Library (MIDI export/import)
# ============================================================================
set(MIDIFILE_DIR ${CMAKE_SOURCE_DIR}/vendor/midifile)

add_library(midifile STATIC
    ${MIDIFILE_DIR}/src/Binasc.cpp
    ${MIDIFILE_DIR}/src/MidiEvent.cpp
    ${MIDIFILE_DIR}/src/MidiEventList.cpp
    ${MIDIFILE_DIR}/src/MidiFile.cpp
    ${MIDIFILE_DIR}/src/MidiMessage.cpp
    ${MIDIFILE_DIR}/src/Options.cpp
)

target_include_directories(midifile PUBLIC
    ${MIDIFILE_DIR}/include
)

# ============================================================================
# Dear ImGui Library
# ============================================================================
set(IMGUI_DIR ${CMAKE_SOURCE_DIR}/vendor/imgui)

add_library(imgui STATIC
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_demo.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    # Backends
    ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp
    ${IMGUI_DIR}/backends/imgui_impl_win32.cpp
)

target_include_directories(imgui PUBLIC
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
)

target_link_libraries(imgui PUBLIC OpenGL::GL)

# ============================================================================
# Main Executable (WIN32 = use WinMain entry point)
# ============================================================================

# The icon and version block. Listed as a source so MSVC compiles it with rc
# and links the result - an .ico sitting next to the exe does nothing, the
# icon has to be inside it. Only on Windows: the .rc is meaningless
# elsewhere and would fail the build.
set(CHIPTUNE_RESOURCES "")
if(WIN32)
    set(CHIPTUNE_RESOURCES assets/ChiptuneTracker.rc)
endif()

add_executable(${PROJECT_NAME} WIN32
    ${CHIPTUNE_RESOURCES}
    src/main.cpp
    # The pitch and drum detection. It used to be compiled only into the
    # standalone VoiceToNote.exe, which is why the tracker could not use any
    # of it and none of it was covered by a test.
    src/AudioAnalyzer.cpp
)

# Header files (for IDE visibility)
target_sources(${PROJECT_NAME} PRIVATE
    src/Types.h
    src/Effects.h
    src/Synthesizer.h
    src/Sequencer.h
    src/FileIO.h
    src/MIDIExport.h
    src/UI.h
    src/TempoMap.h
    src/VoiceCapture.h
    src/LiveVoice.h
    src/VoicePanel.h
    src/AudioAnalyzer.h
)

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/vendor/libremidi/include
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    miniaudio
    midifile
    imgui
    OpenGL::GL
)

# Platform-specific linking
if(WIN32)
    target_link_libraries(${PROJECT_NAME} PRIVATE
        dwmapi      # For DWM composition
        imm32       # For IME support
        comdlg32    # For file dialogs
        shell32     # For shell functions
        winmm       # For MIDI input (libremidi)
    )
elseif(UNIX AND NOT APPLE)
    find_package(Threads REQUIRED)
    target_link_libraries(${PROJECT_NAME} PRIVATE
        Threads::Threads
        dl
        m
    )
elseif(APPLE)
    find_library(COREAUDIO_LIBRARY CoreAudio)
    find_library(AUDIOTOOLBOX_LIBRARY AudioToolbox)
    target_link_libraries(${PROJECT_NAME} PRIVATE
        ${COREAUDIO_LIBRARY}
        ${AUDIOTOOLBOX_LIBRARY}
    )
endif()

# ============================================================================
# Output Directory
# ============================================================================
set_target_properties(${PROJECT_NAME} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

# ============================================================================
# Install Rules
# ============================================================================
install(TARGETS ${PROJECT_NAME}
    RUNTIME DESTINATION bin
)

# ============================================================================
# VoiceToNote Tool
# ============================================================================
add_executable(VoiceToNote WIN32
    src/VoiceToNoteTool.cpp
    src/AudioAnalyzer.cpp
    src/AudioImplementation.cpp
)

target_include_directories(VoiceToNote PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(VoiceToNote PRIVATE
    miniaudio
    imgui
    OpenGL::GL
)

if(WIN32)
    target_link_libraries(VoiceToNote PRIVATE
        dwmapi
        imm32
        comdlg32
        shell32
    )
elseif(UNIX AND NOT APPLE)
    target_link_libraries(VoiceToNote PRIVATE
        Threads::Threads
        dl
        m
    )
elseif(APPLE)
    target_link_libraries(VoiceToNote PRIVATE
        ${COREAUDIO_LIBRARY}
        ${AUDIOTOOLBOX_LIBRARY}
    )
endif()

set_target_properties(VoiceToNote PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

install(TARGETS VoiceToNote
    RUNTIME DESTINATION bin
)

# ============================================================================
# DebugAnalyzer Tool
# ============================================================================
add_executable(DebugAnalyzer
    src/DebugAnalyzer.cpp
    src/AudioAnalyzer.cpp
)

target_include_directories(DebugAnalyzer PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(DebugAnalyzer PRIVATE
    miniaudio
)

set_target_properties(DebugAnalyzer PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

install(TARGETS DebugAnalyzer
    RUNTIME DESTINATION bin
)

# ============================================================================
# Headless Test Suite
#
# Console app - no window, no audio device. Exercises the synth, sequencer,
# effects, file I/O and exporters so wiring gaps and bad audio values are
# caught automatically instead of by ear.
#
#   cmake --build build --config Release --target ChiptuneTests
#   build/bin/Release/ChiptuneTests.exe
# ============================================================================
add_executable(ChiptuneTests
    tests/ChiptuneTests.cpp
    tests/TestMiniaudioImpl.cpp
    src/AudioAnalyzer.cpp
)

target_include_directories(ChiptuneTests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/vendor/libremidi/include
)

# Where the repository is, so a test can read the files that ship beside the
# code - the changelog, in particular. A version that nobody wrote an entry
# for is a release nobody can read the notes of.
target_compile_definitions(ChiptuneTests PRIVATE
    CHIPTUNE_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)

# Links imgui so the theme tests can call ApplyTheme() and inspect the
# resulting style. No window and no GL context is needed - ApplyTheme only
# writes to ImGuiStyle, and a context can exist without a backend.
target_link_libraries(ChiptuneTests PRIVATE
    miniaudio
    midifile
    imgui
)

if(WIN32)
    target_link_libraries(ChiptuneTests PRIVATE winmm comdlg32 shell32)
elseif(UNIX AND NOT APPLE)
    target_link_libraries(ChiptuneTests PRIVATE Threads::Threads dl m)
endif()

set_target_properties(ChiptuneTests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

enable_testing()
add_test(NAME chiptune_tests COMMAND ChiptuneTests)

message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "C++ Standard: ${CMAKE_CXX_STANDARD}")
