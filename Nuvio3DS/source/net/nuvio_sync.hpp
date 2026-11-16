#pragma once

#include "net/http_client.hpp"
#include <string>
#include <vector>

namespace nuvio::net {

struct SyncedItem {
    std::string id;
    std::string type;
    std::string title;
    int progressSeconds = 0;
};

struct UserSettings {
    std::string uiAccent = "sky";          // sky | emerald | amber | violet | rose
    std::string defaultMediaType = "movie"; // movie | series
    std::string preferredQuality = "360p";  // 360p | 480p
    bool autoPlayNext = true;
    bool preferMvdDirect = true;
    bool subtitlesEnabled = true;
    int wifiBufferKb = 4096;               // 2048 | 4096 | 8192
};

struct AccountState {
    std::string supabaseUrl;
    std::string anonKey;
    std::string email;
    std::string accessToken;
    std::string refreshToken;
    std::string userId;
    bool signedIn = false;
    std::vector<SyncedItem> watchlist;
    UserSettings settings;
    bool settingsSynced = false;
};

class NuvioSyncClient {
public:
    explicit NuvioSyncClient(const HttpClient& http);

    bool loadFromSd();
    bool saveToSd() const;

    bool loadSettingsFromSd();
    bool saveSettingsToSd() const;

    const AccountState& state() const { return state_; }
    void setSupabaseConfig(const std::string& url, const std::string& anonKey);

    // Authenticates against Supabase Auth (/auth/v1/token?grant_type=password).
    bool signIn(const std::string& email, const std::string& password, std::string* outError = nullptr);
    void signOut();

    // Pulls synced watchlist / watch progress & user_settings from Nuvio Supabase tables.
    bool pullSyncData(std::string* outError = nullptr);

    // Updates local settings on SD card and upserts to Supabase user_settings when signed in.
    bool updateSettings(const UserSettings& next, std::string* outError = nullptr);
    bool pushSettings(std::string* outError = nullptr);

private:
    const HttpClient& http_;
    AccountState state_;

    static std::string trimTrailingSlash(const std::string& s);
};

} // namespace nuvio::net
