#include "logo_texture_loader.h"

#include "debug_log.h"

#include <wincodec.h>
#include <wrl/client.h>

#include <chrono>
#include <cstring>
#include <utility>

namespace {

using Microsoft::WRL::ComPtr;

constexpr std::size_t kMaximumCompressedBytes = 400 * 1024;
constexpr std::uint8_t kPngSignature[] = { 0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A };

double NowMs() {
    using Clock = std::chrono::steady_clock;
    return std::chrono::duration<double, std::milli>(Clock::now().time_since_epoch()).count();
}

class ComApartment final {
public:
    explicit ComApartment(HRESULT result) noexcept
        : shouldUninitialize_(result == S_OK || result == S_FALSE) {
    }

    ~ComApartment() {
        if (shouldUninitialize_) {
            CoUninitialize();
        }
    }

    ComApartment(const ComApartment&) = delete;
    ComApartment& operator=(const ComApartment&) = delete;

private:
    bool shouldUninitialize_ = false;
};

bool LoadResourceBytes(HMODULE module, int resourceId, const std::uint8_t*& data, DWORD& size) {
    data = nullptr;
    size = 0;
    if (!module) {
        return false;
    }

    const HRSRC resource = FindResourceW(module, MAKEINTRESOURCEW(resourceId), MAKEINTRESOURCEW(10));
    if (!resource) {
        return false;
    }

    size = SizeofResource(module, resource);
    if (size == 0) {
        return false;
    }

    const HGLOBAL loaded = LoadResource(module, resource);
    if (!loaded) {
        return false;
    }

    data = static_cast<const std::uint8_t*>(LockResource(loaded));
    return data != nullptr;
}

void LogHresultFailure(const char* stage, HRESULT result) {
    debuglog::WriteError(
        "[ui][logo] stage=%s failed hr=0x%08lX",
        stage,
        static_cast<unsigned long>(result));
}

} // namespace

bool LogoTextureLoader::DecodeEmbeddedPng(HMODULE module, int resourceId) {
    if (IsReady()) {
        return true;
    }

    ready_.store(false, std::memory_order_release);
    pixels_.clear();

    const double totalBeginMs = NowMs();
    const double resourceBeginMs = NowMs();
    const std::uint8_t* compressedData = nullptr;
    DWORD compressedSize = 0;
    if (!LoadResourceBytes(module, resourceId, compressedData, compressedSize)) {
        debuglog::WriteError(
            "[ui][logo] stage=resource failed id=%d gle=%lu",
            resourceId,
            static_cast<unsigned long>(GetLastError()));
        return false;
    }

    const std::size_t compressedBytes = static_cast<std::size_t>(compressedSize);
    if (compressedBytes < sizeof(kPngSignature)
        || compressedBytes >= kMaximumCompressedBytes
        || std::memcmp(compressedData, kPngSignature, sizeof(kPngSignature)) != 0) {
        debuglog::WriteError(
            "[ui][logo] stage=resource invalid-png bytes=%zu limit=%zu",
            compressedBytes,
            kMaximumCompressedBytes);
        return false;
    }
    const double resourceMs = NowMs() - resourceBeginMs;

    const double decodeBeginMs = NowMs();
    const HRESULT apartmentResult = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(apartmentResult) && apartmentResult != RPC_E_CHANGED_MODE) {
        LogHresultFailure("com-initialize", apartmentResult);
        return false;
    }
    ComApartment apartment(apartmentResult);

    ComPtr<IWICImagingFactory> factory;
    HRESULT result = CoCreateInstance(
        CLSID_WICImagingFactory,
        nullptr,
        CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&factory));
    if (FAILED(result)) {
        LogHresultFailure("wic-factory", result);
        return false;
    }

    ComPtr<IWICStream> stream;
    result = factory->CreateStream(&stream);
    if (FAILED(result)) {
        LogHresultFailure("wic-stream-create", result);
        return false;
    }

    result = stream->InitializeFromMemory(const_cast<BYTE*>(compressedData), compressedSize);
    if (FAILED(result)) {
        LogHresultFailure("wic-stream-init", result);
        return false;
    }

    ComPtr<IWICBitmapDecoder> decoder;
    result = factory->CreateDecoderFromStream(
        stream.Get(),
        nullptr,
        WICDecodeMetadataCacheOnLoad,
        &decoder);
    if (FAILED(result)) {
        LogHresultFailure("wic-decoder", result);
        return false;
    }

    UINT frameCount = 0;
    result = decoder->GetFrameCount(&frameCount);
    if (FAILED(result) || frameCount != 1) {
        debuglog::WriteError(
            "[ui][logo] stage=wic-frame-count failed hr=0x%08lX frames=%u expected=1",
            static_cast<unsigned long>(result),
            frameCount);
        return false;
    }

    ComPtr<IWICBitmapFrameDecode> frame;
    result = decoder->GetFrame(0, &frame);
    if (FAILED(result)) {
        LogHresultFailure("wic-frame", result);
        return false;
    }

    UINT width = 0;
    UINT height = 0;
    result = frame->GetSize(&width, &height);
    if (FAILED(result) || width != kWidth || height != kHeight) {
        debuglog::WriteError(
            "[ui][logo] stage=wic-geometry failed hr=0x%08lX actual=%ux%u expected=%ux%u",
            static_cast<unsigned long>(result),
            width,
            height,
            kWidth,
            kHeight);
        return false;
    }

    WICPixelFormatGUID sourceFormat{};
    result = frame->GetPixelFormat(&sourceFormat);
    if (FAILED(result)) {
        LogHresultFailure("wic-pixel-format", result);
        return false;
    }

    ComPtr<IWICFormatConverter> converter;
    result = factory->CreateFormatConverter(&converter);
    if (FAILED(result)) {
        LogHresultFailure("wic-converter-create", result);
        return false;
    }

    BOOL canConvert = FALSE;
    result = converter->CanConvert(sourceFormat, GUID_WICPixelFormat32bppBGRA, &canConvert);
    if (FAILED(result) || !canConvert) {
        debuglog::WriteError(
            "[ui][logo] stage=wic-converter-check failed hr=0x%08lX canConvert=%d",
            static_cast<unsigned long>(result),
            canConvert ? 1 : 0);
        return false;
    }

    result = converter->Initialize(
        frame.Get(),
        GUID_WICPixelFormat32bppBGRA,
        WICBitmapDitherTypeNone,
        nullptr,
        0.0,
        WICBitmapPaletteTypeCustom);
    if (FAILED(result)) {
        LogHresultFailure("wic-converter-init", result);
        return false;
    }

    std::vector<std::uint8_t> decoded(kDecodedBytes);
    result = converter->CopyPixels(
        nullptr,
        static_cast<UINT>(kRowBytes),
        static_cast<UINT>(decoded.size()),
        decoded.data());
    if (FAILED(result)) {
        LogHresultFailure("wic-copy-pixels", result);
        return false;
    }

    pixels_ = std::move(decoded);
    ready_.store(true, std::memory_order_release);

    const double decodeMs = NowMs() - decodeBeginMs;
    debuglog::WriteInfo(
        "[ui][perf][logo] stage=decode source=wic-png compressed=%zu decoded=%zu resource=%.2fms decode=%.2fms total=%.2fms",
        compressedBytes,
        kDecodedBytes,
        resourceMs,
        decodeMs,
        NowMs() - totalBeginMs);
    return true;
}

bool LogoTextureLoader::IsReady() const noexcept {
    return ready_.load(std::memory_order_acquire);
}

const std::uint8_t* LogoTextureLoader::Pixels() const noexcept {
    return IsReady() ? pixels_.data() : nullptr;
}

std::size_t LogoTextureLoader::PixelBytes() const noexcept {
    return IsReady() ? pixels_.size() : 0;
}

void LogoTextureLoader::Reset() noexcept {
    ready_.store(false, std::memory_order_release);
    std::vector<std::uint8_t>().swap(pixels_);
}
