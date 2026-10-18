#define NOMINMAX
#include <windows.h>
#include <windowsx.h>
#include <roapi.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Gaming.Input.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <algorithm>
#include <stdexcept>
#include "TestProfile.h"

void Require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

namespace
{
    void RunChild(const std::filesystem::path& dll, const std::wstring& mode)
    {
        wchar_t executable[32768]{};
        Require(GetModuleFileNameW(nullptr, executable, 32768) != 0, "Cannot locate integration executable");
        std::wstring command = L"\"" + std::wstring(executable) + L"\" \"" + dll.wstring() + L"\" " + mode;
        STARTUPINFOW startup{sizeof(startup)};
        SECURITY_ATTRIBUTES attributes{sizeof(attributes), nullptr, TRUE};
        const auto report = dll.parent_path() / L"child-output.txt";
        HANDLE output = CreateFileW(report.c_str(), GENERIC_WRITE, FILE_SHARE_READ, &attributes, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        Require(output != INVALID_HANDLE_VALUE, "Cannot create child test report");
        startup.dwFlags = STARTF_USESTDHANDLES;
        startup.hStdOutput = startup.hStdError = output;
        startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
        PROCESS_INFORMATION child{};
        const BOOL created = CreateProcessW(executable, command.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW,
            nullptr, nullptr, &startup, &child);
        CloseHandle(output);
        Require(created != 0, "Cannot launch isolated integration test");
        const DWORD wait = WaitForSingleObject(child.hProcess, 30000);
        DWORD result = 1;
        if (wait != WAIT_OBJECT_0)
        {
            TerminateProcess(child.hProcess, 1);
            WaitForSingleObject(child.hProcess, 5000);
        }
        else GetExitCodeProcess(child.hProcess, &result);
        CloseHandle(child.hThread);
        CloseHandle(child.hProcess);
        std::ifstream messages(report);
        std::cout << messages.rdbuf();
        Require(wait == WAIT_OBJECT_0 && result == 0, "Isolated integration child failed");
    }

    int lastPointerY = -1;
    int hostKeyDown = 0, hostCharacters = 0;
    int nativeSelection = -1;
    int nativeActivations = 0;
    int lastNativeActivation = -1;
    int nativeClicks = 0;
    LRESULT CALLBACK HostProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
    {
        if (message == WM_MOUSEMOVE) lastPointerY = GET_Y_LPARAM(lParam);
        if (message == WM_KEYDOWN) ++hostKeyDown;
        if (message == WM_KEYDOWN && nativeSelection >= 0)
        {
            if (wParam == VK_UP) nativeSelection = std::max(1, nativeSelection - 1);
            if (wParam == VK_DOWN) nativeSelection = std::min(6, nativeSelection + 1);
            if (wParam == VK_RETURN) { ++nativeActivations; lastNativeActivation = nativeSelection; }
            if (wParam == VK_ESCAPE) nativeSelection = 6;
        }
        if (message == WM_CHAR) ++hostCharacters;
        if (message == WM_LBUTTONDOWN) ++nativeClicks;
        return DefWindowProcW(window, message, wParam, lParam);
    }

    void PressKey(HWND window, WPARAM key)
    {
        SendMessageW(window, WM_KEYDOWN, key, 0);
        SendMessageW(window, WM_KEYUP, key, 0);
        MSG queued{};
        while (PeekMessageW(&queued, window, WM_KEYFIRST, WM_KEYLAST, PM_REMOVE)) DispatchMessageW(&queued);
    }

    void RenderLauncher(HWND window, bool flush = true, bool withSpace = true, int expectedHighlight = -1)
    {
        HDC screen = GetDC(window);
        HDC buffer = CreateCompatibleDC(screen);
        HBITMAP bitmap = CreateCompatibleBitmap(screen, 640, 480);
        HGDIOBJ oldBitmap = SelectObject(buffer, bitmap);
        HBRUSH background = CreateSolidBrush(RGB(17, 18, 22));
        RECT all{0, 0, 640, 480};
        FillRect(buffer, &all, background);
        DeleteObject(background);
        HFONT font = CreateFontW(-20, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0, ANTIALIASED_QUALITY, 0, L"Arial");
        HGDIOBJ oldFont = SelectObject(buffer, font);
        SetBkMode(buffer, TRANSPARENT);
        SetTextColor(buffer, RGB(234, 228, 228));
        auto text = [&](const wchar_t* value, int y, UINT align = DT_LEFT, int index = -1) {
            RECT row{40, y, 600, y + 40};
            if (index >= 0 && nativeSelection == index)
            {
                // Use Azimuth's actual highlight API and a narrow label band.
                // An edge sampled near the inserted row misses this highlight.
                HDC highlight = CreateCompatibleDC(buffer);
                BITMAPINFO info{};
                info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
                info.bmiHeader.biWidth = 1;
                info.bmiHeader.biHeight = 1;
                info.bmiHeader.biPlanes = 1;
                info.bmiHeader.biBitCount = 32;
                void* pixels = nullptr;
                HBITMAP pixel = CreateDIBSection(highlight, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
                Require(pixel && pixels, "Cannot create native highlight pixel");
                *static_cast<DWORD*>(pixels) = 0xc03060a8;
                HGDIOBJ previous = SelectObject(highlight, pixel);
                Require(GdiAlphaBlend(buffer, 47, y, 203, 40, highlight, 0, 0, 1, 1, {AC_SRC_OVER, 0, 255, AC_SRC_ALPHA}),
                    "Cannot draw native alpha highlight");
                SelectObject(highlight, previous);
                DeleteObject(pixel);
                DeleteDC(highlight);
            }
            SetTextColor(buffer, index >= 0 && nativeSelection == index ? RGB(255, 255, 255) : RGB(234, 228, 228));
            DrawTextW(buffer, value, -1, &row, align | DT_SINGLELINE | DT_VCENTER);
        };
        text(L"Start", 30, DT_CENTER, 0);
        text(L"Display \u203a", 110, DT_LEFT, 1);
        text(L"Audio \u203a", 190, DT_LEFT, 4);
        text(L"Game \u203a", 230, DT_LEFT, 5);
        text(L"Quit", 270, DT_CENTER, 6);
        text(L"Add a game", withSpace ? 350 : 430, DT_CENTER);
        SelectObject(buffer, oldFont);
        DeleteObject(font);
        if (flush) GdiFlush();
        BitBlt(screen, 0, 0, 640, 480, buffer, 0, 0, SRCCOPY);
        if (expectedHighlight >= 0)
        {
            for (int row = 5; row <= 7; ++row)
            {
                const auto pixel = GetPixel(buffer, 240, 250 + (row - 5) * 40);
                const bool blue = GetBValue(pixel) > GetRValue(pixel) + 30;
                if (blue != (row == expectedHighlight)) std::cerr << "Highlight mismatch: native=" << nativeSelection << " expected=" << expectedHighlight << " row=" << row << " pixel=" << pixel << '\n';
                Require(blue == (row == expectedHighlight), "Game, Controls and Quit must have independent highlight bands");
            }
            if (expectedHighlight == 6)
            {
                const auto pixel = GetPixel(buffer, 240, 130);
                Require(GetBValue(pixel) <= GetRValue(pixel) + 30,
                    "Controls focus must suppress a narrow native highlight on Display as well as Game");
            }
        }
        if (!flush)
        {
            // The original Quit label is centered at x=320. White glyphs on the left
            // of this inserted row prove that the custom Controls entry was rendered.
            int glyphs = 0;
            for (int y = 275; y < 305; y += 2)
                for (int x = 45; x < 240; x += 2)
                {
                    const auto pixel = GetPixel(buffer, x, y);
                    if (GetRValue(pixel) > 140 && GetGValue(pixel) > 140 && GetBValue(pixel) > 140) ++glyphs;
                }
            Require(glyphs > 10, "Controls entry was not rendered into the fullscreen menu bitmap");
            int quitGlyphs = 0;
            for (int y = 315; y < 345; y += 2)
                for (int x = 295; x < 345; x += 2)
                {
                    const auto pixel = GetPixel(buffer, x, y);
                    if (GetRValue(pixel) > 140 && GetGValue(pixel) > 140 && GetBValue(pixel) > 140) ++quitGlyphs;
                }
            Require(quitGlyphs > 5, "Quit text must be moved down together with its pointer coordinates");
        }
        SelectObject(buffer, oldBitmap);
        DeleteObject(bitmap);
        DeleteDC(buffer);
        ReleaseDC(window, screen);
    }
}

int wmain(int argc, wchar_t** argv)
{
    try
    {
        Require(argc >= 2 && argc <= 4, "Expected absolute GamepadMod.dll path and optional test mode");
        winrt::init_apartment(winrt::apartment_type::multi_threaded);
        HMODULE dll = GetModuleHandleW(L"GamepadMod.dll");
        const bool injected = dll != nullptr;
        if (argc == 2 && !injected)
        {
            TestProfile profile(argv[1]);
            RunChild(profile.Dll, L"--suite");
            for (int index = 0; index <= 3; ++index)
            {
                gamepadmod::SaveSelectedPreset(profile.Folder / L"settings.ini", index);
                RunChild(profile.Dll, L"--startup-check " + std::to_wstring(index));
            }
            std::cout << "PASS: isolated menu suite and restart persistence for Off and presets 1..3; user settings untouched\n";
            return 0;
        }
        if (argc == 3) Require(std::wstring_view(argv[2]) == L"--suite", "Unknown integration test mode");
        if (!dll) dll = LoadLibraryW(argv[1]);
        Require(dll != nullptr, "DLL did not load");
        const auto initialize = reinterpret_cast<DWORD(WINAPI*)(void*)>(GetProcAddress(dll, "InitializeGamepadMod"));
        Require(initialize && (injected || initialize(nullptr) == 0), "DLL initialization failed");

        if (argc == 4)
        {
            Require(std::wstring_view(argv[2]) == L"--startup-check", "Unknown integration test mode");
            const int expected = _wtoi(argv[3]);
            namespace gi = winrt::Windows::Gaming::Input;
            const winrt::hstring name = L"Windows.Gaming.Input.Gamepad";
            const GUID iid = winrt::guid_of<gi::IGamepadStatics>();
            using FactoryFn = HRESULT(WINAPI*)(HSTRING, REFIID, void**);
            const auto systemFactory = reinterpret_cast<FactoryFn>(GetProcAddress(GetModuleHandleW(L"combase.dll"), "RoGetActivationFactory"));
            Require(systemFactory != nullptr, "Cannot locate physical gamepad factory");
            gi::IGamepadStatics physical{nullptr}, modified{nullptr};
            winrt::check_hresult(systemFactory(reinterpret_cast<HSTRING>(winrt::get_abi(name)), iid, winrt::put_abi(physical)));
            winrt::check_hresult(RoGetActivationFactory(reinterpret_cast<HSTRING>(winrt::get_abi(name)), iid, winrt::put_abi(modified)));
            Require(modified.Gamepads().Size() == physical.Gamepads().Size() + (expected != 0 ? 1U : 0U),
                "Startup must restore whether the saved virtual gamepad is enabled");
            wchar_t saved[32]{};
            const auto settings = std::filesystem::path(argv[1]).parent_path() / L"settings.ini";
            GetPrivateProfileStringW(L"mod", L"preset", L"missing", saved, 32, settings.c_str());
            Require(std::wstring(saved) == argv[3], "Startup must not overwrite the selected preset");
            std::cout << "PASS: restored saved preset " << expected << " in a fresh process\n";
            return 0;
        }

        const wchar_t* className = L"Azimuth.Mod.Integration.Test";
        WNDCLASSW windowClass{};
        windowClass.lpfnWndProc = HostProcedure;
        windowClass.hInstance = GetModuleHandleW(nullptr);
        windowClass.lpszClassName = className;
        Require(RegisterClassW(&windowClass) != 0, "Cannot register test window");
        HWND window = CreateWindowExW(0, className, L"Azimuth Mod Integration Test", WS_POPUP,
            0, 0, 800, 600, nullptr, nullptr, windowClass.hInstance, nullptr);
        Require(window != nullptr, "Cannot create hidden test window");
        Require(GetMenu(window) == nullptr, "Fullscreen integration must not depend on a Windows menu bar");
        RenderLauncher(window, false);
        RenderLauncher(window, true);
        SendMessageW(window, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(320, 290));
        SendMessageW(window, WM_LBUTTONUP, 0, MAKELPARAM(320, 290));
        RenderLauncher(window);
        Require(GetAsyncKeyState(VK_RETURN) == 0 && GetKeyState(VK_LBUTTON) == 0,
            "Custom submenu must suppress original menu key/button polling");
        POINT suppressedPointer{};
        Require(GetCursorPos(&suppressedPointer) && suppressedPointer.x == -10000 && suppressedPointer.y == -10000,
            "Original menu must not hit-test the old Quit rectangle while Controls is open");

        namespace gi = winrt::Windows::Gaming::Input;
        gi::IGamepadStatics factory{nullptr};
        const winrt::hstring gamepadClass = L"Windows.Gaming.Input.Gamepad";
        const GUID iid = winrt::guid_of<gi::IGamepadStatics>();
        winrt::check_hresult(RoGetActivationFactory(reinterpret_cast<HSTRING>(winrt::get_abi(gamepadClass)), iid, winrt::put_abi(factory)));
        const auto physicalCount = factory.Gamepads().Size();
        int added = 0, removed = 0;
        const auto addedToken = factory.GamepadAdded([&](auto const&, auto const&) { ++added; });
        const auto removedToken = factory.GamepadRemoved([&](auto const&, auto const&) { ++removed; });
        for (int preset = 1; preset <= 3; ++preset)
        {
            PressKey(window, VK_HOME);
            for (int i = 0; i < preset; ++i) PressKey(window, VK_DOWN);
            PressKey(window, VK_RETURN);
            RenderLauncher(window);
            Require(factory.Gamepads().Size() == physicalCount + 1, "Enabled preset must expose exactly one extra gamepad");
            const auto pad = factory.Gamepads().GetAt(0);
            const auto reading = pad.GetCurrentReading();
            Require(reading.Buttons == gi::GamepadButtons::None && reading.LeftTrigger == 0 && reading.RightTrigger == 0,
                "Background input must be neutral");

        }
        Require(added == 1 && removed == 0, "Changing presets must not reconnect the virtual pad");
        PressKey(window, VK_HOME);
        PressKey(window, VK_RETURN);
        Require(factory.Gamepads().Size() == physicalCount, "Off must remove virtual pad from enumeration");
        Require(removed == 1, "Off must notify host of pad removal");
        PressKey(window, VK_ESCAPE);
        RenderLauncher(window);
        SendMessageW(window, WM_MOUSEMOVE, 0, MAKELPARAM(320, 330));
        Require(lastPointerY == 290, "Moved Quit row must translate pointer input to its original coordinates");
        PressKey(window, VK_F9);
        RenderLauncher(window);
        PressKey(window, VK_HOME);
        PressKey(window, VK_DOWN);
        PressKey(window, VK_RETURN);
        PressKey(window, VK_ESCAPE);
        RenderLauncher(window);
        POINT pointer{320, 290};
        ClientToScreen(window, &pointer);
        SendMessageW(window, WM_POINTERDOWN, 0, MAKELPARAM(pointer.x, pointer.y));
        SendMessageW(window, WM_POINTERUP, 0, MAKELPARAM(pointer.x, pointer.y));
        RenderLauncher(window);
        POINT pointerOpened{};
        Require(GetCursorPos(&pointerOpened) && pointerOpened.x == -10000 && pointerOpened.y == -10000,
            "WM_POINTER must actually open Controls and isolate host hit-testing");
        Require(factory.Gamepads().Size() == physicalCount + 1, "F9 must reopen the custom Controls submenu");
        RenderLauncher(window, true, false);
        POINT afterResize{};
        Require(GetCursorPos(&afterResize) && !(afterResize.x == -10000 && afterResize.y == -10000),
            "A layout without room for Controls must release the host input gate");
        RenderLauncher(window);
        PressKey(window, VK_F9);
        RenderLauncher(window);
        PressKey(window, VK_HOME);
        PressKey(window, VK_RETURN);
        Require(factory.Gamepads().Size() == physicalCount, "Off selection after WM_POINTER entry must remove the virtual pad");
        SendMessageW(window, WM_KEYDOWN, VK_BACK, 0);
        POINT whileHeld{};
        Require(GetCursorPos(&whileHeld) && whileHeld.x == -10000, "Closing Backspace must remain captured until key-up");
        const int keysBefore = hostKeyDown, charsBefore = hostCharacters;
        SendMessageW(window, WM_KEYDOWN, VK_BACK, 1LL << 30);
        SendMessageW(window, WM_CHAR, VK_BACK, 0);
        Require(hostKeyDown == keysBefore && hostCharacters == charsBefore, "Closing key auto-repeat and characters must not reach host");
        SendMessageW(window, WM_KEYUP, VK_BACK, 0);
        GetCursorPos(&whileHeld);
        PressKey(window, VK_F8);
        Require(hostKeyDown == keysBefore + 1, "Host keyboard input must resume after closing-key release");

        // Simulate the native seven-row menu, including its white focused label.
        // Each arrow must stop on the inserted row without activating native Quit.
        nativeSelection = 5;
        SendMessageW(window, WM_MOUSEMOVE, 0, MAKELPARAM(320, 250));
        RenderLauncher(window, true, true, 5);
        const int beforeControls = hostKeyDown;
        PressKey(window, VK_DOWN);
        RenderLauncher(window, true, true, 6);
        Require(nativeSelection == 5 && hostKeyDown == beforeControls, "Game -> Controls must insert a keyboard stop");
        PressKey(window, VK_RETURN);
        Require(nativeActivations == 0, "Enter on Controls must never activate native Quit");
        RenderLauncher(window);
        PressKey(window, VK_ESCAPE);
        RenderLauncher(window, true, true, 6);
        PressKey(window, VK_DOWN);
        Require(nativeSelection == 6, "Controls -> Quit must advance native focus once");
        RenderLauncher(window, true, true, 7);
        PressKey(window, VK_RETURN);
        Require(nativeActivations == 1, "Enter on native Quit must retain its own action");
        PressKey(window, VK_UP);
        Require(nativeSelection == 6, "Quit -> Controls must insert a keyboard stop");
        RenderLauncher(window, true, true, 6);
        PressKey(window, VK_UP);
        Require(nativeSelection == 5, "Controls -> Game must advance native focus once");
        RenderLauncher(window, true, true, 5);
        // Native controller input can change focus after a keyboard prediction.
        nativeSelection = 6;
        RenderLauncher(window, true, true, 7);
        PressKey(window, VK_UP);
        RenderLauncher(window, true, true, 6);
        PressKey(window, VK_UP);
        Require(nativeSelection == 5, "Rendered native focus changes must replace stale keyboard predictions");
        nativeSelection = 1;
        SendMessageW(window, WM_MOUSEMOVE, 0, MAKELPARAM(320, 130));
        RenderLauncher(window);
        SendMessageW(window, WM_MOUSEMOVE, 0, MAKELPARAM(320, 290));
        RenderLauncher(window, true, true, 6);
        PressKey(window, VK_DOWN);
        Require(nativeSelection == 6, "Mouse-selected Controls must synchronize an arbitrary native anchor before moving to Quit");
        RenderLauncher(window, true, true, 7);
        const int beforeClick = nativeClicks;
        SendMessageW(window, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(320, 290));
        SendMessageW(window, WM_LBUTTONUP, 0, MAKELPARAM(320, 330));
        Require(nativeClicks == beforeClick, "A Controls drag release over Quit must remain captured");
        // Azimuth's native launcher can inspect queue messages before dispatching.
        // The added row must already own its click at that point.
        RenderLauncher(window);
        MSG queued{};
        PostMessageW(window, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(320, 290));
        Require(PeekMessageW(&queued, window, WM_LBUTTONDOWN, WM_LBUTTONDOWN, PM_REMOVE) && queued.message == WM_NULL,
            "Controls click must be consumed before native pre-dispatch Quit handling");
        PostMessageW(window, WM_LBUTTONUP, 0, MAKELPARAM(320, 290));
        Require(GetMessageW(&queued, window, WM_LBUTTONUP, WM_LBUTTONUP) > 0 && queued.message == WM_NULL,
            "GetMessage must consume the release before native launcher processing");
        RenderLauncher(window);
        PressKey(window, VK_ESCAPE);
        RenderLauncher(window);
        SendMessageW(window, WM_MOUSEMOVE, 0, MAKELPARAM(320, 330));
        PostMessageW(window, WM_MOUSEMOVE, 0, MAKELPARAM(320, 330));
        Require(PeekMessageW(&queued, window, WM_MOUSEMOVE, WM_MOUSEMOVE, PM_REMOVE) && GET_Y_LPARAM(queued.lParam) == 290,
            "Queued Quit coordinates must be translated before native processing");
        DispatchMessageW(&queued);
        Require(lastPointerY == 290, "Dispatch must not translate queued coordinates a second time");
        nativeSelection = 4;
        SendMessageW(window, WM_MOUSEMOVE, 0, MAKELPARAM(320, 210));
        RenderLauncher(window);
        const int beforeRapid = nativeActivations;
        PressKey(window, VK_DOWN); // Audio -> Game, without another rendered frame.
        PressKey(window, VK_DOWN); // Game -> Controls, still without rendering.
        Require(nativeSelection == 5, "Rapid arrows before repaint must stop at Controls");
        PressKey(window, VK_RETURN);
        Require(nativeActivations == beforeRapid, "Rapid navigation Enter must open Controls instead of native Quit");
        RenderLauncher(window);
        PressKey(window, VK_ESCAPE);
        SendMessageW(window, WM_KEYDOWN, VK_DOWN, 0);
        SendMessageW(window, WM_KEYDOWN, VK_DOWN, 1LL << 30);
        SendMessageW(window, WM_KEYUP, VK_DOWN, 0);
        MSG relay{};
        while (PeekMessageW(&relay, window, WM_KEYFIRST, WM_KEYLAST, PM_REMOVE)) DispatchMessageW(&relay);
        Require(nativeSelection == 6, "One held arrow at Controls must not relay two native steps");
        nativeSelection = 1;
        SendMessageW(window, WM_MOUSEMOVE, 0, MAKELPARAM(320, 130));
        RenderLauncher(window);
        PressKey(window, VK_F9);
        RenderLauncher(window);
        PressKey(window, VK_ESCAPE);
        PressKey(window, VK_DOWN);
        Require(nativeSelection == 6, "F9 from Display must preserve the native anchor when returning to Quit");
        RenderLauncher(window);
        PressKey(window, VK_UP); // Quit -> Controls.
        PressKey(window, VK_UP); // Controls -> Game.
        PressKey(window, VK_ESCAPE); // Native Escape focuses Quit.
        PressKey(window, VK_UP);
        const int beforeEscapeEntry = nativeActivations;
        SendMessageW(window, WM_KEYDOWN, VK_RETURN, 0); // Keep the opening Enter held.
        Require(nativeActivations == beforeEscapeEntry, "Up after native Escape must stop at Controls before repaint");
        RenderLauncher(window);
        PressKey(window, VK_END);
        SendMessageW(window, WM_KEYDOWN, VK_RETURN, 1LL << 30); // Repeat activates Back.
        SendMessageW(window, WM_KEYUP, VK_RETURN, 0);
        GetCursorPos(&suppressedPointer);
        const int beforeReleased = hostKeyDown;
        PressKey(window, VK_F8);
        Require(hostKeyDown == beforeReleased + 1, "Closing a held root Enter must release both root and closing-key capture");
        nativeSelection = 5;
        SendMessageW(window, WM_MOUSEMOVE, 0, MAKELPARAM(320, 250));
        RenderLauncher(window);
        PressKey(window, VK_DOWN);
        PostMessageW(window, WM_KEYDOWN, VK_DOWN, 0);
        PostMessageW(window, WM_KEYUP, VK_DOWN, 0);
        PostMessageW(window, WM_KEYDOWN, VK_RETURN, 0);
        PostMessageW(window, WM_KEYUP, VK_RETURN, 0);
        while (PeekMessageW(&relay, window, 0, 0, PM_REMOVE)) DispatchMessageW(&relay);
        Require(lastNativeActivation == 6, "Already queued Down+Enter must synchronize native Quit before activation");
        factory.GamepadAdded(addedToken);
        factory.GamepadRemoved(removedToken);
        DestroyWindow(window);
        UnregisterClassW(className, windowClass.hInstance);
        // IAT hooks are process-lifetime hooks: do not unload the DLL while host imports point to it.
        std::cout << "PASS: in-process DLL, narrow alpha highlights, native focus resynchronization, arrow stops, mouse and pointer entry, keyboard submenu, default Off, three selections, pad enumeration, add/remove events, neutral background input\n";
        return 0;
    }
    catch (const winrt::hresult_error& error) { std::wcerr << L"FAIL: " << error.message().c_str() << L'\n'; return 1; }
    catch (const std::exception& error) { std::cerr << "FAIL: " << error.what() << '\n'; return 1; }
}
