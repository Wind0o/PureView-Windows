#pragma once

#include <windows.h>

#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace pureview {

constexpr UINT kMessageImageReady = WM_APP + 41;

struct DecodedImage {
    std::wstring path;
    UINT originalWidth = 0;
    UINT originalHeight = 0;
    UINT pixelWidth = 0;
    UINT pixelHeight = 0;
    UINT stride = 0;
    bool fullResolution = false;
    std::vector<std::uint8_t> pixels;
};

struct ImageReadyMessage {
    std::uint64_t generation = 0;
    bool preserveViewport = false;
    std::shared_ptr<DecodedImage> image;
    HRESULT error = S_OK;
};

class ImagePipeline {
public:
    explicit ImagePipeline(HWND targetWindow);
    ~ImagePipeline();

    ImagePipeline(const ImagePipeline&) = delete;
    ImagePipeline& operator=(const ImagePipeline&) = delete;

    void Request(
        const std::wstring& path,
        int maxPixelSize,
        std::uint64_t generation,
        bool preserveViewport);
    void Prefetch(const std::vector<std::wstring>& paths, int maxPixelSize);
    void CancelPrefetch();
    void Purge();

    [[nodiscard]] std::size_t cacheLimitBytes() const noexcept {
        return cacheLimitBytes_;
    }

private:
    struct Task {
        std::wstring path;
        int maxPixelSize = 0;
        std::uint64_t generation = 0;
        bool notify = false;
        bool preserveViewport = false;
    };

    struct CacheEntry {
        std::shared_ptr<DecodedImage> image;
        std::uint64_t access = 0;
        std::size_t cost = 0;
    };

    void WorkerLoop();
    void PostReady(ImageReadyMessage* message) const;
    [[nodiscard]] std::wstring CacheKey(const std::wstring& path, int maxPixelSize) const;
    [[nodiscard]] std::shared_ptr<DecodedImage> FindCached(const std::wstring& key);
    void StoreCached(const std::wstring& key, std::shared_ptr<DecodedImage> image);
    [[nodiscard]] static std::shared_ptr<DecodedImage> Decode(
        const std::wstring& path,
        int maxPixelSize,
        HRESULT& error);

    HWND targetWindow_ = nullptr;
    std::size_t cacheLimitBytes_ = 0;
    std::size_t cacheBytes_ = 0;
    std::uint64_t accessCounter_ = 0;

    std::mutex mutex_;
    std::condition_variable condition_;
    std::deque<Task> tasks_;
    std::unordered_map<std::wstring, CacheEntry> cache_;
    bool stopping_ = false;
    std::vector<std::thread> workers_;
};

} // namespace pureview
