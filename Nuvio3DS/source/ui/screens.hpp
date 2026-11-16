#pragma once

#include "net/nuvio_sync.hpp"
#include "player/mvd_player.hpp"
#include "stremio/addon_client.hpp"
#include <3ds.h>
#include <citro2d.h>
#include <string>
#include <vector>

namespace nuvio::ui {

enum class ScreenTab {
    Catalog,
    DetailStreams,
    Player,
    Addons,
    Account,
    Settings
};

class AppUI {
public:
    AppUI(
        stremio::AddonClient& addons,
        net::NuvioSyncClient& sync,
        player::MvdPlayer& player,
        bool isNew3ds);
    ~AppUI();

    void init();
    void handleInput(u32 kDown, const touchPosition& touch);
    void update();
    void render(C3D_RenderTarget* topScreen, C3D_RenderTarget* botScreen);

private:
    stremio::AddonClient& addons_;
    net::NuvioSyncClient& sync_;
    player::MvdPlayer& player_;
    bool isNew3ds_ = false;

    ScreenTab activeTab_ = ScreenTab::Catalog;
    std::string activeType_ = "movie";
    std::string searchQuery_;
    std::string bannerMsg_ = "Ready";

    std::vector<stremio::MetaPreview> catalogItems_;
    size_t selectedCatalogIdx_ = 0;

    stremio::MetaPreview selectedMeta_;
    std::vector<stremio::StreamCandidate> streams_;
    size_t selectedStreamIdx_ = 0;
    size_t selectedAddonIdx_ = 0;
    size_t selectedSettingIdx_ = 0;

    C2D_TextBuf textBuf_ = nullptr;

    u32 accentColor() const;
    void refreshCatalog();
    void openSelectedDetail();
    void playSelectedStream();
    void cycleSetting(size_t idx, int dir);
    std::string promptKeyboard(const char* hint, const char* initialText = "", bool passwordMode = false);

    void drawText(float x, float y, float scale, u32 color, const std::string& text, u32 flags = C2D_WithColor);
    void renderTopCatalog(C3D_RenderTarget* top);
    void renderTopDetail(C3D_RenderTarget* top);
    void renderTopPlayer(C3D_RenderTarget* top);
    void renderTopAddons(C3D_RenderTarget* top);
    void renderTopAccount(C3D_RenderTarget* top);
    void renderTopSettings(C3D_RenderTarget* top);
    void renderBottomTouch(C3D_RenderTarget* bot);
};

} // namespace nuvio::ui
