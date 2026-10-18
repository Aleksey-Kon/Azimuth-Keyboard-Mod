#define NOMINMAX
#include <windows.h>
#include <tlhelp32.h>
#include <filesystem>
#include <fstream>
#include <string>
#include <stdexcept>
#include <memory>
#include "TestProfile.h"

extern "C" void LoadModBootstrap();
extern "C" unsigned char LoadModBootstrapEnd;

namespace
{
    struct Handle
    {
        HANDLE Value = nullptr;
        explicit Handle(HANDLE value = nullptr) : Value(value) {}
        ~Handle() { if (Value && Value != INVALID_HANDLE_VALUE) CloseHandle(Value); }
        Handle(const Handle&) = delete;
        Handle& operator=(const Handle&) = delete;
    };

    uintptr_t RemoteModule(DWORD pid, const std::wstring& name)
    {
        Handle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid));
        if (snapshot.Value == INVALID_HANDLE_VALUE) throw std::runtime_error("Cannot inspect emulator modules");
        MODULEENTRY32W item{sizeof(item)};
        if (Module32FirstW(snapshot.Value, &item))
        {
            do
            {
                if (_wcsicmp(item.szModule, name.c_str()) == 0) return reinterpret_cast<uintptr_t>(item.modBaseAddr);
            } while (Module32NextW(snapshot.Value, &item));
        }
        throw std::runtime_error("Module was not loaded into emulator");
    }

    DWORD RunRemote(HANDLE process, LPTHREAD_START_ROUTINE function, void* argument)
    {
        Handle thread(CreateRemoteThread(process, nullptr, 0, function, argument, 0, nullptr));
        if (!thread.Value) throw std::runtime_error("Cannot start the mod initialization thread");
        if (WaitForSingleObject(thread.Value, 30000) != WAIT_OBJECT_0)
        {
            TerminateProcess(process, 1);
            WaitForSingleObject(process, 5000);
            throw std::runtime_error("Mod initialization timed out; emulator was stopped");
        }
        DWORD result = 0;
        if (!GetExitCodeThread(thread.Value, &result)) throw std::runtime_error("Cannot read mod initialization result");
        return result;
    }

    void Inject(HANDLE process, DWORD pid, const std::filesystem::path& dll)
    {
        // A freshly suspended process has no initialized module list yet. Its first
        // remote thread finishes Windows loader initialization before running this stub.
        const auto bootstrap = reinterpret_cast<const unsigned char*>(&LoadModBootstrap);
        const auto bootstrapSize = reinterpret_cast<uintptr_t>(&LoadModBootstrapEnd) - reinterpret_cast<uintptr_t>(bootstrap);
        const std::wstring path = dll.wstring();
        const size_t size = (path.size() + 1) * sizeof(wchar_t);
        void* remotePath = VirtualAllocEx(process, nullptr, size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
        if (!remotePath) throw std::runtime_error("Cannot allocate the mod path");
        void* remoteCode = nullptr;
        try
        {
            SIZE_T written = 0;
            if (!WriteProcessMemory(process, remotePath, path.c_str(), size, &written) || written != size)
                throw std::runtime_error("Cannot copy the mod path");
            remoteCode = VirtualAllocEx(process, nullptr, bootstrapSize, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
            if (!remoteCode || !WriteProcessMemory(process, remoteCode, bootstrap, bootstrapSize, &written) || written != bootstrapSize)
                throw std::runtime_error("Cannot initialize the module loader");
            DWORD previous;
            if (!VirtualProtectEx(process, remoteCode, bootstrapSize, PAGE_EXECUTE_READ, &previous) ||
                !FlushInstructionCache(process, remoteCode, bootstrapSize)) throw std::runtime_error("Cannot prepare the module loader");
            RunRemote(process, reinterpret_cast<LPTHREAD_START_ROUTINE>(remoteCode), remotePath);
        }
        catch (...)
        {
            TerminateProcess(process, 1);
            WaitForSingleObject(process, 5000);
            VirtualFreeEx(process, remotePath, 0, MEM_RELEASE);
            if (remoteCode) VirtualFreeEx(process, remoteCode, 0, MEM_RELEASE);
            throw;
        }
        VirtualFreeEx(process, remotePath, 0, MEM_RELEASE);
        VirtualFreeEx(process, remoteCode, 0, MEM_RELEASE);

        // The thread exit code is only 32 bits. Get the 64-bit DLL base from the module snapshot.
        const auto remoteBase = RemoteModule(pid, dll.filename().wstring());
        HMODULE image = LoadLibraryExW(dll.c_str(), nullptr, DONT_RESOLVE_DLL_REFERENCES);
        if (!image) throw std::runtime_error("Cannot inspect the mod DLL");
        const auto entry = GetProcAddress(image, "InitializeGamepadMod");
        const auto offset = reinterpret_cast<uintptr_t>(entry) - reinterpret_cast<uintptr_t>(image);
        FreeLibrary(image);
        if (!entry) throw std::runtime_error("Mod initialization export is missing");
        const DWORD result = RunRemote(process, reinterpret_cast<LPTHREAD_START_ROUTINE>(remoteBase + offset), nullptr);
        if (result != 0) throw std::runtime_error("Mod initialization failed (code " + std::to_string(result) + "); see data/gamepad/gamepad-mod.log");
    }
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR arguments, int)
{
    PROCESS_INFORMATION process{};
    const bool selfTest = std::wstring(arguments) == L"--self-test";
    std::filesystem::path testReport;
    std::unique_ptr<TestProfile> testProfile;
    try
    {
        wchar_t path[32768]{};
        if (!GetModuleFileNameW(nullptr, path, 32768)) throw std::runtime_error("Cannot locate launcher");
        const auto workspace = std::filesystem::path(path).parent_path();
        const auto folder = workspace / L"data" / L"gamepad";
        const auto azimuth = workspace / L"azimuth.exe";
        testReport = folder / L"tests" / L"launcher-self-test.txt";
        const auto emulator = selfTest ? folder / L"tests" / L"IntegrationTests.exe" : azimuth;
        auto dll = folder / L"GamepadMod.dll";
        if (!std::filesystem::is_regular_file(azimuth) || !std::filesystem::is_regular_file(emulator) || !std::filesystem::is_regular_file(dll))
            throw std::runtime_error("Keep Start-Azimuth-Gamepad.exe beside azimuth.exe and GamepadMod.dll in data/gamepad");
        if (selfTest)
        {
            testProfile = std::make_unique<TestProfile>(dll);
            dll = testProfile->Dll;
        }
        std::wstring command = L"\"" + emulator.wstring() + L"\" " + (selfTest ? L"\"" + dll.wstring() + L"\"" : std::wstring(arguments));
        STARTUPINFOW startup{sizeof(startup)};
        if (!CreateProcessW(emulator.c_str(), command.data(), nullptr, nullptr, FALSE, CREATE_SUSPENDED | (selfTest ? CREATE_NO_WINDOW : 0),
            nullptr, workspace.c_str(), &startup, &process)) throw std::runtime_error("Cannot start azimuth.exe");
        Inject(process.hProcess, process.dwProcessId, dll);
        if (ResumeThread(process.hThread) == static_cast<DWORD>(-1)) throw std::runtime_error("Cannot resume emulator");
        if (selfTest)
        {
            if (WaitForSingleObject(process.hProcess, 30000) != WAIT_OBJECT_0) throw std::runtime_error("Integration target timed out");
            DWORD exitCode = 1;
            if (!GetExitCodeProcess(process.hProcess, &exitCode) || exitCode != 0) throw std::runtime_error("Injected integration target failed");
            std::ofstream report(testReport);
            report << "PASS: suspended x64 process, DLL injection, fullscreen rendered Controls entry, pointer and keyboard submenu, all presets, default Off, gamepad lifecycle\n";
        }
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
        return 0;
    }
    catch (const std::exception& error)
    {
        if (process.hProcess)
        {
            TerminateProcess(process.hProcess, 1);
            WaitForSingleObject(process.hProcess, 5000);
            CloseHandle(process.hThread);
            CloseHandle(process.hProcess);
        }
        if (selfTest) { std::ofstream report(testReport); report << "FAIL: " << error.what() << '\n'; }
        else MessageBoxA(nullptr, error.what(), "Azimuth Gamepad Mod", MB_OK | MB_ICONERROR);
        return 1;
    }
}
