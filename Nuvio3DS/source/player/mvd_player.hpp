#pragma once

#include "net/http_client.hpp"
#include <3ds.h>
#include <citro2d.h>
#include <citro3d.h>
#include <cstdint>
#include <string>
#include <vector>

namespace nuvio::player {

enum class PlayerState {
    Idle,
    Buffering,
    Playing,
    Paused,
    Error
};

struct PlaybackStats {
    PlayerState state = PlayerState::Idle;
    uint32_t decodedFrames = 0;
    uint64_t bytesDownloaded = 0;
    uint32_t videoWidth = 400;
    uint32_t videoHeight = 240;
    bool mvdHardwareReady = false;
    std::string statusMessage;
};

class MvdPlayer {
public:
    explicit MvdPlayer(const net::HttpClient& http);
    ~MvdPlayer();

    bool initHardware(bool isNew3ds);
    void shutdownHardware();

    bool openStream(const std::string& url, const std::string& title);
    void stop();
    void togglePause();

    // Advances NAL feeding + MVD hardware decoding once per frame.
    void update();

    // Draws the current decoded RGB565 frame scaled to the 400x240 top screen.
    void renderTopScreen(C3D_RenderTarget* topTarget);

    const PlaybackStats& stats() const { return stats_; }
    const std::string& title() const { return title_; }
    bool isActive() const {
        return stats_.state == PlayerState::Buffering ||
               stats_.state == PlayerState::Playing ||
               stats_.state == PlayerState::Paused;
    }

private:
    const net::HttpClient& http_;
    PlaybackStats stats_;
    std::string streamUrl_;
    std::string title_;

    bool hardwareInitialized_ = false;
    bool hasTexture_ = false;
    C3D_Tex videoTex_{};
    Tex3DS_SubTexture subTex_{};
    C2D_Image videoImage_{};

    u8* nalInBuf_ = nullptr;    // Linear memory input buffer for MVDSTD
    u8* rgbOutBuf_ = nullptr;   // Linear memory RGB565 output buffer (400x240x2)
    size_t nalInBufCap_ = 0x40000; // 256 KB per Annex-B access unit
    MVDSTD_Config mvdConfig_{};

    // Background HTTP ring buffer
    Thread downloadThread_ = nullptr;
    LightLock ringLock_{};
    volatile bool stopRequested_ = false;
    std::vector<uint8_t> ringBuf_;
    size_t ringHead_ = 0;
    size_t ringTail_ = 0;
    size_t ringCount_ = 0;

    static void downloadThreadEntry(void* arg);
    void runDownloadLoop();
    bool extractNextAnnexBNal(std::vector<uint8_t>& outNal);
    void uploadRgb565ToTexture();
};

} // namespace nuvio::player
