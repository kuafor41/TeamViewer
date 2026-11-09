#include <windows.h>
#include <shellapi.h>

#include "deskawake/motion.hpp"
#include "deskawake/scheduler.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <random>
#include <string>
#include <vector>

namespace {

constexpr wchar_t kWindowClass[] = L"DeskAwakeMainWindow";
constexpr wchar_t kWindowTitle[] = L"DeskAwake";
constexpr UINT kTrayMessage = WM_APP + 10;
constexpr UINT kUserActivityMessage = WM_APP + 11;
constexpr UINT_PTR kTimerId = 1;
constexpr UINT kTimerPeriodMs = 20;
constexpr ULONGLONG kQuietPeriodMs = 1500;

constexpr int kIdStart = 1001;
constexpr int kIdStop = 1002;
constexpr int kIdPointer = 1003;
constexpr int kIdPower = 1004;
constexpr int kIdBattery = 1005;
constexpr int kIdStatus = 1006;
constexpr int kIdTrayShow = 1101;
constexpr int kIdTrayToggle = 1102;
constexpr int kIdTrayExit = 1103;
constexpr int kHotkeyId = 1;

HWND g_window{};
HWND g_startButton{};
HWND g_stopButton{};
HWND g_pointerCheck{};
HWND g_powerCheck{};
HWND g_batteryCheck{};
HWND g_statusLabel{};

HHOOK g_mouseHook{};
HHOOK g_keyboardHook{};
bool g_trayAdded{};
bool g_running{};
bool g_powerRequestActive{};
bool g_pointerMotion{true};
bool g_keepAwake{true};
bool g_pauseOnBattery{true};

ULONGLONG g_lastPhysicalInput{};
ULONGLONG g_nextActionAt{};
ULONGLONG g_nextPathStepAt{};
ULONGLONG g_lastStatusUpdate{};
std::vector<deskawake::Point> g_activePath;
std::size_t g_activePathIndex{};
deskawake::Point g_previousPoint{};

deskawake::Scheduler g_scheduler{};
std::mt19937 g_random{std::random_device{}()};

void setStatus(const std::wstring& text) {
    if (g_statusLabel != nullptr) {
        SetWindowTextW(g_statusLabel, text.c_str());
    }
}

void releasePowerRequest() {
    if (g_powerRequestActive) {
        SetThreadExecutionState(ES_CONTINUOUS);
        g_powerRequestActive = false;
    }
}

bool applyPowerRequest() {
    if (!g_keepAwake) {
        releasePowerRequest();
        return true;
    }

    const EXECUTION_STATE result = SetThreadExecutionState(
        ES_CONTINUOUS | ES_SYSTEM_REQUIRED | ES_DISPLAY_REQUIRED
    );
    g_powerRequestActive = result != 0;
    return g_powerRequestActive;
}

void abortPath() {
    g_activePath.clear();
    g_activePathIndex = 0;
    g_nextPathStepAt = 0;
}

void stopSession() {
    g_running = false;
    g_nextActionAt = 0;
    abortPath();
    releasePowerRequest();
    setStatus(L"Stopped");
    if (g_startButton != nullptr) EnableWindow(g_startButton, TRUE);
    if (g_stopButton != nullptr) EnableWindow(g_stopButton, FALSE);
}

void startSession() {
    if (g_running) return;

    g_pointerMotion = SendMessageW(g_pointerCheck, BM_GETCHECK, 0, 0) == BST_CHECKED;
    g_keepAwake = SendMessageW(g_powerCheck, BM_GETCHECK, 0, 0) == BST_CHECKED;
    g_pauseOnBattery = SendMessageW(g_batteryCheck, BM_GETCHECK, 0, 0) == BST_CHECKED;

    g_running = true;
    g_lastPhysicalInput = GetTickCount64();
    g_nextActionAt = 0;
    abortPath();

    if (!applyPowerRequest() && g_keepAwake) {
        setStatus(L"Running; Windows power request could not be applied");
    } else {
        setStatus(L"Running; waiting for an idle input period");
    }

    EnableWindow(g_startButton, FALSE);
    EnableWindow(g_stopButton, TRUE);
}

void toggleSession() {
    if (g_running) stopSession();
    else startSession();
}

INPUT makeAbsoluteMove(deskawake::Point point) {
    const int left = GetSystemMetrics(SM_XVIRTUALSCREEN);
    const int top = GetSystemMetrics(SM_YVIRTUALSCREEN);
    const int width = std::max(1, GetSystemMetrics(SM_CXVIRTUALSCREEN));
    const int height = std::max(1, GetSystemMetrics(SM_CYVIRTUALSCREEN));
    const double xRange = static_cast<double>(std::max(1, width - 1));
    const double yRange = static_cast<double>(std::max(1, height - 1));

    const int pixelX = std::clamp(static_cast<int>(std::lround(point.x)), left, left + width - 1);
    const int pixelY = std::clamp(static_cast<int>(std::lround(point.y)), top, top + height - 1);

    INPUT input{};
    input.type = INPUT_MOUSE;
    input.mi.dx = static_cast<LONG>(std::lround(
        (static_cast<double>(pixelX - left) * 65535.0) / xRange
    ));
    input.mi.dy = static_cast<LONG>(std::lround(
        (static_cast<double>(pixelY - top) * 65535.0) / yRange
    ));
    input.mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_VIRTUALDESK;
    return input;
}

bool sendAbsoluteMove(deskawake::Point point) {
    INPUT input = makeAbsoluteMove(point);
    return SendInput(1, &input, sizeof(INPUT)) == 1;
}

bool sendMicroMove() {
    POINT current{};
    if (!GetCursorPos(&current)) return false;

    const int left = GetSystemMetrics(SM_XVIRTUALSCREEN);
    const int top = GetSystemMetrics(SM_YVIRTUALSCREEN);
    const int right = left + std::max(1, GetSystemMetrics(SM_CXVIRTUALSCREEN)) - 1;
    const int bottom = top + std::max(1, GetSystemMetrics(SM_CYVIRTUALSCREEN)) - 1;

    POINT offset = current;
    if (current.x < right) ++offset.x;
    else if (current.x > left) --offset.x;
    else if (current.y < bottom) ++offset.y;
    else if (current.y > top) --offset.y;
    else return false;

    INPUT inputs[2]{};
    inputs[0] = makeAbsoluteMove({static_cast<double>(offset.x), static_cast<double>(offset.y)});
    inputs[1] = makeAbsoluteMove({static_cast<double>(current.x), static_cast<double>(current.y)});
    return SendInput(2, inputs, sizeof(INPUT)) == 2;
}

bool beginHumanPath() {
    POINT current{};
    if (!GetCursorPos(&current)) return false;

    const int left = GetSystemMetrics(SM_XVIRTUALSCREEN);
    const int top = GetSystemMetrics(SM_YVIRTUALSCREEN);
    const int width = std::max(1, GetSystemMetrics(SM_CXVIRTUALSCREEN));
    const int height = std::max(1, GetSystemMetrics(SM_CYVIRTUALSCREEN));
    const int marginX = std::min(12, width / 4);
    const int marginY = std::min(12, height / 4);

    std::uniform_real_distribution<double> angleDistribution(0.0, 6.283185307179586);
    std::uniform_real_distribution<double> distanceDistribution(70.0, 210.0);
    const double angle = angleDistribution(g_random);
    const double distance = distanceDistribution(g_random);

    const int minX = left + marginX;
    const int maxX = left + width - 1 - marginX;
    const int minY = top + marginY;
    const int maxY = top + height - 1 - marginY;

    const deskawake::Point start{static_cast<double>(current.x), static_cast<double>(current.y)};
    const deskawake::Point target{
        static_cast<double>(std::clamp(
            static_cast<int>(std::lround(current.x + std::cos(angle) * distance)), minX, maxX)),
        static_cast<double>(std::clamp(
            static_cast<int>(std::lround(current.y + std::sin(angle) * distance)), minY, maxY))
    };

    if (std::abs(target.x - start.x) < 1.0 && std::abs(target.y - start.y) < 1.0) return false;

    deskawake::TrajectoryOptions options{};
    options.sample_count = 28;
    options.overshoot_fraction = 0.035;
    options.tremor_pixels = 0.35;
    options.seed = g_random();

    g_activePath = deskawake::generateHumanTrajectory(start, target, options);
    g_activePathIndex = 1;
    g_previousPoint = start;
    g_nextPathStepAt = GetTickCount64();
    return !g_activePath.empty();
}

void finishPath() {
    abortPath();
    g_nextActionAt = GetTickCount64() +
        static_cast<ULONGLONG>(g_scheduler.nextDelaySeconds()) * 1000ULL;
    setStatus(L"Running; pointer route completed");
}

void updateRunningState() {
    if (!g_running) return;

    const ULONGLONG now = GetTickCount64();
    SYSTEM_POWER_STATUS powerStatus{};
    const bool onBattery = GetSystemPowerStatus(&powerStatus) && powerStatus.ACLineStatus == 0;

    if (g_pauseOnBattery && onBattery) {
        abortPath();
        g_nextActionAt = 0;
        releasePowerRequest();
        setStatus(L"Paused; battery power detected");
        return;
    }

    if (g_keepAwake && !g_powerRequestActive) applyPowerRequest();
    else if (!g_keepAwake) releasePowerRequest();

    if (now - g_lastPhysicalInput < kQuietPeriodMs) {
        abortPath();
        g_nextActionAt = 0;
        setStatus(L"Paused; physical keyboard or mouse activity detected");
        return;
    }

    if (!g_activePath.empty()) {
        if (now < g_nextPathStepAt) return;

        if (g_activePathIndex >= g_activePath.size()) {
            finishPath();
            return;
        }

        const deskawake::Point point = g_activePath[g_activePathIndex];
        if (!sendAbsoluteMove(point)) {
            abortPath();
            g_nextActionAt = now +
                static_cast<ULONGLONG>(g_scheduler.nextDelaySeconds()) * 1000ULL;
            setStatus(L"Pointer route stopped; Windows rejected an input event");
            return;
        }

        g_previousPoint = point;
        ++g_activePathIndex;
        g_nextPathStepAt = now + 12;
        return;
    }

    if (g_nextActionAt == 0) {
        g_nextActionAt = now +
            static_cast<ULONGLONG>(g_scheduler.nextDelaySeconds()) * 1000ULL;
    }

    if (now >= g_nextActionAt) {
        if (!g_pointerMotion) {
            g_nextActionAt = now +
                static_cast<ULONGLONG>(g_scheduler.nextDelaySeconds()) * 1000ULL;
            setStatus(L"Running; Windows power protection only");
            return;
        }

        const deskawake::ActionKind action = g_scheduler.nextAction();
        if (action == deskawake::ActionKind::Micro) {
            const bool moved = sendMicroMove();
            g_nextActionAt = now +
                static_cast<ULONGLONG>(g_scheduler.nextDelaySeconds()) * 1000ULL;
            setStatus(moved ? L"Running; micro movement completed"
                            : L"Running; micro movement unavailable at this screen position");
        } else if (!beginHumanPath()) {
            g_nextActionAt = now +
                static_cast<ULONGLONG>(g_scheduler.nextDelaySeconds()) * 1000ULL;
            setStatus(L"Running; could not create a pointer route");
        } else {
            setStatus(L"Running; pointer route in progress");
        }
        return;
    }

    if (now - g_lastStatusUpdate >= 500) {
        g_lastStatusUpdate = now;
        const ULONGLONG remainingMs = g_nextActionAt > now ? g_nextActionAt - now : 0;
        const unsigned long seconds = static_cast<unsigned long>((remainingMs + 999) / 1000);
        wchar_t buffer[160]{};
        swprintf_s(buffer, L"Running; next scheduled action in %lu seconds", seconds);
        setStatus(buffer);
    }
}

void addTrayIcon() {
    if (g_trayAdded || g_window == nullptr) return;

    NOTIFYICONDATAW data{};
    data.cbSize = sizeof(data);
    data.hWnd = g_window;
    data.uID = 1;
    data.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    data.uCallbackMessage = kTrayMessage;
    data.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wcscpy_s(data.szTip, L"DeskAwake");
    g_trayAdded = Shell_NotifyIconW(NIM_ADD, &data) == TRUE;
}

void removeTrayIcon() {
    if (!g_trayAdded || g_window == nullptr) return;
    NOTIFYICONDATAW data{};
    data.cbSize = sizeof(data);
    data.hWnd = g_window;
    data.uID = 1;
    Shell_NotifyIconW(NIM_DELETE, &data);
    g_trayAdded = false;
}

void showTrayMenu() {
    HMENU menu = CreatePopupMenu();
    if (menu == nullptr) return;

    AppendMenuW(menu, MF_STRING, kIdTrayShow, L"Show DeskAwake");
    AppendMenuW(menu, MF_STRING, kIdTrayToggle, g_running ? L"Stop" : L"Start");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kIdTrayExit, L"Exit");

    POINT point{};
    GetCursorPos(&point);
    SetForegroundWindow(g_window);
    TrackPopupMenu(menu, TPM_RIGHTBUTTON, point.x, point.y, 0, g_window, nullptr);
    DestroyMenu(menu);
}

void createControls(HWND window) {
    const HFONT font = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
    g_startButton = CreateWindowW(L"BUTTON", L"Start",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
        24, 24, 112, 32, window, reinterpret_cast<HMENU>(kIdStart), nullptr, nullptr);
    g_stopButton = CreateWindowW(L"BUTTON", L"Stop",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
        148, 24, 112, 32, window, reinterpret_cast<HMENU>(kIdStop), nullptr, nullptr);
    g_pointerCheck = CreateWindowW(L"BUTTON", L"Enable pointer movement",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
        24, 82, 300, 24, window, reinterpret_cast<HMENU>(kIdPointer), nullptr, nullptr);
    g_powerCheck = CreateWindowW(L"BUTTON", L"Keep display and system awake",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
        24, 112, 320, 24, window, reinterpret_cast<HMENU>(kIdPower), nullptr, nullptr);
    g_batteryCheck = CreateWindowW(L"BUTTON", L"Pause when running on battery",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
        24, 142, 320, 24, window, reinterpret_cast<HMENU>(kIdBattery), nullptr, nullptr);
    g_statusLabel = CreateWindowW(L"STATIC", L"Stopped",
        WS_CHILD | WS_VISIBLE | SS_LEFT,
        24, 190, 410, 36, window, reinterpret_cast<HMENU>(kIdStatus), nullptr, nullptr);

    for (HWND control : {g_startButton, g_stopButton, g_pointerCheck,
                         g_powerCheck, g_batteryCheck, g_statusLabel}) {
        if (control != nullptr) {
            SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        }
    }

    SendMessageW(g_pointerCheck, BM_SETCHECK, BST_CHECKED, 0);
    SendMessageW(g_powerCheck, BM_SETCHECK, BST_CHECKED, 0);
    SendMessageW(g_batteryCheck, BM_SETCHECK, BST_CHECKED, 0);
    EnableWindow(g_stopButton, FALSE);
}

LRESULT CALLBACK mouseHookProc(int code, WPARAM wParam, LPARAM lParam) {
    if (code == HC_ACTION && g_window != nullptr) {
        const auto* data = reinterpret_cast<const MSLLHOOKSTRUCT*>(lParam);
        if ((data->flags & LLMHF_INJECTED) == 0) {
            PostMessageW(g_window, kUserActivityMessage, 0, 0);
        }
    }
    return CallNextHookEx(g_mouseHook, code, wParam, lParam);
}

LRESULT CALLBACK keyboardHookProc(int code, WPARAM wParam, LPARAM lParam) {
    if (code == HC_ACTION && g_window != nullptr) {
        const auto* data = reinterpret_cast<const KBDLLHOOKSTRUCT*>(lParam);
        if ((data->flags & LLKHF_INJECTED) == 0) {
            PostMessageW(g_window, kUserActivityMessage, 0, 0);
        }
    }
    return CallNextHookEx(g_keyboardHook, code, wParam, lParam);
}

bool installHooks() {
    const HINSTANCE instance = GetModuleHandleW(nullptr);
    g_mouseHook = SetWindowsHookExW(WH_MOUSE_LL, mouseHookProc, instance, 0);
    g_keyboardHook = SetWindowsHookExW(WH_KEYBOARD_LL, keyboardHookProc, instance, 0);
    return g_mouseHook != nullptr && g_keyboardHook != nullptr;
}

void removeHooks() {
    if (g_mouseHook != nullptr) {
        UnhookWindowsHookEx(g_mouseHook);
        g_mouseHook = nullptr;
    }
    if (g_keyboardHook != nullptr) {
        UnhookWindowsHookEx(g_keyboardHook);
        g_keyboardHook = nullptr;
    }
}

LRESULT CALLBACK windowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_CREATE:
        createControls(window);
        g_lastPhysicalInput = GetTickCount64();
        SetTimer(window, kTimerId, kTimerPeriodMs, nullptr);
        RegisterHotKey(window, kHotkeyId, MOD_CONTROL | MOD_SHIFT | MOD_NOREPEAT, 'J');
        if (!installHooks()) setStatus(L"Input monitoring could not be initialized");
        return 0;

    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case kIdStart:
            startSession();
            return 0;
        case kIdStop:
            stopSession();
            return 0;
        case kIdTrayShow:
            ShowWindow(window, SW_SHOW);
            SetForegroundWindow(window);
            return 0;
        case kIdTrayToggle:
            toggleSession();
            return 0;
        case kIdTrayExit:
            removeTrayIcon();
            DestroyWindow(window);
            return 0;
        default:
            break;
        }
        break;

    case WM_HOTKEY:
        if (wParam == kHotkeyId) {
            toggleSession();
            return 0;
        }
        break;

    case kUserActivityMessage:
        g_lastPhysicalInput = GetTickCount64();
        g_nextActionAt = 0;
        abortPath();
        if (g_running) setStatus(L"Paused; waiting for physical input to stop");
        return 0;

    case WM_TIMER:
        if (wParam == kTimerId) {
            updateRunningState();
            return 0;
        }
        break;

    case kTrayMessage:
        if (lParam == WM_RBUTTONUP || lParam == WM_CONTEXTMENU) {
            showTrayMenu();
            return 0;
        }
        if (lParam == WM_LBUTTONDBLCLK) {
            ShowWindow(window, SW_SHOW);
            SetForegroundWindow(window);
            return 0;
        }
        break;

    case WM_SYSCOMMAND:
        if ((wParam & 0xFFF0) == SC_MINIMIZE) {
            addTrayIcon();
            ShowWindow(window, SW_HIDE);
            return 0;
        }
        break;

    case WM_CLOSE:
        addTrayIcon();
        ShowWindow(window, SW_HIDE);
        return 0;

    case WM_DESTROY:
        stopSession();
        removeHooks();
        removeTrayIcon();
        UnregisterHotKey(window, kHotkeyId);
        KillTimer(window, kTimerId);
        PostQuitMessage(0);
        return 0;

    default:
        break;
    }

    return DefWindowProcW(window, message, wParam, lParam);
}

} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.lpfnWndProc = windowProc;
    windowClass.hInstance = instance;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    windowClass.hIconSm = windowClass.hIcon;
    windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    windowClass.lpszClassName = kWindowClass;

    if (RegisterClassExW(&windowClass) == 0) return static_cast<int>(GetLastError());

    g_window = CreateWindowExW(
        0, kWindowClass, kWindowTitle,
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, 480, 285,
        nullptr, nullptr, instance, nullptr
    );
    if (g_window == nullptr) return static_cast<int>(GetLastError());

    ShowWindow(g_window, showCommand);
    UpdateWindow(g_window);

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return static_cast<int>(message.wParam);
}
