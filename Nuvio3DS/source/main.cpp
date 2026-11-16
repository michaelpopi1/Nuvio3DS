#include "net/http_client.hpp"
#include "net/nuvio_sync.hpp"
#include "player/mvd_player.hpp"
#include "stremio/addon_client.hpp"
#include "ui/screens.hpp"

#include <3ds.h>
#include <citro2d.h>
#include <citro3d.h>
#include <cstdlib>
#include <malloc.h>

namespace {

constexpr size_t kSocAlign = 0x1000;
constexpr size_t kSocBufferSize = 0x100000; // 1 MB socket shared memory

} // namespace

int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;

    gfxInitDefault();
    romfsInit();

    // Enable 804 MHz + L2 cache when running on New Nintendo 3DS / New 3DS XL.
    bool isNew3ds = false;
    APT_CheckNew3DS(&isNew3ds);
    if (isNew3ds) {
        osSetSpeedupEnable(true);
    }

    // Initialize SOC network service for libcurl + mbedTLS.
    u32* socMem = static_cast<u32*>(memalign(kSocAlign, kSocBufferSize));
    bool socOk = false;
    if (socMem) {
        socOk = R_SUCCEEDED(socInit(socMem, kSocBufferSize));
    }

    ndspInit();
    C3D_Init(C3D_DEFAULT_CMDBUF_SIZE);
    C2D_Init(C2D_DEFAULT_MAX_OBJECTS);
    C2D_Prepare();

    C3D_RenderTarget* topScreen = C2D_CreateScreenTarget(GFX_TOP, GFX_LEFT);
    C3D_RenderTarget* botScreen = C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);

    nuvio::net::HttpClient::globalInit();
    nuvio::net::HttpClient http(false);
    nuvio::stremio::AddonClient addons(http);
    addons.loadAddonsFromSd();

    nuvio::net::NuvioSyncClient sync(http);
    sync.loadFromSd();

    nuvio::player::MvdPlayer player(http);
    player.initHardware(isNew3ds);

    nuvio::ui::AppUI ui(addons, sync, player, isNew3ds);
    ui.init();

    while (aptMainLoop()) {
        hidScanInput();
        u32 kDown = hidKeysDown();
        if (kDown & KEY_START) break;

        touchPosition touch{};
        hidTouchRead(&touch);

        ui.handleInput(kDown, touch);
        ui.update();

        C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
        ui.render(topScreen, botScreen);
        C3D_FrameEnd(0);
    }

    player.stop();
    player.shutdownHardware();
    nuvio::net::HttpClient::globalCleanup();

    C2D_Fini();
    C3D_Fini();
    ndspExit();

    if (socOk) socExit();
    if (socMem) free(socMem);

    romfsExit();
    gfxExit();
    return 0;
}
