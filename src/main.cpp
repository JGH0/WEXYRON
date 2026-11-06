/**
 * @file main.cpp
 * @brief Main entry point for the OpenGL 3D Game application
 * @author OpenGL Game Developer
 * @date 2025
 */

#include "core/game.hpp"
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <ctime>
#include <exception>
#if defined(_WIN32)
#include <cstdio>
#include <fstream>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <dbghelp.h>
#endif

// ---------------------------------------------------------------------------
// Failure diagnostics (release builds on Windows are started by double-click,
// which closes the console as soon as the process ends)
// ---------------------------------------------------------------------------
namespace {
void waitForEnterOnFailure() {
#if defined(_WIN32)
    std::cerr << "Press Enter to close..." << std::endl;
    std::cerr.flush();
    std::fflush(stdout);
    (void)std::getchar();
#endif
}

#if defined(_WIN32)
LONG WINAPI onAccessViolation(EXCEPTION_POINTERS* info) {
    const DWORD code = info && info->ExceptionRecord
                           ? info->ExceptionRecord->ExceptionCode
                           : 0;
    const void* address = info && info->ExceptionRecord
                              ? info->ExceptionRecord->ExceptionAddress
                              : nullptr;
    std::cerr << "\n*** Crash: exception 0x" << std::hex << code
              << " at address " << address << std::dec << " ***" << std::endl;

    // Symbolized stack. dbghelp is loaded at runtime so the build does not
    // depend on the import library being present in every MinGW install.
    HMODULE dbghelp = LoadLibraryA("dbghelp.dll");
    if (dbghelp && info && info->ContextRecord) {
        using SymSetOptions_t = DWORD(WINAPI*)(DWORD);
        using SymInitialize_t = BOOL(WINAPI*)(HANDLE, PCSTR, BOOL);
        using SymCleanup_t = BOOL(WINAPI*)(HANDLE);
        using StackWalk64_t = BOOL(WINAPI*)(DWORD, HANDLE, HANDLE, LPSTACKFRAME64,
                                            PVOID, PREAD_PROCESS_MEMORY_ROUTINE64,
                                            PFUNCTION_TABLE_ACCESS_ROUTINE64,
                                            PGET_MODULE_BASE_ROUTINE64,
                                            PTRANSLATE_ADDRESS_ROUTINE64);
        using SymFunctionTableAccess64_t = PVOID(WINAPI*)(HANDLE, DWORD64);
        using SymGetModuleBase64_t = DWORD64(WINAPI*)(HANDLE, DWORD64);
        using SymFromAddr_t = BOOL(WINAPI*)(HANDLE, DWORD64, PDWORD64, PSYMBOL_INFO);
        using SymGetLineFromAddr64_t = BOOL(WINAPI*)(HANDLE, DWORD64, PDWORD, PIMAGEHLP_LINE64);

        auto symSetOptions = (SymSetOptions_t)GetProcAddress(dbghelp, "SymSetOptions");
        auto symInitialize = (SymInitialize_t)GetProcAddress(dbghelp, "SymInitialize");
        auto symCleanup = (SymCleanup_t)GetProcAddress(dbghelp, "SymCleanup");
        auto stackWalk64 = (StackWalk64_t)GetProcAddress(dbghelp, "StackWalk64");
        auto symFuncTable = (SymFunctionTableAccess64_t)GetProcAddress(dbghelp, "SymFunctionTableAccess64");
        auto symModuleBase = (SymGetModuleBase64_t)GetProcAddress(dbghelp, "SymGetModuleBase64");
        auto symFromAddr = (SymFromAddr_t)GetProcAddress(dbghelp, "SymFromAddr");
        auto symGetLine = (SymGetLineFromAddr64_t)GetProcAddress(dbghelp, "SymGetLineFromAddr64");

        const HANDLE process = GetCurrentProcess();
        CONTEXT* ctx = info->ContextRecord;
        if (symSetOptions && symInitialize && stackWalk64 && symFuncTable &&
            symModuleBase && ctx) {
            symSetOptions(SYMOPT_LOAD_LINES | SYMOPT_UNDNAME);
            if (symInitialize(process, nullptr, TRUE)) {
                STACKFRAME64 frame{};
                const DWORD machine = IMAGE_FILE_MACHINE_AMD64;
                frame.AddrPC.Offset = ctx->Rip;
                frame.AddrPC.Mode = AddrModeFlat;
                frame.AddrFrame.Offset = ctx->Rbp;
                frame.AddrFrame.Mode = AddrModeFlat;
                frame.AddrStack.Offset = ctx->Rsp;
                frame.AddrStack.Mode = AddrModeFlat;
                for (int i = 0; i < 32; ++i) {
                    if (!stackWalk64(machine, process, GetCurrentThread(), &frame, ctx,
                                     nullptr, symFuncTable, symModuleBase, nullptr)) {
                        break;
                    }
                    std::cerr << "  #" << i << " 0x" << std::hex << frame.AddrPC.Offset
                              << std::dec;
                    if (symFromAddr) {
                        char buffer[sizeof(SYMBOL_INFO) + 256] = {};
                        auto* symbol = reinterpret_cast<SYMBOL_INFO*>(buffer);
                        symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
                        symbol->MaxNameLen = 255;
                        DWORD64 displacement = 0;
                        if (symFromAddr(process, frame.AddrPC.Offset, &displacement, symbol)) {
                            std::cerr << " " << symbol->Name;
                        }
                        IMAGEHLP_LINE64 line{};
                        line.SizeOfStruct = sizeof(line);
                        DWORD lineDisplacement = 0;
                        if (symGetLine &&
                            symGetLine(process, frame.AddrPC.Offset, &lineDisplacement, &line)) {
                            std::cerr << " (" << line.FileName << ":" << line.LineNumber << ")";
                        }
                    }
                    std::cerr << std::endl;
                }
                if (symCleanup) symCleanup(process);
            }
        }
    }
    std::fflush(stdout);
    return EXCEPTION_EXECUTE_HANDLER;
}
#endif

void installCrashHandlers() {
#if defined(_WIN32)
    SetUnhandledExceptionFilter(onAccessViolation);
#endif
}

// Writes to two streams at once (console + log file), so diagnostics survive
// both a closed console and a process that is killed mid-run.
class TeeBuf : public std::streambuf {
public:
    TeeBuf(std::streambuf* a, std::streambuf* b) : first_(a), second_(b) {}

protected:
    int overflow(int ch) override {
        if (ch == traits_type::eof()) return traits_type::not_eof(ch);
        const char c = static_cast<char>(ch);
        const bool ok = first_->sputc(c) != traits_type::eof() &&
                        second_->sputc(c) != traits_type::eof();
        return ok ? ch : traits_type::eof();
    }
    std::streamsize xsputn(const char* s, std::streamsize n) override {
        first_->sputn(s, n);
        second_->sputn(s, n);
        return n;
    }
    int sync() override {
        first_->pubsync();
        second_->pubsync();
        return 0;
    }

private:
    std::streambuf* first_;
    std::streambuf* second_;
};
} // namespace


/**
 * @brief Main function that initializes and runs the game loop
 *
 * Creates a Game instance with window dimensions 800x600, initializes it,
 * and runs the main game loop with delta time calculations. With CUBE_MENU_TEST
 * set, a scripted menu run is executed instead (development aid).
 *
 * @return int Returns 0 on successful execution
 */
// ---------------------------------------------------------------------------
// Scripted UI check (development aid)
//
// CUBE_MENU_TEST="menu:settings;down;enter;dump" opens a menu, sends keys and
// prints the page after each step. It lets the UI be exercised on a headless
// machine (see tools/ui_screenshots.sh). Without the variable the normal game
// loop runs.
// ---------------------------------------------------------------------------
static void runMenuTest(Game& game, const char* script) {
    std::stringstream steps(script);
    std::string step;
    while (std::getline(steps, step, ';')) {
        if (step.empty()) continue;
        if (step.rfind("menu:", 0) == 0) {
            game.debugOpenMenu(step.substr(5));
            game.render();   // first frame of the screen
            if (step.substr(5) == "main") game.render();
        } else if (step.rfind("select:", 0) == 0) {
            game.debugMenuSelect(step.substr(7));
            game.render();
        } else if (step.rfind("game:", 0) == 0) {
            // Run the real frame path (update + render) for N frames.
            game.debugRunFrames(std::atoi(step.c_str() + 5));
        } else if (step.rfind("map:", 0) == 0) {
            // Start a real single-player match on this map (screenshot aid).
            game.debugStartMapForTest(step.substr(4));
            game.debugRunFrames(30);
        } else if (step == "dump") {
            game.debugMenuDump();
        } else if (step == "shot") {
            game.render();
        } else {
            game.debugMenuKey(step);
            game.render();
            // Let the game loop build the page this key opens before the next
            // step acts on it (a real player cannot press faster either).
            struct timespec ts = {0, 60000000};
            nanosleep(&ts, nullptr);
        }
        std::cout << "step " << step << std::endl;
    }
    game.debugMenuDump();
    std::cout << "ready" << std::endl;
    std::cout.flush();

    // Keep the last frame on screen so an external screenshot can capture it.
    const double holdUntil = glfwGetTime() + 4.0;
    while (glfwGetTime() < holdUntil) {
        game.render();
        glfwPollEvents();
        struct timespec ts = {0, 16000000};
        nanosleep(&ts, nullptr);
    }
}

int main() {
    installCrashHandlers();

#ifdef _WIN32
    // Tee stdout/stderr into a log next to the exe (keeping the console).
    // Unbuffered so the last line survives a hard kill - that is what tells us
    // where a silent exit happened.
    static std::ofstream logFile("game-log.txt", std::ios::out | std::ios::trunc);
    static TeeBuf coutTee(std::cout.rdbuf(), logFile.is_open() ? logFile.rdbuf()
                                                              : std::cout.rdbuf());
    static TeeBuf cerrTee(std::cerr.rdbuf(), logFile.is_open() ? logFile.rdbuf()
                                                              : std::cerr.rdbuf());
    if (logFile.is_open()) {
        std::cout.rdbuf(&coutTee);
        std::cerr.rdbuf(&cerrTee);
        std::cout << std::unitbuf;
        std::cerr << std::unitbuf;
    }
#endif

    int exitCode = 0;

    try {
        Game game(800, 600);
        game.init();

        if (const char* script = std::getenv("CUBE_MENU_TEST")) {
            runMenuTest(game, script);
            // The Game lives until main() returns, i.e. until after glfwTerminate()
            // has run; shut the sound device down before that so its mixing
            // thread is joined while everything it touches still exists.
            game.cleanup();
        } else {
            float lastFrame = glfwGetTime();
            while (!game.shouldClose()) {
                float currentFrame = glfwGetTime();
                float deltaTime = currentFrame - lastFrame;
                lastFrame = currentFrame;
                if (deltaTime > 0.05f) deltaTime = 0.05f; // Cap at 50ms to avoid physics issues

                game.processInput(deltaTime);
                game.update(deltaTime); // Only update physics when playing
                game.render();
                game.frameWait();
                game.endFrame(deltaTime);
            }

            game.cleanup();
        }
    } catch (const std::exception& e) {
        // Without this, an uncaught exception aborts the process and a
        // double-clicked release build shows nothing at all.
        std::cerr << "Fatal error: " << e.what() << std::endl;
        exitCode = 1;
    } catch (...) {
        std::cerr << "Fatal error: unknown exception" << std::endl;
        exitCode = 1;
    }

    waitForEnterOnFailure();
    return exitCode;
}
