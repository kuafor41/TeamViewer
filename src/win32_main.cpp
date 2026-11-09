#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <commctrl.h>
#include <winreg.h>
#include <shobjidl.h>
#include <propkey.h>
#include <propvarutil.h>
#include <psapi.h>

#include "deskawake/motion.hpp"
#include "deskawake/scheduler.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cwchar>
#include <iterator>
#include <utility>
#include <limits>
#include <sstream>
#include <iomanip>
#include <random>
#include <string>
#include <vector>

namespace {

constexpr wchar_t kWindowClass[] = L"AcrobatMainWindow";
constexpr wchar_t kWindowTitle[] = L"Acrobat";
constexpr wchar_t kRunKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr wchar_t kRunValue[] = L"Acrobat";
constexpr UINT kTrayMessage = WM_APP + 10;
constexpr UINT kUserActivityMessage = WM_APP + 11;
constexpr UINT_PTR kEngineTimerId = 1;
constexpr UINT_PTR kScheduleTimerId = 2;
constexpr UINT kEngineTimerMs = 20;
constexpr ULONGLONG kQuietPeriodMs = 1500;
constexpr int kHotkeyId = 1;
constexpr UINT_PTR kDiagnosticsTimerId = 3;
constexpr UINT kDiagnosticsRefreshMs = 1000;
constexpr wchar_t kAppUserModelId[] = L"Acrobat.Desktop";
struct HotkeyDefinition { const wchar_t* label; UINT modifiers; UINT virtualKey; };
constexpr std::array<HotkeyDefinition, 4> kHotkeyDefinitions{{
    {L"Ctrl + Shift + J", MOD_CONTROL | MOD_SHIFT | MOD_NOREPEAT, 'J'},
    {L"Ctrl + Alt + J", MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, 'J'},
    {L"Ctrl + Shift + F10", MOD_CONTROL | MOD_SHIFT | MOD_NOREPEAT, VK_F10},
    {L"Ctrl + Alt + F12", MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, VK_F12}
}};

enum ControlId : int {
    IdTabs = 2000,
    IdStart = 2001,
    IdStop = 2002,
    IdPointer = 2003,
    IdPower = 2004,
    IdBattery = 2005,
    IdDistance = 2006,
    IdAngle = 2007,
    IdCountdown = 2008,
    IdAutoExit = 2009,
    IdSchedule = 2010,
    IdDayMonday = 2020,
    IdStartTime = 2030,
    IdEndTime = 2031,
    IdStartup = 2032,
    IdLaunchTarget = 2033,
    IdTargetPath = 2034,
    IdTargetInterval = 2035,
    IdHotkey = 2036,
    IdStatus = 2040,
    IdTrayShow = 2050,
    IdTrayToggle = 2051,
    IdTrayExit = 2052
};

struct AppSettings {
    bool pointerEnabled{true};
    bool keepAwake{true};
    bool pauseOnBattery{true};
    int distancePixels{120};
    int angleDegrees{-1};
    int countdownMinutes{};
    bool autoExit{};
    int hotkeyPreset{};
    bool scheduleEnabled{};
    unsigned int daysMask{0x3Eu};
    std::wstring startTime{L"09:00"};
    std::wstring endTime{L"18:00"};
    bool startupEnabled{};
    bool launchTarget{};
    std::wstring targetPath;
    int targetIntervalMinutes{};
};

HWND g_window{};
HWND g_tabs{};
HWND g_startButton{};
HWND g_stopButton{};
HWND g_pointerCheck{};
HWND g_powerCheck{};
HWND g_batteryCheck{};
HWND g_distanceEdit{};
HWND g_angleEdit{};
HWND g_countdownEdit{};
HWND g_autoExitCheck{};
HWND g_scheduleCheck{};
HWND g_dayChecks[7]{};
HWND g_startTimeEdit{};
HWND g_endTimeEdit{};
HWND g_startupCheck{};
HWND g_launchTargetCheck{};
HWND g_targetPathEdit{};
HWND g_targetIntervalCombo{};
HWND g_hotkeyCombo{};
HWND g_statusLabel{};
HWND g_diagCpuLabel{};
HWND g_diagMemoryLabel{};
HWND g_diagBatteryLabel{};
HWND g_diagInputLabel{};
HWND g_diagSessionLabel{};

std::vector<HWND> g_generalControls;
std::vector<HWND> g_scheduleControls;
std::vector<HWND> g_diagnosticsControls;
HFONT g_regularFont{};
HFONT g_titleFont{};
HFONT g_statusFont{};
HBRUSH g_backgroundBrush{};

HHOOK g_mouseHook{};
HHOOK g_keyboardHook{};
bool g_inputMonitoringReady{};
bool g_trayAdded{};
bool g_running{};
bool g_powerRequestActive{};
bool g_powerRequestAttempted{};
bool g_startedBySchedule{};
bool g_scheduleStateKnown{};
bool g_lastScheduleActive{};
bool g_hotkeyRegistered{};
bool g_comInitialized{};
ULONGLONG g_lastPhysicalInput{};
ULONGLONG g_nextActionAt{};
ULONGLONG g_countdownDeadline{};
ULONGLONG g_nextPathStepAt{};
ULONGLONG g_lastStatusUpdate{};
ULONGLONG g_nextTargetLaunchAt{};
ULONGLONG g_sessionStartedAt{};
ULONGLONG g_lastDiagnosticsUpdate{};
ULONGLONG g_lastCpuSampleTick{};
ULONGLONG g_physicalInputCount{};
ULONGLONG g_sessionCount{};
ULONGLONG g_lastKernelTime{};
ULONGLONG g_lastUserTime{};
double g_cpuUsagePercent{};
SIZE_T g_workingSetBytes{};
std::vector<deskawake::Point> g_activePath;
std::size_t g_activePathIndex{};
AppSettings g_settings{};
deskawake::Scheduler g_scheduler{};
std::mt19937 g_random{std::random_device{}()};
std::wstring g_iniPath;

void updateStatusParts();
void updateDiagnostics();
void setStatus(const std::wstring& value) {
    if (g_statusLabel != nullptr) {
        constexpr std::size_t kVisibleStatusChars = 32;
        std::wstring visible = value;
        if (visible.size() > kVisibleStatusChars) {
            visible.resize(kVisibleStatusChars - 3);
            visible += L"...";
        }
        SendMessageW(g_statusLabel, SB_SETTEXTW, 0, reinterpret_cast<LPARAM>(visible.c_str()));
        SendMessageW(g_statusLabel, SB_SETTIPTEXTW, 0, reinterpret_cast<LPARAM>(value.c_str()));
    }
    if (g_window != nullptr) {
        std::wstring title = L"Acrobat";
        if (value.find(L"Paused") != std::wstring::npos) title += L" — Paused";
        else if (g_running || value.find(L"Session active") != std::wstring::npos ||
                 value.find(L"Started by weekly") != std::wstring::npos) title += L" — Running";
        SetWindowTextW(g_window, title.c_str());
    }
    updateStatusParts();
}

void updateStatusParts() {
    if (g_statusLabel == nullptr) return;
    const ULONGLONG now = GetTickCount64();
    wchar_t sessionText[96]{};
    wchar_t nextText[64]{};
    wchar_t powerText[64]{};

    if (g_running && g_countdownDeadline != 0) {
        const ULONGLONG remaining = g_countdownDeadline > now ? g_countdownDeadline - now : 0;
        const ULONGLONG seconds = (remaining + 999) / 1000;
        swprintf_s(sessionText, L"Left %02llu:%02llu:%02llu", seconds / 3600,
            (seconds / 60) % 60, seconds % 60);
    } else if (g_running && g_sessionStartedAt != 0) {
        const ULONGLONG seconds = (now - g_sessionStartedAt) / 1000;
        swprintf_s(sessionText, L"Elapsed %02llu:%02llu:%02llu", seconds / 3600,
            (seconds / 60) % 60, seconds % 60);
    } else wcscpy_s(sessionText, L"Session —");

    if (!g_running) wcscpy_s(nextText, L"Next —");
    else if (!g_settings.pointerEnabled) wcscpy_s(nextText, L"Power only");
    else {
        const ULONGLONG remaining = g_nextActionAt > now ? g_nextActionAt - now : 0;
        swprintf_s(nextText, L"Next %02llu s", (remaining + 999) / 1000);
    }

    SYSTEM_POWER_STATUS power{};
    if (GetSystemPowerStatus(&power)) {
        if (power.ACLineStatus == 1) wcscpy_s(powerText, L"AC");
        else if (power.ACLineStatus == 0 && power.BatteryLifePercent != 255)
            swprintf_s(powerText, L"Bat %u%%", static_cast<unsigned int>(power.BatteryLifePercent));
        else if (power.ACLineStatus == 0) wcscpy_s(powerText, L"Battery");
        else wcscpy_s(powerText, L"Power ?");
    } else wcscpy_s(powerText, L"Power ?");

    SendMessageW(g_statusLabel, SB_SETTEXTW, 1, reinterpret_cast<LPARAM>(sessionText));
    SendMessageW(g_statusLabel, SB_SETTEXTW, 2, reinterpret_cast<LPARAM>(nextText));
    SendMessageW(g_statusLabel, SB_SETTEXTW, 3, reinterpret_cast<LPARAM>(powerText));
}

bool registerGlobalHotkey() {
    if (g_window == nullptr) return false;
    if (g_hotkeyRegistered) {
        UnregisterHotKey(g_window, kHotkeyId);
        g_hotkeyRegistered = false;
    }
    const int preset = std::clamp(g_settings.hotkeyPreset, 0,
        static_cast<int>(kHotkeyDefinitions.size()) - 1);
    const auto& def = kHotkeyDefinitions[static_cast<std::size_t>(preset)];
    g_hotkeyRegistered = RegisterHotKey(g_window, kHotkeyId, def.modifiers, def.virtualKey) == TRUE;
    return g_hotkeyRegistered;
}

std::wstring executableDirectory() {
    std::array<wchar_t, MAX_PATH> buffer{};
    const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length == 0 || length >= buffer.size()) return L".\\";
    std::wstring path(buffer.data(), length);
    const std::size_t separator = path.find_last_of(L"\\/");
    if (separator == std::wstring::npos) return L".\\";
    path.resize(separator + 1);
    return path;
}

bool fileExists(const std::wstring& path) {
    const DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

std::wstring settingsPath() {
    const std::wstring exeDir = executableDirectory();
    if (fileExists(exeDir + L"portable.flag")) return exeDir + L"Acrobat.ini";

    std::array<wchar_t, MAX_PATH> appData{};
    if (SHGetFolderPathW(nullptr, CSIDL_APPDATA | CSIDL_FLAG_CREATE, nullptr, SHGFP_TYPE_CURRENT,
                         appData.data()) == S_OK) {
        const std::wstring directory = std::wstring(appData.data()) + L"\\Acrobat";
        CreateDirectoryW(directory.c_str(), nullptr);
        return directory + L"\\settings.ini";
    }
    return exeDir + L"Acrobat.ini";
}

int readInt(const wchar_t* section, const wchar_t* key, int fallback) {
    return GetPrivateProfileIntW(section, key, fallback, g_iniPath.c_str());
}

std::wstring readString(const wchar_t* section, const wchar_t* key, const wchar_t* fallback) {
    std::array<wchar_t, 1024> buffer{};
    GetPrivateProfileStringW(section, key, fallback, buffer.data(),
                             static_cast<DWORD>(buffer.size()), g_iniPath.c_str());
    return buffer.data();
}

void writeInt(const wchar_t* section, const wchar_t* key, int value) {
    wchar_t buffer[32]{};
    swprintf_s(buffer, L"%d", value);
    WritePrivateProfileStringW(section, key, buffer, g_iniPath.c_str());
}

void loadSettings() {
    g_iniPath = settingsPath();
    g_settings.pointerEnabled = readInt(L"General", L"PointerEnabled", 1) != 0;
    g_settings.keepAwake = readInt(L"General", L"KeepAwake", 1) != 0;
    g_settings.pauseOnBattery = readInt(L"General", L"PauseOnBattery", 1) != 0;
    g_settings.distancePixels = std::clamp(readInt(L"Motion", L"DistancePixels", 120), 1, 1000);
    g_settings.angleDegrees = std::clamp(readInt(L"Motion", L"AngleDegrees", -1), -1, 359);
    g_settings.countdownMinutes = std::clamp(readInt(L"Duration", L"CountdownMinutes", 0), 0, 1440);
    g_settings.autoExit = readInt(L"Duration", L"AutoExit", 0) != 0;
    g_settings.hotkeyPreset = std::clamp(readInt(L"General", L"HotkeyPreset", 0), 0, 3);
    g_settings.scheduleEnabled = readInt(L"Schedule", L"Enabled", 0) != 0;
    g_settings.daysMask = static_cast<unsigned int>(readInt(L"Schedule", L"DaysMask", 0x3E)) & 0x7F;
    g_settings.startTime = readString(L"Schedule", L"Start", L"09:00");
    g_settings.endTime = readString(L"Schedule", L"End", L"18:00");
    g_settings.startupEnabled = readInt(L"General", L"Startup", 0) != 0;
    g_settings.launchTarget = readInt(L"Automation", L"Enabled", 0) != 0;
    g_settings.targetPath = readString(L"Automation", L"Target", L"");
    g_settings.targetIntervalMinutes = std::clamp(readInt(L"Automation", L"IntervalMinutes", 0), 0, 60);
}

void saveSettings() {
    if (g_iniPath.empty()) return;
    writeInt(L"General", L"PointerEnabled", g_settings.pointerEnabled ? 1 : 0);
    writeInt(L"General", L"KeepAwake", g_settings.keepAwake ? 1 : 0);
    writeInt(L"General", L"PauseOnBattery", g_settings.pauseOnBattery ? 1 : 0);
    writeInt(L"General", L"Startup", g_settings.startupEnabled ? 1 : 0);
    writeInt(L"Motion", L"DistancePixels", g_settings.distancePixels);
    writeInt(L"Motion", L"AngleDegrees", g_settings.angleDegrees);
    writeInt(L"Duration", L"CountdownMinutes", g_settings.countdownMinutes);
    writeInt(L"Duration", L"AutoExit", g_settings.autoExit ? 1 : 0);
    writeInt(L"General", L"HotkeyPreset", g_settings.hotkeyPreset);
    writeInt(L"Schedule", L"Enabled", g_settings.scheduleEnabled ? 1 : 0);
    writeInt(L"Schedule", L"DaysMask", static_cast<int>(g_settings.daysMask));
    WritePrivateProfileStringW(L"Schedule", L"Start", g_settings.startTime.c_str(), g_iniPath.c_str());
    WritePrivateProfileStringW(L"Schedule", L"End", g_settings.endTime.c_str(), g_iniPath.c_str());
    writeInt(L"Automation", L"Enabled", g_settings.launchTarget ? 1 : 0);
    WritePrivateProfileStringW(L"Automation", L"Target", g_settings.targetPath.c_str(), g_iniPath.c_str());
    writeInt(L"Automation", L"IntervalMinutes", g_settings.targetIntervalMinutes);
}

bool updateStartupRegistration(bool enabled) {
    HKEY key{};
    const LONG openResult = RegCreateKeyExW(HKEY_CURRENT_USER, kRunKey, 0, nullptr, 0,
        KEY_SET_VALUE | KEY_QUERY_VALUE, nullptr, &key, nullptr);
    if (openResult != ERROR_SUCCESS) return false;

    LONG result = ERROR_SUCCESS;
    if (enabled) {
        // Resolve the executable path independently of its containing directory.
        std::array<wchar_t, MAX_PATH> exe{};
        const DWORD length = GetModuleFileNameW(nullptr, exe.data(), static_cast<DWORD>(exe.size()));
        if (length == 0 || length >= exe.size()) {
            RegCloseKey(key);
            return false;
        }
        const std::wstring fullCommand = L"\"" + std::wstring(exe.data(), length) + L"\" --minimized";
        result = RegSetValueExW(key, kRunValue, 0, REG_SZ,
            reinterpret_cast<const BYTE*>(fullCommand.c_str()),
            static_cast<DWORD>((fullCommand.size() + 1) * sizeof(wchar_t)));
    } else {
        result = RegDeleteValueW(key, kRunValue);
        if (result == ERROR_FILE_NOT_FOUND) result = ERROR_SUCCESS;
    }
    RegCloseKey(key);
    return result == ERROR_SUCCESS;
}

void syncSettingsFromControls() {
    g_settings.pointerEnabled = SendMessageW(g_pointerCheck, BM_GETCHECK, 0, 0) == BST_CHECKED;
    g_settings.keepAwake = SendMessageW(g_powerCheck, BM_GETCHECK, 0, 0) == BST_CHECKED;
    g_settings.pauseOnBattery = SendMessageW(g_batteryCheck, BM_GETCHECK, 0, 0) == BST_CHECKED;

    wchar_t text[64]{};
    GetWindowTextW(g_distanceEdit, text, static_cast<int>(std::size(text)));
    try { g_settings.distancePixels = std::clamp(std::stoi(text), 1, 1000); }
    catch (...) { g_settings.distancePixels = 120; }

    GetWindowTextW(g_angleEdit, text, static_cast<int>(std::size(text)));
    try { g_settings.angleDegrees = std::clamp(std::stoi(text), -1, 359); }
    catch (...) { g_settings.angleDegrees = -1; }

    GetWindowTextW(g_countdownEdit, text, static_cast<int>(std::size(text)));
    try { g_settings.countdownMinutes = std::clamp(std::stoi(text), 0, 10080); }
    catch (...) { g_settings.countdownMinutes = 0; }
    g_settings.autoExit = SendMessageW(g_autoExitCheck, BM_GETCHECK, 0, 0) == BST_CHECKED;
    const LRESULT hotkeySelection = SendMessageW(g_hotkeyCombo, CB_GETCURSEL, 0, 0);
    g_settings.hotkeyPreset = hotkeySelection == CB_ERR ? 0 :
        std::clamp(static_cast<int>(hotkeySelection), 0, 3);

    g_settings.scheduleEnabled = SendMessageW(g_scheduleCheck, BM_GETCHECK, 0, 0) == BST_CHECKED;
    g_settings.daysMask = 0;
    for (int index = 0; index < 7; ++index) {
        if (SendMessageW(g_dayChecks[index], BM_GETCHECK, 0, 0) == BST_CHECKED)
            g_settings.daysMask |= (1u << index);
    }
    wchar_t timeBuffer[16]{};
    GetWindowTextW(g_startTimeEdit, timeBuffer, static_cast<int>(std::size(timeBuffer)));
    g_settings.startTime = timeBuffer;
    GetWindowTextW(g_endTimeEdit, timeBuffer, static_cast<int>(std::size(timeBuffer)));
    g_settings.endTime = timeBuffer;
    g_settings.startupEnabled = SendMessageW(g_startupCheck, BM_GETCHECK, 0, 0) == BST_CHECKED;
    g_settings.launchTarget = SendMessageW(g_launchTargetCheck, BM_GETCHECK, 0, 0) == BST_CHECKED;
    std::array<wchar_t, 1024> target{};
    GetWindowTextW(g_targetPathEdit, target.data(), static_cast<int>(target.size()));
    g_settings.targetPath = target.data();

    const LRESULT intervalIndex = SendMessageW(g_targetIntervalCombo, CB_GETCURSEL, 0, 0);
    const LRESULT intervalData = intervalIndex == CB_ERR ? 0 :
        SendMessageW(g_targetIntervalCombo, CB_GETITEMDATA, intervalIndex, 0);
    g_settings.targetIntervalMinutes = intervalData == CB_ERR ? 0 : static_cast<int>(intervalData);
}

void populateControlsFromSettings() {
    SendMessageW(g_pointerCheck, BM_SETCHECK, g_settings.pointerEnabled ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(g_powerCheck, BM_SETCHECK, g_settings.keepAwake ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(g_batteryCheck, BM_SETCHECK, g_settings.pauseOnBattery ? BST_CHECKED : BST_UNCHECKED, 0);

    wchar_t number[32]{};
    swprintf_s(number, L"%d", g_settings.distancePixels);
    SetWindowTextW(g_distanceEdit, number);
    swprintf_s(number, L"%d", g_settings.angleDegrees);
    SetWindowTextW(g_angleEdit, number);

    swprintf_s(number, L"%d", g_settings.countdownMinutes);
    SetWindowTextW(g_countdownEdit, number);
    SendMessageW(g_autoExitCheck, BM_SETCHECK, g_settings.autoExit ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(g_hotkeyCombo, CB_SETCURSEL, g_settings.hotkeyPreset, 0);

    SendMessageW(g_scheduleCheck, BM_SETCHECK, g_settings.scheduleEnabled ? BST_CHECKED : BST_UNCHECKED, 0);
    for (int index = 0; index < 7; ++index) {
        SendMessageW(g_dayChecks[index], BM_SETCHECK,
            (g_settings.daysMask & (1u << index)) ? BST_CHECKED : BST_UNCHECKED, 0);
    }
    SetWindowTextW(g_startTimeEdit, g_settings.startTime.c_str());
    SetWindowTextW(g_endTimeEdit, g_settings.endTime.c_str());
    SendMessageW(g_startupCheck, BM_SETCHECK, g_settings.startupEnabled ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(g_launchTargetCheck, BM_SETCHECK, g_settings.launchTarget ? BST_CHECKED : BST_UNCHECKED, 0);
    SetWindowTextW(g_targetPathEdit, g_settings.targetPath.c_str());

    const std::array<int, 4> intervals{0, 5, 15, 30};
    int selected = 0;
    for (std::size_t index = 0; index < intervals.size(); ++index) {
        if (intervals[index] == g_settings.targetIntervalMinutes) selected = static_cast<int>(index);
    }
    SendMessageW(g_targetIntervalCombo, CB_SETCURSEL, selected, 0);
}

bool parseTime(const std::wstring& value, int& minutes) {
    if (value.size() != 5 || value[2] != L':') return false;
    if (value[0] < L'0' || value[0] > L'9' || value[1] < L'0' || value[1] > L'9' ||
        value[3] < L'0' || value[3] > L'9' || value[4] < L'0' || value[4] > L'9') return false;
    const int hour = (value[0] - L'0') * 10 + (value[1] - L'0');
    const int minute = (value[3] - L'0') * 10 + (value[4] - L'0');
    if (hour > 23 || minute > 59) return false;
    minutes = hour * 60 + minute;
    return true;
}

bool isScheduleActive(const SYSTEMTIME& now) {
    int startMinute{}, endMinute{};
    if (!parseTime(g_settings.startTime, startMinute) ||
        !parseTime(g_settings.endTime, endMinute) || startMinute == endMinute) return false;

    const int currentMinute = static_cast<int>(now.wHour) * 60 + now.wMinute;
    const int day = static_cast<int>(now.wDayOfWeek); // Sunday=0
    const int previousDay = (day + 6) % 7;

    if (startMinute < endMinute) {
        return (g_settings.daysMask & (1u << day)) != 0 &&
            currentMinute >= startMinute && currentMinute < endMinute;
    }
    if (currentMinute >= startMinute) {
        return (g_settings.daysMask & (1u << day)) != 0;
    }
    if (currentMinute < endMinute) {
        return (g_settings.daysMask & (1u << previousDay)) != 0;
    }
    return false;
}

void showPage(int page) {
    for (HWND control : g_generalControls) ShowWindow(control, page == 0 ? SW_SHOW : SW_HIDE);
    for (HWND control : g_scheduleControls) ShowWindow(control, page == 1 ? SW_SHOW : SW_HIDE);
    for (HWND control : g_diagnosticsControls) ShowWindow(control, page == 2 ? SW_SHOW : SW_HIDE);
}

void setControlFont(HWND control, HFONT font) {
    if (control != nullptr) SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
}

HWND createPageControl(int page, const wchar_t* cls, const wchar_t* text, DWORD style,
                       int x, int y, int width, int height, int id) {
    HWND control = CreateWindowExW(
        0, cls, text, WS_CHILD | style, x, y, width, height,
        g_window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
        GetModuleHandleW(nullptr), nullptr
    );
    setControlFont(control, g_regularFont);
    if (page == 0) g_generalControls.push_back(control);
    else if (page == 1) g_scheduleControls.push_back(control);
    else g_diagnosticsControls.push_back(control);
    return control;
}

void setEnabledState() {
    if (g_startButton) EnableWindow(g_startButton, !g_running);
    if (g_stopButton) EnableWindow(g_stopButton, g_running);
    for (HWND control : g_generalControls) {
        const int id = GetDlgCtrlID(control);
        if (id != IdStart && id != IdStop) EnableWindow(control, !g_running);
    }
}

void createControls(HWND window) {
    g_regularFont = CreateFontW(-14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    g_titleFont = CreateFontW(-22, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    g_statusFont = CreateFontW(-12, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

    HWND title = CreateWindowW(L"STATIC", L"Acrobat", WS_CHILD | WS_VISIBLE,
        22, 16, 220, 30, window, nullptr, nullptr, nullptr);
    HWND subtitle = CreateWindowW(L"STATIC", L"Pointer control  •  Session power management",
        WS_CHILD | WS_VISIBLE, 24, 48, 340, 22, window, nullptr, nullptr, nullptr);
    setControlFont(title, g_titleFont);
    setControlFont(subtitle, g_regularFont);

    g_startButton = CreateWindowW(L"BUTTON", L"Start session",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
        398, 19, 88, 32, window, reinterpret_cast<HMENU>(IdStart), nullptr, nullptr);
    g_stopButton = CreateWindowW(L"BUTTON", L"Stop",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
        496, 19, 74, 32, window, reinterpret_cast<HMENU>(IdStop), nullptr, nullptr);
    setControlFont(g_startButton, g_regularFont);
    setControlFont(g_stopButton, g_regularFont);
    EnableWindow(g_stopButton, FALSE);

    g_tabs = CreateWindowExW(0, WC_TABCONTROLW, L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | TCS_TABS,
        14, 78, 570, 287, window, reinterpret_cast<HMENU>(IdTabs), nullptr, nullptr);
    setControlFont(g_tabs, g_regularFont);
    TCITEMW tab{};
    tab.mask = TCIF_TEXT;
    tab.pszText = const_cast<LPWSTR>(L"Session");
    TabCtrl_InsertItem(g_tabs, 0, &tab);
    tab.pszText = const_cast<LPWSTR>(L"Schedule & Automation");
    TabCtrl_InsertItem(g_tabs, 1, &tab);
    tab.pszText = const_cast<LPWSTR>(L"Diagnostics");
    TabCtrl_InsertItem(g_tabs, 2, &tab);

    g_pointerCheck = createPageControl(0, L"BUTTON", L"Enable pointer movement",
        WS_TABSTOP | BS_AUTOCHECKBOX, 30, 116, 195, 24, IdPointer);
    g_powerCheck = createPageControl(0, L"BUTTON", L"Keep display and system awake",
        WS_TABSTOP | BS_AUTOCHECKBOX, 240, 116, 220, 24, IdPower);
    g_batteryCheck = createPageControl(0, L"BUTTON", L"Pause on battery or below 20%",
        WS_TABSTOP | BS_AUTOCHECKBOX, 30, 147, 240, 24, IdBattery);

    createPageControl(0, L"STATIC", L"Travel distance (px)", SS_LEFT,
        30, 190, 118, 22, 0);
    g_distanceEdit = createPageControl(0, L"EDIT", L"120",
        WS_TABSTOP | WS_BORDER | ES_NUMBER | ES_AUTOHSCROLL, 150, 187, 62, 25, IdDistance);
    createPageControl(0, L"STATIC", L"Angle (-1 = random)", SS_LEFT,
        238, 190, 130, 22, 0);
    g_angleEdit = createPageControl(0, L"EDIT", L"-1",
        WS_TABSTOP | WS_BORDER | ES_AUTOHSCROLL, 370, 187, 55, 25, IdAngle);

    createPageControl(0, L"STATIC", L"Session limit (min; 0 = unlimited)", SS_LEFT,
        30, 243, 205, 22, 0);
    g_countdownEdit = createPageControl(0, L"EDIT", L"0",
        WS_TABSTOP | WS_BORDER | ES_NUMBER | ES_AUTOHSCROLL, 238, 239, 62, 25, IdCountdown);
    g_autoExitCheck = createPageControl(0, L"BUTTON", L"Exit when finished",
        WS_TABSTOP | BS_AUTOCHECKBOX, 315, 241, 160, 24, IdAutoExit);
    createPageControl(0, L"STATIC",
        L"Physical input pauses pointer movement; Ctrl+Shift+J toggles the session.",
        SS_LEFT, 30, 286, 440, 34, 0);

    g_scheduleCheck = createPageControl(1, L"BUTTON", L"Enable weekly schedule",
        WS_TABSTOP | BS_AUTOCHECKBOX, 30, 112, 220, 24, IdSchedule);
    createPageControl(1, L"STATIC", L"Active days", SS_LEFT, 30, 144, 100, 20, 0);
    const std::array<const wchar_t*, 7> dayNames{
        L"Sun", L"Mon", L"Tue", L"Wed", L"Thu", L"Fri", L"Sat"
    };
    for (int index = 0; index < 7; ++index) {
        g_dayChecks[index] = createPageControl(1, L"BUTTON", dayNames[index],
            WS_TABSTOP | BS_AUTOCHECKBOX, 30 + index * 72, 166, 64, 22, IdDayMonday + index);
    }
    createPageControl(1, L"STATIC", L"Start (HH:MM)", SS_LEFT, 30, 205, 95, 22, 0);
    g_startTimeEdit = createPageControl(1, L"EDIT", L"09:00",
        WS_TABSTOP | WS_BORDER | ES_AUTOHSCROLL, 130, 201, 72, 25, IdStartTime);
    createPageControl(1, L"STATIC", L"End (HH:MM)", SS_LEFT, 286, 205, 88, 22, 0);
    g_endTimeEdit = createPageControl(1, L"EDIT", L"18:00",
        WS_TABSTOP | WS_BORDER | ES_AUTOHSCROLL, 380, 201, 72, 25, IdEndTime);
    g_startupCheck = createPageControl(1, L"BUTTON", L"Start with Windows (minimized)",
        WS_TABSTOP | BS_AUTOCHECKBOX, 30, 237, 280, 24, IdStartup);
    g_launchTargetCheck = createPageControl(1, L"BUTTON", L"Open target on session start",
        WS_TABSTOP | BS_AUTOCHECKBOX, 30, 269, 210, 24, IdLaunchTarget);
    g_targetPathEdit = createPageControl(1, L"EDIT", L"",
        WS_TABSTOP | WS_BORDER | ES_AUTOHSCROLL, 250, 267, 320, 25, IdTargetPath);
    createPageControl(1, L"STATIC", L"Repeat", SS_LEFT, 30, 303, 54, 22, 0);
    g_targetIntervalCombo = createPageControl(1, WC_COMBOBOXW, L"",
        WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL, 88, 299, 116, 100, IdTargetInterval);
    const std::array<std::pair<const wchar_t*, int>, 4> intervalItems{{
        {L"On start only", 0}, {L"Every 5 min", 5},
        {L"Every 15 min", 15}, {L"Every 30 min", 30}
    }};
    for (const auto& item : intervalItems) {
        const LRESULT index = SendMessageW(g_targetIntervalCombo, CB_ADDSTRING, 0,
            reinterpret_cast<LPARAM>(item.first));
        SendMessageW(g_targetIntervalCombo, CB_SETITEMDATA, index, item.second);
    }

    createPageControl(1, L"STATIC", L"Global shortcut", SS_LEFT,
        30, 338, 90, 22, 0);
    g_hotkeyCombo = createPageControl(1, WC_COMBOBOXW, L"",
        WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL, 130, 334, 190, 100, IdHotkey);
    for (const auto& hotkey : kHotkeyDefinitions) {
        SendMessageW(g_hotkeyCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(hotkey.label));
    }

    g_diagCpuLabel = createPageControl(2, L"STATIC", L"CPU: measuring…", SS_LEFT,
        30, 120, 440, 24, 0);
    g_diagMemoryLabel = createPageControl(2, L"STATIC", L"Working set: measuring…", SS_LEFT,
        30, 155, 440, 24, 0);
    g_diagBatteryLabel = createPageControl(2, L"STATIC", L"Power: measuring…", SS_LEFT,
        30, 190, 440, 24, 0);
    g_diagInputLabel = createPageControl(2, L"STATIC", L"Physical input events: 0", SS_LEFT,
        30, 225, 440, 24, 0);
    g_diagSessionLabel = createPageControl(2, L"STATIC", L"Session count: 0", SS_LEFT,
        30, 260, 440, 24, 0);
    createPageControl(2, L"STATIC",
        L"Local diagnostics only. No EDR/UEBA detection or evasion logic is used.",
        SS_LEFT, 30, 310, 450, 36, 0);

    g_statusLabel = CreateWindowExW(0, STATUSCLASSNAMEW, nullptr,
        WS_CHILD | WS_VISIBLE | SBARS_TOOLTIPS,
        0, 0, 0, 0, window, reinterpret_cast<HMENU>(IdStatus),
        GetModuleHandleW(nullptr), nullptr);
    setControlFont(g_statusLabel, g_statusFont);
    populateControlsFromSettings();
    showPage(0);
}

void releasePowerRequest() {
    if (g_powerRequestActive) {
        SetThreadExecutionState(ES_CONTINUOUS);
        g_powerRequestActive = false;
    }
    g_powerRequestAttempted = false;
}

bool applyPowerRequest() {
    if (!g_settings.keepAwake) {
        releasePowerRequest();
        return true;
    }
    g_powerRequestAttempted = true;
    const EXECUTION_STATE result = SetThreadExecutionState(
        ES_CONTINUOUS | ES_SYSTEM_REQUIRED | ES_DISPLAY_REQUIRED);
    g_powerRequestActive = result != 0;
    return g_powerRequestActive;
}

void abortPath() {
    g_activePath.clear();
    g_activePathIndex = 0;
    g_nextPathStepAt = 0;
}

void launchConfiguredTarget() {
    if (!g_settings.launchTarget || g_settings.targetPath.empty()) return;
    const HINSTANCE result = ShellExecuteW(g_window, L"open", g_settings.targetPath.c_str(),
        nullptr, nullptr, SW_SHOWNORMAL);
    if (reinterpret_cast<INT_PTR>(result) <= 32) {
        setStatus(L"Session active; target could not be opened");
    }
}

void runTargetIfDue(ULONGLONG now) {
    if (!g_running || !g_settings.launchTarget || g_settings.targetPath.empty() ||
        g_settings.targetIntervalMinutes <= 0) return;
    if (g_nextTargetLaunchAt == 0) {
        g_nextTargetLaunchAt = now +
            static_cast<ULONGLONG>(g_settings.targetIntervalMinutes) * 60ULL * 1000ULL;
        return;
    }
    if (now >= g_nextTargetLaunchAt) {
        launchConfiguredTarget();
        g_nextTargetLaunchAt = now +
            static_cast<ULONGLONG>(g_settings.targetIntervalMinutes) * 60ULL * 1000ULL;
    }
}

void stopSession(bool fromSchedule = false) {
    g_running = false;
    g_startedBySchedule = false;
    g_sessionStartedAt = 0;
    g_nextActionAt = 0;
    g_countdownDeadline = 0;
    g_nextTargetLaunchAt = 0;
    abortPath();
    releasePowerRequest();
    setStatus(fromSchedule ? L"Schedule window ended; session stopped" : L"Stopped");
    updateStatusParts();
    setEnabledState();
}

void startSession(bool fromSchedule = false) {
    if (g_running) return;
    syncSettingsFromControls();

    if (g_settings.pointerEnabled && !g_inputMonitoringReady) {
        setStatus(L"Cannot start: physical input monitoring is unavailable");
        return;
    }
    if (!g_settings.pointerEnabled && !g_settings.keepAwake) {
        setStatus(L"Select pointer movement or Windows power protection first");
        return;
    }

    g_running = true;
    g_startedBySchedule = fromSchedule;
    g_sessionStartedAt = GetTickCount64();
    ++g_sessionCount;
    g_lastPhysicalInput = g_sessionStartedAt;
    g_nextActionAt = 0;
    g_countdownDeadline = g_settings.countdownMinutes > 0
        ? g_lastPhysicalInput + static_cast<ULONGLONG>(g_settings.countdownMinutes) * 60ULL * 1000ULL
        : 0;
    g_nextTargetLaunchAt = 0;
    abortPath();

    const bool powerOk = applyPowerRequest();
    if (!powerOk && g_settings.keepAwake) {
        setStatus(L"Session active; Windows power request failed");
    } else {
        setStatus(fromSchedule ? L"Started by weekly schedule" : L"Session active");
    }
    setEnabledState();
    updateStatusParts();
    updateDiagnostics();
    saveSettings();
    if (g_settings.startupEnabled && !updateStartupRegistration(true)) {
        setStatus(L"Session active; Windows startup registration failed");
    }
    launchConfiguredTarget();
    if (g_settings.targetIntervalMinutes > 0) {
        g_nextTargetLaunchAt = GetTickCount64() +
            static_cast<ULONGLONG>(g_settings.targetIntervalMinutes) * 60ULL * 1000ULL;
    } else {
        g_nextTargetLaunchAt = std::numeric_limits<ULONGLONG>::max();
    }
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
        static_cast<double>(pixelX - left) * 65535.0 / xRange));
    input.mi.dy = static_cast<LONG>(std::lround(
        static_cast<double>(pixelY - top) * 65535.0 / yRange));
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
    std::uniform_int_distribution<int> delta(-1, 1);

    POINT offset{};
    for (int attempt = 0; attempt < 8; ++attempt) {
        const int dx = delta(g_random);
        const int dy = delta(g_random);
        if (dx == 0 && dy == 0) continue;
        offset.x = std::clamp<int>(static_cast<int>(current.x) + dx, left, right);
        offset.y = std::clamp<int>(static_cast<int>(current.y) + dy, top, bottom);
        if (offset.x != current.x || offset.y != current.y) break;
    }
    if (offset.x == current.x && offset.y == current.y) return false;

    INPUT inputs[2]{};
    inputs[0] = makeAbsoluteMove({static_cast<double>(offset.x), static_cast<double>(offset.y)});
    inputs[1] = makeAbsoluteMove({static_cast<double>(current.x), static_cast<double>(current.y)});
    const UINT sent = SendInput(2, inputs, sizeof(INPUT));
    if (sent != 2) SetCursorPos(current.x, current.y);
    return sent == 2;
}

bool beginHumanPath() {
    POINT current{};
    if (!GetCursorPos(&current)) return false;

    const int left = GetSystemMetrics(SM_XVIRTUALSCREEN);
    const int top = GetSystemMetrics(SM_YVIRTUALSCREEN);
    const int width = std::max(1, GetSystemMetrics(SM_CXVIRTUALSCREEN));
    const int height = std::max(1, GetSystemMetrics(SM_CYVIRTUALSCREEN));
    const double distance = static_cast<double>(g_settings.distancePixels);
    double angleDegrees = static_cast<double>(g_settings.angleDegrees);
    if (g_settings.angleDegrees < 0) {
        std::uniform_real_distribution<double> randomAngle(0.0, 360.0);
        angleDegrees = randomAngle(g_random);
    }
    constexpr double kPi = 3.14159265358979323846;
    const double angle = angleDegrees * kPi / 180.0;

    const int minX = left + std::min(12, width / 4);
    const int maxX = left + width - 1 - std::min(12, width / 4);
    const int minY = top + std::min(12, height / 4);
    const int maxY = top + height - 1 - std::min(12, height / 4);
    const deskawake::Point start{static_cast<double>(current.x), static_cast<double>(current.y)};
    const deskawake::Point target{
        static_cast<double>(std::clamp(static_cast<int>(std::lround(current.x + std::cos(angle) * distance)), minX, maxX)),
        static_cast<double>(std::clamp(static_cast<int>(std::lround(current.y + std::sin(angle) * distance)), minY, maxY))
    };
    if (std::abs(target.x - start.x) < 1.0 && std::abs(target.y - start.y) < 1.0) return false;

    deskawake::TrajectoryOptions options{};
    options.sample_count = 28;
    options.overshoot_fraction = 0.035;
    options.perlin_amplitude_pixels = 0.35;
    options.gaussian_sigma_pixels = 0.18;
    options.seed = g_random();
    g_activePath = deskawake::generateHumanTrajectory(start, target, options);
    g_activePathIndex = 1;
    g_nextPathStepAt = GetTickCount64();
    return !g_activePath.empty();
}

void finishPath() {
    abortPath();
    g_nextActionAt = GetTickCount64() +
        static_cast<ULONGLONG>(g_scheduler.nextDelaySeconds()) * 1000ULL;
    setStatus(L"Session active; pointer route completed");
}

void updateRunningState() {
    if (!g_running) { updateStatusParts(); return; }
    const ULONGLONG now = GetTickCount64();

    if (g_countdownDeadline != 0 && now >= g_countdownDeadline) {
        const bool exitAfterStop = g_settings.autoExit;
        stopSession();
        setStatus(exitAfterStop ? L"Countdown finished; exiting" : L"Countdown finished; session stopped");
        if (exitAfterStop && g_window != nullptr) DestroyWindow(g_window);
        return;
    }

    SYSTEM_POWER_STATUS powerStatus{};
    const bool powerKnown = GetSystemPowerStatus(&powerStatus) == TRUE;
    const bool onBattery = powerKnown &&
        (powerStatus.ACLineStatus == 0 ||
         (powerStatus.BatteryLifePercent != 255 && powerStatus.BatteryLifePercent <= 20));
    if (g_settings.pauseOnBattery && onBattery) {
        abortPath();
        g_nextActionAt = 0;
        releasePowerRequest();
        setStatus(L"Paused; battery disconnected or at/below 20%");
        return;
    }

    if (g_settings.keepAwake && !g_powerRequestActive && !g_powerRequestAttempted) {
        applyPowerRequest();
    } else if (!g_settings.keepAwake) {
        releasePowerRequest();
    }

    runTargetIfDue(now);
    updateStatusParts();
    updateDiagnostics();

    if (!g_settings.pointerEnabled) {
        if (now - g_lastStatusUpdate >= 1000) {
            g_lastStatusUpdate = now;
            setStatus(L"Session active; Windows power protection only");
        }
        return;
    }

    if (now - g_lastPhysicalInput < kQuietPeriodMs) {
        abortPath();
        g_nextActionAt = 0;
        setStatus(L"Paused; physical input detected");
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
        ++g_activePathIndex;
        g_nextPathStepAt = now + 12;
        return;
    }

    if (g_nextActionAt == 0) {
        g_nextActionAt = now +
            static_cast<ULONGLONG>(g_scheduler.nextDelaySeconds()) * 1000ULL;
    }
    if (now >= g_nextActionAt) {
        if (g_scheduler.nextAction() == deskawake::ActionKind::Micro) {
            const bool moved = sendMicroMove();
            g_nextActionAt = now +
                static_cast<ULONGLONG>(g_scheduler.nextDelaySeconds()) * 1000ULL;
            setStatus(moved ? L"Session active; micro movement completed"
                            : L"Session active; micro movement unavailable");
        } else if (!beginHumanPath()) {
            g_nextActionAt = now +
                static_cast<ULONGLONG>(g_scheduler.nextDelaySeconds()) * 1000ULL;
            setStatus(L"Session active; could not create pointer route");
        } else {
            setStatus(L"Session active; pointer route in progress");
        }
        return;
    }

    if (now - g_lastStatusUpdate >= 1000) {
        g_lastStatusUpdate = now;
        const ULONGLONG remaining = g_nextActionAt > now ? g_nextActionAt - now : 0;
        wchar_t status[160]{};
        swprintf_s(status, L"Session active  •  next movement in %lu sec",
            static_cast<unsigned long>((remaining + 999) / 1000));
        setStatus(status);
    }
}

ULONGLONG fileTimeToUInt64(const FILETIME& value) {
    return (static_cast<ULONGLONG>(value.dwHighDateTime) << 32) |
        static_cast<ULONGLONG>(value.dwLowDateTime);
}

void updateDiagnostics() {
    const ULONGLONG now = GetTickCount64();
    if (now - g_lastDiagnosticsUpdate < kDiagnosticsRefreshMs) return;
    g_lastDiagnosticsUpdate = now;

    PROCESS_MEMORY_COUNTERS_EX memory{};
    memory.cb = sizeof(memory);
    if (GetProcessMemoryInfo(GetCurrentProcess(),
        reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory), sizeof(memory))) {
        g_workingSetBytes = memory.WorkingSetSize;
    }

    FILETIME creation{}, exit{}, kernel{}, user{};
    if (GetProcessTimes(GetCurrentProcess(), &creation, &exit, &kernel, &user)) {
        const ULONGLONG kernelNow = fileTimeToUInt64(kernel);
        const ULONGLONG userNow = fileTimeToUInt64(user);
        if (g_lastCpuSampleTick != 0 && now > g_lastCpuSampleTick) {
            const ULONGLONG cpuDelta = (kernelNow - g_lastKernelTime) +
                (userNow - g_lastUserTime);
            const ULONGLONG wall100ns = (now - g_lastCpuSampleTick) * 10000ULL;
            SYSTEM_INFO systemInfo{};
            GetSystemInfo(&systemInfo);
            const DWORD cores = std::max<DWORD>(1, systemInfo.dwNumberOfProcessors);
            g_cpuUsagePercent = wall100ns == 0 ? 0.0 :
                (100.0 * static_cast<double>(cpuDelta) /
                 static_cast<double>(wall100ns) / static_cast<double>(cores));
            g_cpuUsagePercent = std::clamp(g_cpuUsagePercent, 0.0, 100.0);
        }
        g_lastKernelTime = kernelNow;
        g_lastUserTime = userNow;
        g_lastCpuSampleTick = now;
    }

    wchar_t buffer[192]{};
    if (g_diagCpuLabel != nullptr) {
        swprintf_s(buffer, L"Process CPU: %.2f%% (sampled over ~1 second)", g_cpuUsagePercent);
        SetWindowTextW(g_diagCpuLabel, buffer);
    }
    if (g_diagMemoryLabel != nullptr) {
        swprintf_s(buffer, L"Working set: %.1f MB",
            static_cast<double>(g_workingSetBytes) / (1024.0 * 1024.0));
        SetWindowTextW(g_diagMemoryLabel, buffer);
    }

    SYSTEM_POWER_STATUS power{};
    if (g_diagBatteryLabel != nullptr) {
        if (GetSystemPowerStatus(&power)) {
            if (power.ACLineStatus == 1) wcscpy_s(buffer, L"Power source: AC");
            else if (power.ACLineStatus == 0 && power.BatteryLifePercent != 255)
                swprintf_s(buffer, L"Power source: Battery (%u%%)",
                    static_cast<unsigned int>(power.BatteryLifePercent));
            else wcscpy_s(buffer, L"Power source: Battery level unavailable");
        } else wcscpy_s(buffer, L"Power source: unavailable");
        SetWindowTextW(g_diagBatteryLabel, buffer);
    }
    if (g_diagInputLabel != nullptr) {
        swprintf_s(buffer, L"Physical keyboard/mouse events observed: %llu",
            g_physicalInputCount);
        SetWindowTextW(g_diagInputLabel, buffer);
    }
    if (g_diagSessionLabel != nullptr) {
        swprintf_s(buffer, L"Sessions started this run: %llu", g_sessionCount);
        SetWindowTextW(g_diagSessionLabel, buffer);
    }
}

void updateSchedule() {
    SYSTEMTIME local{};
    GetLocalTime(&local);
    const bool active = g_settings.scheduleEnabled && isScheduleActive(local);
    if (!g_scheduleStateKnown || active != g_lastScheduleActive) {
        g_scheduleStateKnown = true;
        g_lastScheduleActive = active;
        if (active && !g_running) {
            startSession(true);
        } else if (!active && g_running && g_startedBySchedule) {
            stopSession(true);
        }
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
    data.hIcon = LoadIconW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(101));
    if (data.hIcon == nullptr) data.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wcscpy_s(data.szTip, L"Acrobat");
    g_trayAdded = Shell_NotifyIconW(NIM_ADD, &data) == TRUE;
    if (g_trayAdded) {
        data.uVersion = NOTIFYICON_VERSION_4;
        Shell_NotifyIconW(NIM_SETVERSION, &data);
    }
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
    AppendMenuW(menu, MF_STRING, IdTrayShow, L"Open Acrobat");
    AppendMenuW(menu, MF_STRING, IdTrayToggle, g_running ? L"Stop session" : L"Start session");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, IdTrayExit, L"Exit");
    POINT point{};
    GetCursorPos(&point);
    SetForegroundWindow(g_window);
    TrackPopupMenu(menu, TPM_RIGHTBUTTON | TPM_BOTTOMALIGN, point.x, point.y, 0, g_window, nullptr);
    PostMessageW(g_window, WM_NULL, 0, 0);
    DestroyMenu(menu);
}

void showOrHidePageControls(int page) {
    showPage(std::clamp(page, 0, 2));
}

LRESULT CALLBACK mouseHookProc(int code, WPARAM wParam, LPARAM lParam) {
    if (code == HC_ACTION && g_window != nullptr) {
        const auto* data = reinterpret_cast<const MSLLHOOKSTRUCT*>(lParam);
        if ((data->flags & LLMHF_INJECTED) == 0) {
            static ULONGLONG lastMovePosted{};
            const ULONGLONG now = GetTickCount64();
            const bool isMove = (wParam == WM_MOUSEMOVE);
            if (!isMove || now - lastMovePosted >= 25) {
                if (isMove) lastMovePosted = now;
                PostMessageW(g_window, kUserActivityMessage, 0, 0);
            }
        }
    }
    return CallNextHookEx(g_mouseHook, code, wParam, lParam);
}

LRESULT CALLBACK keyboardHookProc(int code, WPARAM wParam, LPARAM lParam) {
    if (code == HC_ACTION && g_window != nullptr &&
        (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN ||
         wParam == WM_KEYUP || wParam == WM_SYSKEYUP)) {
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

void saveFromUi() {
    syncSettingsFromControls();
    saveSettings();
}

HRESULT addJumpTask(IObjectCollection* collection, const wchar_t* title, const wchar_t* arguments) {
    if (collection == nullptr) return E_POINTER;
    IShellLinkW* link = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&link));
    if (FAILED(hr)) return hr;

    std::array<wchar_t, MAX_PATH> executable{};
    const DWORD length = GetModuleFileNameW(nullptr, executable.data(),
        static_cast<DWORD>(executable.size()));
    if (length == 0 || length >= executable.size()) {
        link->Release();
        return HRESULT_FROM_WIN32(GetLastError());
    }

    hr = link->SetPath(executable.data());
    if (SUCCEEDED(hr)) hr = link->SetArguments(arguments);
    if (SUCCEEDED(hr)) hr = link->SetDescription(title);
    if (SUCCEEDED(hr)) {
        IPropertyStore* properties = nullptr;
        hr = link->QueryInterface(IID_PPV_ARGS(&properties));
        if (SUCCEEDED(hr)) {
            PROPVARIANT value{};
            hr = InitPropVariantFromString(title, &value);
            if (SUCCEEDED(hr)) hr = properties->SetValue(PKEY_Title, value);
            if (SUCCEEDED(hr)) hr = properties->Commit();
            PropVariantClear(&value);
            properties->Release();
        }
    }
    if (SUCCEEDED(hr)) hr = collection->AddObject(link);
    link->Release();
    return hr;
}

void rebuildJumpList() {
    ICustomDestinationList* destination = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_DestinationList, nullptr, CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&destination));
    if (FAILED(hr)) return;

    UINT maximumSlots{};
    IObjectArray* removedItems = nullptr;
    hr = destination->BeginList(&maximumSlots, IID_PPV_ARGS(&removedItems));
    if (removedItems != nullptr) removedItems->Release();

    IObjectCollection* tasks = nullptr;
    if (SUCCEEDED(hr)) {
        hr = CoCreateInstance(CLSID_EnumerableObjectCollection, nullptr, CLSCTX_INPROC_SERVER,
            IID_PPV_ARGS(&tasks));
    }
    if (SUCCEEDED(hr)) hr = addJumpTask(tasks, L"Open Acrobat", L"--show");
    if (SUCCEEDED(hr)) hr = addJumpTask(tasks, L"Start session", L"--start");
    if (SUCCEEDED(hr)) hr = addJumpTask(tasks, L"Stop session", L"--stop");
    if (SUCCEEDED(hr)) hr = addJumpTask(tasks, L"Exit Acrobat", L"--exit");

    IObjectArray* taskArray = nullptr;
    if (SUCCEEDED(hr)) hr = tasks->QueryInterface(IID_PPV_ARGS(&taskArray));
    if (SUCCEEDED(hr)) hr = destination->AddUserTasks(taskArray);
    if (taskArray != nullptr) taskArray->Release();
    if (tasks != nullptr) tasks->Release();

    if (SUCCEEDED(hr)) destination->CommitList();
    else destination->AbortList();
    destination->Release();
}

std::wstring commandFromLine(const wchar_t* commandLine) {
    if (commandLine == nullptr) return L"show";
    if (wcsstr(commandLine, L"--start") != nullptr) return L"start";
    if (wcsstr(commandLine, L"--stop") != nullptr) return L"stop";
    if (wcsstr(commandLine, L"--exit") != nullptr) return L"exit";
    if (wcsstr(commandLine, L"--minimized") != nullptr) return L"minimized";
    return L"show";
}

bool forwardToExistingInstance(const wchar_t* commandLine) {
    HWND existing = FindWindowW(kWindowClass, nullptr);
    if (existing == nullptr) return false;
    const std::wstring command = commandFromLine(commandLine);
    COPYDATASTRUCT data{};
    data.dwData = 0x41435242;
    data.cbData = static_cast<DWORD>((command.size() + 1) * sizeof(wchar_t));
    data.lpData = const_cast<wchar_t*>(command.c_str());
    DWORD_PTR response{};
    SendMessageTimeoutW(existing, WM_COPYDATA, 0, reinterpret_cast<LPARAM>(&data),
        SMTO_ABORTIFHUNG | SMTO_BLOCK, 2500, &response);
    return true;
}

LRESULT CALLBACK windowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_NCCREATE:
        g_window = window;
        return DefWindowProcW(window, message, wParam, lParam);

    case WM_CREATE:
        g_backgroundBrush = CreateSolidBrush(RGB(246, 248, 251));
        createControls(window);
        setStatus(L"Stopped");
        updateStatusParts();
        updateDiagnostics();
        if (g_settings.startupEnabled && !updateStartupRegistration(true)) {
            setStatus(L"Windows startup registration could not be restored");
        }
        g_lastPhysicalInput = GetTickCount64();
        SetTimer(window, kEngineTimerId, kEngineTimerMs, nullptr);
        SetTimer(window, kScheduleTimerId, 1000, nullptr);
        SetTimer(window, kDiagnosticsTimerId, kDiagnosticsRefreshMs, nullptr);
        if (!registerGlobalHotkey()) setStatus(L"Selected global shortcut is unavailable");
        g_inputMonitoringReady = installHooks();
        if (!g_inputMonitoringReady) {
            setStatus(L"Input monitoring unavailable; pointer mode cannot start");
        }
        updateSchedule();
        return 0;

    case WM_SIZE:
        if (g_statusLabel != nullptr) {
            SendMessageW(g_statusLabel, WM_SIZE, 0, 0);
            RECT statusRect{};
            GetClientRect(g_statusLabel, &statusRect);
            const int width = std::max(200L, statusRect.right);
            int parts[4] = {width * 39 / 100, width * 68 / 100, width * 84 / 100, -1};
            SendMessageW(g_statusLabel, SB_SETPARTS, 4, reinterpret_cast<LPARAM>(parts));
            updateStatusParts();
        }
        return 0;

    case WM_COPYDATA: {
        const auto* data = reinterpret_cast<const COPYDATASTRUCT*>(lParam);
        if (data == nullptr || data->dwData != 0x41435242 || data->lpData == nullptr ||
            data->cbData < sizeof(wchar_t)) return FALSE;
        const auto* value = static_cast<const wchar_t*>(data->lpData);
        const std::wstring command(value);
        if (command == L"start") startSession();
        else if (command == L"stop") stopSession();
        else if (command == L"exit") DestroyWindow(window);
        else if (command == L"show") {
            ShowWindow(window, SW_SHOW);
            SetForegroundWindow(window);
        }
        return TRUE;
    }

    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IdStart:
            startSession();
            return 0;
        case IdStop:
            stopSession();
            return 0;
        case IdTrayShow:
            ShowWindow(window, SW_SHOW);
            SetForegroundWindow(window);
            return 0;
        case IdTrayToggle:
            toggleSession();
            return 0;
        case IdTrayExit:
            saveFromUi();
            removeTrayIcon();
            DestroyWindow(window);
            return 0;
        default:
            break;
        }
        if (HIWORD(wParam) == BN_CLICKED || HIWORD(wParam) == CBN_SELCHANGE ||
            HIWORD(wParam) == EN_KILLFOCUS) {
            saveFromUi();
            if (LOWORD(wParam) == IdHotkey) {
                if (!registerGlobalHotkey()) setStatus(L"Selected global shortcut is unavailable");
                else setStatus(L"Global shortcut updated");
            }
            if (LOWORD(wParam) == IdStartup) {
                if (!updateStartupRegistration(g_settings.startupEnabled)) {
                    setStatus(L"Could not update Windows startup registration");
                } else {
                    setStatus(g_settings.startupEnabled ? L"Windows startup enabled" : L"Windows startup disabled");
                }
            }
        }
        break;

    case WM_NOTIFY:
        if (reinterpret_cast<NMHDR*>(lParam)->idFrom == IdTabs &&
            reinterpret_cast<NMHDR*>(lParam)->code == TCN_SELCHANGE) {
            showOrHidePageControls(TabCtrl_GetCurSel(g_tabs));
            return 0;
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
        ++g_physicalInputCount;
        if (g_running && g_settings.pointerEnabled) {
            setStatus(L"Paused; waiting for physical input to stop");
        }
        updateDiagnostics();
        return 0;

    case WM_TIMER:
        if (wParam == kEngineTimerId) {
            updateRunningState();
            return 0;
        }
        if (wParam == kScheduleTimerId) {
            updateSchedule();
            updateStatusParts();
            return 0;
        }
        if (wParam == kDiagnosticsTimerId) {
            updateDiagnostics();
            updateStatusParts();
            return 0;
        }
        break;

    case kTrayMessage:
        if (LOWORD(lParam) == WM_RBUTTONUP || LOWORD(lParam) == WM_CONTEXTMENU ||
            lParam == WM_RBUTTONUP || lParam == WM_CONTEXTMENU) {
            showTrayMenu();
            return 0;
        }
        if (LOWORD(lParam) == WM_LBUTTONDBLCLK || LOWORD(lParam) == NIN_SELECT ||
            lParam == WM_LBUTTONDBLCLK) {
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
        saveFromUi();
        addTrayIcon();
        ShowWindow(window, SW_HIDE);
        return 0;

    case WM_CTLCOLORSTATIC: {
        HDC dc = reinterpret_cast<HDC>(wParam);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, RGB(37, 47, 61));
        return reinterpret_cast<LRESULT>(g_backgroundBrush);
    }

    case WM_ERASEBKGND: {
        RECT rect{};
        GetClientRect(window, &rect);
        FillRect(reinterpret_cast<HDC>(wParam), &rect, g_backgroundBrush);
        return 1;
    }

    case WM_DESTROY:
        saveFromUi();
        stopSession();
        removeHooks();
        removeTrayIcon();
        if (g_hotkeyRegistered) UnregisterHotKey(window, kHotkeyId);
        KillTimer(window, kEngineTimerId);
        KillTimer(window, kScheduleTimerId);
        KillTimer(window, kDiagnosticsTimerId);
        if (g_regularFont) DeleteObject(g_regularFont);
        if (g_titleFont) DeleteObject(g_titleFont);
        if (g_statusFont) DeleteObject(g_statusFont);
        if (g_backgroundBrush) DeleteObject(g_backgroundBrush);
        PostQuitMessage(0);
        return 0;

    default:
        break;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR commandLine, int showCommand) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    if (forwardToExistingInstance(commandLine)) return 0;

    loadSettings();
    const HRESULT comResult = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    g_comInitialized = SUCCEEDED(comResult);
    if (g_comInitialized) {
        SetCurrentProcessExplicitAppUserModelID(kAppUserModelId);
        rebuildJumpList();
    }

    INITCOMMONCONTROLSEX controls{};
    controls.dwSize = sizeof(controls);
    controls.dwICC = ICC_TAB_CLASSES | ICC_BAR_CLASSES;
    InitCommonControlsEx(&controls);

    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.lpfnWndProc = windowProc;
    windowClass.hInstance = instance;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(101));
    if (windowClass.hIcon == nullptr) windowClass.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    windowClass.hIconSm = windowClass.hIcon;
    windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    windowClass.lpszClassName = kWindowClass;
    if (RegisterClassExW(&windowClass) == 0) return static_cast<int>(GetLastError());

    g_window = CreateWindowExW(
        WS_EX_APPWINDOW, kWindowClass, kWindowTitle,
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, 600, 440,
        nullptr, nullptr, instance, nullptr);
    if (g_window == nullptr) return static_cast<int>(GetLastError());

    const std::wstring startupCommand = commandFromLine(commandLine);
    const bool startMinimized = startupCommand == L"minimized";
    if (startupCommand == L"exit" || startupCommand == L"stop") {
        DestroyWindow(g_window);
    } else if (startMinimized) {
        addTrayIcon();
        ShowWindow(g_window, SW_HIDE);
    } else {
        ShowWindow(g_window, showCommand);
        UpdateWindow(g_window);
    }
    if (startupCommand == L"start" && g_window != nullptr) startSession();

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    if (g_comInitialized) CoUninitialize();
    return static_cast<int>(message.wParam);
}
