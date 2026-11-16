#include "ui/screens.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace nuvio::ui {

namespace {

constexpr u32 kColBg = C2D_Color32(15, 18, 25, 255);
constexpr u32 kColPanel = C2D_Color32(24, 29, 40, 255);
constexpr u32 kColPanelHi = C2D_Color32(36, 46, 66, 255);
constexpr u32 kColSky = C2D_Color32(56, 189, 248, 255);
constexpr u32 kColEmerald = C2D_Color32(52, 211, 153, 255);
constexpr u32 kColAmber = C2D_Color32(245, 158, 11, 255);
constexpr u32 kColViolet = C2D_Color32(167, 139, 250, 255);
constexpr u32 kColRose = C2D_Color32(251, 113, 133, 255);
constexpr u32 kColGreen = C2D_Color32(34, 197, 94, 255);
constexpr u32 kColRed = C2D_Color32(239, 68, 68, 255);
constexpr u32 kColText = C2D_Color32(241, 245, 249, 255);
constexpr u32 kColMuted = C2D_Color32(148, 163, 184, 255);

std::string truncateText(const std::string& s, size_t maxLen) {
    if (s.size() <= maxLen) return s;
    if (maxLen <= 3) return s.substr(0, maxLen);
    return s.substr(0, maxLen - 3) + "...";
}

constexpr size_t kNumSettingsRows = 7;

} // namespace

AppUI::AppUI(
    stremio::AddonClient& addons,
    net::NuvioSyncClient& sync,
    player::MvdPlayer& player,
    bool isNew3ds)
    : addons_(addons), sync_(sync), player_(player), isNew3ds_(isNew3ds) {}

AppUI::~AppUI() {
    if (textBuf_) {
        C2D_TextBufDelete(textBuf_);
        textBuf_ = nullptr;
    }
}

u32 AppUI::accentColor() const {
    const std::string& a = sync_.state().settings.uiAccent;
    if (a == "emerald") return kColEmerald;
    if (a == "amber") return kColAmber;
    if (a == "violet") return kColViolet;
    if (a == "rose") return kColRose;
    return kColSky;
}

void AppUI::init() {
    textBuf_ = C2D_TextBufNew(4096);
    activeType_ = sync_.state().settings.defaultMediaType;
    refreshCatalog();
}

void AppUI::drawText(float x, float y, float scale, u32 color, const std::string& text, u32 flags) {
    if (!textBuf_ || text.empty()) return;
    C2D_Text parsed;
    const char* end = C2D_TextParse(&parsed, textBuf_, text.c_str());
    if (end) {
        C2D_TextOptimize(&parsed);
        C2D_DrawText(&parsed, flags, x, y, 0.5f, scale, scale, color);
    }
}

std::string AppUI::promptKeyboard(const char* hint, const char* initialText, bool passwordMode) {
    SwkbdState swkbd;
    char buf[256] = {0};
    swkbdInit(&swkbd, SWKBD_TYPE_NORMAL, 2, sizeof(buf) - 1);
    swkbdSetHintText(&swkbd, hint);
    if (passwordMode) {
        swkbdSetPasswordMode(&swkbd, SWKBD_PASSWORD_HIDE_DELAY);
    }
    if (initialText && initialText[0] != '\0') {
        swkbdSetInitialText(&swkbd, initialText);
    }
    SwkbdButton btn = swkbdInputText(&swkbd, buf, sizeof(buf));
    if (btn == SWKBD_BUTTON_RIGHT) {
        return std::string(buf);
    }
    return "";
}

void AppUI::refreshCatalog() {
    catalogItems_.clear();
    selectedCatalogIdx_ = 0;

    for (const auto& addon : addons_.addons()) {
        if (!addon.enabled) continue;
        for (const auto& cat : addon.catalogs) {
            if (cat.type != activeType_) continue;
            std::string err;
            if (addons_.fetchCatalog(addon, cat, searchQuery_, catalogItems_, &err)) {
                bannerMsg_ = addon.name + ": " + std::to_string(catalogItems_.size()) + " items";
                return;
            }
            bannerMsg_ = "Catalog error: " + err;
            return;
        }
    }
    bannerMsg_ = "No enabled catalog addon for " + activeType_;
}

void AppUI::openSelectedDetail() {
    if (selectedCatalogIdx_ >= catalogItems_.size()) return;
    selectedMeta_ = catalogItems_[selectedCatalogIdx_];
    addons_.fetchMetaDetail(selectedMeta_.type, selectedMeta_.id, selectedMeta_);

    std::string err;
    streams_.clear();
    selectedStreamIdx_ = 0;
    addons_.fetchStreams(selectedMeta_.type, selectedMeta_.id, streams_, &err);
    if (streams_.empty() && !err.empty()) {
        bannerMsg_ = err;
    } else {
        bannerMsg_ = std::to_string(streams_.size()) + " stream sources found";
    }
    activeTab_ = ScreenTab::DetailStreams;
}

void AppUI::playSelectedStream() {
    if (selectedStreamIdx_ >= streams_.size()) return;
    const auto& cand = streams_[selectedStreamIdx_];
    if (cand.compat == stremio::StreamCompat::UnsupportedP2P &&
        addons_.transcodeProxyUrl().empty()) {
        bannerMsg_ = "Torrent stream blocked: set transcode/debrid proxy first";
        return;
    }
    std::string resolved = addons_.resolvePlaybackUrl(cand);
    if (player_.openStream(resolved, selectedMeta_.name)) {
        activeTab_ = ScreenTab::Player;
        bannerMsg_ = "Streaming via MVD hardware";
    } else {
        bannerMsg_ = player_.stats().statusMessage;
    }
}

void AppUI::cycleSetting(size_t idx, int dir) {
    net::UserSettings next = sync_.state().settings;
    if (idx == 0) {
        static const char* kAccents[5] = {"sky", "emerald", "amber", "violet", "rose"};
        int cur = 0;
        for (int i = 0; i < 5; ++i) {
            if (next.uiAccent == kAccents[i]) { cur = i; break; }
        }
        cur = (cur + (dir >= 0 ? 1 : 4)) % 5;
        next.uiAccent = kAccents[cur];
    } else if (idx == 1) {
        next.defaultMediaType = (next.defaultMediaType == "movie") ? "series" : "movie";
        activeType_ = next.defaultMediaType;
        refreshCatalog();
    } else if (idx == 2) {
        next.preferredQuality = (next.preferredQuality == "360p") ? "480p" : "360p";
    } else if (idx == 3) {
        next.preferMvdDirect = !next.preferMvdDirect;
    } else if (idx == 4) {
        next.autoPlayNext = !next.autoPlayNext;
    } else if (idx == 5) {
        next.subtitlesEnabled = !next.subtitlesEnabled;
    } else if (idx == 6) {
        if (next.wifiBufferKb == 2048) next.wifiBufferKb = (dir >= 0) ? 4096 : 8192;
        else if (next.wifiBufferKb == 4096) next.wifiBufferKb = (dir >= 0) ? 8192 : 2048;
        else next.wifiBufferKb = (dir >= 0) ? 2048 : 4096;
    }

    std::string err;
    if (sync_.updateSettings(next, &err)) {
        bannerMsg_ = sync_.state().signedIn ? "Settings saved & synced" : "Settings saved to SD";
    } else {
        bannerMsg_ = err.empty() ? "Settings saved to SD" : err;
    }
}

void AppUI::handleInput(u32 kDown, const touchPosition& touch) {
    // Cycle main views with Y
    if (kDown & KEY_Y) {
        if (activeTab_ == ScreenTab::Catalog) activeTab_ = ScreenTab::Addons;
        else if (activeTab_ == ScreenTab::Addons) activeTab_ = ScreenTab::Account;
        else if (activeTab_ == ScreenTab::Account) activeTab_ = ScreenTab::Settings;
        else activeTab_ = ScreenTab::Catalog;
        return;
    }

    // X opens search (on Catalog/Detail) or Addon install (on Addons)
    if (kDown & KEY_X) {
        if (activeTab_ == ScreenTab::Addons) {
            std::string url = promptKeyboard("https://addon.example.com/manifest.json");
            if (!url.empty()) {
                std::string err;
                if (addons_.installAddon(url, &err)) bannerMsg_ = "Addon installed";
                else bannerMsg_ = "Install failed: " + err;
            }
        } else {
            std::string q = promptKeyboard("Search titles...", searchQuery_.c_str());
            searchQuery_ = q;
            refreshCatalog();
            activeTab_ = ScreenTab::Catalog;
        }
        return;
    }

    // Touch handling on Bottom Screen (320x240)
    if (kDown & KEY_TOUCH) {
        // 5-tab bottom bar (64px wide each across 320px width): y in [4, 32]
        if (touch.py >= 4 && touch.py <= 32) {
            if (touch.px < 64) activeTab_ = ScreenTab::Catalog;
            else if (touch.px < 128) activeTab_ = ScreenTab::DetailStreams;
            else if (touch.px < 192) activeTab_ = ScreenTab::Addons;
            else if (touch.px < 256) activeTab_ = ScreenTab::Account;
            else activeTab_ = ScreenTab::Settings;
            return;
        }

        if (activeTab_ == ScreenTab::Catalog) {
            if (touch.py >= 42 && touch.py < 214) {
                size_t col = (touch.px >= 160) ? 1 : 0;
                size_t row = (touch.py >= 128) ? 1 : 0;
                size_t pageStart = (selectedCatalogIdx_ / 4) * 4;
                size_t targetIdx = pageStart + row * 2 + col;
                if (targetIdx < catalogItems_.size()) {
                    if (selectedCatalogIdx_ == targetIdx) openSelectedDetail();
                    else selectedCatalogIdx_ = targetIdx;
                }
            }
        } else if (activeTab_ == ScreenTab::DetailStreams) {
            if (touch.py >= 42 && touch.py < 222) {
                size_t row = static_cast<size_t>((touch.py - 42) / 36);
                size_t pageStart = (selectedStreamIdx_ / 5) * 5;
                size_t targetIdx = pageStart + row;
                if (targetIdx < streams_.size()) {
                    if (selectedStreamIdx_ == targetIdx) playSelectedStream();
                    else selectedStreamIdx_ = targetIdx;
                }
            }
        } else if (activeTab_ == ScreenTab::Addons) {
            if (touch.py >= 196 && touch.py <= 232) {
                if (touch.px < 160) {
                    std::string url = promptKeyboard("https://addon.example.com/manifest.json");
                    if (!url.empty()) {
                        std::string err;
                        if (addons_.installAddon(url, &err)) bannerMsg_ = "Addon installed";
                        else bannerMsg_ = "Error: " + err;
                    }
                } else {
                    std::string proxy = promptKeyboard("http://192.168.1.x:8787/transcode?url=",
                                                       addons_.transcodeProxyUrl().c_str());
                    addons_.setTranscodeProxyUrl(proxy);
                    addons_.saveAddonsToSd();
                    bannerMsg_ = proxy.empty() ? "Proxy cleared" : "Transcode proxy saved";
                }
            } else if (touch.py >= 42 && touch.py < 186) {
                size_t row = static_cast<size_t>((touch.py - 42) / 36);
                if (row < addons_.addons().size()) {
                    selectedAddonIdx_ = row;
                    addons_.toggleAddon(row);
                    refreshCatalog();
                }
            }
        } else if (activeTab_ == ScreenTab::Account) {
            if (touch.py >= 46 && touch.py < 84) {
                std::string supaUrl = promptKeyboard("https://your-project.supabase.co",
                                                     sync_.state().supabaseUrl.c_str());
                std::string anonKey = promptKeyboard("Supabase Anon Key",
                                                     sync_.state().anonKey.c_str());
                if (!supaUrl.empty() && !anonKey.empty()) {
                    sync_.setSupabaseConfig(supaUrl, anonKey);
                    bannerMsg_ = "Supabase config saved";
                }
            } else if (touch.py >= 92 && touch.py < 130) {
                std::string email = promptKeyboard("Email address", sync_.state().email.c_str());
                std::string pwd = promptKeyboard("Password", "", true);
                if (!email.empty() && !pwd.empty()) {
                    std::string err;
                    if (sync_.signIn(email, pwd, &err)) {
                        sync_.pullSyncData();
                        activeType_ = sync_.state().settings.defaultMediaType;
                        refreshCatalog();
                        bannerMsg_ = "Signed in & synced";
                    } else {
                        bannerMsg_ = "Login failed: " + err;
                    }
                }
            } else if (touch.py >= 138 && touch.py < 176) {
                if (sync_.state().signedIn) {
                    std::string err;
                    if (sync_.pullSyncData(&err)) {
                        activeType_ = sync_.state().settings.defaultMediaType;
                        refreshCatalog();
                        bannerMsg_ = "Synced watchlist & settings";
                    } else {
                        bannerMsg_ = "Sync error: " + err;
                    }
                }
            } else if (touch.py >= 184 && touch.py < 216 && sync_.state().signedIn) {
                sync_.signOut();
                bannerMsg_ = "Signed out";
            }
        } else if (activeTab_ == ScreenTab::Settings) {
            if (touch.py >= 42 && touch.py < 222) {
                size_t row = static_cast<size_t>((touch.py - 42) / 36);
                size_t pageStart = (selectedSettingIdx_ / 5) * 5;
                size_t targetIdx = pageStart + row;
                if (targetIdx < kNumSettingsRows) {
                    selectedSettingIdx_ = targetIdx;
                    cycleSetting(targetIdx, 1);
                }
            }
        } else if (activeTab_ == ScreenTab::Player) {
            if (touch.px < 160 && touch.py >= 140) player_.togglePause();
            else if (touch.px >= 160 && touch.py >= 140) {
                player_.stop();
                activeTab_ = ScreenTab::DetailStreams;
            }
        }
    }

    // D-Pad / Button controls
    if (activeTab_ == ScreenTab::Catalog) {
        if ((kDown & KEY_L) || (kDown & KEY_R)) {
            activeType_ = (activeType_ == "movie") ? "series" : "movie";
            refreshCatalog();
        }
        if ((kDown & KEY_RIGHT) && !catalogItems_.empty()) {
            selectedCatalogIdx_ = (selectedCatalogIdx_ + 1) % catalogItems_.size();
        }
        if ((kDown & KEY_LEFT) && !catalogItems_.empty()) {
            selectedCatalogIdx_ = (selectedCatalogIdx_ == 0) ? catalogItems_.size() - 1 : selectedCatalogIdx_ - 1;
        }
        if ((kDown & KEY_DOWN) && !catalogItems_.empty()) {
            selectedCatalogIdx_ = (selectedCatalogIdx_ + 2 < catalogItems_.size())
                                      ? selectedCatalogIdx_ + 2
                                      : (selectedCatalogIdx_ + 1) % catalogItems_.size();
        }
        if ((kDown & KEY_UP) && !catalogItems_.empty()) {
            selectedCatalogIdx_ = (selectedCatalogIdx_ >= 2)
                                      ? selectedCatalogIdx_ - 2
                                      : (selectedCatalogIdx_ == 0 ? catalogItems_.size() - 1 : selectedCatalogIdx_ - 1);
        }
        if (kDown & KEY_A) openSelectedDetail();
    } else if (activeTab_ == ScreenTab::DetailStreams) {
        if ((kDown & KEY_DOWN) && !streams_.empty()) {
            selectedStreamIdx_ = (selectedStreamIdx_ + 1) % streams_.size();
        }
        if ((kDown & KEY_UP) && !streams_.empty()) {
            selectedStreamIdx_ = (selectedStreamIdx_ == 0) ? streams_.size() - 1 : selectedStreamIdx_ - 1;
        }
        if (kDown & KEY_A) playSelectedStream();
        if (kDown & KEY_B) activeTab_ = ScreenTab::Catalog;
    } else if (activeTab_ == ScreenTab::Player) {
        if (kDown & KEY_A) player_.togglePause();
        if (kDown & KEY_B) {
            player_.stop();
            activeTab_ = ScreenTab::DetailStreams;
        }
    } else if (activeTab_ == ScreenTab::Addons) {
        if ((kDown & KEY_DOWN) && !addons_.addons().empty()) {
            selectedAddonIdx_ = (selectedAddonIdx_ + 1) % addons_.addons().size();
        }
        if ((kDown & KEY_UP) && !addons_.addons().empty()) {
            selectedAddonIdx_ = (selectedAddonIdx_ == 0) ? addons_.addons().size() - 1 : selectedAddonIdx_ - 1;
        }
        if ((kDown & KEY_A) && selectedAddonIdx_ < addons_.addons().size()) {
            addons_.toggleAddon(selectedAddonIdx_);
            refreshCatalog();
        }
        if (kDown & KEY_B) activeTab_ = ScreenTab::Catalog;
    } else if (activeTab_ == ScreenTab::Account) {
        if (kDown & KEY_B) activeTab_ = ScreenTab::Catalog;
    } else if (activeTab_ == ScreenTab::Settings) {
        if (kDown & KEY_DOWN) {
            selectedSettingIdx_ = (selectedSettingIdx_ + 1) % kNumSettingsRows;
        }
        if (kDown & KEY_UP) {
            selectedSettingIdx_ = (selectedSettingIdx_ == 0) ? kNumSettingsRows - 1 : selectedSettingIdx_ - 1;
        }
        if ((kDown & KEY_A) || (kDown & KEY_RIGHT) || (kDown & KEY_R)) {
            cycleSetting(selectedSettingIdx_, 1);
        }
        if ((kDown & KEY_LEFT) || (kDown & KEY_L)) {
            cycleSetting(selectedSettingIdx_, -1);
        }
        if (kDown & KEY_B) activeTab_ = ScreenTab::Catalog;
    }
}

void AppUI::update() {
    player_.update();
}

void AppUI::renderTopCatalog(C3D_RenderTarget* top) {
    u32 colAcc = accentColor();
    C2D_SceneBegin(top);
    C2D_TargetClear(top, kColBg);

    // 3DS Homebrew top status bar (400x26)
    C2D_DrawRectSolid(0, 0, 0.1f, 400, 26, kColPanel);
    drawText(10, 5, 0.48f, colAcc, "NUVIO");
    drawText(70, 6, 0.38f, kColMuted,
             std::string(activeType_ == "movie" ? "Movies" : "Series") +
             (searchQuery_.empty() ? "" : " | \"" + truncateText(searchQuery_, 14) + "\""));
    drawText(268, 6, 0.36f, isNew3ds_ ? kColGreen : kColAmber,
             isNew3ds_ ? "N3DS 804MHz" : "Old3DS");
    // Tiny 3DS Wi-Fi signal bars + battery indicator
    C2D_DrawRectSolid(352, 13, 0.2f, 3, 5, kColGreen);
    C2D_DrawRectSolid(357, 10, 0.2f, 3, 8, kColGreen);
    C2D_DrawRectSolid(362, 7, 0.2f, 3, 11, kColGreen);
    C2D_DrawRectSolid(372, 7, 0.2f, 18, 11, kColPanelHi);
    C2D_DrawRectSolid(374, 9, 0.25f, 12, 7, kColGreen);
    C2D_DrawRectSolid(390, 10, 0.2f, 2, 5, kColMuted);

    if (catalogItems_.empty()) {
        drawText(20, 96, 0.50f, kColText, "No items loaded.");
        drawText(20, 122, 0.42f, kColMuted, bannerMsg_);
        return;
    }

    // Nuvio-style cinema hero stage (poster card left + metadata right)
    const auto& item = catalogItems_[selectedCatalogIdx_];
    C2D_DrawRectSolid(12, 34, 0.1f, 376, 170, kColPanel);

    // 2:3 Poster artwork frame
    C2D_DrawRectSolid(22, 44, 0.15f, 94, 140, kColBg);
    C2D_DrawRectSolid(22, 44, 0.20f, 94, 4, colAcc);
    drawText(30, 56, 0.40f, colAcc, item.releaseInfo.empty() ? "HD" : item.releaseInfo);
    drawText(30, 98, 0.65f, kColText, truncateText(item.name, 7));
    drawText(30, 162, 0.34f, kColGreen, "MVD 480p");

    // Hero details column
    drawText(128, 46, 0.56f, kColText, truncateText(item.name, 24));
    drawText(128, 74, 0.40f, colAcc,
             item.type + "  |  " + (item.releaseInfo.empty() ? "N/A" : item.releaseInfo) +
             (item.rating.empty() ? "" : "  |  IMDb " + item.rating));
    drawText(128, 98, 0.38f, kColMuted, truncateText(item.description, 135));

    // Action CTA pill mimicking Nuvio Play button + 3DS A prompt
    C2D_DrawRectSolid(128, 160, 0.2f, 132, 26, colAcc);
    drawText(142, 166, 0.42f, kColBg, "(A) Open Sources");

    drawText(14, 214, 0.38f, kColMuted,
             "(A) Sources   (X) Search   (L/R) Type   (Y) Tabs");
}

void AppUI::renderTopDetail(C3D_RenderTarget* top) {
    u32 colAcc = accentColor();
    C2D_SceneBegin(top);
    C2D_TargetClear(top, kColBg);

    C2D_DrawRectSolid(0, 0, 0.1f, 400, 34, kColPanel);
    drawText(12, 8, 0.52f, kColText, truncateText(selectedMeta_.name, 34));

    C2D_DrawRectSolid(16, 46, 0.1f, 368, 128, kColPanel);
    drawText(26, 56, 0.42f, colAcc,
             selectedMeta_.releaseInfo + (selectedMeta_.runtime.empty() ? "" : "  |  " + selectedMeta_.runtime));
    drawText(26, 80, 0.40f, kColMuted, truncateText(selectedMeta_.description, 175));

    drawText(16, 186, 0.42f, kColText,
             "Streams (" + std::to_string(streams_.size()) + ") - select on bottom screen");
    drawText(16, 214, 0.40f, kColMuted, "A: Play Stream  |  B: Back to Catalog");
}

void AppUI::renderTopPlayer(C3D_RenderTarget* top) {
    u32 colAcc = accentColor();
    player_.renderTopScreen(top);

    const auto& st = player_.stats();
    if (st.state != player::PlayerState::Playing || st.decodedFrames == 0) {
        C2D_DrawRectSolid(20, 68, 0.2f, 360, 100, kColPanel);
        drawText(34, 82, 0.52f, kColText, truncateText(player_.title(), 30));
        drawText(34, 110, 0.42f, colAcc, st.statusMessage);
        char buf[96];
        std::snprintf(buf, sizeof(buf), "Frames: %lu  |  Buffered: %llu KB",
                      static_cast<unsigned long>(st.decodedFrames),
                      static_cast<unsigned long long>(st.bytesDownloaded / 1024));
        drawText(34, 134, 0.40f, kColMuted, buf);
    }
}

void AppUI::renderTopAddons(C3D_RenderTarget* top) {
    u32 colAcc = accentColor();
    C2D_SceneBegin(top);
    C2D_TargetClear(top, kColBg);

    C2D_DrawRectSolid(0, 0, 0.1f, 400, 34, kColPanel);
    drawText(12, 8, 0.55f, colAcc, "Nuvio3DS Addons & Proxy");

    C2D_DrawRectSolid(16, 48, 0.1f, 368, 150, kColPanel);
    drawText(28, 60, 0.45f, kColText, "SD Config: sdmc:/3ds/Nuvio3DS/addons.json");
    drawText(28, 86, 0.42f, kColMuted,
             "Installed Addons: " + std::to_string(addons_.addons().size()));
    drawText(28, 112, 0.42f, kColMuted,
             "Transcode Proxy: " + (addons_.transcodeProxyUrl().empty() ? "Not configured" : addons_.transcodeProxyUrl()));
    drawText(28, 146, 0.40f, colAcc, bannerMsg_);
    drawText(16, 214, 0.40f, kColMuted, "A: Toggle Addon  |  X: Install URL  |  B: Back");
}

void AppUI::renderTopAccount(C3D_RenderTarget* top) {
    u32 colAcc = accentColor();
    C2D_SceneBegin(top);
    C2D_TargetClear(top, kColBg);

    C2D_DrawRectSolid(0, 0, 0.1f, 400, 34, kColPanel);
    drawText(12, 8, 0.55f, colAcc, "Nuvio Account Sync (Supabase)");

    const auto& st = sync_.state();
    C2D_DrawRectSolid(16, 48, 0.1f, 368, 152, kColPanel);
    drawText(28, 58, 0.44f, st.signedIn ? kColGreen : kColAmber,
             st.signedIn ? "Signed in: " + st.email : "Not signed in");
    drawText(28, 82, 0.38f, kColMuted,
             "Supabase: " + (st.supabaseUrl.empty() ? "Not configured" : truncateText(st.supabaseUrl, 36)));
    drawText(28, 106, 0.38f, kColText,
             "Synced Library: " + std::to_string(st.watchlist.size()) +
             "  |  Settings: " + (st.settingsSynced ? "Cloud Synced" : "SD Local"));
    drawText(28, 132, 0.36f, kColMuted,
             "Watchlist, UI color & playback prefs sync via Supabase.");
    drawText(28, 150, 0.36f, kColMuted,
             "3rd-party addon/debrid keys remain local in addons.json.");
    drawText(16, 214, 0.40f, kColMuted, "Touch bottom buttons to sign in & sync | B: Back");
}

void AppUI::renderTopSettings(C3D_RenderTarget* top) {
    u32 colAcc = accentColor();
    const auto& st = sync_.state();
    const auto& cfg = st.settings;

    C2D_SceneBegin(top);
    C2D_TargetClear(top, kColBg);

    C2D_DrawRectSolid(0, 0, 0.1f, 400, 34, kColPanel);
    drawText(12, 8, 0.55f, colAcc, "Nuvio3DS Settings");
    drawText(278, 10, 0.40f, st.signedIn ? kColGreen : kColAmber,
             st.signedIn ? "ACCOUNT SYNC" : "SD LOCAL");

    C2D_DrawRectSolid(16, 48, 0.1f, 368, 152, kColPanel);
    C2D_DrawRectSolid(28, 60, 0.2f, 18, 18, colAcc);
    drawText(54, 60, 0.48f, kColText, "UI Accent Color: " + cfg.uiAccent);

    drawText(28, 92, 0.40f, kColMuted,
             "Default Catalog: " + cfg.defaultMediaType + "  |  Quality: " + cfg.preferredQuality);
    drawText(28, 116, 0.40f, kColMuted,
             std::string("MVD Direct First: ") + (cfg.preferMvdDirect ? "ON" : "OFF") +
             "  |  Auto-Play: " + (cfg.autoPlayNext ? "ON" : "OFF"));
    drawText(28, 140, 0.40f, kColMuted,
             std::string("Subtitles: ") + (cfg.subtitlesEnabled ? "ON" : "OFF") +
             "  |  Buffer: " + std::to_string(cfg.wifiBufferKb / 1024) + " MB");
    drawText(28, 170, 0.36f, colAcc,
             st.signedIn ? "Synced to user_settings + sdmc:/3ds/Nuvio3DS/settings.json"
                         : "Saved locally (sign in on Account tab to sync)");

    drawText(16, 214, 0.40f, kColMuted, "D-Pad Up/Down: Select  |  A/L/R: Change  |  B: Back");
}

void AppUI::renderBottomTouch(C3D_RenderTarget* bot) {
    u32 colAcc = accentColor();
    C2D_SceneBegin(bot);
    C2D_TargetClear(bot, kColBg);

    // 5-tab bottom bar (62px each across 320px width)
    const char* labels[5] = {"Catalog", "Streams", "Addons", "Account", "Settings"};
    ScreenTab tabs[5] = {
        ScreenTab::Catalog,
        ScreenTab::DetailStreams,
        ScreenTab::Addons,
        ScreenTab::Account,
        ScreenTab::Settings
    };
    for (int i = 0; i < 5; ++i) {
        bool active = (activeTab_ == tabs[i]);
        C2D_DrawRectSolid(4.0f + i * 62.8f, 4.0f, 0.1f, 60.0f, 28.0f,
                          active ? kColPanelHi : kColPanel);
        drawText(7.0f + i * 62.8f, 11.0f, 0.34f,
                 active ? colAcc : kColMuted, labels[i]);
    }

    if (activeTab_ == ScreenTab::Catalog) {
        size_t pageStart = (selectedCatalogIdx_ / 4) * 4;
        for (size_t i = 0; i < 4 && pageStart + i < catalogItems_.size(); ++i) {
            size_t idx = pageStart + i;
            bool sel = (idx == selectedCatalogIdx_);
            float x = (i % 2 == 0) ? 8.0f : 164.0f;
            float y = (i < 2) ? 42.0f : 128.0f;
            // Card + 3DS homebrew selection frame
            if (sel) C2D_DrawRectSolid(x - 2, y - 2, 0.08f, 152, 82, colAcc);
            C2D_DrawRectSolid(x, y, 0.1f, 148, 78, sel ? kColPanelHi : kColPanel);
            // Mini 2:3 poster box inside card
            C2D_DrawRectSolid(x + 6, y + 6, 0.14f, 40, 64, kColBg);
            C2D_DrawRectSolid(x + 6, y + 6, 0.16f, 40, 3, colAcc);
            drawText(x + 12, y + 26, 0.44f, colAcc, truncateText(catalogItems_[idx].name, 2));
            drawText(x + 54, y + 10, 0.38f, sel ? kColText : kColMuted,
                     truncateText(catalogItems_[idx].name, 13));
            drawText(x + 54, y + 34, 0.34f, kColMuted,
                     catalogItems_[idx].releaseInfo.empty() ? "N/A" : catalogItems_[idx].releaseInfo);
            if (!catalogItems_[idx].rating.empty()) {
                drawText(x + 54, y + 52, 0.34f, colAcc, "IMDb " + catalogItems_[idx].rating);
            }
        }
    } else if (activeTab_ == ScreenTab::DetailStreams) {
        size_t pageStart = (selectedStreamIdx_ / 5) * 5;
        for (size_t i = 0; i < 5 && pageStart + i < streams_.size(); ++i) {
            size_t idx = pageStart + i;
            bool sel = (idx == selectedStreamIdx_);
            const auto& st = streams_[idx];
            u32 badgeCol = kColAmber;
            const char* badgeTxt = "PROXY";
            if (st.compat == stremio::StreamCompat::MvdDirect) {
                badgeCol = kColGreen;
                badgeTxt = "MVD";
            } else if (st.compat == stremio::StreamCompat::UnsupportedP2P) {
                badgeCol = kColRed;
                badgeTxt = "P2P";
            }
            float y = 42.0f + i * 36.0f;
            C2D_DrawRectSolid(8, y, 0.1f, 304, 32, sel ? kColPanelHi : kColPanel);
            if (sel) C2D_DrawRectSolid(8, y, 0.15f, 3, 32, colAcc);
            drawText(14, y + 7, 0.40f, badgeCol, badgeTxt);
            drawText(58, y + 7, 0.42f, sel ? kColText : kColMuted,
                     truncateText(st.name + " " + st.title, 26));
        }
    } else if (activeTab_ == ScreenTab::Addons) {
        const auto& list = addons_.addons();
        for (size_t i = 0; i < 4 && i < list.size(); ++i) {
            float y = 42.0f + i * 36.0f;
            bool sel = (i == selectedAddonIdx_);
            C2D_DrawRectSolid(8, y, 0.1f, 304, 32, sel ? kColPanelHi : kColPanel);
            drawText(16, y + 7, 0.42f, list[i].enabled ? kColGreen : kColMuted,
                     list[i].enabled ? "[ON]" : "[OFF]");
            drawText(64, y + 7, 0.42f, kColText, truncateText(list[i].name, 26));
        }
        C2D_DrawRectSolid(8, 196, 0.1f, 148, 34, kColPanelHi);
        drawText(26, 205, 0.44f, colAcc, "+ Add Addon");
        C2D_DrawRectSolid(164, 196, 0.1f, 148, 34, kColPanelHi);
        drawText(184, 205, 0.44f, kColText, "Set Proxy URL");
    } else if (activeTab_ == ScreenTab::Account) {
        C2D_DrawRectSolid(12, 46, 0.1f, 296, 38, kColPanelHi);
        drawText(24, 57, 0.44f, kColText, "1. Set Nuvio / Supabase URL & Key");

        C2D_DrawRectSolid(12, 92, 0.1f, 296, 38, kColPanelHi);
        drawText(24, 103, 0.44f, colAcc, "2. Sign In (Email & Password)");

        C2D_DrawRectSolid(12, 138, 0.1f, 296, 38, kColPanel);
        drawText(24, 149, 0.44f, sync_.state().signedIn ? kColGreen : kColMuted,
                 "3. Pull Watchlist & Settings");

        if (sync_.state().signedIn) {
            C2D_DrawRectSolid(12, 184, 0.1f, 296, 32, kColRed);
            drawText(120, 192, 0.42f, kColText, "Sign Out");
        }
    } else if (activeTab_ == ScreenTab::Settings) {
        const auto& cfg = sync_.state().settings;
        const char* names[kNumSettingsRows] = {
            "UI Color",
            "Default Catalog",
            "Stream Quality",
            "Prefer MVD Direct",
            "Auto-Play Next",
            "Subtitles",
            "Wi-Fi Buffer"
        };
        std::string vals[kNumSettingsRows] = {
            cfg.uiAccent,
            cfg.defaultMediaType,
            cfg.preferredQuality,
            cfg.preferMvdDirect ? "ON" : "OFF",
            cfg.autoPlayNext ? "ON" : "OFF",
            cfg.subtitlesEnabled ? "ON" : "OFF",
            std::to_string(cfg.wifiBufferKb / 1024) + " MB"
        };

        size_t pageStart = (selectedSettingIdx_ / 5) * 5;
        for (size_t i = 0; i < 5 && pageStart + i < kNumSettingsRows; ++i) {
            size_t idx = pageStart + i;
            bool sel = (idx == selectedSettingIdx_);
            float y = 42.0f + i * 36.0f;
            C2D_DrawRectSolid(8, y, 0.1f, 304, 32, sel ? kColPanelHi : kColPanel);
            if (sel) C2D_DrawRectSolid(8, y, 0.15f, 3, 32, colAcc);
            drawText(16, y + 7, 0.42f, sel ? kColText : kColMuted, names[idx]);
            drawText(214, y + 7, 0.42f, idx == 0 ? colAcc : kColText, vals[idx]);
        }
    } else if (activeTab_ == ScreenTab::Player) {
        const auto& st = player_.stats();
        C2D_DrawRectSolid(12, 48, 0.1f, 296, 76, kColPanel);
        drawText(22, 58, 0.45f, kColText, truncateText(player_.title(), 28));
        drawText(22, 82, 0.40f, colAcc, st.statusMessage);
        C2D_DrawRectSolid(12, 148, 0.1f, 142, 46, kColPanelHi);
        drawText(48, 162, 0.48f, kColText,
                 st.state == player::PlayerState::Paused ? "Resume" : "Pause");
        C2D_DrawRectSolid(166, 148, 0.1f, 142, 46, kColRed);
        drawText(214, 162, 0.48f, kColText, "Stop");
    }

    drawText(10, 224, 0.36f, kColMuted, truncateText(bannerMsg_, 44));
}

void AppUI::render(C3D_RenderTarget* topScreen, C3D_RenderTarget* botScreen) {
    C2D_TextBufClear(textBuf_);
    switch (activeTab_) {
        case ScreenTab::Catalog: renderTopCatalog(topScreen); break;
        case ScreenTab::DetailStreams: renderTopDetail(topScreen); break;
        case ScreenTab::Player: renderTopPlayer(topScreen); break;
        case ScreenTab::Addons: renderTopAddons(topScreen); break;
        case ScreenTab::Account: renderTopAccount(topScreen); break;
        case ScreenTab::Settings: renderTopSettings(topScreen); break;
    }
    renderBottomTouch(botScreen);
}

} // namespace nuvio::ui
