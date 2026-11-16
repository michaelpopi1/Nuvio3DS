#include "player/mvd_player.hpp"

#include <algorithm>
#include <cstring>

namespace nuvio::player {

namespace {

constexpr uint32_t kVideoWidth = 400;
constexpr uint32_t kVideoHeight = 240;
constexpr uint32_t kTexWidth = 512;
constexpr uint32_t kTexHeight = 256;
constexpr size_t kRingCapacity = 2 * 1024 * 1024; // 2 MB ring buffer in FCRAM

} // namespace

MvdPlayer::MvdPlayer(const net::HttpClient& http) : http_(http) {
    LightLock_Init(&ringLock_);
    ringBuf_.resize(kRingCapacity);
}

MvdPlayer::~MvdPlayer() {
    stop();
    shutdownHardware();
}

bool MvdPlayer::initHardware(bool isNew3ds) {
    if (hardwareInitialized_) return true;

    if (!isNew3ds) {
        stats_.mvdHardwareReady = false;
        stats_.statusMessage = "Old 3DS detected: MVD hardware H.264 requires New 3DS";
        return false;
    }

    Result rc = mvdstdInit(
        MVDMODE_VIDEOPROCESSING,
        MVD_INPUT_H264,
        MVD_OUTPUT_RGB565,
        MVD_DEFAULT_WORKBUF_SIZE,
        nullptr);

    if (R_FAILED(rc)) {
        stats_.mvdHardwareReady = false;
        stats_.statusMessage = "mvdstdInit failed (check mvd:STD in nuvio3ds.xml / RSF)";
        return false;
    }

    nalInBuf_ = static_cast<u8*>(linearAlloc(nalInBufCap_));
    rgbOutBuf_ = static_cast<u8*>(linearAlloc(kVideoWidth * kVideoHeight * 2));
    if (!nalInBuf_ || !rgbOutBuf_) {
        if (nalInBuf_) { linearFree(nalInBuf_); nalInBuf_ = nullptr; }
        if (rgbOutBuf_) { linearFree(rgbOutBuf_); rgbOutBuf_ = nullptr; }
        mvdstdExit();
        stats_.statusMessage = "linearAlloc failed for MVD buffers";
        return false;
    }

    std::memset(rgbOutBuf_, 0, kVideoWidth * kVideoHeight * 2);

    mvdstdGenerateDefaultConfig(
        &mvdConfig_,
        kVideoWidth,
        kVideoHeight,
        kVideoWidth,
        kVideoHeight,
        reinterpret_cast<u32*>(nalInBuf_),
        reinterpret_cast<u32*>(rgbOutBuf_),
        nullptr);

    if (!C3D_TexInit(&videoTex_, kTexWidth, kTexHeight, GPU_RGB565)) {
        linearFree(nalInBuf_);
        linearFree(rgbOutBuf_);
        nalInBuf_ = nullptr;
        rgbOutBuf_ = nullptr;
        mvdstdExit();
        stats_.statusMessage = "C3D_TexInit failed";
        return false;
    }

    C3D_TexSetFilter(&videoTex_, GPU_LINEAR, GPU_LINEAR);
    subTex_.width = kVideoWidth;
    subTex_.height = kVideoHeight;
    subTex_.left = 0.0f;
    subTex_.top = 1.0f;
    subTex_.right = static_cast<float>(kVideoWidth) / static_cast<float>(kTexWidth);
    subTex_.bottom = 1.0f - (static_cast<float>(kVideoHeight) / static_cast<float>(kTexHeight));
    videoImage_.tex = &videoTex_;
    videoImage_.subtex = &subTex_;

    hasTexture_ = true;
    hardwareInitialized_ = true;
    stats_.mvdHardwareReady = true;
    stats_.statusMessage = "MVDSTD H.264 hardware ready";
    return true;
}

void MvdPlayer::shutdownHardware() {
    if (hasTexture_) {
        C3D_TexDelete(&videoTex_);
        hasTexture_ = false;
    }
    if (nalInBuf_) {
        linearFree(nalInBuf_);
        nalInBuf_ = nullptr;
    }
    if (rgbOutBuf_) {
        linearFree(rgbOutBuf_);
        rgbOutBuf_ = nullptr;
    }
    if (hardwareInitialized_) {
        mvdstdExit();
        hardwareInitialized_ = false;
    }
}

bool MvdPlayer::openStream(const std::string& url, const std::string& title) {
    stop();

    if (url.empty()) {
        stats_.state = PlayerState::Error;
        stats_.statusMessage = "Empty stream URL";
        return false;
    }

    if (!hardwareInitialized_) {
        stats_.state = PlayerState::Error;
        if (stats_.statusMessage.empty()) {
            stats_.statusMessage = "MVD hardware decoder unavailable";
        }
        return false;
    }

    streamUrl_ = url;
    title_ = title;
    stats_.state = PlayerState::Buffering;
    stats_.decodedFrames = 0;
    stats_.bytesDownloaded = 0;
    stats_.statusMessage = "Connecting via mbedTLS/curl...";

    LightLock_Lock(&ringLock_);
    ringHead_ = 0;
    ringTail_ = 0;
    ringCount_ = 0;
    LightLock_Unlock(&ringLock_);

    stopRequested_ = false;
    s32 prio = 0;
    svcGetThreadPriority(&prio, CUR_THREAD_HANDLE);
    // Run network streamer on Core 1 (or lower priority) so UI stays responsive at 60fps.
    downloadThread_ = threadCreate(downloadThreadEntry, this, 32 * 1024, prio + 1, 1, false);
    if (!downloadThread_) {
        stats_.state = PlayerState::Error;
        stats_.statusMessage = "Failed to spawn network streaming thread";
        return false;
    }

    return true;
}

void MvdPlayer::stop() {
    stopRequested_ = true;
    if (downloadThread_) {
        threadJoin(downloadThread_, U64_MAX);
        threadFree(downloadThread_);
        downloadThread_ = nullptr;
    }
    stats_.state = PlayerState::Idle;
}

void MvdPlayer::togglePause() {
    if (stats_.state == PlayerState::Playing) {
        stats_.state = PlayerState::Paused;
        stats_.statusMessage = "Paused";
    } else if (stats_.state == PlayerState::Paused) {
        stats_.state = PlayerState::Playing;
        stats_.statusMessage = "Playing (MVD Hardware)";
    }
}

void MvdPlayer::downloadThreadEntry(void* arg) {
    auto* self = static_cast<MvdPlayer*>(arg);
    if (self) self->runDownloadLoop();
}

void MvdPlayer::runDownloadLoop() {
    std::string err;
    bool ok = http_.streamRange(
        streamUrl_,
        0,
        0,
        [this](const uint8_t* data, size_t len) -> bool {
            if (stopRequested_) return false;
            size_t offset = 0;
            while (offset < len && !stopRequested_) {
                LightLock_Lock(&ringLock_);
                size_t space = kRingCapacity - ringCount_;
                size_t toWrite = std::min(space, len - offset);
                for (size_t i = 0; i < toWrite; ++i) {
                    ringBuf_[ringHead_] = data[offset + i];
                    ringHead_ = (ringHead_ + 1) % kRingCapacity;
                }
                ringCount_ += toWrite;
                stats_.bytesDownloaded += toWrite;
                LightLock_Unlock(&ringLock_);

                offset += toWrite;
                if (offset < len) {
                    svcSleepThread(5000000ULL); // 5ms backoff when ring buffer is full
                }
            }
            return !stopRequested_;
        },
        0,
        &err);

    if (!ok && !stopRequested_) {
        stats_.state = PlayerState::Error;
        stats_.statusMessage = err.empty() ? "Stream disconnected" : err;
    }
}

bool MvdPlayer::extractNextAnnexBNal(std::vector<uint8_t>& outNal) {
    LightLock_Lock(&ringLock_);
    if (ringCount_ < 8) {
        LightLock_Unlock(&ringLock_);
        return false;
    }

    auto peek = [this](size_t idx) -> uint8_t {
        return ringBuf_[(ringTail_ + idx) % kRingCapacity];
    };

    // Locate Annex-B start code (00 00 01 or 00 00 00 01).
    size_t start = 0;
    bool foundStart = false;
    while (start + 4 <= ringCount_) {
        if (peek(start) == 0x00 && peek(start + 1) == 0x00 &&
            (peek(start + 2) == 0x01 || (peek(start + 2) == 0x00 && peek(start + 3) == 0x01))) {
            foundStart = true;
            break;
        }
        ++start;
    }

    if (!foundStart) {
        ringTail_ = (ringTail_ + ringCount_) % kRingCapacity;
        ringCount_ = 0;
        LightLock_Unlock(&ringLock_);
        return false;
    }

    if (start > 0) {
        ringTail_ = (ringTail_ + start) % kRingCapacity;
        ringCount_ -= start;
    }

    size_t scLen = (peek(2) == 0x01) ? 3 : 4;
    size_t end = scLen;
    bool foundNext = false;
    while (end + 4 <= ringCount_) {
        if (peek(end) == 0x00 && peek(end + 1) == 0x00 &&
            (peek(end + 2) == 0x01 || (peek(end + 2) == 0x00 && peek(end + 3) == 0x01))) {
            foundNext = true;
            break;
        }
        ++end;
    }

    if (!foundNext) {
        LightLock_Unlock(&ringLock_);
        return false;
    }

    size_t nalSize = std::min(end, nalInBufCap_);
    outNal.resize(nalSize);
    for (size_t i = 0; i < nalSize; ++i) {
        outNal[i] = peek(i);
    }

    ringTail_ = (ringTail_ + end) % kRingCapacity;
    ringCount_ -= end;
    LightLock_Unlock(&ringLock_);
    return true;
}

void MvdPlayer::uploadRgb565ToTexture() {
    if (!hasTexture_ || !rgbOutBuf_) return;
    // Copy linear RGB565 output into the GPU texture via GX_DisplayTransfer / CPU tile-safe copy.
    GSPGPU_FlushDataCache(rgbOutBuf_, kVideoWidth * kVideoHeight * 2);
    u16* dst = static_cast<u16*>(videoTex_.data);
    const u16* src = reinterpret_cast<const u16*>(rgbOutBuf_);
    for (uint32_t y = 0; y < kVideoHeight; ++y) {
        std::memcpy(dst + y * kTexWidth, src + y * kVideoWidth, kVideoWidth * sizeof(u16));
    }
    GSPGPU_FlushDataCache(videoTex_.data, kTexWidth * kTexHeight * 2);
}

void MvdPlayer::update() {
    if (!hardwareInitialized_) return;
    if (stats_.state != PlayerState::Buffering && stats_.state != PlayerState::Playing) return;

    if (stats_.state == PlayerState::Buffering) {
        LightLock_Lock(&ringLock_);
        size_t buffered = ringCount_;
        LightLock_Unlock(&ringLock_);
        if (buffered < 64 * 1024) return; // Wait for 64 KB prebuffer
        stats_.state = PlayerState::Playing;
        stats_.statusMessage = "Playing (MVD Hardware)";
    }

    std::vector<uint8_t> nal;
    if (!extractNextAnnexBNal(nal)) return;

    std::memcpy(nalInBuf_, nal.data(), nal.size());
    GSPGPU_FlushDataCache(nalInBuf_, nal.size());

    MVDSTD_SetConfig(&mvdConfig_);
    Result rc = mvdstdProcessVideoFrame(nalInBuf_, nal.size(), 0, nullptr);
    if (MVD_CHECKNALUPROC_SUCCESS(rc)) {
        while (mvdstdRenderVideoFrame(&mvdConfig_, true) == MVD_STATUS_BUSY) {
            svcSleepThread(200000ULL);
        }
        uploadRgb565ToTexture();
        ++stats_.decodedFrames;
    }
}

void MvdPlayer::renderTopScreen(C3D_RenderTarget* topTarget) {
    C2D_SceneBegin(topTarget);
    C2D_TargetClear(topTarget, C2D_Color32(10, 12, 18, 255));
    if (hasTexture_ && stats_.decodedFrames > 0) {
        C2D_DrawImageAt(videoImage_, 0.0f, 0.0f, 0.0f, nullptr, 1.0f, 1.0f);
    }
}

} // namespace nuvio::player
