#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
#include <commctrl.h>
#include <roapi.h>
#include <winstring.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Gaming.Input.h>
#include <winrt/Windows.System.h>
#include <atomic>
#include <mutex>
#include <vector>
#include "Preset.h"
#include "Settings.h"
#include "LauncherMenu.h"

namespace gi = winrt::Windows::Gaming::Input;
namespace wf = winrt::Windows::Foundation;
namespace
{
    HMODULE module;
    std::filesystem::path root;
    std::array<gamepadmod::Preset, 3> presets;
    constexpr std::array<const wchar_t*, 3> files = { L"01-wasd.ini", L"02-arrows.ini", L"03-smooth.ini" };
    std::atomic<int> active{0};
    std::mutex stateMutex;
    gamepadmod::InputAccumulator inputState;
    using RoFactory = HRESULT(WINAPI*)(HSTRING, REFIID, void**);
    RoFactory realFactory;
    void NotifyVirtual(bool enabled);

    void Log(const std::string& text)
    {
        std::ofstream out(root / L"gamepad-mod.log", std::ios::app);
        out << text << '\n';
    }

    std::wstring Wide(const std::string& value)
    {
        const int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), nullptr, 0);
        if (!count) return L"Preset";
        std::wstring output(count, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), output.data(), count);
        return output;
    }

    void Reload()
    {
        std::array<gamepadmod::Preset, 3> loaded;
        for (size_t i = 0; i < files.size(); ++i) loaded[i] = gamepadmod::LoadPreset(root / L"presets" / files[i]);
        std::lock_guard<std::mutex> lock(stateMutex);
        presets = std::move(loaded);
        inputState.Clear();
    }

    void Select(int index)
    {
        int previous;
        {
            std::lock_guard<std::mutex> lock(stateMutex);
            gamepadmod::SaveSelectedPreset(root / L"settings.ini", index);
            inputState.Clear();
            previous = active.exchange(index);
        }
        if ((previous == 0) != (index == 0)) NotifyVirtual(index != 0);
    }

    bool HasFocus()
    {
        const HWND foreground = GetForegroundWindow();
        DWORD pid = 0;
        const DWORD thread = GetWindowThreadProcessId(foreground, &pid);
        if (!foreground || pid != GetCurrentProcessId() || gamepadmod::IsLauncherInputCaptured()) return false;
        GUITHREADINFO info{sizeof(info)};
        return GetGUIThreadInfo(thread, &info) && !(info.flags & (GUI_INMENUMODE | GUI_POPUPMENUMODE));
    }

    struct KeyboardPad : winrt::implements<KeyboardPad, gi::IGamepad, gi::IGameController, gi::IGamepad2>
    {
        gi::IGamepadStatics Physical{nullptr};
        explicit KeyboardPad(gi::IGamepadStatics const& physical) : Physical(physical) {}
        winrt::hstring GetRuntimeClassName() const { return L"Windows.Gaming.Input.Gamepad"; }
        gi::GamepadVibration Vibration() const noexcept { return {}; }
        void Vibration(gi::GamepadVibration const&) noexcept {}
        gi::GamepadReading GetCurrentReading()
        {
            const auto now = GetTickCount64();
            gi::GamepadReading result{};
            result.Timestamp = now * 1000;
            if (active.load() == 0)
            {
                return result;
            }
            const bool focused = HasFocus();
            std::lock_guard<std::mutex> lock(stateMutex);
            const int selected = active.load();
            if (!focused || selected == 0)
            {
                inputState.Clear();
                return result;
            }
            const auto reading = inputState.Read(presets[selected - 1], [](int key) { return (GetAsyncKeyState(key) & 0x8000) != 0; }, now);
            result.Buttons = static_cast<gi::GamepadButtons>(reading.Buttons);
            result.LeftThumbstickX = reading.LeftX; result.LeftThumbstickY = reading.LeftY;
            result.RightThumbstickX = reading.RightX; result.RightThumbstickY = reading.RightY;
            result.LeftTrigger = reading.LeftTrigger; result.RightTrigger = reading.RightTrigger;
            return result;
        }
        gi::GameControllerButtonLabel GetButtonLabel(gi::GamepadButtons) const noexcept { return gi::GameControllerButtonLabel::None; }
        winrt::event<wf::TypedEventHandler<gi::IGameController, gi::Headset>> connected, disconnected;
        winrt::event<wf::TypedEventHandler<gi::IGameController, winrt::Windows::System::UserChangedEventArgs>> userChanged;
        winrt::event_token HeadsetConnected(wf::TypedEventHandler<gi::IGameController, gi::Headset> const& handler) { return connected.add(handler); }
        void HeadsetConnected(winrt::event_token const& token) noexcept { connected.remove(token); }
        winrt::event_token HeadsetDisconnected(wf::TypedEventHandler<gi::IGameController, gi::Headset> const& handler) { return disconnected.add(handler); }
        void HeadsetDisconnected(winrt::event_token const& token) noexcept { disconnected.remove(token); }
        winrt::event_token UserChanged(wf::TypedEventHandler<gi::IGameController, winrt::Windows::System::UserChangedEventArgs> const& handler) { return userChanged.add(handler); }
        void UserChanged(winrt::event_token const& token) noexcept { userChanged.remove(token); }
        gi::Headset Headset() const noexcept { return nullptr; }
        bool IsWireless() const noexcept { return false; }
        winrt::Windows::System::User User() const noexcept { return nullptr; }
    };

    struct KeyboardFactory : winrt::implements<KeyboardFactory, gi::IGamepadStatics, gi::IGamepadStatics2>
    {
        gi::IGamepadStatics Physical{nullptr};
        gi::Gamepad Pad{nullptr};
        winrt::event<wf::EventHandler<gi::Gamepad>> Added, Removed;
        winrt::event_token addedToken{}, removedToken{};
        bool hasAdded = false, hasRemoved = false;
        explicit KeyboardFactory(gi::IGamepadStatics const& physical) : Physical(physical), Pad(winrt::make<KeyboardPad>(physical).as<gi::Gamepad>()) {}
        void Start()
        {
            auto weak = get_weak();
            addedToken = Physical.GamepadAdded([weak](auto const& sender, auto const& pad) { if (auto self = weak.get()) self->Added(sender, pad); });
            hasAdded = true;
            removedToken = Physical.GamepadRemoved([weak](auto const& sender, auto const& pad) { if (auto self = weak.get()) self->Removed(sender, pad); });
            hasRemoved = true;
        }
        ~KeyboardFactory()
        {
            try { if (hasAdded) Physical.GamepadAdded(addedToken); } catch (...) {}
            try { if (hasRemoved) Physical.GamepadRemoved(removedToken); } catch (...) {}
        }
        auto Gamepads()
        {
            std::vector<gi::Gamepad> items;
            if (active.load() != 0) items.push_back(Pad);
            for (const auto& pad : Physical.Gamepads()) items.push_back(pad);
            return winrt::single_threaded_vector<gi::Gamepad>(std::move(items)).GetView();
        }
        winrt::event_token GamepadAdded(wf::EventHandler<gi::Gamepad> const& handler) { return Added.add(handler); }
        void GamepadAdded(winrt::event_token const& token) noexcept { Added.remove(token); }
        winrt::event_token GamepadRemoved(wf::EventHandler<gi::Gamepad> const& handler) { return Removed.add(handler); }
        void GamepadRemoved(winrt::event_token const& token) noexcept { Removed.remove(token); }
        gi::Gamepad FromGameController(gi::IGameController const& controller)
        {
            if (controller == Pad.as<gi::IGameController>()) return Pad;
            const auto factory2 = Physical.try_as<gi::IGamepadStatics2>();
            return factory2 ? factory2.FromGameController(controller) : nullptr;
        }
    };

    std::mutex factoryMutex;
    winrt::com_ptr<KeyboardFactory> currentFactory;
    void NotifyVirtual(bool enabled)
    {
        winrt::com_ptr<KeyboardFactory> factory;
        { std::lock_guard<std::mutex> lock(factoryMutex); factory = currentFactory; }
        if (factory)
        {
            if (enabled) factory->Added(nullptr, factory->Pad);
            else factory->Removed(nullptr, factory->Pad);
        }
    }

    HRESULT WINAPI HookFactory(HSTRING name, REFIID iid, void** result) noexcept
    {
        if (!result) return E_POINTER;
        UINT32 length = 0;
        const wchar_t* text = WindowsGetStringRawBuffer(name, &length);
        if (std::wstring_view(text ? text : L"", length) != L"Windows.Gaming.Input.Gamepad" ||
            (winrt::guid(iid) != winrt::guid_of<gi::IGamepadStatics>() && winrt::guid(iid) != winrt::guid_of<gi::IGamepadStatics2>()))
            return realFactory(name, iid, result);
        *result = nullptr;
        try
        {
            static std::once_flag once;
            static winrt::com_ptr<KeyboardFactory> factory;
            std::call_once(once, [&] {
                gi::IGamepadStatics physical{nullptr};
                winrt::check_hresult(realFactory(name, winrt::guid_of<gi::IGamepadStatics>(), winrt::put_abi(physical)));
                factory = winrt::make_self<KeyboardFactory>(physical);
                factory->Start();
                { std::lock_guard<std::mutex> lock(factoryMutex); currentFactory = factory; }
                Log("Gamepad factory connected; active preset: " + std::to_string(active.load()));
            });
            return factory->QueryInterface(iid, result);
        }
        catch (...) { return winrt::to_hresult(); }
    }

    void EditConfig(HWND window)
    {
        const auto path = root / L"presets" / files[std::max(1, active.load()) - 1];
        const auto parameters = L"\"" + path.wstring() + L"\"";
        if (reinterpret_cast<INT_PTR>(ShellExecuteW(window, L"open", L"notepad.exe", parameters.c_str(), nullptr, SW_SHOWNORMAL)) <= 32)
            throw std::runtime_error("Cannot open preset editor");
    }

    void ResetInput()
    {
        std::lock_guard<std::mutex> lock(stateMutex);
        inputState.Clear();
    }

    bool SwapImport(const char* name, void* replacement, void** original)
    {
        auto base = reinterpret_cast<unsigned char*>(GetModuleHandleW(nullptr));
        auto dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
        auto nt = reinterpret_cast<IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
        auto descriptor = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(base + nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress);
        for (; descriptor->Name; ++descriptor)
        {
            if (!descriptor->OriginalFirstThunk) continue;
            auto names = reinterpret_cast<IMAGE_THUNK_DATA64*>(base + descriptor->OriginalFirstThunk);
            auto slots = reinterpret_cast<IMAGE_THUNK_DATA64*>(base + descriptor->FirstThunk);
            for (; names->u1.AddressOfData; ++names, ++slots)
            {
                if (IMAGE_SNAP_BY_ORDINAL64(names->u1.Ordinal)) continue;
                const auto imported = reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(base + names->u1.AddressOfData);
                if (strcmp(reinterpret_cast<const char*>(imported->Name), name)) continue;
                DWORD protection;
                if (!VirtualProtect(&slots->u1.Function, sizeof(void*), PAGE_READWRITE, &protection)) return false;
                *original = reinterpret_cast<void*>(slots->u1.Function);
                InterlockedExchangePointer(reinterpret_cast<void* volatile*>(&slots->u1.Function), replacement);
                DWORD ignored;
                VirtualProtect(&slots->u1.Function, sizeof(void*), protection, &ignored);
                return true;
            }
        }
        return false;
    }
}

extern "C" __declspec(dllexport) DWORD WINAPI InitializeGamepadMod(void*) noexcept
{
    try
    {
        wchar_t path[32768]{};
        if (!GetModuleFileNameW(module, path, 32768)) return 1;
        root = std::filesystem::path(path).parent_path();
        Reload();
        active.store(gamepadmod::LoadSelectedPreset(root / L"settings.ini"));
        if (!SwapImport("RoGetActivationFactory", reinterpret_cast<void*>(&HookFactory), reinterpret_cast<void**>(&realFactory))) return 2;
        const gamepadmod::MenuCallbacks menuCallbacks{
            Select, [] { return active.load(); }, EditConfig, Reload, ResetInput,
            [](const char* message) { Log(message); }
        };
        if (!gamepadmod::InstallLauncherMenu(SwapImport, menuCallbacks)) return 4;
        Log("Initialized: custom launcher Controls menu hooks installed, active preset: " + std::to_string(active.load()));
        return 0;
    }
    catch (const std::exception& error) { Log(error.what()); return 5; }
    catch (...) { return 6; }
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH) module = instance;
    return TRUE;
}
