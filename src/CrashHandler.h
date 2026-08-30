#pragma once

/*
 * ChiptuneTracker - Crash diagnostics
 *
 * An access violation with no output tells you nothing. This installs a
 * last-chance handler that writes the exception code, the faulting address
 * and a symbolised stack trace to both stderr and crash-log.txt, so a
 * crash reported by a user - or one that appears once in sixty automated
 * runs and then hides - arrives with enough detail to act on.
 *
 * Costs nothing until something goes wrong: no hooks, no polling, just a
 * handler the OS calls on the way down.
 */

#ifdef _WIN32

#include <windows.h>
#include <dbghelp.h>

#include <cstdio>
#include <ctime>

#pragma comment(lib, "dbghelp.lib")

namespace ChiptuneTracker {
namespace crash {

inline const char* exceptionName(DWORD code) {
    switch (code) {
        case EXCEPTION_ACCESS_VIOLATION:      return "ACCESS_VIOLATION";
        case EXCEPTION_STACK_OVERFLOW:        return "STACK_OVERFLOW";
        case EXCEPTION_INT_DIVIDE_BY_ZERO:    return "INT_DIVIDE_BY_ZERO";
        case EXCEPTION_FLT_DIVIDE_BY_ZERO:    return "FLT_DIVIDE_BY_ZERO";
        case EXCEPTION_ILLEGAL_INSTRUCTION:   return "ILLEGAL_INSTRUCTION";
        case EXCEPTION_PRIV_INSTRUCTION:      return "PRIV_INSTRUCTION";
        case EXCEPTION_IN_PAGE_ERROR:         return "IN_PAGE_ERROR";
        case EXCEPTION_ARRAY_BOUNDS_EXCEEDED: return "ARRAY_BOUNDS_EXCEEDED";
        case 0xC0000409:                      return "STACK_BUFFER_OVERRUN";
        default:                              return "UNKNOWN";
    }
}

// Writes the report to one stream. Called twice - once for stderr so it
// shows up in a console run, once for the log file so an unattended run
// still leaves evidence.
inline void writeReport(std::FILE* out, EXCEPTION_POINTERS* info) {
    const DWORD code = info->ExceptionRecord->ExceptionCode;

    std::time_t now = std::time(nullptr);
    char when[64] = {};
    std::strftime(when, sizeof(when), "%Y-%m-%d %H:%M:%S", std::localtime(&now));

    std::fprintf(out, "\n=== ChiptuneTracker crashed ===\n");
    std::fprintf(out, "when:    %s\n", when);
    std::fprintf(out, "code:    0x%08lX (%s)\n",
                 static_cast<unsigned long>(code), exceptionName(code));
    std::fprintf(out, "address: 0x%p\n", info->ExceptionRecord->ExceptionAddress);

    // An access violation says which address it touched and whether it was
    // reading or writing, which usually identifies the bug on its own.
    if (code == EXCEPTION_ACCESS_VIOLATION &&
        info->ExceptionRecord->NumberParameters >= 2) {
        const ULONG_PTR operation = info->ExceptionRecord->ExceptionInformation[0];
        const ULONG_PTR target = info->ExceptionRecord->ExceptionInformation[1];
        const char* what = (operation == 0) ? "reading"
                         : (operation == 1) ? "writing"
                         : "executing";
        std::fprintf(out, "detail:  %s 0x%p%s\n", what,
                     reinterpret_cast<void*>(target),
                     target < 0x10000 ? "  (null or near-null - a dead pointer)" : "");
    }

    std::fprintf(out, "thread:  %lu\n", GetCurrentThreadId());
    std::fprintf(out, "\nstack:\n");

    HANDLE process = GetCurrentProcess();
    SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES);
    SymInitialize(process, nullptr, TRUE);

    CONTEXT context = *info->ContextRecord;
    STACKFRAME64 frame = {};
    frame.AddrPC.Offset = context.Rip;
    frame.AddrPC.Mode = AddrModeFlat;
    frame.AddrFrame.Offset = context.Rbp;
    frame.AddrFrame.Mode = AddrModeFlat;
    frame.AddrStack.Offset = context.Rsp;
    frame.AddrStack.Mode = AddrModeFlat;

    // Room for a name plus the symbol struct that precedes it.
    alignas(SYMBOL_INFO) char symbolBuffer[sizeof(SYMBOL_INFO) + 512] = {};
    SYMBOL_INFO* symbol = reinterpret_cast<SYMBOL_INFO*>(symbolBuffer);
    symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
    symbol->MaxNameLen = 511;

    for (int depth = 0; depth < 40; ++depth) {
        if (!StackWalk64(IMAGE_FILE_MACHINE_AMD64, process, GetCurrentThread(),
                         &frame, &context, nullptr,
                         SymFunctionTableAccess64, SymGetModuleBase64, nullptr)) {
            break;
        }
        if (frame.AddrPC.Offset == 0) break;

        DWORD64 displacement = 0;
        if (SymFromAddr(process, frame.AddrPC.Offset, &displacement, symbol)) {
            std::fprintf(out, "  %2d  %s + 0x%llX", depth, symbol->Name,
                         static_cast<unsigned long long>(displacement));

            IMAGEHLP_LINE64 line = {};
            line.SizeOfStruct = sizeof(line);
            DWORD lineDisplacement = 0;
            if (SymGetLineFromAddr64(process, frame.AddrPC.Offset,
                                     &lineDisplacement, &line)) {
                std::fprintf(out, "   [%s:%lu]", line.FileName, line.LineNumber);
            }
            std::fprintf(out, "\n");
        } else {
            std::fprintf(out, "  %2d  0x%llX  (no symbol)\n", depth,
                         static_cast<unsigned long long>(frame.AddrPC.Offset));
        }
    }

    SymCleanup(process);
    std::fprintf(out, "=== end of report ===\n");
    std::fflush(out);
}

inline LONG WINAPI handler(EXCEPTION_POINTERS* info) {
    writeReport(stderr, info);

    if (std::FILE* log = std::fopen("crash-log.txt", "a")) {
        writeReport(log, info);
        std::fclose(log);
    }

    return EXCEPTION_EXECUTE_HANDLER;   // let the process die, having said why
}

} // namespace crash

// Call once, as early in startup as possible.
inline void installCrashHandler() {
    SetUnhandledExceptionFilter(crash::handler);
}

} // namespace ChiptuneTracker

#else
namespace ChiptuneTracker { inline void installCrashHandler() {} }
#endif
