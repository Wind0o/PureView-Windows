#include "viewer_window.hpp"

#include "../resources/resource.h"

#include <d2d1helper.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <shlobj_core.h>
#include <shlwapi.h>
#include <windowsx.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cwctype>
#include <filesystem>
#include <iterator>
#include <system_error>
#include <thread>
#include <unordered_set>
#include <utility>

namespace pureview {
namespace {

constexpr UINT kMessageCatalogReady = WM_APP + 42;
constexpr UINT_PTR kCursorTimer = 1;
constexpr int kResizeBorder = 8;
constexpr int kChromeTop = 10;
constexpr int kChromeHeight = 36;
constexpr int kChromeButtonWidth = 36;
constexpr int kChromeWidth = 3 * kChromeButtonWidth + 12;

struct CatalogReadyMessage {
    std::uint64_t generation = 0;
    bool directoryRequest = false;
    std::wstring selectedPath;
    std::vector<std::wstring> images;
};

const std::unordered_set<std::wstring>& SupportedExtensions() {
    static const std::unordered_set<std::wstring> extensions = {
        L".jpg", L".jpeg", L".jpe", L".jfif",
        L".png", L".gif", L".tif", L".tiff", L".bmp", L".dib",
        L".ico", L".jxr", L".wdp", L".hdp", L".dds",
        L".webp", L".heic", L".heif", L".avif",
        L".jp2", L".j2k", L".jpf", L".jpx", L".jxl",
        L".dng", L".raw", L".arw", L".cr2", L".cr3", L".nef",
        L".nrw", L".orf", L".raf", L".rw2", L".pef", L".srw",
        L".x3f", L".3fr", L".erf", L".kdc", L".mos", L".mrw",
        L".psd", L".tga", L".hdr", L".exr",
    };
    return extensions;
}

std::wstring Lowercase(std::wstring value) {
    std::transform(value.begin(), value.end(), value.begin(), [](wchar_t character) {
        return static_cast<wchar_t>(std::towlower(character));
    });
    return value;
}

bool IsSupportedImagePath(const std::filesystem::path& path) {
    return SupportedExtensions().contains(Lowercase(path.extension().wstring()));
}

std::wstring NormalizePath(const std::wstring& rawPath) {
    std::error_code error;
    const auto absolute = std::filesystem::absolute(rawPath, error);
    return (error ? std::filesystem::path(rawPath) : absolute)
        .lexically_normal()
        .wstring();
}

bool NaturalPathLess(const std::wstring& lhs, const std::wstring& rhs) {
    const auto lhsName = std::filesystem::path(lhs).filename().wstring();
    const auto rhsName = std::filesystem::path(rhs).filename().wstring();
    return StrCmpLogicalW(lhsName.c_str(), rhsName.c_str()) < 0;
}

std::vector<std::wstring> ScanDirectory(const std::wstring& directory) {
    std::vector<std::wstring> images;
    std::error_code error;
    for (std::filesystem::directory_iterator iterator(
             directory,
             std::filesystem::directory_options::skip_permission_denied,
             error),
         end;
         !error && iterator != end;
         iterator.increment(error)) {
        const auto& entry = *iterator;
        if (!entry.is_regular_file(error) || error || !IsSupportedImagePath(entry.path())) {
            error.clear();
            continue;
        }
        const DWORD attributes = GetFileAttributesW(entry.path().c_str());
        if (attributes != INVALID_FILE_ATTRIBUTES
            && (attributes & (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM)) != 0) {
            continue;
        }
        images.push_back(entry.path().lexically_normal().wstring());
    }
    std::sort(images.begin(), images.end(), NaturalPathLess);
    return images;
}

D2D1_RECT_F AspectRect(
    float imageWidth,
    float imageHeight,
    float areaWidth,
    float areaHeight,
    bool fill) {
    if (imageWidth <= 0.0F || imageHeight <= 0.0F
        || areaWidth <= 0.0F || areaHeight <= 0.0F) {
        return D2D1::RectF(0.0F, 0.0F, areaWidth, areaHeight);
    }
    const float xScale = areaWidth / imageWidth;
    const float yScale = areaHeight / imageHeight;
    const float scale = fill ? std::max(xScale, yScale) : std::min(xScale, yScale);
    const float width = imageWidth * scale;
    const float height = imageHeight * scale;
    return D2D1::RectF(
        (areaWidth - width) / 2.0F,
        (areaHeight - height) / 2.0F,
        (areaWidth + width) / 2.0F,
        (areaHeight + height) / 2.0F);
}

D2D1_RECT_F ScaledRect(D2D1_RECT_F base, double zoom, D2D1_POINT_2F pan) {
    const float centerX = (base.left + base.right) / 2.0F + pan.x;
    const float centerY = (base.top + base.bottom) / 2.0F + pan.y;
    const float halfWidth = (base.right - base.left) * static_cast<float>(zoom) / 2.0F;
    const float halfHeight = (base.bottom - base.top) * static_cast<float>(zoom) / 2.0F;
    return D2D1::RectF(
        centerX - halfWidth,
        centerY - halfHeight,
        centerX + halfWidth,
        centerY + halfHeight);
}

void SetRoundedPreference(HWND window, bool fullscreen) {
    constexpr DWORD cornerAttribute = 33;
    const int preference = fullscreen ? 1 : 2; // DWMWCP_DONOTROUND / DWMWCP_ROUND
    DwmSetWindowAttribute(
        window,
        static_cast<DWMWINDOWATTRIBUTE>(cornerAttribute),
        &preference,
        sizeof(preference));
}

} // namespace

ViewerWindow::~ViewerWindow() {
    pipeline_.reset();
    DiscardRenderTarget();
    d2dFactory_.Reset();
}

bool ViewerWindow::Create(HINSTANCE instance) {
    instance_ = instance;

    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    windowClass.lpfnWndProc = WindowProcedure;
    windowClass.hInstance = instance;
    windowClass.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(IDI_PUREVIEW));
    windowClass.hIconSm = windowClass.hIcon;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hbrBackground = nullptr;
    windowClass.lpszClassName = kWindowClassName;
    if (!RegisterClassExW(&windowClass) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        return false;
    }

    const DWORD style = WS_POPUP | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX;
    window_ = CreateWindowExW(
        WS_EX_APPWINDOW | WS_EX_ACCEPTFILES,
        kWindowClassName,
        L"PureView",
        style,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        960,
        640,
        nullptr,
        nullptr,
        instance,
        this);
    return window_ != nullptr;
}

void ViewerWindow::Show(int command) {
    ShowWindow(window_, command);
    UpdateWindow(window_);
    SetForegroundWindow(window_);
}

void ViewerWindow::PromptToOpen() {
    ShowOpenDialog();
}

void ViewerWindow::OpenPath(const std::wstring& rawPath) {
    const std::wstring path = NormalizePath(rawPath);
    const DWORD attributes = GetFileAttributesW(path.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES) {
        MessageBoxW(window_, L"文件不存在或无法访问。", L"PureView", MB_OK | MB_ICONWARNING);
        return;
    }

    const bool isDirectory = (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
    if (!isDirectory && !IsSupportedImagePath(path)) {
        MessageBoxW(window_, L"这个文件扩展名不在 PureView 的图片列表中。", L"PureView", MB_OK | MB_ICONWARNING);
        return;
    }

    ++catalogGeneration_;
    if (isDirectory) {
        imagePaths_.clear();
        currentIndex_ = 0;
        StartCatalogScan(path, true);
    } else {
        imagePaths_ = {path};
        currentIndex_ = 0;
        RequestCurrentImage();
        StartCatalogScan(path, false);
    }
}

LRESULT CALLBACK ViewerWindow::WindowProcedure(
    HWND window,
    UINT message,
    WPARAM wParam,
    LPARAM lParam) {
    ViewerWindow* viewer = nullptr;
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        viewer = static_cast<ViewerWindow*>(create->lpCreateParams);
        viewer->window_ = window;
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(viewer));
    } else {
        viewer = reinterpret_cast<ViewerWindow*>(
            GetWindowLongPtrW(window, GWLP_USERDATA));
    }

    if (viewer) {
        const LRESULT result = viewer->HandleMessage(message, wParam, lParam);
        if (message == WM_NCDESTROY) {
            SetWindowLongPtrW(window, GWLP_USERDATA, 0);
            viewer->window_ = nullptr;
        }
        return result;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

LRESULT ViewerWindow::HandleMessage(UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_CREATE:
        if (!InitializeDeviceIndependentResources()) {
            return -1;
        }
        pipeline_ = std::make_unique<ImagePipeline>(window_);
        DragAcceptFiles(window_, TRUE);
        SetRoundedPreference(window_, false);
        return 0;

    case WM_NCCALCSIZE:
        return 0;

    case WM_NCHITTEST: {
        const POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        return HitTest(point);
    }

    case WM_NCLBUTTONDBLCLK:
        if (zoom_ > 1.001) {
            ResetZoom();
        } else {
            ToggleFullscreen();
        }
        return 0;

    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT:
        Paint();
        return 0;

    case WM_SIZE:
        if (renderTarget_) {
            renderTarget_->Resize(D2D1::SizeU(
                LOWORD(lParam),
                HIWORD(lParam)));
        }
        ConstrainPan();
        InvalidateRect(window_, nullptr, FALSE);
        return 0;

    case WM_DPICHANGED:
        if (!fullscreen_) {
            const auto* suggested = reinterpret_cast<RECT*>(lParam);
            SetWindowPos(
                window_,
                nullptr,
                suggested->left,
                suggested->top,
                suggested->right - suggested->left,
                suggested->bottom - suggested->top,
                SWP_NOZORDER | SWP_NOACTIVATE);
        }
        return 0;

    case WM_GETMINMAXINFO: {
        auto* info = reinterpret_cast<MINMAXINFO*>(lParam);
        info->ptMinTrackSize = {240, 160};
        return 0;
    }

    case WM_COMPACTING:
        if (pipeline_) {
            pipeline_->Purge();
        }
        return 0;

    case WM_MOUSEWHEEL: {
        POINT anchor{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        ScreenToClient(window_, &anchor);
        const auto action = scrollResolver_.Resolve(
            static_cast<double>(GET_WHEEL_DELTA_WPARAM(wParam)),
            (GET_KEYSTATE_WPARAM(wParam) & MK_CONTROL) != 0,
            std::chrono::steady_clock::now());
        switch (action.kind) {
        case ScrollActionKind::PreviousImage:
            Navigate(-1);
            break;
        case ScrollActionKind::NextImage:
            Navigate(1);
            break;
        case ScrollActionKind::Zoom:
            SetZoom(zoom_ * action.zoomMultiplier, anchor);
            break;
        case ScrollActionKind::None:
            break;
        }
        return 0;
    }

    case WM_MOUSEMOVE: {
        const POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        if (!trackingMouse_) {
            TRACKMOUSEEVENT tracking{
                sizeof(tracking),
                TME_LEAVE,
                window_,
                0,
            };
            TrackMouseEvent(&tracking);
            trackingMouse_ = true;
        }
        cursorHidden_ = false;
        SetCursor(LoadCursorW(nullptr, IDC_ARROW));

        RECT client{};
        GetClientRect(window_, &client);
        const bool nearTop = point.y >= 0 && point.y <= 58;
        SetChromeVisible(nearTop);
        KillTimer(window_, kCursorTimer);
        if (!nearTop) {
            SetTimer(window_, kCursorTimer, fullscreen_ ? 700 : 1250, nullptr);
        }

        if (draggingImage_) {
            pan_.x = panAtDragStart_.x + static_cast<float>(point.x - dragOrigin_.x);
            pan_.y = panAtDragStart_.y + static_cast<float>(point.y - dragOrigin_.y);
            ConstrainPan();
            InvalidateRect(window_, nullptr, FALSE);
        }
        return 0;
    }

    case WM_MOUSELEAVE:
        trackingMouse_ = false;
        SetChromeVisible(false);
        return 0;

    case WM_TIMER:
        if (wParam == kCursorTimer) {
            KillTimer(window_, kCursorTimer);
            cursorHidden_ = true;
            SetCursor(nullptr);
        }
        return 0;

    case WM_SETCURSOR:
        if (cursorHidden_ && LOWORD(lParam) == HTCLIENT) {
            SetCursor(nullptr);
            return TRUE;
        }
        break;

    case WM_LBUTTONDOWN: {
        SetFocus(window_);
        const POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        if (const int button = ChromeButtonAt(point); button >= 0) {
            InvokeChromeButton(button);
            return 0;
        }
        if (zoom_ > 1.001) {
            draggingImage_ = true;
            dragOrigin_ = point;
            panAtDragStart_ = pan_;
            SetCapture(window_);
            return 0;
        }
        POINT screenPoint = point;
        ClientToScreen(window_, &screenPoint);
        ReleaseCapture();
        SendMessageW(
            window_,
            WM_NCLBUTTONDOWN,
            HTCAPTION,
            MAKELPARAM(screenPoint.x, screenPoint.y));
        return 0;
    }

    case WM_LBUTTONUP:
        if (draggingImage_) {
            draggingImage_ = false;
            ReleaseCapture();
        }
        return 0;

    case WM_LBUTTONDBLCLK:
        if (zoom_ > 1.001) {
            ResetZoom();
        } else {
            ToggleFullscreen();
        }
        return 0;

    case WM_KEYDOWN: {
        RECT client{};
        GetClientRect(window_, &client);
        const POINT center{
            (client.right - client.left) / 2,
            (client.bottom - client.top) / 2,
        };
        switch (wParam) {
        case VK_LEFT:
        case VK_UP:
            Navigate(-1);
            return 0;
        case VK_RIGHT:
        case VK_DOWN:
        case VK_SPACE:
            Navigate(1);
            return 0;
        case VK_ESCAPE:
            if (zoom_ > 1.001) {
                ResetZoom();
            } else if (fullscreen_) {
                ToggleFullscreen();
            }
            return 0;
        case VK_F11:
        case 'F':
            ToggleFullscreen();
            return 0;
        case VK_ADD:
        case VK_OEM_PLUS:
            SetZoom(zoom_ * 1.2, center);
            return 0;
        case VK_SUBTRACT:
        case VK_OEM_MINUS:
            SetZoom(zoom_ / 1.2, center);
            return 0;
        case '0':
            ResetZoom();
            return 0;
        case 'O':
            if ((GetKeyState(VK_CONTROL) & 0x8000) != 0) {
                ShowOpenDialog();
                return 0;
            }
            break;
        default:
            break;
        }
        break;
    }

    case WM_DROPFILES: {
        const HDROP drop = reinterpret_cast<HDROP>(wParam);
        const UINT length = DragQueryFileW(drop, 0, nullptr, 0);
        std::wstring path(length + 1, L'\0');
        DragQueryFileW(drop, 0, path.data(), static_cast<UINT>(path.size()));
        path.resize(length);
        DragFinish(drop);
        OpenPath(path);
        return 0;
    }

    case WM_COPYDATA: {
        const auto* data = reinterpret_cast<COPYDATASTRUCT*>(lParam);
        if (data && data->lpData && data->cbData >= sizeof(wchar_t)) {
            const auto* characters = static_cast<const wchar_t*>(data->lpData);
            const std::size_t count = data->cbData / sizeof(wchar_t);
            std::wstring path(characters, characters + count);
            if (!path.empty() && path.back() == L'\0') {
                path.pop_back();
            }
            if (!path.empty()) {
                OpenPath(path);
            }
            ShowWindow(window_, SW_RESTORE);
            SetForegroundWindow(window_);
        }
        return TRUE;
    }

    case kMessageImageReady:
        ApplyImageMessage(lParam);
        return 0;

    case kMessageCatalogReady:
        ApplyCatalogMessage(lParam);
        return 0;

    case WM_CLOSE:
        DestroyWindow(window_);
        return 0;

    case WM_DESTROY:
        pipeline_.reset();
        PostQuitMessage(0);
        return 0;

    default:
        break;
    }
    return DefWindowProcW(window_, message, wParam, lParam);
}

bool ViewerWindow::InitializeDeviceIndependentResources() {
    D2D1_FACTORY_OPTIONS options{};
#if defined(_DEBUG)
    options.debugLevel = D2D1_DEBUG_LEVEL_INFORMATION;
#endif
    return SUCCEEDED(D2D1CreateFactory(
        D2D1_FACTORY_TYPE_SINGLE_THREADED,
        __uuidof(ID2D1Factory),
        &options,
        reinterpret_cast<void**>(d2dFactory_.GetAddressOf())));
}

bool ViewerWindow::EnsureRenderTarget() {
    if (renderTarget_) {
        return true;
    }
    RECT client{};
    GetClientRect(window_, &client);
    const D2D1_SIZE_U size = D2D1::SizeU(
        std::max(1L, client.right - client.left),
        std::max(1L, client.bottom - client.top));
    const HRESULT result = d2dFactory_->CreateHwndRenderTarget(
        D2D1::RenderTargetProperties(),
        D2D1::HwndRenderTargetProperties(window_, size, D2D1_PRESENT_OPTIONS_IMMEDIATELY),
        &renderTarget_);
    if (FAILED(result)) {
        return false;
    }
    CreateDeviceBitmap();
    return true;
}

void ViewerWindow::DiscardRenderTarget() {
    bitmap_.Reset();
    renderTarget_.Reset();
}

void ViewerWindow::CreateDeviceBitmap() {
    bitmap_.Reset();
    if (!renderTarget_ || !decodedImage_ || decodedImage_->pixels.empty()) {
        return;
    }
    const auto properties = D2D1::BitmapProperties(
        D2D1::PixelFormat(
            DXGI_FORMAT_B8G8R8A8_UNORM,
            D2D1_ALPHA_MODE_PREMULTIPLIED),
        96.0F,
        96.0F);
    renderTarget_->CreateBitmap(
        D2D1::SizeU(decodedImage_->pixelWidth, decodedImage_->pixelHeight),
        decodedImage_->pixels.data(),
        decodedImage_->stride,
        properties,
        &bitmap_);
}

void ViewerWindow::Paint() {
    PAINTSTRUCT paint{};
    BeginPaint(window_, &paint);
    if (!EnsureRenderTarget()) {
        EndPaint(window_, &paint);
        return;
    }

    const D2D1_SIZE_F size = renderTarget_->GetSize();
    renderTarget_->BeginDraw();
    renderTarget_->SetTransform(D2D1::Matrix3x2F::Identity());
    renderTarget_->Clear(D2D1::ColorF(0x10141C));

    if (bitmap_ && decodedImage_) {
        if (fullscreen_) {
            const auto background = AspectRect(
                static_cast<float>(decodedImage_->originalWidth),
                static_cast<float>(decodedImage_->originalHeight),
                size.width,
                size.height,
                true);
            renderTarget_->DrawBitmap(
                bitmap_.Get(),
                background,
                0.68F,
                D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);

            Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> dim;
            renderTarget_->CreateSolidColorBrush(
                D2D1::ColorF(0.0F, 0.0F, 0.0F, 0.42F),
                &dim);
            renderTarget_->FillRectangle(D2D1::RectF(0, 0, size.width, size.height), dim.Get());
        }

        const auto base = AspectRect(
            static_cast<float>(decodedImage_->originalWidth),
            static_cast<float>(decodedImage_->originalHeight),
            size.width,
            size.height,
            !fullscreen_);
        const auto destination = ScaledRect(base, zoom_, pan_);
        renderTarget_->DrawBitmap(
            bitmap_.Get(),
            destination,
            1.0F,
            D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
    }

    if (chromeVisible_) {
        const float panelRight = size.width - 10.0F;
        const float panelLeft = panelRight - static_cast<float>(kChromeWidth);
        const float panelTop = static_cast<float>(kChromeTop);
        const float panelBottom = panelTop + static_cast<float>(kChromeHeight);

        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> panelBrush;
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> iconBrush;
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> closeBrush;
        renderTarget_->CreateSolidColorBrush(D2D1::ColorF(0.02F, 0.03F, 0.05F, 0.70F), &panelBrush);
        renderTarget_->CreateSolidColorBrush(D2D1::ColorF(1.0F, 1.0F, 1.0F, 0.88F), &iconBrush);
        renderTarget_->CreateSolidColorBrush(D2D1::ColorF(0xE95454, 0.92F), &closeBrush);
        renderTarget_->FillRoundedRectangle(
            D2D1::RoundedRect(
                D2D1::RectF(panelLeft, panelTop, panelRight, panelBottom),
                10.0F,
                10.0F),
            panelBrush.Get());

        const float firstCenter = panelLeft + 12.0F + kChromeButtonWidth / 2.0F;
        const float centerY = (panelTop + panelBottom) / 2.0F;
        const float centers[] = {
            firstCenter,
            firstCenter + kChromeButtonWidth,
            firstCenter + 2.0F * kChromeButtonWidth,
        };
        renderTarget_->DrawLine(
            D2D1::Point2F(centers[0] - 5.0F, centerY + 3.0F),
            D2D1::Point2F(centers[0] + 5.0F, centerY + 3.0F),
            iconBrush.Get(),
            1.6F);
        renderTarget_->DrawRectangle(
            D2D1::RectF(centers[1] - 5.0F, centerY - 5.0F, centers[1] + 5.0F, centerY + 5.0F),
            iconBrush.Get(),
            1.5F);
        renderTarget_->FillEllipse(
            D2D1::Ellipse(D2D1::Point2F(centers[2], centerY), 9.0F, 9.0F),
            closeBrush.Get());
        renderTarget_->DrawLine(
            D2D1::Point2F(centers[2] - 3.5F, centerY - 3.5F),
            D2D1::Point2F(centers[2] + 3.5F, centerY + 3.5F),
            iconBrush.Get(),
            1.4F);
        renderTarget_->DrawLine(
            D2D1::Point2F(centers[2] + 3.5F, centerY - 3.5F),
            D2D1::Point2F(centers[2] - 3.5F, centerY + 3.5F),
            iconBrush.Get(),
            1.4F);
    }

    const HRESULT result = renderTarget_->EndDraw();
    if (result == D2DERR_RECREATE_TARGET) {
        DiscardRenderTarget();
    }
    EndPaint(window_, &paint);
}

void ViewerWindow::ShowOpenDialog() {
    Microsoft::WRL::ComPtr<IFileOpenDialog> dialog;
    if (FAILED(CoCreateInstance(
            CLSID_FileOpenDialog,
            nullptr,
            CLSCTX_INPROC_SERVER,
            IID_PPV_ARGS(&dialog)))) {
        return;
    }
    const COMDLG_FILTERSPEC filters[] = {
        {L"图片", L"*.jpg;*.jpeg;*.png;*.gif;*.tif;*.tiff;*.bmp;*.webp;*.heic;*.heif;*.avif;*.jxl;*.dng;*.raw;*.ico;*.jxr;*.dds;*.psd;*.tga;*.hdr;*.exr"},
        {L"所有文件", L"*.*"},
    };
    dialog->SetFileTypes(static_cast<UINT>(std::size(filters)), filters);
    dialog->SetTitle(L"用 PureView 打开图片");
    FILEOPENDIALOGOPTIONS options{};
    dialog->GetOptions(&options);
    dialog->SetOptions(options | FOS_FORCEFILESYSTEM | FOS_FILEMUSTEXIST);
    if (FAILED(dialog->Show(window_))) {
        return;
    }

    Microsoft::WRL::ComPtr<IShellItem> item;
    if (FAILED(dialog->GetResult(&item))) {
        return;
    }
    PWSTR path = nullptr;
    if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)) && path) {
        OpenPath(path);
        CoTaskMemFree(path);
    }
}

void ViewerWindow::StartCatalogScan(const std::wstring& path, bool isDirectory) {
    const std::uint64_t generation = catalogGeneration_;
    const HWND target = window_;
    std::thread([generation, target, path, isDirectory] {
        const std::wstring directory = isDirectory
            ? path
            : std::filesystem::path(path).parent_path().wstring();
        auto images = ScanDirectory(directory);
        if (!isDirectory
            && std::find(images.begin(), images.end(), path) == images.end()) {
            images.push_back(path);
            std::sort(images.begin(), images.end(), NaturalPathLess);
        }
        auto* result = new CatalogReadyMessage{
            generation,
            isDirectory,
            isDirectory ? std::wstring{} : path,
            std::move(images),
        };
        if (!PostMessageW(
                target,
                kMessageCatalogReady,
                0,
                reinterpret_cast<LPARAM>(result))) {
            delete result;
        }
    }).detach();
}

void ViewerWindow::ApplyCatalogMessage(LPARAM lParam) {
    std::unique_ptr<CatalogReadyMessage> message(
        reinterpret_cast<CatalogReadyMessage*>(lParam));
    if (!message || message->generation != catalogGeneration_) {
        return;
    }
    if (message->images.empty()) {
        if (message->directoryRequest) {
            MessageBoxW(window_, L"这个文件夹里没有可显示的图片。", L"PureView", MB_OK | MB_ICONINFORMATION);
        }
        return;
    }

    imagePaths_ = std::move(message->images);
    if (message->directoryRequest) {
        currentIndex_ = 0;
        RequestCurrentImage();
        return;
    }

    const auto selected = std::find(
        imagePaths_.begin(),
        imagePaths_.end(),
        message->selectedPath);
    currentIndex_ = selected == imagePaths_.end()
        ? 0
        : static_cast<int>(std::distance(imagePaths_.begin(), selected));
    PrefetchNeighbors();
}

void ViewerWindow::RequestCurrentImage(bool fullResolution) {
    if (!pipeline_ || currentIndex_ < 0
        || currentIndex_ >= static_cast<int>(imagePaths_.size())) {
        return;
    }
    if (!fullResolution) {
        ++requestGeneration_;
        fullResolutionRequested_ = false;
        pipeline_->CancelPrefetch();
    } else if (fullResolutionRequested_) {
        return;
    } else {
        fullResolutionRequested_ = true;
    }
    pipeline_->Request(
        imagePaths_[currentIndex_],
        fullResolution ? 0 : PreviewPixelSize(),
        requestGeneration_,
        fullResolution);
}

void ViewerWindow::ApplyImageMessage(LPARAM lParam) {
    std::unique_ptr<ImageReadyMessage> message(
        reinterpret_cast<ImageReadyMessage*>(lParam));
    if (!message || message->generation != requestGeneration_) {
        return;
    }
    if (!message->image) {
        if (message->preserveViewport) {
            fullResolutionRequested_ = false;
            return;
        }
        MessageBoxW(window_, L"Windows 图像解码器无法打开这张图片。可安装对应的 WIC 编解码器后重试。", L"PureView", MB_OK | MB_ICONWARNING);
        return;
    }

    if (!message->preserveViewport) {
        zoom_ = 1.0;
        pan_ = {0.0F, 0.0F};
    }
    decodedImage_ = std::move(message->image);
    if (!fullscreen_ && !message->preserveViewport) {
        FitWindowToImage(
            decodedImage_->originalWidth,
            decodedImage_->originalHeight);
    }
    CreateDeviceBitmap();
    ConstrainPan();
    InvalidateRect(window_, nullptr, FALSE);

    const auto title = std::filesystem::path(decodedImage_->path).filename().wstring()
        + L" — PureView";
    SetWindowTextW(window_, title.c_str());
    hasPresentedImage_ = true;
    ShowWindow(window_, SW_SHOWNORMAL);
    SetForegroundWindow(window_);

    if (!message->preserveViewport) {
        PrefetchNeighbors();
    }
}

void ViewerWindow::PrefetchNeighbors() {
    if (!pipeline_ || imagePaths_.empty()) {
        return;
    }
    std::vector<std::wstring> paths;
    for (const int index : PrefetchIndices(
             currentIndex_,
             static_cast<int>(imagePaths_.size()),
             lastDirection_)) {
        paths.push_back(imagePaths_[index]);
    }
    pipeline_->Prefetch(paths, PreviewPixelSize());
}

void ViewerWindow::Navigate(int offset) {
    const int next = currentIndex_ + offset;
    if (next < 0 || next >= static_cast<int>(imagePaths_.size())) {
        return;
    }
    currentIndex_ = next;
    lastDirection_ = offset < 0 ? -1 : 1;
    RequestCurrentImage();
}

void ViewerWindow::FitWindowToImage(UINT pixelWidth, UINT pixelHeight) {
    const HMONITOR monitor = MonitorFromWindow(window_, MONITOR_DEFAULTTONEAREST);
    MONITORINFO monitorInfo{sizeof(monitorInfo)};
    GetMonitorInfoW(monitor, &monitorInfo);
    const RECT work = monitorInfo.rcWork;
    const Rect screen{
        static_cast<double>(work.left),
        static_cast<double>(work.top),
        static_cast<double>(work.right - work.left),
        static_cast<double>(work.bottom - work.top),
    };

    RECT current{};
    GetWindowRect(window_, &current);
    const Rect previous{
        static_cast<double>(current.left),
        static_cast<double>(current.top),
        static_cast<double>(current.right - current.left),
        static_cast<double>(current.bottom - current.top),
    };
    const double backingScale = static_cast<double>(GetDpiForWindow(window_)) / 96.0;
    const bool preserve = hasPresentedImage_ && IsWindowVisible(window_);
    const Rect frame = PlanWindowFrame(
        {static_cast<double>(pixelWidth), static_cast<double>(pixelHeight)},
        backingScale,
        screen,
        preserve ? std::optional<Rect>(previous) : std::nullopt,
        preserve);
    SetWindowPos(
        window_,
        nullptr,
        static_cast<int>(frame.x),
        static_cast<int>(frame.y),
        static_cast<int>(frame.width),
        static_cast<int>(frame.height),
        SWP_NOZORDER | SWP_NOACTIVATE);
}

void ViewerWindow::SetZoom(double proposedZoom, POINT anchor) {
    const double newZoom = std::clamp(proposedZoom, 1.0, 32.0);
    if (std::abs(newZoom - zoom_) < 0.0001) {
        return;
    }
    RECT client{};
    GetClientRect(window_, &client);
    const double centerX = (client.right - client.left) / 2.0;
    const double centerY = (client.bottom - client.top) / 2.0;
    const double relativeX = anchor.x - centerX - pan_.x;
    const double relativeY = anchor.y - centerY - pan_.y;
    const double ratio = newZoom / zoom_;
    pan_.x = static_cast<float>(anchor.x - centerX - relativeX * ratio);
    pan_.y = static_cast<float>(anchor.y - centerY - relativeY * ratio);
    zoom_ = newZoom;
    ConstrainPan();
    InvalidateRect(window_, nullptr, FALSE);

    if (zoom_ >= 1.6 && decodedImage_ && !decodedImage_->fullResolution) {
        RequestCurrentImage(true);
    }
}

void ViewerWindow::ResetZoom() {
    zoom_ = 1.0;
    pan_ = {0.0F, 0.0F};
    InvalidateRect(window_, nullptr, FALSE);
}

void ViewerWindow::ConstrainPan() {
    if (zoom_ <= 1.0 || !decodedImage_) {
        pan_ = {0.0F, 0.0F};
        return;
    }
    RECT client{};
    GetClientRect(window_, &client);
    const float width = static_cast<float>(client.right - client.left);
    const float height = static_cast<float>(client.bottom - client.top);
    const auto base = AspectRect(
        static_cast<float>(decodedImage_->originalWidth),
        static_cast<float>(decodedImage_->originalHeight),
        width,
        height,
        !fullscreen_);
    const float scaledWidth = (base.right - base.left) * static_cast<float>(zoom_);
    const float scaledHeight = (base.bottom - base.top) * static_cast<float>(zoom_);
    const float maxX = std::max(0.0F, (scaledWidth - width) / 2.0F);
    const float maxY = std::max(0.0F, (scaledHeight - height) / 2.0F);
    pan_.x = std::clamp(pan_.x, -maxX, maxX);
    pan_.y = std::clamp(pan_.y, -maxY, maxY);
}

void ViewerWindow::ToggleFullscreen() {
    if (!fullscreen_) {
        GetWindowRect(window_, &windowedFrame_);
        const HMONITOR monitor = MonitorFromWindow(window_, MONITOR_DEFAULTTONEAREST);
        MONITORINFO info{sizeof(info)};
        GetMonitorInfoW(monitor, &info);
        fullscreen_ = true;
        SetWindowLongPtrW(window_, GWL_STYLE, WS_POPUP | WS_VISIBLE);
        SetRoundedPreference(window_, true);
        SetWindowPos(
            window_,
            HWND_TOP,
            info.rcMonitor.left,
            info.rcMonitor.top,
            info.rcMonitor.right - info.rcMonitor.left,
            info.rcMonitor.bottom - info.rcMonitor.top,
            SWP_FRAMECHANGED | SWP_SHOWWINDOW);
    } else {
        fullscreen_ = false;
        SetWindowLongPtrW(
            window_,
            GWL_STYLE,
            WS_POPUP | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX);
        SetRoundedPreference(window_, false);
        SetWindowPos(
            window_,
            nullptr,
            windowedFrame_.left,
            windowedFrame_.top,
            windowedFrame_.right - windowedFrame_.left,
            windowedFrame_.bottom - windowedFrame_.top,
            SWP_NOZORDER | SWP_FRAMECHANGED | SWP_SHOWWINDOW);
    }
    ConstrainPan();
    InvalidateRect(window_, nullptr, FALSE);
}

void ViewerWindow::SetChromeVisible(bool visible) {
    if (chromeVisible_ == visible) {
        return;
    }
    chromeVisible_ = visible;
    InvalidateRect(window_, nullptr, FALSE);
}

int ViewerWindow::ChromeButtonAt(POINT point) const {
    if (!chromeVisible_) {
        return -1;
    }
    RECT client{};
    GetClientRect(window_, &client);
    const int panelLeft = client.right - 10 - kChromeWidth;
    if (point.x < panelLeft + 12 || point.x >= client.right - 10
        || point.y < kChromeTop || point.y >= kChromeTop + kChromeHeight) {
        return -1;
    }
    const int button = (point.x - panelLeft - 12) / kChromeButtonWidth;
    return button >= 0 && button <= 2 ? button : -1;
}

void ViewerWindow::InvokeChromeButton(int button) {
    switch (button) {
    case 0:
        ShowWindow(window_, SW_MINIMIZE);
        break;
    case 1:
        ToggleFullscreen();
        break;
    case 2:
        DestroyWindow(window_);
        break;
    default:
        break;
    }
}

int ViewerWindow::HitTest(POINT screenPoint) const {
    if (fullscreen_) {
        return HTCLIENT;
    }
    RECT frame{};
    GetWindowRect(window_, &frame);
    const int border = MulDiv(kResizeBorder, static_cast<int>(GetDpiForWindow(window_)), 96);
    const bool left = screenPoint.x < frame.left + border;
    const bool right = screenPoint.x >= frame.right - border;
    const bool top = screenPoint.y < frame.top + border;
    const bool bottom = screenPoint.y >= frame.bottom - border;
    if (top && left) return HTTOPLEFT;
    if (top && right) return HTTOPRIGHT;
    if (bottom && left) return HTBOTTOMLEFT;
    if (bottom && right) return HTBOTTOMRIGHT;
    if (left) return HTLEFT;
    if (right) return HTRIGHT;
    if (top) return HTTOP;
    if (bottom) return HTBOTTOM;
    return HTCLIENT;
}

int ViewerWindow::PreviewPixelSize() const {
    const HMONITOR monitor = MonitorFromWindow(window_, MONITOR_DEFAULTTONEAREST);
    MONITORINFO info{sizeof(info)};
    GetMonitorInfoW(monitor, &info);
    return PreviewMaxPixelSize({
        static_cast<double>(info.rcMonitor.right - info.rcMonitor.left),
        static_cast<double>(info.rcMonitor.bottom - info.rcMonitor.top),
    });
}

} // namespace pureview
