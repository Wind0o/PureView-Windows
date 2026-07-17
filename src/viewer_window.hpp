#pragma once

#include "image_loader.hpp"
#include "pureview/core.hpp"

#include <d2d1.h>
#include <windows.h>
#include <wrl/client.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace pureview {

inline constexpr wchar_t kWindowClassName[] = L"PureView.Native.Window";

class ViewerWindow {
public:
    ViewerWindow() = default;
    ~ViewerWindow();

    ViewerWindow(const ViewerWindow&) = delete;
    ViewerWindow& operator=(const ViewerWindow&) = delete;

    bool Create(HINSTANCE instance);
    void Show(int command);
    void OpenPath(const std::wstring& path);
    void PromptToOpen();
    [[nodiscard]] HWND handle() const noexcept { return window_; }

    static LRESULT CALLBACK WindowProcedure(
        HWND window,
        UINT message,
        WPARAM wParam,
        LPARAM lParam);

private:
    LRESULT HandleMessage(UINT message, WPARAM wParam, LPARAM lParam);
    bool InitializeDeviceIndependentResources();
    bool EnsureRenderTarget();
    void DiscardRenderTarget();
    void CreateDeviceBitmap();
    void Paint();

    void ShowOpenDialog();
    void StartCatalogScan(const std::wstring& path, bool isDirectory);
    void ApplyCatalogMessage(LPARAM lParam);
    void RequestCurrentImage(bool fullResolution = false);
    void ApplyImageMessage(LPARAM lParam);
    void PrefetchNeighbors();
    void Navigate(int offset);
    void FitWindowToImage(UINT pixelWidth, UINT pixelHeight);

    void SetZoom(double proposedZoom, POINT anchor);
    void ResetZoom();
    void ConstrainPan();
    void ToggleFullscreen();
    void SetChromeVisible(bool visible);
    [[nodiscard]] int ChromeButtonAt(POINT point) const;
    void InvokeChromeButton(int button);
    [[nodiscard]] int HitTest(POINT screenPoint) const;
    [[nodiscard]] int PreviewPixelSize() const;

    HWND window_ = nullptr;
    HINSTANCE instance_ = nullptr;
    std::unique_ptr<ImagePipeline> pipeline_;

    Microsoft::WRL::ComPtr<ID2D1Factory> d2dFactory_;
    Microsoft::WRL::ComPtr<ID2D1HwndRenderTarget> renderTarget_;
    Microsoft::WRL::ComPtr<ID2D1Bitmap> bitmap_;
    std::shared_ptr<DecodedImage> decodedImage_;

    std::vector<std::wstring> imagePaths_;
    int currentIndex_ = 0;
    int lastDirection_ = 1;
    std::uint64_t requestGeneration_ = 0;
    std::uint64_t catalogGeneration_ = 0;
    bool fullResolutionRequested_ = false;
    bool hasPresentedImage_ = false;

    ScrollIntentResolver scrollResolver_;
    double zoom_ = 1.0;
    D2D1_POINT_2F pan_{0.0F, 0.0F};
    POINT dragOrigin_{};
    D2D1_POINT_2F panAtDragStart_{};
    bool draggingImage_ = false;

    bool chromeVisible_ = false;
    bool trackingMouse_ = false;
    bool cursorHidden_ = false;
    bool fullscreen_ = false;
    RECT windowedFrame_{};
};

} // namespace pureview
