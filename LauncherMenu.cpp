#define NOMINMAX
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <algorithm>
#include <atomic>
#include <memory>
#include <string_view>
#include <string>
#include <stdexcept>
#include <deque>
#include "LauncherMenu.h"

namespace gamepadmod
{
    namespace
    {
        constexpr wchar_t property[] = L"AzimuthGamepadMod.Launcher";
        constexpr UINT_PTR subclassId = 0xA21;
        MenuCallbacks callbacks{};
        std::atomic<bool> panelOpen{false};
        std::atomic<bool> waitingForRelease{false};
        std::atomic<int> releaseKey{0};
        std::atomic<unsigned> rootKeys{0};
        HWND hostWindow = nullptr;
        using CreateFn = decltype(&CreateWindowExW);
        using TextFn = decltype(&DrawTextW);
        using BlitFn = decltype(&BitBlt);
        using FlushFn = decltype(&GdiFlush);
        using AlphaBlendFn = decltype(&GdiAlphaBlend);
        using CursorFn = decltype(&GetCursorPos);
        using AsyncKeyFn = decltype(&GetAsyncKeyState);
        using KeyFn = decltype(&GetKeyState);
        using GetMessageFn = decltype(&GetMessageW);
        using PeekMessageFn = decltype(&PeekMessageW);
        using DispatchFn = decltype(&DispatchMessageW);
        CreateFn originalCreate;
        TextFn originalText;
        BlitFn originalBlit;
        FlushFn originalFlush;
        AlphaBlendFn originalAlphaBlend;
        CursorFn originalCursor;
        AsyncKeyFn originalAsyncKey;
        KeyFn originalKey;
        GetMessageFn originalGetMessage;
        PeekMessageFn originalPeekMessage;
        DispatchFn originalDispatch;
        thread_local MSG* routingMessage = nullptr;
        thread_local bool queueConsumed = false;
        thread_local bool dispatchRouted = false;
        thread_local MSG routedMessage{};
        thread_local std::deque<MSG> navigationMessages;

        struct UiState
        {
            bool RootVisible = false;
            bool Hover = false;
            bool Pressed = false;
            bool Open = false;
            bool ControlsFocused = false;
            int NativeSelected = -1;
            int Anchor = 5;
            bool StartEnabled = false;
            int Selected = 0;
            int ClosingKey = 0;
            int Width = 0, Height = 0;
            int Shift = 0;
            RECT Entry{};
            RECT NativeRows[7]{};
            RECT Choices[7]{};
            LOGFONTW Font{};
        };

        struct Frame
        {
            HDC Dc = nullptr;
            bool Start = false, Display = false, Game = false, Quit = false;
            RECT GameRect{}, QuitRect{};
            int InkBottom = 0;
            int NativeSelected = -1;
            RECT Rows[7]{};
            bool StartEnabled = false;
            LOGFONTW Font{};
        };
        thread_local Frame frame;
        bool SuppressHostInput();

        unsigned RootKey(int key)
        {
            switch (key)
            {
            case VK_UP: return 1;
            case VK_DOWN: return 2;
            case VK_RETURN: return 4;
            case VK_SPACE: return 8;
            default: return 0;
            }
        }

        UiState* State(HWND window) { return reinterpret_cast<UiState*>(GetPropW(window, property)); }

        void Repaint(HWND window) { InvalidateRect(window, nullptr, FALSE); }

        void SetOpen(HWND window, UiState& ui, bool open, int closingKey = 0)
        {
            if (open && !ui.Open && !ui.ControlsFocused) ui.Anchor = ui.NativeSelected;
            ui.Open = open;
            ui.Pressed = false;
            ui.Selected = callbacks.Active();
            if (!open) ui.ControlsFocused = true;
            panelOpen.store(open);
            waitingForRelease.store(!open);
            releaseKey.store(open ? 0 : closingKey);
            ui.ClosingKey = open ? 0 : closingKey;
            callbacks.ResetInput();
            Repaint(window);
        }

        void Activate(HWND window, UiState& ui, int closingKey = 0)
        {
            try
            {
                if (ui.Selected <= 3) callbacks.Select(ui.Selected);
                else if (ui.Selected == 4) callbacks.Edit(window);
                else if (ui.Selected == 5) callbacks.Reload();
                else SetOpen(window, ui, false, closingKey);
                Repaint(window);
            }
            catch (const std::exception& error)
            {
                MessageBoxA(window, error.what(), "Gamepad preset error", MB_OK | MB_ICONERROR);
            }
            catch (...)
            {
                MessageBoxW(window, L"Could not apply gamepad settings.", L"Gamepad Controls", MB_OK | MB_ICONERROR);
            }
        }

        POINT ClientPoint(LPARAM position)
        {
            // Azimuth presents its launcher raster without rescaling the client
            // pointer coordinates. Do not scale by GetClientRect: DPI virtualization
            // and a differently sized render buffer otherwise hit the old Quit row.
            return { GET_X_LPARAM(position), GET_Y_LPARAM(position) };
        }

        bool IsPointerMessage(UINT message)
        {
            return message == WM_POINTERDOWN || message == WM_POINTERUP || message == WM_POINTERUPDATE;
        }

        void LeaveControls(HWND window, UiState& ui, int destination)
        {
            const int anchor = ui.Anchor >= 0 ? ui.Anchor : destination;
            const WPARAM key = destination < anchor ? VK_UP : VK_DOWN;
            const LPARAM scan = static_cast<LPARAM>(MapVirtualKeyW(static_cast<UINT>(key), MAPVK_VK_TO_VSC)) << 16;
            // Mouse entry can leave native focus on any original row. Move that
            // focus to Game or Quit before returning control to the native menu.
            for (int i = 0; i < std::abs(destination - anchor); ++i)
            {
                MSG down{};
                down.hwnd = window;
                down.message = WM_KEYDOWN;
                down.wParam = key;
                down.lParam = scan | 1;
                down.time = GetTickCount();
                navigationMessages.push_back(down);
                down.message = WM_KEYUP;
                down.lParam = scan | 0xc0000001LL;
                navigationMessages.push_back(down);
            }
            // Wake GetMessage if this was invoked by a sent window message.
            // Relay keys themselves are returned before later queued user input.
            if (!navigationMessages.empty()) PostMessageW(window, WM_NULL, 0, 0);
            ui.NativeSelected = destination;
            ui.ControlsFocused = false;
            ui.Hover = false;
        }

        LRESULT CALLBACK WindowProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam, UINT_PTR, DWORD_PTR)
        {
            if (dispatchRouted) return DefSubclassProc(window, message, wParam, lParam);
            const UINT originalMessage = message;
            bool pointerConverted = false;
            UiState* ui = State(window);
            if (!ui)
            {
                if (routingMessage) { queueConsumed = false; return 0; }
                return DefSubclassProc(window, message, wParam, lParam);
            }
            if (message == WM_NCDESTROY)
            {
                if (hostWindow == window) { hostWindow = nullptr; panelOpen.store(false); waitingForRelease.store(false); releaseKey.store(0); rootKeys.store(0); }
                RemovePropW(window, property);
                RemoveWindowSubclass(window, WindowProcedure, subclassId);
                navigationMessages.erase(std::remove_if(navigationMessages.begin(), navigationMessages.end(),
                    [window](const MSG& pending) { return pending.hwnd == window; }), navigationMessages.end());
                delete ui;
                return DefSubclassProc(window, message, wParam, lParam);
            }
            if (message == WM_ACTIVATEAPP || message == WM_KILLFOCUS || message == WM_ENTERMENULOOP ||
                message == WM_EXITMENULOOP || (message == WM_ACTIVATE && LOWORD(wParam) == WA_INACTIVE)) callbacks.ResetInput();
            if ((message == WM_ACTIVATEAPP && !wParam) || message == WM_KILLFOCUS ||
                (message == WM_ACTIVATE && LOWORD(wParam) == WA_INACTIVE)) { releaseKey.store(0); rootKeys.store(0); }
            if ((message == WM_KEYUP || message == WM_SYSKEYUP) && (rootKeys.load() & RootKey(static_cast<int>(wParam))))
            {
                rootKeys.fetch_and(~RootKey(static_cast<int>(wParam)));
                if (static_cast<int>(wParam) == ui->ClosingKey) releaseKey.store(0);
                return 0;
            }
            if (!ui->Open)
            {
                const bool keyDown = message == WM_KEYDOWN || message == WM_SYSKEYDOWN;
                const bool keyUp = message == WM_KEYUP || message == WM_SYSKEYUP;
                const bool character = message == WM_CHAR || message == WM_SYSCHAR;
                if (ui->ClosingKey && keyUp && static_cast<int>(wParam) == ui->ClosingKey)
                {
                    releaseKey.store(0);
                    return 0;
                }
                if (ui->ClosingKey && static_cast<int>(wParam) == ui->ClosingKey &&
                    (character || (keyDown && (lParam & (1LL << 30))))) return 0;
                const bool input = keyDown || keyUp || character || IsPointerMessage(message) ||
                    message == WM_MOUSEMOVE || message == WM_LBUTTONDOWN || message == WM_LBUTTONUP || message == WM_MOUSEWHEEL;
                if (input && SuppressHostInput()) return 0;
                if (keyDown && !(lParam & (1LL << 30))) ui->ClosingKey = 0;
            }
            // CoreWindow may use WM_POINTER rather than synthesized WM_MOUSE messages.
            if (ui->RootVisible && IsPointerMessage(message))
            {
                POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
                ScreenToClient(window, &point);
                lParam = MAKELPARAM(point.x, point.y);
                pointerConverted = true;
                message = message == WM_POINTERDOWN ? WM_LBUTTONDOWN : message == WM_POINTERUP ? WM_LBUTTONUP : WM_MOUSEMOVE;
            }
            if (ui->RootVisible && message == WM_KEYDOWN && wParam == VK_F9)
            {
                if (!(lParam & (1LL << 30))) SetOpen(window, *ui, !ui->Open, VK_F9);
                return 0;
            }
            if (ui->RootVisible && !ui->Open && message == WM_KEYDOWN)
            {
                bool consume = false;
                if ((rootKeys.load() & RootKey(static_cast<int>(wParam))) && (lParam & (1LL << 30))) return 0;
                if (wParam == VK_UP || wParam == VK_DOWN)
                {
                    const bool down = wParam == VK_DOWN;
                    if (ui->ControlsFocused)
                    {
                        LeaveControls(window, *ui, down ? 6 : 5);
                        consume = true;
                    }
                    else if ((down && ui->NativeSelected == 5) || (!down && ui->NativeSelected == 6))
                    {
                        ui->ControlsFocused = true;
                        ui->Anchor = ui->NativeSelected;
                        consume = true;
                    }
                    else if (ui->NativeSelected >= 0)
                    {
                        const int first = ui->StartEnabled ? 0 : 1;
                        const int next = ui->NativeSelected + (down ? 1 : -1);
                        // Native wrapping can also visit the player-name field.
                        // Re-observe that boundary rather than inventing its layout.
                        ui->NativeSelected = next >= first && next <= 6 ? next : -1;
                    }
                }
                else if (ui->ControlsFocused && (wParam == VK_RETURN || wParam == VK_SPACE))
                {
                    SetOpen(window, *ui, true);
                    consume = true;
                }
                else
                {
                    if (wParam == VK_ESCAPE) { ui->NativeSelected = 6; ui->ControlsFocused = false; }
                }
                if (consume)
                {
                    rootKeys.fetch_or(RootKey(static_cast<int>(wParam)));
                    Repaint(window);
                    return 0;
                }
                Repaint(window);
            }
            if (ui->RootVisible && ui->Open)
            {
                if (message == WM_KEYDOWN)
                {
                    if (wParam == VK_ESCAPE || wParam == VK_BACK) SetOpen(window, *ui, false, static_cast<int>(wParam));
                    else if (wParam == VK_UP) ui->Selected = (ui->Selected + 6) % 7;
                    else if (wParam == VK_DOWN) ui->Selected = (ui->Selected + 1) % 7;
                    else if (wParam == VK_HOME) ui->Selected = 0;
                    else if (wParam == VK_END) ui->Selected = 6;
                    else if (wParam == VK_RETURN || wParam == VK_SPACE) Activate(window, *ui, static_cast<int>(wParam));
                    Repaint(window);
                    return 0;
                }
                if (message == WM_KEYUP || message == WM_CHAR || message == WM_SYSCHAR || message == WM_MOUSEWHEEL) return 0;
                if (message == WM_MOUSEMOVE || message == WM_LBUTTONDOWN || message == WM_LBUTTONUP || message == WM_LBUTTONDBLCLK)
                {
                    const POINT point = ClientPoint(lParam);
                    int hovered = -1;
                    for (int i = 0; i < 7; ++i) if (PtInRect(&ui->Choices[i], point)) hovered = i;
                    if (hovered >= 0) ui->Selected = hovered;
                    if (message == WM_LBUTTONDOWN) ui->Pressed = hovered >= 0;
                    if (message == WM_LBUTTONUP)
                    {
                        if (ui->Pressed && hovered >= 0) Activate(window, *ui);
                        ui->Pressed = false;
                    }
                    Repaint(window);
                    return 0;
                }
            }
            else if (ui->RootVisible && (message == WM_MOUSEMOVE || message == WM_LBUTTONDOWN || message == WM_LBUTTONUP || message == WM_LBUTTONDBLCLK))
            {
                const POINT point = ClientPoint(lParam);
                const bool hover = PtInRect(&ui->Entry, point) != 0;
                if (ui->Hover != hover) { ui->Hover = hover; Repaint(window); }
                if (hover)
                {
                    if (!ui->ControlsFocused) ui->Anchor = ui->NativeSelected;
                    ui->ControlsFocused = true;
                }
                else if (message != WM_LBUTTONUP || !ui->Pressed) ui->ControlsFocused = false;
                if (hover)
                {
                    if (message == WM_LBUTTONDOWN) ui->Pressed = true;
                    if (message == WM_LBUTTONUP && ui->Pressed)
                    {
                        callbacks.Log("Controls entry activated; original Quit click suppressed.");
                        SetOpen(window, *ui, true);
                    }
                    return 0;
                }
                if (message == WM_MOUSEMOVE || message == WM_LBUTTONDOWN)
                {
                    for (int i = 0; i < 7; ++i)
                    {
                        RECT row = ui->NativeRows[i];
                        row.left = ui->Entry.left;
                        row.right = ui->Entry.right;
                        if (i == 6) OffsetRect(&row, 0, ui->Shift);
                        if (PtInRect(&row, point) && (i != 0 || ui->StartEnabled)) ui->NativeSelected = i;
                    }
                }
                if (message == WM_LBUTTONUP && ui->Pressed)
                {
                    ui->Pressed = false;
                    return 0;
                }
                // The rendered Quit row is moved down. Translate its pointer events back
                // to the original layout so the host still receives the correct hit test.
                if (point.y >= ui->Entry.bottom)
                {
                    lParam = MAKELPARAM(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) - ui->Shift);
                }
            }
            if (pointerConverted)
            {
                POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
                ClientToScreen(window, &point);
                lParam = MAKELPARAM(point.x, point.y);
            }
            if (routingMessage)
            {
                routingMessage->message = originalMessage;
                routingMessage->wParam = wParam;
                routingMessage->lParam = lParam;
                queueConsumed = false;
                return 0;
            }
            return DefSubclassProc(window, originalMessage, wParam, lParam);
        }

        bool IsQueuedInput(UINT message)
        {
            return (message >= WM_KEYFIRST && message <= WM_KEYLAST) ||
                (message >= WM_MOUSEFIRST && message <= WM_MOUSELAST) || IsPointerMessage(message);
        }

        void RouteQueuedInput(MSG* message)
        {
            if (!message || message->hwnd != hostWindow) return;
            if (!IsQueuedInput(message->message)) return;
            // The native launcher inspects MSG before DispatchMessage. A subclass
            // alone runs after its Quit hit test; consume/translate input at retrieval.
            queueConsumed = true;
            routingMessage = message;
            WindowProcedure(message->hwnd, message->message, message->wParam, message->lParam, 0, 0);
            routingMessage = nullptr;
            if (queueConsumed) { message->message = WM_NULL; message->wParam = 0; message->lParam = 0; }
            routedMessage = *message;
        }

        bool TakeNavigation(MSG* message, HWND window, UINT minimum, UINT maximum, bool remove)
        {
            if (!message) return false;
            const auto pending = std::find_if(navigationMessages.begin(), navigationMessages.end(), [&](const MSG& item) {
                const bool sameWindow = !window || window == item.hwnd || IsChild(window, item.hwnd);
                const bool inRange = (!minimum && !maximum) || (item.message >= minimum && item.message <= maximum);
                return sameWindow && inRange;
            });
            if (pending == navigationMessages.end()) return false;
            *message = *pending;
            routedMessage = *message;
            if (remove) navigationMessages.erase(pending);
            return true;
        }

        BOOL WINAPI HookGetMessage(LPMSG message, HWND window, UINT minimum, UINT maximum)
        {
            if (TakeNavigation(message, window, minimum, maximum, true)) return TRUE;
            const BOOL result = originalGetMessage(message, window, minimum, maximum);
            if (result > 0) RouteQueuedInput(message);
            return result;
        }

        BOOL WINAPI HookPeekMessage(LPMSG message, HWND window, UINT minimum, UINT maximum, UINT remove)
        {
            if (TakeNavigation(message, window, minimum, maximum, (remove & PM_REMOVE) != 0)) return TRUE;
            const BOOL result = originalPeekMessage(message, window, minimum, maximum, remove);
            if (result && (remove & PM_REMOVE)) RouteQueuedInput(message);
            return result;
        }

        LRESULT WINAPI HookDispatch(const MSG* message)
        {
            const bool previous = dispatchRouted;
            dispatchRouted = message && message->hwnd == routedMessage.hwnd && message->message == routedMessage.message &&
                message->wParam == routedMessage.wParam && message->lParam == routedMessage.lParam;
            const LRESULT result = originalDispatch(message);
            dispatchRouted = previous;
            return result;
        }

        void Attach(HWND window)
        {
            if (!window || State(window) || (GetWindowLongPtrW(window, GWL_STYLE) & WS_CHILD)) return;
            wchar_t title[256]{};
            GetWindowTextW(window, title, 256);
            if (!wcsstr(title, L"Azimuth")) return;
            auto state = std::make_unique<UiState>();
            if (!SetPropW(window, property, state.get())) throw std::runtime_error("Cannot attach launcher state");
            if (!SetWindowSubclass(window, WindowProcedure, subclassId, 0))
            {
                RemovePropW(window, property);
                throw std::runtime_error("Cannot attach launcher input handler");
            }
            state.release();
            hostWindow = window;
            callbacks.Log("Launcher window attached; custom-rendered Controls entry will be inserted at presentation.");
        }

        HWND WINAPI HookCreate(DWORD extended, LPCWSTR name, LPCWSTR title, DWORD style, int x, int y, int width, int height, HWND parent, HMENU menu, HINSTANCE instance, LPVOID parameter)
        {
            HWND window = originalCreate(extended, name, title, style, x, y, width, height, parent, menu, instance, parameter);
            try { if (!parent) Attach(window); } catch (...) { callbacks.Log("Could not attach custom launcher controls."); }
            return window;
        }

        int WINAPI HookText(HDC dc, LPCWSTR text, int count, LPRECT rect, UINT format)
        {
            if (text && rect && !(format & (DT_CALCRECT | DT_MODIFYSTRING)))
            {
                if (frame.Dc != dc) { frame = {}; frame.Dc = dc; }
                const std::wstring_view value(text, count < 0 ? wcslen(text) : static_cast<size_t>(count));
                TEXTMETRICW metric{};
                GetTextMetricsW(dc, &metric);
                const int bottom = (format & DT_VCENTER) ? (rect->top + rect->bottom + metric.tmHeight) / 2 : rect->top + metric.tmHeight;
                frame.InkBottom = std::max(frame.InkBottom, bottom);
                if (GetTextColor(dc) != RGB(0, 0, 0))
                {
                    int row = -1;
                    if (value == L"Start") row = 0;
                    else if (value.substr(0, 7) == L"Display") row = 1;
                    else if (value.substr(0, 8) == L"Graphics") row = 2;
                    else if (value.substr(0, 10) == L"Frame rate") row = 3;
                    else if (value.substr(0, 5) == L"Audio") row = 4;
                    else if (value.substr(0, 5) == L"Game ") row = 5;
                    else if (value == L"Quit") row = 6;
                    if (row >= 0) frame.Rows[row] = *rect;
                    if (row == 0) frame.StartEnabled = GetTextColor(dc) != RGB(128, 128, 140);
                    if (value == L"Start") frame.Start = true;
                    if (value.substr(0, 7) == L"Display") frame.Display = true;
                    if (value.substr(0, 5) == L"Game ") { frame.Game = true; frame.GameRect = *rect; }
                    if (value == L"Quit")
                    {
                        frame.Quit = true;
                        frame.QuitRect = *rect;
                        GetObjectW(GetCurrentObject(dc, OBJ_FONT), sizeof(frame.Font), &frame.Font);
                    }
                    // Azimuth paints its focused native label white; the other labels
                    // are light grey. Preserve its selection and insert one extra stop.
                    if (GetTextColor(dc) == RGB(255, 255, 255))
                    {
                        if (value.substr(0, 5) == L"Game ") frame.NativeSelected = 5;
                        else if (value == L"Quit") frame.NativeSelected = 6;
                        else if (value == L"Start") frame.NativeSelected = 0;
                        else if (value.substr(0, 7) == L"Display") frame.NativeSelected = 1;
                        else if (value.substr(0, 8) == L"Graphics") frame.NativeSelected = 2;
                        else if (value.substr(0, 10) == L"Frame rate") frame.NativeSelected = 3;
                        else if (value.substr(0, 5) == L"Audio") frame.NativeSelected = 4;
                        else return originalText(dc, text, count, rect, format);
                    }
                    UiState* ui = hostWindow ? State(hostWindow) : nullptr;
                    if (row >= 0 && ui && ui->RootVisible && ui->ControlsFocused && GetTextColor(dc) == RGB(255, 255, 255))
                    {
                        const COLORREF previous = SetTextColor(dc, RGB(234, 228, 228));
                        const int result = originalText(dc, text, count, rect, format);
                        SetTextColor(dc, previous);
                        return result;
                    }
                }
            }
            return originalText(dc, text, count, rect, format);
        }

        void Fill(HDC dc, const RECT& rect, COLORREF color)
        {
            HBRUSH brush = CreateSolidBrush(color);
            FillRect(dc, &rect, brush);
            DeleteObject(brush);
        }

        void Text(HDC dc, RECT rect, const std::wstring& text, UINT align = DT_LEFT)
        {
            SetBkMode(dc, TRANSPARENT);
            SetTextColor(dc, RGB(234, 228, 228));
            DrawTextW(dc, text.c_str(), static_cast<int>(text.size()), &rect, align | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
        }

        void PaintPanel(HDC dc, UiState& ui)
        {
            RECT entire{0, 0, ui.Width, ui.Height};
            Fill(dc, entire, RGB(17, 18, 22));
            LOGFONTW font = ui.Font;
            font.lfHeight = -std::clamp(ui.Height / 24, 16, 44);
            HFONT handle = CreateFontIndirectW(&font);
            HGDIOBJ previous = SelectObject(dc, handle);
            const int rowHeight = std::max(24, ui.Height / 12);
            const int left = ui.Width / 12;
            const int right = ui.Width - left;
            RECT heading{left, ui.Height / 12, right, ui.Height / 6};
            Text(dc, heading, L"Gamepad Controls", DT_CENTER);
            static const wchar_t* labels[] = { L"Off", L"Preset 1", L"Preset 2", L"Preset 3", L"Edit preset config", L"Reload configs", L"Back" };
            for (int i = 0; i < 7; ++i)
            {
                RECT row{left, ui.Height / 5 + rowHeight * i, right, ui.Height / 5 + rowHeight * (i + 1)};
                ui.Choices[i] = row;
                if (ui.Selected == i) Fill(dc, row, RGB(54, 99, 167));
                row.left += rowHeight / 3;
                row.right -= rowHeight / 3;
                Text(dc, row, labels[i]);
                if (i <= 3 && callbacks.Active() == i) Text(dc, row, L"Selected", DT_RIGHT);
            }
            SelectObject(dc, previous);
            DeleteObject(handle);
        }

        void Enhance(HDC dc)
        {
            if (frame.Dc != dc) return;
            const Frame observed = frame;
            frame = {};
            UiState* ui = hostWindow ? State(hostWindow) : nullptr;
            if (!ui) return;
            auto hide = [&] {
                ui->RootVisible = false;
                ui->Open = false;
                ui->Pressed = false;
                ui->ControlsFocused = false;
                panelOpen.store(false);
                releaseKey.store(0);
                rootKeys.store(0);
            };
            const bool root = observed.Start && observed.Display && observed.Game && observed.Quit;
            if (!root) { hide(); return; }
            BITMAP bitmap{};
            if (!GetObjectW(GetCurrentObject(dc, OBJ_BITMAP), sizeof(bitmap), &bitmap) || bitmap.bmWidth <= 0 || bitmap.bmHeight <= 0) { hide(); return; }
            ui->Width = bitmap.bmWidth;
            ui->Height = bitmap.bmHeight;
            ui->Font = observed.Font;
            const int naturalStep = observed.QuitRect.top - observed.GameRect.top;
            const int available = ui->Height - observed.InkBottom - 4;
            const int minimum = std::max(18L, std::abs(observed.Font.lfHeight) + 8);
            const int step = std::min(naturalStep, available);
            if (step < minimum) { hide(); return; }
            ui->RootVisible = true;
            // Predictions are useful between key messages, but each rendered frame
            // is authoritative (native mouse/gamepad navigation can change focus).
            ui->NativeSelected = observed.NativeSelected;
            if (ui->ControlsFocused && observed.NativeSelected >= 0) ui->Anchor = observed.NativeSelected;
            ui->StartEnabled = observed.StartEnabled;
            std::copy(std::begin(observed.Rows), std::end(observed.Rows), std::begin(ui->NativeRows));
            ui->Shift = step;
            ui->Entry = { std::max(0L, observed.GameRect.left - step / 3), observed.QuitRect.top,
                ui->Width - std::max(0L, observed.GameRect.left - step / 3), observed.QuitRect.top + step };
            const int saved = SaveDC(dc);
            if (ui->Open) PaintPanel(dc, *ui);
            else
            {
                RECT lower{0, ui->Entry.top, ui->Width, ui->Height};
                ScrollDC(dc, 0, step, &lower, &lower, nullptr, nullptr);
                // ScrollDC copies native Quit's blue background as well as its text.
                // Paint both rows explicitly so their highlights can never merge.
                RECT quit = ui->Entry;
                OffsetRect(&quit, 0, step);
                Fill(dc, quit, !ui->ControlsFocused && observed.NativeSelected == 6 ? RGB(54, 99, 167) : RGB(17, 18, 22));
                Fill(dc, ui->Entry, RGB(17, 18, 22));
                if (ui->ControlsFocused) Fill(dc, ui->Entry, RGB(54, 99, 167));
                HFONT font = CreateFontIndirectW(&ui->Font);
                HGDIOBJ previous = SelectObject(dc, font);
                RECT label = ui->Entry;
                label.left += step / 3; label.right -= step / 3;
                Text(dc, label, L"Gamepad Controls ›");
                const int selected = callbacks.Active();
                Text(dc, label, selected ? L"Preset " + std::to_wstring(selected) : L"Off", DT_RIGHT);
                Text(dc, quit, L"Quit", DT_CENTER);
                SelectObject(dc, previous);
                DeleteObject(font);
            }
            RestoreDC(dc, saved);
            static bool logged = false;
            if (!logged) { callbacks.Log("Custom-rendered gamepad entry inserted into Azimuth launcher menu."); logged = true; }
        }

        BOOL WINAPI HookFlush()
        {
            try { if (frame.Dc) Enhance(frame.Dc); } catch (...) { frame = {}; }
            return originalFlush();
        }

        BOOL WINAPI HookAlphaBlend(HDC destination, int x, int y, int width, int height,
            HDC source, int sourceX, int sourceY, int sourceWidth, int sourceHeight, BLENDFUNCTION blend)
        {
            UiState* ui = hostWindow ? State(hostWindow) : nullptr;
            // The native highlight is a stretched premultiplied 1x1 blue pixel.
            // Skip it before drawing text, preserving the original background and
            // narrow highlight geometry instead of flood-filling an assumed edge.
            if (ui && ui->RootVisible && ui->ControlsFocused && sourceWidth == 1 && sourceHeight == 1 &&
                blend.BlendOp == AC_SRC_OVER && blend.SourceConstantAlpha == 255 && blend.AlphaFormat == AC_SRC_ALPHA &&
                GetPixel(source, sourceX, sourceY) == RGB(48, 96, 168)) return TRUE;
            return originalAlphaBlend(destination, x, y, width, height, source, sourceX, sourceY, sourceWidth, sourceHeight, blend);
        }

        BOOL WINAPI HookBlit(HDC destination, int x, int y, int width, int height, HDC source, int sourceX, int sourceY, DWORD operation)
        {
            try { if (source == frame.Dc && operation == SRCCOPY) Enhance(source); } catch (...) { frame = {}; }
            return originalBlit(destination, x, y, width, height, source, sourceX, sourceY, operation);
        }

        bool OwnsPointer(POINT* sourcePoint = nullptr)
        {
            UiState* ui = hostWindow ? State(hostWindow) : nullptr;
            if (!ui || !ui->RootVisible) return false;
            if (ui->Open) return true;
            POINT point{};
            if (!originalCursor(&point)) return false;
            if (sourcePoint) *sourcePoint = point;
            ScreenToClient(hostWindow, &point);
            const POINT pixel = ClientPoint(MAKELPARAM(point.x, point.y));
            return PtInRect(&ui->Entry, pixel) != 0;
        }

        bool SuppressHostInput()
        {
            if (panelOpen.load()) return true;
            if (!waitingForRelease.load()) return false;
            if (releaseKey.load() != 0) return true;
            for (int key : {VK_ESCAPE, VK_BACK, VK_RETURN, VK_SPACE, VK_F9, VK_LBUTTON})
                if (originalAsyncKey(key) & 0x8000) return true;
            waitingForRelease.store(false);
            return false;
        }

        BOOL WINAPI HookCursor(LPPOINT point)
        {
            if (!originalCursor(point)) return FALSE;
            UiState* ui = hostWindow ? State(hostWindow) : nullptr;
            if (!ui || !ui->RootVisible) return TRUE;
            POINT client = *point;
            ScreenToClient(hostWindow, &client);
            const POINT pixel = ClientPoint(MAKELPARAM(client.x, client.y));
            if (SuppressHostInput() || ui->ControlsFocused || PtInRect(&ui->Entry, pixel))
            {
                // Azimuth also polls cursor/button state outside the window callback.
                // Keep the original menu from hit-testing its old Quit rectangle.
                point->x = -10000; point->y = -10000;
            }
            else if (pixel.y >= ui->Entry.bottom)
            {
                point->y -= ui->Shift;
            }
            return TRUE;
        }

        bool IsMouseKey(int key) { return key == VK_LBUTTON || key == VK_RBUTTON || key == VK_MBUTTON || key == VK_XBUTTON1 || key == VK_XBUTTON2; }

        SHORT WINAPI HookAsyncKey(int key)
        {
            if ((rootKeys.load() & RootKey(key)) || SuppressHostInput() || (IsMouseKey(key) && OwnsPointer())) return 0;
            return originalAsyncKey(key);
        }

        SHORT WINAPI HookKey(int key)
        {
            if ((rootKeys.load() & RootKey(key)) || SuppressHostInput() || (IsMouseKey(key) && OwnsPointer())) return 0;
            return originalKey(key);
        }
    }

    bool InstallLauncherMenu(ImportHook hook, MenuCallbacks value)
    {
        callbacks = value;
        return hook("CreateWindowExW", reinterpret_cast<void*>(&HookCreate), reinterpret_cast<void**>(&originalCreate)) &&
            hook("DrawTextW", reinterpret_cast<void*>(&HookText), reinterpret_cast<void**>(&originalText)) &&
            hook("BitBlt", reinterpret_cast<void*>(&HookBlit), reinterpret_cast<void**>(&originalBlit)) &&
            hook("GdiFlush", reinterpret_cast<void*>(&HookFlush), reinterpret_cast<void**>(&originalFlush)) &&
            hook("GdiAlphaBlend", reinterpret_cast<void*>(&HookAlphaBlend), reinterpret_cast<void**>(&originalAlphaBlend)) &&
            hook("GetCursorPos", reinterpret_cast<void*>(&HookCursor), reinterpret_cast<void**>(&originalCursor)) &&
            hook("GetAsyncKeyState", reinterpret_cast<void*>(&HookAsyncKey), reinterpret_cast<void**>(&originalAsyncKey)) &&
            hook("GetKeyState", reinterpret_cast<void*>(&HookKey), reinterpret_cast<void**>(&originalKey)) &&
            hook("GetMessageW", reinterpret_cast<void*>(&HookGetMessage), reinterpret_cast<void**>(&originalGetMessage)) &&
            hook("PeekMessageW", reinterpret_cast<void*>(&HookPeekMessage), reinterpret_cast<void**>(&originalPeekMessage)) &&
            hook("DispatchMessageW", reinterpret_cast<void*>(&HookDispatch), reinterpret_cast<void**>(&originalDispatch));
    }

    bool IsLauncherInputCaptured() noexcept { return rootKeys.load() != 0 || SuppressHostInput(); }
}
