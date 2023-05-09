#define WINVER 0x0602  // Make WS_EX_NOREDIRECTIONBITMAP available
#include <string>
#include <vector>
#include <string_view>
#include <windows.h>
#include <windowsx.h>
#include <d3d11.h>
#include <d3d11_2.h>
#include <dcomp.h>
#include <dxgi.h>
#include <wrl.h>
#include "sokol_gfx.h"
#include "sokol_time.h"
#include "glm/glm.hpp"
#include "yommd.hpp"

template <typename T> using ComPtr = Microsoft::WRL::ComPtr<T>;

namespace {
const void *getRenderTargetView();
const void *getDepthStencilView();
}

class AppMain {
public:
    AppMain();
    ~AppMain();
    void Setup(const CmdArgs& cmdArgs);
    void UpdateDisplay();
    void Terminate();
    bool IsRunning() const;
    sg_context_desc GetSokolContext() const;
    glm::vec2 GetWindowSize() const;
    glm::vec2 GetDrawableSize() const;
    const ID3D11RenderTargetView *GetRenderTargetView() const;
    const ID3D11DepthStencilView *GetDepthStencilView() const;
private:
    static LRESULT CALLBACK windowProc(
            HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
    static DWORD WINAPI showMenu(LPVOID param);

    void createWindow();
    void createDrawable();
    void createStatusIcon();
    void createTaskbar();
    LRESULT handleMessage(UINT uMsg, WPARAM wParam, LPARAM lParam);
private:
    static constexpr PCWSTR windowClassName_ = L"yoMMD AppMain";
    static constexpr UINT YOMMD_WM_TOGGLE_ENABLE_MOUSE = WM_APP;
    static constexpr UINT YOMMD_WM_SHOW_TASKBAR_MENU = WM_APP + 1;

    bool isRunning_;
    Routine routine_;
    HWND hwnd_;
    ComPtr<IDXGISwapChain1> swapChain_;
    ComPtr<ID3D11Texture2D> renderTarget_;
    ComPtr<ID3D11RenderTargetView> renderTargetView_;
    ComPtr<ID3D11Device> d3Device_;
    ComPtr<ID3D11DeviceContext> deviceContext_;
    ComPtr<IDXGIDevice> dxgiDevice_;
    ComPtr<IDXGIFactory2> dxFactory_;
    ComPtr<ID3D11Texture2D> depthStencilBuffer_;
    ComPtr<ID3D11DepthStencilView> depthStencilView_;
    ComPtr<IDCompositionDevice> dcompDevice_;
    ComPtr<IDCompositionTarget> dcompTarget_;
    ComPtr<IDCompositionVisual> dcompVisual_;

    HANDLE hMenuThread_;
    HICON hTaskbarIcon_;
    NOTIFYICONDATAW taskbarIconDesc_;
};

namespace {
namespace globals {
AppMain appMain;
}
}

AppMain::AppMain() :
    isRunning_(true),
    hwnd_(nullptr),
    hMenuThread_(nullptr), hTaskbarIcon_(nullptr)
{}

AppMain::~AppMain() {
    Terminate();
}

void AppMain::Setup(const CmdArgs& cmdArgs) {
    createWindow();
    createDrawable();
    createTaskbar();
    routine_.Init(cmdArgs);

    // Every initialization must be finished.  Now let's show window.
    ShowWindow(hwnd_, SW_SHOWNORMAL);
}

void AppMain::UpdateDisplay() {
    routine_.Update();
    routine_.Draw();
    swapChain_->Present(1, 0);
    dcompDevice_->Commit();
}

void AppMain::Terminate() {
    routine_.Terminate();
    DestroyWindow(hwnd_);
    hwnd_ = nullptr;
    UnregisterClassW(windowClassName_, GetModuleHandleW(nullptr));

    if (hTaskbarIcon_) {
        DestroyIcon(hTaskbarIcon_);
        hTaskbarIcon_ = nullptr;
    }

    Shell_NotifyIconW(NIM_DELETE, &taskbarIconDesc_);

    DWORD exitCode;
    if (GetExitCodeThread(hMenuThread_, &exitCode) &&
            exitCode == STILL_ACTIVE) {
        Info::Log("Thread is still running. Wait finishing.");
        // Right click menu/toolbar menu must not appear so long.
        // We should be able to wait for it is closed.
        WaitForSingleObject(hMenuThread_, INFINITE);
        CloseHandle(hMenuThread_);
    }
}

bool AppMain::IsRunning() const {
    return isRunning_;
}

sg_context_desc AppMain::GetSokolContext() const {
    return sg_context_desc {
        .sample_count = Constant::SampleCount,
        .d3d11 = {
            .device = reinterpret_cast<const void *>(d3Device_.Get()),
            .device_context = reinterpret_cast<const void *>(
                    deviceContext_.Get()),
            .render_target_view_cb = getRenderTargetView,
            .depth_stencil_view_cb = getDepthStencilView,
        }
    };
}

glm::vec2 AppMain::GetWindowSize() const {
    RECT rect;
    if (!GetClientRect(hwnd_, &rect)) {
        Err::Log("Failed to get window rect");
        return glm::vec2(1.0f, 1.0f);  // glm::vec2(0, 0) cause error.
    }
    return glm::vec2(rect.right - rect.left, rect.bottom - rect.top);
}

glm::vec2 AppMain::GetDrawableSize() const {
    D3D11_TEXTURE2D_DESC desc;
    renderTarget_->GetDesc(&desc);
    return glm::vec2(desc.Width, desc.Height);
}

const ID3D11RenderTargetView *AppMain::GetRenderTargetView() const {
    return renderTargetView_.Get();
}

const ID3D11DepthStencilView *AppMain::GetDepthStencilView() const {
    return depthStencilView_.Get();
}

void AppMain::createWindow() {
    constexpr DWORD winStyle = WS_POPUP;
    constexpr DWORD winExStyle =
        WS_EX_NOREDIRECTIONBITMAP |
        WS_EX_NOACTIVATE |
        WS_EX_TOPMOST |
        WS_EX_LAYERED |  // TODO: Is there another way?
        WS_EX_TRANSPARENT;

    const HINSTANCE hInstance = GetModuleHandleW(nullptr);
    const HICON appIcon = LoadIconW(hInstance, L"YOMMD_APPICON_ID");
    if (!appIcon) {
        Err::Log("Failed to load application icon.");
    }

    RECT rect = {};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &rect, 0);


    WNDCLASSEXW wc = {};

    wc.cbSize        = sizeof(wc);
    wc.style         = 0;
    wc.lpfnWndProc   = windowProc,
    wc.hInstance     = hInstance;
    wc.lpszClassName = windowClassName_;
    wc.hIcon         = appIcon;
    wc.hIconSm       = appIcon;
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);

    RegisterClassExW(&wc);

    hwnd_ = CreateWindowExW(
        winExStyle, windowClassName_, L"yoMMD", winStyle,
        rect.left, rect.top,
        rect.right - rect.left, rect.bottom - rect.top,
        nullptr, nullptr,
        hInstance, this);

    if (!hwnd_)
        Err::Exit("Failed to create window.");

    // Don't call ShowWindow() here.  Postpone showing window until
    // MMD model setup finished.
}

void AppMain::createDrawable() {
    if (!hwnd_) {
        Err::Exit("Internal error: createDrawable() must be called after createWindow()");
    }
    constexpr auto failif = [](HRESULT hr, auto&& ...errMsg) {
        if (FAILED(hr))
            Err::Exit(std::forward<decltype(errMsg)>(errMsg)...);
    };

    HRESULT hr;

    UINT createFlags = D3D11_CREATE_DEVICE_SINGLETHREADED |
        D3D11_CREATE_DEVICE_BGRA_SUPPORT;
#ifdef _DEBUG
    createFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif
    hr = D3D11CreateDevice(
            nullptr,
            D3D_DRIVER_TYPE_HARDWARE,
            nullptr,
            createFlags,
            nullptr, 0, // Use highest available feature level
            D3D11_SDK_VERSION,
            d3Device_.GetAddressOf(),
            nullptr,
            deviceContext_.GetAddressOf());
    failif(hr, "Failed to create d3d11 device");

    hr = d3Device_.As(&dxgiDevice_);
    failif(hr, "device_.As() failed:", __FILE__, __LINE__);

    hr = CreateDXGIFactory2(0, __uuidof(dxFactory_.Get()),
            reinterpret_cast<void **>(dxFactory_.GetAddressOf()));
    failif(hr, "Failed to create DXGIFactory2");

    glm::vec2 size(GetWindowSize());
    DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
    swapChainDesc.Width = static_cast<UINT>(size.x);
    swapChainDesc.Height = static_cast<UINT>(size.y);
    swapChainDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
    swapChainDesc.BufferCount = 2;
    swapChainDesc.SampleDesc.Count = 1;
    swapChainDesc.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;

    hr = dxFactory_->CreateSwapChainForComposition(
            dxgiDevice_.Get(), &swapChainDesc,
            nullptr, swapChain_.GetAddressOf());
    failif(hr, "Failed to create swap chain.");

    hr = swapChain_->GetBuffer(0, __uuidof(renderTarget_.Get()),
            reinterpret_cast<void **>(renderTarget_.GetAddressOf()));
    failif(hr, "Failed to get buffer from swap chain.");

    hr = d3Device_->CreateRenderTargetView(renderTarget_.Get(), nullptr,
            renderTargetView_.GetAddressOf());
    failif(hr, "Failed to get render target view.");

    D3D11_TEXTURE2D_DESC stencilDesc = {
        .Width = static_cast<UINT>(size.x),
        .Height = static_cast<UINT>(size.y),
        .MipLevels = 1,
        .ArraySize = 1,
        .Format = DXGI_FORMAT_D24_UNORM_S8_UINT,
        .SampleDesc = swapChainDesc.SampleDesc,
        .Usage = D3D11_USAGE_DEFAULT,
        .BindFlags = D3D11_BIND_DEPTH_STENCIL,
    };
    hr = d3Device_->CreateTexture2D(
            &stencilDesc, nullptr, depthStencilBuffer_.GetAddressOf());
    failif(hr, "Failed to create depth stencil buffer.");

    D3D11_DEPTH_STENCIL_VIEW_DESC stencilViewDesc = {
        .Format = stencilDesc.Format,
        .ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2DMS,
    };
    hr = d3Device_->CreateDepthStencilView(
            reinterpret_cast<ID3D11Resource*>(depthStencilBuffer_.Get()),
            &stencilViewDesc,
            depthStencilView_.GetAddressOf());
    failif(hr, "Failed to create depth stencil view.");

    hr = DCompositionCreateDevice(
       dxgiDevice_.Get(),
       __uuidof(dcompDevice_.Get()),
       reinterpret_cast<void **>(dcompDevice_.GetAddressOf()));
    failif(hr, "Failed to create DirectComposition device.");

    hr = dcompDevice_->CreateTargetForHwnd(
            hwnd_, true, dcompTarget_.GetAddressOf());
    failif(hr, "Failed to DirectComposition render target.");

    hr = dcompDevice_->CreateVisual(dcompVisual_.GetAddressOf());
    failif(hr, "Failed to create DirectComposition visual object.");

    dcompVisual_->SetContent(swapChain_.Get());
    dcompTarget_->SetRoot(dcompVisual_.Get());
}

void AppMain::createTaskbar() {
    auto iconData = Resource::getStatusIconData();
    hTaskbarIcon_ = CreateIconFromResource(
            const_cast<PBYTE>(iconData.data()),
            iconData.length(),
            TRUE,
            0x00030000);
    if (!hTaskbarIcon_) {
        Err::Log("Failed to load icon. Fallback to Windows' default application icon.");
        hTaskbarIcon_ = LoadIconA(nullptr, IDI_APPLICATION);
        if (!hTaskbarIcon_) {
            Err::Exit("Icon fallback failed.");
        }
    }

    taskbarIconDesc_.cbSize = sizeof(taskbarIconDesc_);
    taskbarIconDesc_.hWnd = hwnd_;
    taskbarIconDesc_.uID = 100;  // TODO: What value should be here?
    taskbarIconDesc_.hIcon = hTaskbarIcon_;
    taskbarIconDesc_.uVersion = NOTIFYICON_VERSION_4;
    taskbarIconDesc_.uCallbackMessage = YOMMD_WM_SHOW_TASKBAR_MENU;
    wcscpy_s(taskbarIconDesc_.szTip,
            sizeof(taskbarIconDesc_.szTip), L"yoMMD");
    taskbarIconDesc_.uFlags =
        NIF_ICON | NIF_TIP | NIF_SHOWTIP | NIF_MESSAGE;

    Shell_NotifyIconW(NIM_ADD, &taskbarIconDesc_);
}

DWORD WINAPI AppMain::showMenu(LPVOID param) {
    constexpr DWORD winStyle = WS_CHILD;
    constexpr PCWSTR wcName = L"yoMMD-menu-window";
    AppMain& app = *reinterpret_cast<AppMain*>(param);
    const HWND& parentWin = app.hwnd_;
    HWND hwnd;

    const LONG parentWinExStyle = GetWindowLongW(parentWin, GWL_EXSTYLE);
    if (parentWinExStyle == 0) {
        Info::Log("Failed to get parent window's style");
    }

    // TODO: Register once
    WNDCLASSW wc = {};

    wc.style         = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
    wc.lpfnWndProc   = DefWindowProcW,
    wc.hInstance     = GetModuleHandleW(nullptr);
    wc.lpszClassName = wcName;
    wc.hIcon         = LoadIcon(nullptr, IDI_WINLOGO);
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);

    RegisterClassW(&wc);

    hwnd = CreateWindowExW(
            0, wcName, L"", winStyle, 0, 0, 0, 0, parentWin, nullptr,
            GetModuleHandleW(nullptr), parentWin
            );
    if (!hwnd) {
        Err::Log("Failed to create dummy window for menu.");
        return 1;
    }

    POINT point;
    if (!GetCursorPos(&point)) {
        Err::Log("Failed to get mouse point");
        DestroyWindow(hwnd);
        UnregisterClassW(wcName, GetModuleHandleW(nullptr));
        return 1;
    }

#define Cmd(identifier) static_cast<std::underlying_type<Command>::type>(Command::identifier)
    enum class Command : UINT_PTR {
        None,
        EnableMouse,
        ResetPosition,
        Quit,
    };

    HMENU hmenu = CreatePopupMenu();
    AppendMenuW(hmenu, MF_STRING, Cmd(EnableMouse), L"&Enable Mouse");
    AppendMenuW(hmenu, MF_STRING, Cmd(ResetPosition), L"&Reset Position");
    AppendMenuW(hmenu, MF_SEPARATOR, Cmd(None), L"");
    AppendMenuW(hmenu, MF_STRING, Cmd(Quit), L"&Quit");

    if (parentWinExStyle == 0) {
        EnableMenuItem(hmenu, Cmd(EnableMouse), MF_DISABLED);
    } else if (parentWinExStyle & WS_EX_TRANSPARENT) {
        CheckMenuItem(hmenu, Cmd(EnableMouse), MF_UNCHECKED);
    } else {
        CheckMenuItem(hmenu, Cmd(EnableMouse), MF_CHECKED);
    }

    UINT menuFlags = TPM_RIGHTBUTTON | TPM_NONOTIFY | TPM_RETURNCMD;

    SetForegroundWindow(hwnd);
    auto cmdID = TrackPopupMenu(
            hmenu, menuFlags, point.x, point.y, 0, hwnd, NULL);

    switch (cmdID) {
    case Cmd(EnableMouse):
        if (parentWinExStyle != 0) {
            SetWindowLongW(parentWin, GWL_EXSTYLE,
                    parentWinExStyle ^ WS_EX_TRANSPARENT);
            // Call SetWindowPos() function?
        }
        break;
    case Cmd(ResetPosition):
        app.routine_.ResetModelPosition();
        break;
    case Cmd(Quit):
        SendMessageW(parentWin, WM_DESTROY, 0, 0);
        break;
    }

    DestroyWindow(hwnd);
#undef Cmd

    UnregisterClassW(wcName, GetModuleHandleW(nullptr));
    DestroyMenu(hmenu);
    return 0;
}

LRESULT CALLBACK AppMain::windowProc(
        HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    using This_T = AppMain;
    This_T *pThis = nullptr;

    if (uMsg == WM_NCCREATE) {
        CREATESTRUCT* pCreate =
            reinterpret_cast<CREATESTRUCT *>(lParam);
        pThis = static_cast<This_T *>(pCreate->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)pThis);

        pThis->hwnd_ = hwnd;
    } else {
        pThis = reinterpret_cast<This_T *>(GetWindowLongPtrW(
                    hwnd, GWLP_USERDATA));
    }

    if (pThis) {
        return pThis->handleMessage(uMsg, wParam, lParam);
    } else {
        return DefWindowProcW(hwnd, uMsg, wParam, lParam);
    }
}

LRESULT AppMain::handleMessage(UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
    case WM_DESTROY:
        PostQuitMessage(0);
        isRunning_ = false;
        return 0;
    case WM_LBUTTONDOWN:
        routine_.OnMouseDown();
        return 0;
    case WM_MOUSEMOVE:
        if (wParam & MK_LBUTTON) {
            routine_.OnMouseDragged();
        }
        return 0;
    case WM_MOUSEWHEEL:
        {
            const int deltaDeg =
                GET_WHEEL_DELTA_WPARAM(wParam) * WHEEL_DELTA;
            const float delta =
                static_cast<float>(deltaDeg) / 360.0f;
            routine_.OnWheelScrolled(delta);
            return 0;
        }
    case YOMMD_WM_SHOW_TASKBAR_MENU:
        if (const auto msg = LOWORD(lParam);
                !(msg == WM_LBUTTONDOWN || msg == WM_RBUTTONDOWN))
            return 0;
        // Fallthrough
    case WM_RBUTTONDOWN:
        {
            DWORD exitCode;
            if (GetExitCodeThread(hMenuThread_, &exitCode) &&
                    exitCode == STILL_ACTIVE) {
                Info::Log("Thread is running");
                return 0;
            }

            hMenuThread_ = CreateThread(
                    NULL, 0, AppMain::showMenu, this, 0, NULL);
            return 0;
        }
    default:
        return DefWindowProc(hwnd_, uMsg, wParam, lParam);
    }
}

namespace Context {
sg_context_desc getSokolContext() {
    return globals::appMain.GetSokolContext();
}
glm::vec2 getWindowSize() {
    return globals::appMain.GetWindowSize();
}
glm::vec2 getDrawableSize() {
    return globals::appMain.GetDrawableSize();
}
glm::vec2 getMousePosition() {
    POINT pos;
    if (!GetCursorPos(&pos))
        return glm::vec2();
    glm::vec2 size(getWindowSize());
    return glm::vec2(pos.x, size.y - pos.y);  // Make origin bottom-left.
}
}

namespace {
const void *getRenderTargetView() {
    return reinterpret_cast<const void *>(
            globals::appMain.GetRenderTargetView());
}
const void *getDepthStencilView() {
    return reinterpret_cast<const void *>(
            globals::appMain.GetDepthStencilView());
}
}

int WINAPI WinMain(
        HINSTANCE hInstance, HINSTANCE, LPSTR pCmdLine, int nCmdShow) {
    (void)hInstance;
    (void)pCmdLine;
    (void)nCmdShow;

    int argc = 0;
    LPWSTR cmdline = GetCommandLineW();
    LPWSTR *argv = CommandLineToArgvW(cmdline, &argc);

    std::vector<std::string> args;
    for (int i = 0; i < argc; ++i) {
        args.push_back(String::wideToMulti<char>(argv[i]));
    }

    const auto cmdArgs = CmdArgs::Parse(args);

    args.clear();

    globals::appMain.Setup(cmdArgs);

    MSG msg = {};
    constexpr double millSecPerFrame = 1000.0 / Constant::FPS;
    uint64_t timeLastFrame = stm_now();
    for (;;) {
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (!globals::appMain.IsRunning())
            break;

        globals::appMain.UpdateDisplay();

        const double elapsedMillSec = stm_ms(stm_since(timeLastFrame));
        const auto shouldSleepFor = millSecPerFrame - elapsedMillSec;
        timeLastFrame = stm_now();

        if (shouldSleepFor > 0 &&
                static_cast<DWORD>(shouldSleepFor) > 0) {
            Sleep(static_cast<DWORD>(shouldSleepFor));
        }
    }
    globals::appMain.Terminate();

    return 0;
}
