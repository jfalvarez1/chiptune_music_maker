#pragma once

/*
 * ChiptuneTracker - Autosave and crash recovery
 *
 * Losing an hour of work to a crash is the worst thing an audio tool can do
 * to someone, and this one has had crashes. The crash handler makes them
 * diagnosable; this makes them survivable.
 *
 * Two rules shape the design:
 *
 *   1. AUTOSAVE MUST NEVER LOSE THE PREVIOUS AUTOSAVE. Writing straight to
 *      the recovery file means a crash *during* the write destroys the only
 *      copy. So it writes a temporary file, then swaps it into place, and
 *      keeps the previous generation as a second chance.
 *
 *   2. AUTOSAVE MUST NEVER TOUCH THE USER'S FILE. It writes beside it, never
 *      over it. Nobody expects a background timer to modify the document
 *      they have not saved.
 *
 * The recovery file is removed on a clean exit, so its mere presence at
 * startup is the signal that the last session ended badly.
 */

#include "Types.h"
#include "ProjectSerializer.h"

#include <cstdio>
#include <ctime>
#include <string>

#ifdef _WIN32
#include <windows.h>
#endif

namespace ChiptuneTracker {

class Autosave {
public:
    // Long enough not to interrupt, short enough that losing the interval
    // is annoying rather than devastating.
    static constexpr float INTERVAL_SECONDS = 90.0f;

    void setDirectory(const std::string& directory) {
        m_directory = directory;
        if (!m_directory.empty()) {
            const char last = m_directory.back();
            if (last != '/' && last != '\\') m_directory += '/';
        }
    }

    std::string recoveryPath() const { return m_directory + "recovery.ctp"; }
    std::string previousPath() const { return m_directory + "recovery-previous.ctp"; }

    // True if the last session did not shut down cleanly.
    bool hasRecoverableSession() const {
        return fileExists(recoveryPath()) || fileExists(previousPath());
    }

    // Whichever recovery file actually exists, preferring the newest.
    std::string bestRecoveryPath() const {
        if (fileExists(recoveryPath())) return recoveryPath();
        if (fileExists(previousPath())) return previousPath();
        return {};
    }

    // Call once per frame. Writes at most every INTERVAL_SECONDS, and only
    // when something has actually changed.
    void update(const Project& project, float deltaSeconds) {
        if (!m_enabled) return;

        m_elapsed += deltaSeconds;
        if (m_elapsed < INTERVAL_SECONDS) return;
        m_elapsed = 0.0f;

        if (!m_dirty) return;      // nothing changed; do not churn the disk
        save(project);
    }

    // Mark the project as having unsaved changes. Cheap enough to call from
    // any edit path.
    void markDirty() { m_dirty = true; }

    void save(const Project& project) {
        const std::string temp = m_directory + "recovery.ctp.tmp";

        // Write to a temporary file first. A crash mid-write then damages
        // only the temporary, and the previous recovery is still intact.
        if (!saveProjectFile(project, temp)) return;

        // Keep one generation back. If the newest recovery turns out to be
        // the one that was being written when things went wrong, there is
        // still something to fall back to.
        std::remove(previousPath().c_str());
        std::rename(recoveryPath().c_str(), previousPath().c_str());

        if (std::rename(temp.c_str(), recoveryPath().c_str()) != 0) {
            // Rename can fail if something else holds the file open. Leave
            // the temporary in place rather than losing the data silently.
            return;
        }

        m_dirty = false;
        ++m_saveCount;
    }

    // Called on a clean shutdown. The recovery files existing at startup is
    // precisely the signal that the last run crashed, so a tidy exit has to
    // clear them.
    void clearOnCleanExit() {
        std::remove(recoveryPath().c_str());
        std::remove(previousPath().c_str());
        std::remove((m_directory + "recovery.ctp.tmp").c_str());
    }

    void setEnabled(bool enabled) { m_enabled = enabled; }
    bool isEnabled() const { return m_enabled; }
    int saveCount() const { return m_saveCount; }
    float secondsUntilNextSave() const {
        return m_enabled ? (INTERVAL_SECONDS - m_elapsed) : 0.0f;
    }

    // How long ago the recovery file was written, for the restore prompt -
    // "48 minutes ago" tells you far more than a path does.
    std::string describeRecoveryAge() const {
        const std::string path = bestRecoveryPath();
        if (path.empty()) return "unknown";

#ifdef _WIN32
        WIN32_FILE_ATTRIBUTE_DATA data{};
        if (!GetFileAttributesExA(path.c_str(), GetFileExInfoStandard, &data)) {
            return "unknown";
        }

        FILETIME nowFile{};
        GetSystemTimeAsFileTime(&nowFile);

        ULARGE_INTEGER then{}, now{};
        then.LowPart = data.ftLastWriteTime.dwLowDateTime;
        then.HighPart = data.ftLastWriteTime.dwHighDateTime;
        now.LowPart = nowFile.dwLowDateTime;
        now.HighPart = nowFile.dwHighDateTime;

        if (now.QuadPart <= then.QuadPart) return "just now";

        const unsigned long long seconds =
            (now.QuadPart - then.QuadPart) / 10000000ULL;   // 100ns units

        char buffer[64];
        if (seconds < 60) {
            std::snprintf(buffer, sizeof(buffer), "%llu seconds ago", seconds);
        } else if (seconds < 3600) {
            std::snprintf(buffer, sizeof(buffer), "%llu minutes ago", seconds / 60);
        } else {
            std::snprintf(buffer, sizeof(buffer), "%.1f hours ago", seconds / 3600.0);
        }
        return buffer;
#else
        return "recently";
#endif
    }

private:
    static bool fileExists(const std::string& path) {
        if (std::FILE* f = std::fopen(path.c_str(), "rb")) {
            std::fclose(f);
            return true;
        }
        return false;
    }

    std::string m_directory;
    float m_elapsed = 0.0f;
    bool m_enabled = true;
    bool m_dirty = false;
    int m_saveCount = 0;
};

} // namespace ChiptuneTracker
