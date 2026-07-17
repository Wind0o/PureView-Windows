#include "image_loader.hpp"

#include "pureview/core.hpp"

#include <wincodec.h>
#include <wrl/client.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <propvarutil.h>
#include <utility>

namespace pureview {
namespace {

using Microsoft::WRL::ComPtr;

std::uint16_t ReadOrientation(IWICBitmapFrameDecode* frame) {
    ComPtr<IWICMetadataQueryReader> reader;
    if (FAILED(frame->GetMetadataQueryReader(&reader))) {
        return 1;
    }

    constexpr const wchar_t* queries[] = {
        L"/app1/ifd/{ushort=274}",
        L"/ifd/{ushort=274}",
    };
    for (const auto* query : queries) {
        PROPVARIANT value;
        PropVariantInit(&value);
        const HRESULT result = reader->GetMetadataByName(query, &value);
        if (SUCCEEDED(result)) {
            std::uint16_t orientation = 1;
            if (value.vt == VT_UI2) {
                orientation = value.uiVal;
            } else if (value.vt == VT_UI4) {
                orientation = static_cast<std::uint16_t>(value.ulVal);
            }
            PropVariantClear(&value);
            if (orientation >= 1 && orientation <= 8) {
                return orientation;
            }
        } else {
            PropVariantClear(&value);
        }
    }
    return 1;
}

WICBitmapTransformOptions TransformForOrientation(std::uint16_t orientation) {
    switch (orientation) {
    case 2:
        return WICBitmapTransformFlipHorizontal;
    case 3:
        return WICBitmapTransformRotate180;
    case 4:
        return WICBitmapTransformFlipVertical;
    case 5:
        return static_cast<WICBitmapTransformOptions>(
            WICBitmapTransformRotate90 | WICBitmapTransformFlipHorizontal);
    case 6:
        return WICBitmapTransformRotate90;
    case 7:
        return static_cast<WICBitmapTransformOptions>(
            WICBitmapTransformRotate270 | WICBitmapTransformFlipHorizontal);
    case 8:
        return WICBitmapTransformRotate270;
    default:
        return WICBitmapTransformRotate0;
    }
}

std::size_t PhysicalMemoryBytes() {
    MEMORYSTATUSEX status{};
    status.dwLength = sizeof(status);
    if (GlobalMemoryStatusEx(&status)) {
        return static_cast<std::size_t>(
            std::min<std::uint64_t>(
                status.ullTotalPhys,
                std::numeric_limits<std::size_t>::max()));
    }
    return 8ULL * 1024ULL * 1024ULL * 1024ULL;
}

} // namespace

ImagePipeline::ImagePipeline(HWND targetWindow)
    : targetWindow_(targetWindow),
      cacheLimitBytes_(CacheLimitBytes(PhysicalMemoryBytes())) {
    const unsigned int concurrency = std::thread::hardware_concurrency();
    const unsigned int workerCount = concurrency >= 10 ? 3U : 2U;
    workers_.reserve(workerCount);
    for (unsigned int index = 0; index < workerCount; ++index) {
        workers_.emplace_back([this] { WorkerLoop(); });
    }
}

ImagePipeline::~ImagePipeline() {
    {
        std::lock_guard lock(mutex_);
        stopping_ = true;
        tasks_.clear();
    }
    condition_.notify_all();
    for (auto& worker : workers_) {
        if (worker.joinable()) {
            worker.join();
        }
    }
}

void ImagePipeline::Request(
    const std::wstring& path,
    int maxPixelSize,
    std::uint64_t generation,
    bool preserveViewport) {
    const std::wstring key = CacheKey(path, maxPixelSize);
    if (auto cached = FindCached(key)) {
        auto* message = new ImageReadyMessage{
            generation,
            preserveViewport,
            std::move(cached),
            S_OK,
        };
        PostReady(message);
        return;
    }

    {
        std::lock_guard lock(mutex_);
        std::erase_if(tasks_, [](const Task& task) {
            return task.notify;
        });
        tasks_.push_front({
            path,
            maxPixelSize,
            generation,
            true,
            preserveViewport,
        });
    }
    condition_.notify_one();
}

void ImagePipeline::Prefetch(const std::vector<std::wstring>& paths, int maxPixelSize) {
    std::lock_guard lock(mutex_);
    std::erase_if(tasks_, [](const Task& task) {
        return !task.notify;
    });
    for (const auto& path : paths) {
        const std::wstring key = CacheKey(path, maxPixelSize);
        if (cache_.contains(key)) {
            continue;
        }
        const bool alreadyQueued = std::any_of(
            tasks_.begin(),
            tasks_.end(),
            [&](const Task& task) {
                return !task.notify
                    && task.maxPixelSize == maxPixelSize
                    && task.path == path;
            });
        if (!alreadyQueued) {
            tasks_.push_back({path, maxPixelSize, 0, false, false});
        }
    }
    condition_.notify_all();
}

void ImagePipeline::CancelPrefetch() {
    std::lock_guard lock(mutex_);
    std::erase_if(tasks_, [](const Task& task) {
        return !task.notify;
    });
}

void ImagePipeline::Purge() {
    std::lock_guard lock(mutex_);
    cache_.clear();
    cacheBytes_ = 0;
}

void ImagePipeline::WorkerLoop() {
    const HRESULT comResult = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    for (;;) {
        Task task;
        {
            std::unique_lock lock(mutex_);
            condition_.wait(lock, [this] {
                return stopping_ || !tasks_.empty();
            });
            if (stopping_) {
                break;
            }
            task = std::move(tasks_.front());
            tasks_.pop_front();
        }

        const std::wstring key = CacheKey(task.path, task.maxPixelSize);
        auto image = FindCached(key);
        HRESULT error = S_OK;
        if (!image) {
            image = Decode(task.path, task.maxPixelSize, error);
            if (image) {
                StoreCached(key, image);
            }
        }

        if (task.notify) {
            auto* message = new ImageReadyMessage{
                task.generation,
                task.preserveViewport,
                std::move(image),
                error,
            };
            PostReady(message);
        }
    }
    if (SUCCEEDED(comResult)) {
        CoUninitialize();
    }
}

void ImagePipeline::PostReady(ImageReadyMessage* message) const {
    if (!PostMessageW(
            targetWindow_,
            kMessageImageReady,
            0,
            reinterpret_cast<LPARAM>(message))) {
        delete message;
    }
}

std::wstring ImagePipeline::CacheKey(const std::wstring& path, int maxPixelSize) const {
    return path + L"|" + std::to_wstring(maxPixelSize);
}

std::shared_ptr<DecodedImage> ImagePipeline::FindCached(const std::wstring& key) {
    std::lock_guard lock(mutex_);
    const auto found = cache_.find(key);
    if (found == cache_.end()) {
        return {};
    }
    found->second.access = ++accessCounter_;
    return found->second.image;
}

void ImagePipeline::StoreCached(
    const std::wstring& key,
    std::shared_ptr<DecodedImage> image) {
    const std::size_t cost = image ? image->pixels.size() : 0;
    if (cost > cacheLimitBytes_) {
        return;
    }
    std::lock_guard lock(mutex_);

    if (const auto found = cache_.find(key); found != cache_.end()) {
        cacheBytes_ -= found->second.cost;
        cache_.erase(found);
    }

    while (!cache_.empty() && cacheBytes_ + cost > cacheLimitBytes_) {
        const auto oldest = std::min_element(
            cache_.begin(),
            cache_.end(),
            [](const auto& lhs, const auto& rhs) {
                return lhs.second.access < rhs.second.access;
            });
        cacheBytes_ -= oldest->second.cost;
        cache_.erase(oldest);
    }

    cache_.emplace(key, CacheEntry{std::move(image), ++accessCounter_, cost});
    cacheBytes_ += cost;
}

std::shared_ptr<DecodedImage> ImagePipeline::Decode(
    const std::wstring& path,
    int maxPixelSize,
    HRESULT& error) {
    ComPtr<IWICImagingFactory> factory;
    error = CoCreateInstance(
        CLSID_WICImagingFactory,
        nullptr,
        CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&factory));
    if (FAILED(error)) {
        return {};
    }

    ComPtr<IWICBitmapDecoder> decoder;
    error = factory->CreateDecoderFromFilename(
        path.c_str(),
        nullptr,
        GENERIC_READ,
        WICDecodeMetadataCacheOnDemand,
        &decoder);
    if (FAILED(error)) {
        return {};
    }

    ComPtr<IWICBitmapFrameDecode> frame;
    error = decoder->GetFrame(0, &frame);
    if (FAILED(error)) {
        return {};
    }

    ComPtr<IWICBitmapSource> orientedSource;
    const std::uint16_t orientation = ReadOrientation(frame.Get());
    const auto transform = TransformForOrientation(orientation);
    if (transform != WICBitmapTransformRotate0) {
        ComPtr<IWICBitmapFlipRotator> rotator;
        error = factory->CreateBitmapFlipRotator(&rotator);
        if (FAILED(error)) {
            return {};
        }
        error = rotator->Initialize(frame.Get(), transform);
        if (FAILED(error)) {
            return {};
        }
        orientedSource = rotator;
    } else {
        orientedSource = frame;
    }

    UINT originalWidth = 0;
    UINT originalHeight = 0;
    error = orientedSource->GetSize(&originalWidth, &originalHeight);
    if (FAILED(error) || originalWidth == 0 || originalHeight == 0) {
        return {};
    }

    UINT targetWidth = originalWidth;
    UINT targetHeight = originalHeight;
    const UINT longestEdge = std::max(originalWidth, originalHeight);
    if (maxPixelSize > 0 && longestEdge > static_cast<UINT>(maxPixelSize)) {
        const double scale = static_cast<double>(maxPixelSize)
            / static_cast<double>(longestEdge);
        targetWidth = std::max(1U, static_cast<UINT>(std::round(originalWidth * scale)));
        targetHeight = std::max(1U, static_cast<UINT>(std::round(originalHeight * scale)));
    }

    ComPtr<IWICBitmapSource> scaledSource = orientedSource;
    ComPtr<IWICBitmapScaler> scaler;
    if (targetWidth != originalWidth || targetHeight != originalHeight) {
        error = factory->CreateBitmapScaler(&scaler);
        if (FAILED(error)) {
            return {};
        }
        error = scaler->Initialize(
            orientedSource.Get(),
            targetWidth,
            targetHeight,
            WICBitmapInterpolationModeFant);
        if (FAILED(error)) {
            return {};
        }
        scaledSource = scaler;
    }

    ComPtr<IWICFormatConverter> converter;
    error = factory->CreateFormatConverter(&converter);
    if (FAILED(error)) {
        return {};
    }
    error = converter->Initialize(
        scaledSource.Get(),
        GUID_WICPixelFormat32bppPBGRA,
        WICBitmapDitherTypeNone,
        nullptr,
        0.0,
        WICBitmapPaletteTypeCustom);
    if (FAILED(error)) {
        return {};
    }

    const std::uint64_t stride64 = static_cast<std::uint64_t>(targetWidth) * 4ULL;
    const std::uint64_t byteCount64 = stride64 * static_cast<std::uint64_t>(targetHeight);
    if (stride64 > std::numeric_limits<UINT>::max()
        || byteCount64 > std::numeric_limits<UINT>::max()
        || byteCount64 > std::numeric_limits<std::size_t>::max()) {
        error = E_OUTOFMEMORY;
        return {};
    }

    auto decoded = std::make_shared<DecodedImage>();
    decoded->path = path;
    decoded->originalWidth = originalWidth;
    decoded->originalHeight = originalHeight;
    decoded->pixelWidth = targetWidth;
    decoded->pixelHeight = targetHeight;
    decoded->stride = static_cast<UINT>(stride64);
    decoded->fullResolution = targetWidth == originalWidth && targetHeight == originalHeight;
    decoded->pixels.resize(static_cast<std::size_t>(byteCount64));

    error = converter->CopyPixels(
        nullptr,
        decoded->stride,
        static_cast<UINT>(decoded->pixels.size()),
        decoded->pixels.data());
    if (FAILED(error)) {
        return {};
    }
    return decoded;
}

} // namespace pureview
