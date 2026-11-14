// ---------------------------------------------------------------------------
//  StreamPlayer
//  Оболочка на WebView2 для веб-плеера «Paste & Play»: интерфейс берётся
//  из assets/index.html, вшитого в exe. Ничего не перерисовывается в C++ —
//  поэтому вид совпадает с исходной страницей один в один.
// ---------------------------------------------------------------------------

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#ifndef WINVER
#define WINVER 0x0A00
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif

#include <windows.h>
#include <commctrl.h>
#include <dwmapi.h>
#include <objbase.h>
#include <shellapi.h>

#include <string>

#include <wrl.h>
#include "WebView2.h"

using Microsoft::WRL::Callback;
using Microsoft::WRL::ComPtr;

#pragma comment(linker, "/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

namespace {

// Идентификаторы ресурсов из appfiles.rc
constexpr int kResIndex = 2001;
constexpr int kResDemo = 2002;
constexpr int kResPoster = 2003;

// Увеличивай, если обновил файлы в assets/ — тогда они распакуются заново.
constexpr char kAssetsVersion[] = "1";

constexpr wchar_t kVirtualHost[] = L"app.local";
constexpr wchar_t kStartUrl[] = L"https://app.local/index.html";
constexpr wchar_t kWindowTitle[] = L"Paste & Play";
constexpr wchar_t kMainClass[] = L"StreamPlayerMain";
constexpr wchar_t kContentClass[] = L"StreamPlayerContent";

HWND g_main = nullptr;
HWND g_content = nullptr;
HBRUSH g_background = nullptr;

ComPtr<ICoreWebView2Controller> g_controller;
ComPtr<ICoreWebView2> g_webview;

bool g_fullscreen = false;
WINDOWPLACEMENT g_savedPlacement = {};

// ---------------------------------------------------------------------------
//  Файлы и ресурсы
// ---------------------------------------------------------------------------

std::wstring Env(const wchar_t* name)
{
    const DWORD needed = GetEnvironmentVariableW(name, nullptr, 0);
    if (needed == 0) {
        return std::wstring();
    }
    std::wstring value(static_cast<size_t>(needed), L'\0');
    const DWORD written = GetEnvironmentVariableW(name, value.data(), needed);
    value.resize(static_cast<size_t>(written));
    return value;
}

std::wstring Join(const std::wstring& dir, const std::wstring& name)
{
    if (dir.empty()) {
        return name;
    }
    std::wstring result = dir;
    if (result.back() != L'\\' && result.back() != L'/') {
        result.push_back(L'\\');
    }
    result += name;
    return result;
}

bool FileExists(const std::wstring& path)
{
    const DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

bool WriteResourceToFile(int id, const std::wstring& path)
{
    HRSRC info = FindResourceW(nullptr, MAKEINTRESOURCEW(id), RT_RCDATA);
    if (info == nullptr) {
        return false;
    }
    const DWORD size = SizeofResource(nullptr, info);
    HGLOBAL handle = LoadResource(nullptr, info);
    if (handle == nullptr) {
        return false;
    }
    const void* bytes = LockResource(handle);
    if (bytes == nullptr) {
        return false;
    }

    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }
    DWORD written = 0;
    const BOOL ok = WriteFile(file, bytes, size, &written, nullptr);
    CloseHandle(file);
    return ok != FALSE && written == size;
}

std::string ReadSmallFile(const std::wstring& path)
{
    std::string result;
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return result;
    }
    char buffer[256] = {};
    DWORD read = 0;
    if (ReadFile(file, buffer, static_cast<DWORD>(sizeof(buffer) - 1), &read, nullptr) != FALSE) {
        result.assign(buffer, static_cast<size_t>(read));
    }
    CloseHandle(file);
    return result;
}

bool WriteSmallFile(const std::wstring& path, const std::string& text)
{
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }
    DWORD written = 0;
    const BOOL ok = WriteFile(file, text.data(), static_cast<DWORD>(text.size()), &written, nullptr);
    CloseHandle(file);
    return ok != FALSE;
}

// Распаковывает вшитые файлы в %LOCALAPPDATA%\StreamPlayer\app
bool EnsureAssets(std::wstring& appDir)
{
    const std::wstring base = Env(L"LOCALAPPDATA");
    if (base.empty()) {
        return false;
    }

    const std::wstring root = Join(base, L"StreamPlayer");
    CreateDirectoryW(root.c_str(), nullptr);

    appDir = Join(root, L"app");
    CreateDirectoryW(appDir.c_str(), nullptr);

    const std::wstring mediaDir = Join(appDir, L"media");
    CreateDirectoryW(mediaDir.c_str(), nullptr);

    const std::wstring index = Join(appDir, L"index.html");
    const std::wstring demo = Join(mediaDir, L"demo.mp4");
    const std::wstring poster = Join(mediaDir, L"poster.jpg");
    const std::wstring marker = Join(appDir, L"assets.txt");

    if (ReadSmallFile(marker) == kAssetsVersion && FileExists(index) && FileExists(demo) &&
        FileExists(poster)) {
        return true;
    }

    if (!WriteResourceToFile(kResIndex, index) || !WriteResourceToFile(kResDemo, demo) ||
        !WriteResourceToFile(kResPoster, poster)) {
        return false;
    }
    WriteSmallFile(marker, kAssetsVersion);
    return true;
}

std::wstring WebViewDataDir()
{
    const std::wstring base = Env(L"LOCALAPPDATA");
    if (base.empty()) {
        return std::wstring();
    }
    const std::wstring root = Join(base, L"StreamPlayer");
    CreateDirectoryW(root.c_str(), nullptr);
    const std::wstring data = Join(root, L"WebView2");
    CreateDirectoryW(data.c_str(), nullptr);
    return data;
}

// ---------------------------------------------------------------------------
//  Окно
// ---------------------------------------------------------------------------

void Layout()
{
    if (g_main == nullptr || g_content == nullptr) {
        return;
    }
    RECT client = {};
    GetClientRect(g_main, &client);
    MoveWindow(g_content, 0, 0, client.right, client.bottom, TRUE);

    if (g_controller.Get() != nullptr) {
        RECT bounds = {};
        GetClientRect(g_content, &bounds);
        g_controller->put_Bounds(bounds);
    }
}

void ApplyDarkFrame(HWND hwnd)
{
    const BOOL dark = TRUE;
    // DWMWA_USE_IMMERSIVE_DARK_MODE = 20 (Windows 10 2004+)
    DwmSetWindowAttribute(hwnd, 20, &dark, sizeof(dark));
}

void ToggleFullScreen(bool fullscreen)
{
    if (fullscreen == g_fullscreen || g_main == nullptr) {
        return;
    }
    g_fullscreen = fullscreen;

    if (fullscreen) {
        g_savedPlacement.length = sizeof(g_savedPlacement);
        GetWindowPlacement(g_main, &g_savedPlacement);

        MONITORINFO monitor = {};
        monitor.cbSize = sizeof(monitor);
        if (GetMonitorInfoW(MonitorFromWindow(g_main, MONITOR_DEFAULTTOPRIMARY), &monitor) != FALSE) {
            SetWindowLongPtrW(g_main, GWL_STYLE, WS_POPUP | WS_VISIBLE);
            SetWindowPos(g_main, HWND_TOP, monitor.rcMonitor.left, monitor.rcMonitor.top,
                         monitor.rcMonitor.right - monitor.rcMonitor.left,
                         monitor.rcMonitor.bottom - monitor.rcMonitor.top,
                         SWP_FRAMECHANGED | SWP_NOOWNERZORDER);
        }
    } else {
        SetWindowLongPtrW(g_main, GWL_STYLE, WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN);
        SetWindowPlacement(g_main, &g_savedPlacement);
        SetWindowPos(g_main, nullptr, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER |
                         SWP_FRAMECHANGED);
    }
}

// ---------------------------------------------------------------------------
//  WebView2
// ---------------------------------------------------------------------------

void ConfigureWebView()
{
    if (!g_webview) {
        return;
    }

    ComPtr<ICoreWebView2Settings> settings;
    if (SUCCEEDED(g_webview->get_Settings(&settings)) && settings.Get() != nullptr) {
        settings->put_IsStatusBarEnabled(FALSE);
        settings->put_AreDevToolsEnabled(TRUE);
    }

    EventRegistrationToken token = {};

    // window.open() и target=_blank — во внешний браузер.
    ComPtr<ICoreWebView2NewWindowRequestedEventHandler> newWindowHandler =
        Callback<ICoreWebView2NewWindowRequestedEventHandler>(
            [](ICoreWebView2*, ICoreWebView2NewWindowRequestedEventArgs* args) -> HRESULT {
                LPWSTR uri = nullptr;
                if (args != nullptr && SUCCEEDED(args->get_Uri(&uri)) && uri != nullptr) {
                    args->put_Handled(TRUE);
                    ShellExecuteW(nullptr, L"open", uri, nullptr, nullptr, SW_SHOWNORMAL);
                    CoTaskMemFree(uri);
                }
                return S_OK;
            });
    g_webview->add_NewWindowRequested(newWindowHandler.Get(), &token);

    // Заголовок окна берём у страницы.
    ComPtr<ICoreWebView2DocumentTitleChangedEventHandler> titleHandler =
        Callback<ICoreWebView2DocumentTitleChangedEventHandler>(
            [](ICoreWebView2* sender, IUnknown*) -> HRESULT {
                LPWSTR title = nullptr;
                if (sender != nullptr && SUCCEEDED(sender->get_DocumentTitle(&title)) &&
                    title != nullptr) {
                    if (title[0] != L'\0') {
                        SetWindowTextW(g_main, title);
                    }
                    CoTaskMemFree(title);
                }
                return S_OK;
            });
    g_webview->add_DocumentTitleChanged(titleHandler.Get(), &token);

    // <video> на весь экран -> окно тоже на весь экран.
    ComPtr<ICoreWebView2ContainsFullScreenElementChangedEventHandler> fullscreenHandler =
        Callback<ICoreWebView2ContainsFullScreenElementChangedEventHandler>(
            [](ICoreWebView2* sender, IUnknown*) -> HRESULT {
                BOOL contains = FALSE;
                if (sender != nullptr &&
                    SUCCEEDED(sender->get_ContainsFullScreenElement(&contains))) {
                    ToggleFullScreen(contains != FALSE);
                }
                return S_OK;
            });
    g_webview->add_ContainsFullScreenElementChanged(fullscreenHandler.Get(), &token);
}

void ConfigureController()
{
    // Тёмный фон: без белой вспышки до первой отрисовки страницы.
    ComPtr<ICoreWebView2Controller2> controller2;
    if (SUCCEEDED(g_controller.As(&controller2)) && controller2.Get() != nullptr) {
        COREWEBVIEW2_COLOR background = {};
        background.A = 255;
        background.R = 8;
        background.G = 8;
        background.B = 11;
        controller2->put_DefaultBackgroundColor(background);
    }

    // F11 — на весь экран.
    EventRegistrationToken token = {};
    ComPtr<ICoreWebView2AcceleratorKeyPressedEventHandler> keyHandler =
        Callback<ICoreWebView2AcceleratorKeyPressedEventHandler>(
            [](ICoreWebView2Controller*,
               ICoreWebView2AcceleratorKeyPressedEventArgs* args) -> HRESULT {
                COREWEBVIEW2_KEY_EVENT_KIND kind = COREWEBVIEW2_KEY_EVENT_KIND_KEY_DOWN;
                UINT key = 0;
                if (args != nullptr && SUCCEEDED(args->get_KeyEventKind(&kind)) &&
                    SUCCEEDED(args->get_VirtualKey(&key)) &&
                    kind == COREWEBVIEW2_KEY_EVENT_KIND_KEY_DOWN && key == VK_F11) {
                    ToggleFullScreen(!g_fullscreen);
                    args->put_Handled(TRUE);
                }
                return S_OK;
            });
    g_controller->add_AcceleratorKeyPressed(keyHandler.Get(), &token);
}

void StartWebView(const std::wstring& appDir)
{
    const std::wstring userData = WebViewDataDir();

    ComPtr<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler> envHandler =
        Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
            [appDir](HRESULT result, ICoreWebView2Environment* env) -> HRESULT {
                if (FAILED(result) || env == nullptr) {
                    MessageBoxW(g_main,
                                L"WebView2 Runtime не найден.\n\n"
                                L"Установи «Microsoft Edge WebView2 Runtime» "
                                L"и запусти программу снова.",
                                L"StreamPlayer", MB_ICONERROR | MB_OK);
                    return S_OK;
                }

                ComPtr<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>
                    controllerHandler =
                        Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                            [appDir](HRESULT result2,
                                     ICoreWebView2Controller* controller) -> HRESULT {
                                if (FAILED(result2) || controller == nullptr) {
                                    MessageBoxW(g_main, L"Не удалось создать окно WebView2.",
                                                L"StreamPlayer", MB_ICONERROR | MB_OK);
                                    return S_OK;
                                }
                                g_controller = controller;

                                RECT bounds = {};
                                GetClientRect(g_content, &bounds);
                                g_controller->put_Bounds(bounds);

                                if (FAILED(g_controller->get_CoreWebView2(&g_webview)) ||
                                    !g_webview) {
                                    return S_OK;
                                }
                                ConfigureController();
                                ConfigureWebView();

                                // Отдаём распакованные файлы как https://app.local/
                                // (полноценный origin: работает clipboard, localStorage,
                                //  iframe-плееры и parent= для Twitch).
                                // SetVirtualHostNameToFolderMapping живёт в ICoreWebView2_3.
                                ComPtr<ICoreWebView2_3> webview3;
                                if (FAILED(g_webview.As(&webview3)) || !webview3) {
                                    MessageBoxW(g_main,
                                                L"Версия WebView2 Runtime слишком старая.\n"
                                                L"Обнови «Microsoft Edge WebView2 Runtime».",
                                                L"StreamPlayer", MB_ICONERROR | MB_OK);
                                    return S_OK;
                                }
                                webview3->SetVirtualHostNameToFolderMapping(
                                    kVirtualHost, appDir.c_str(),
                                    COREWEBVIEW2_HOST_RESOURCE_ACCESS_KIND_ALLOW);
                                g_webview->Navigate(kStartUrl);
                                return S_OK;
                            });
                env->CreateCoreWebView2Controller(g_content, controllerHandler.Get());
                return S_OK;
            });

    const HRESULT hr = CreateCoreWebView2EnvironmentWithOptions(
        nullptr, userData.empty() ? nullptr : userData.c_str(), nullptr, envHandler.Get());
    if (FAILED(hr)) {
        MessageBoxW(g_main,
                    L"Не удалось загрузить WebView2 Runtime.\n"
                    L"Скорее всего он не установлен в системе.",
                    L"StreamPlayer", MB_ICONERROR | MB_OK);
    }
}

// ---------------------------------------------------------------------------
//  Окно приложения
// ---------------------------------------------------------------------------

LRESULT CALLBACK MainProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message) {
    case WM_CREATE: {
        g_main = hwnd;

        g_content = CreateWindowExW(0, kContentClass, L"", WS_CHILD | WS_VISIBLE,
                                    0, 0, 0, 0, hwnd, nullptr, nullptr, nullptr);
        ApplyDarkFrame(hwnd);

        std::wstring appDir;
        if (!EnsureAssets(appDir)) {
            MessageBoxW(hwnd,
                        L"Не удалось распаковать файлы интерфейса в\n"
                        L"%LOCALAPPDATA%\\StreamPlayer\\app\n\n"
                        L"Проверь права на папку профиля.",
                        L"StreamPlayer", MB_ICONERROR | MB_OK);
            return -1;
        }

        Layout();
        StartWebView(appDir);
        return 0;
    }

    case WM_SIZE:
        Layout();
        return 0;

    case WM_GETMINMAXINFO: {
        auto* info = reinterpret_cast<MINMAXINFO*>(lParam);
        const int dpi = GetDpiForWindow(hwnd);
        info->ptMinTrackSize.x = MulDiv(760, dpi, 96);
        info->ptMinTrackSize.y = MulDiv(520, dpi, 96);
        return 0;
    }

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;

    default:
        break;
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

void EnableDpiAwareness()
{
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (user32 == nullptr) {
        return;
    }
    using SetContextFn = BOOL(WINAPI*)(DPI_AWARENESS_CONTEXT);
    SetContextFn setContext =
        reinterpret_cast<SetContextFn>(GetProcAddress(user32, "SetProcessDpiAwarenessContext"));
    if (setContext != nullptr) {
        // DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 == (HANDLE)-4
        setContext(reinterpret_cast<DPI_AWARENESS_CONTEXT>(static_cast<INT_PTR>(-4)));
    }
}

}  // namespace

int APIENTRY wWinMain(HINSTANCE instance, HINSTANCE, LPWSTR, int)
{
    EnableDpiAwareness();
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    INITCOMMONCONTROLSEX controls = {};
    controls.dwSize = static_cast<DWORD>(sizeof(controls));
    controls.dwICC = ICC_WIN95_CLASSES;
    InitCommonControlsEx(&controls);

    g_background = CreateSolidBrush(RGB(8, 8, 11));

    WNDCLASSEXW mainClass = {};
    mainClass.cbSize = static_cast<UINT>(sizeof(mainClass));
    mainClass.style = CS_HREDRAW | CS_VREDRAW;
    mainClass.lpfnWndProc = MainProc;
    mainClass.hInstance = instance;
    // Иконка вшита в exe (ресурс ICON с id 1, см. appfiles.rc.in).
    mainClass.hIcon = static_cast<HICON>(LoadImageW(instance, MAKEINTRESOURCEW(1), IMAGE_ICON,
                                                    GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), LR_DEFAULTCOLOR));
    mainClass.hIconSm = static_cast<HICON>(LoadImageW(instance, MAKEINTRESOURCEW(1), IMAGE_ICON,
                                                      GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR));
    if (!mainClass.hIcon) mainClass.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    if (!mainClass.hIconSm) mainClass.hIconSm = LoadIconW(nullptr, IDI_APPLICATION);
    mainClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    mainClass.hbrBackground = g_background;
    mainClass.lpszClassName = kMainClass;
    RegisterClassExW(&mainClass);

    WNDCLASSEXW contentClass = {};
    contentClass.cbSize = static_cast<UINT>(sizeof(contentClass));
    contentClass.lpfnWndProc = DefWindowProcW;
    contentClass.hInstance = instance;
    contentClass.hbrBackground = g_background;
    contentClass.lpszClassName = kContentClass;
    RegisterClassExW(&contentClass);

    const int dpi = GetDpiForSystem();
    RECT frame = { 0, 0, MulDiv(1180, dpi, 96), MulDiv(820, dpi, 96) };
    AdjustWindowRectEx(&frame, WS_OVERLAPPEDWINDOW, FALSE, 0);

    HWND window = CreateWindowExW(0, kMainClass, kWindowTitle,
                                  WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                                  CW_USEDEFAULT, CW_USEDEFAULT,
                                  frame.right - frame.left, frame.bottom - frame.top,
                                  nullptr, nullptr, instance, nullptr);
    if (window == nullptr) {
        if (g_background != nullptr) {
            DeleteObject(g_background);
        }
        CoUninitialize();
        return 1;
    }

    ShowWindow(window, SW_SHOWDEFAULT);
    UpdateWindow(window);

    MSG message = {};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    g_webview.Reset();
    if (g_controller.Get() != nullptr) {
        g_controller->Close();
        g_controller.Reset();
    }

    if (g_background != nullptr) {
        DeleteObject(g_background);
        g_background = nullptr;
    }
    CoUninitialize();
    return 0;
}
