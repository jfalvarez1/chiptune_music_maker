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
    # Fast floating point for audio performance
    add_compile_options(/fp:fast)
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
# Main Executable
# ============================================================================
add_executable(${PROJECT_NAME}
    src/main.cpp
    src/AudioEngine.cpp
)

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    miniaudio
    imgui
    OpenGL::GL
)

# Platform-specific linking
if(WIN32)
    target_link_libraries(${PROJECT_NAME} PRIVATE
        dwmapi      # For DWM composition
        imm32       # For IME support
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

message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "C++ Standard: ${CMAKE_CXX_STANDARD}")
