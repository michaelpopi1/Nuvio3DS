#include "net/nuvio_sync.hpp"
#include "util/mini_json.hpp"

#include <cstdio>
#include <sys/stat.h>

namespace nuvio::net {

namespace {

constexpr const char* kAccountPath = "sdmc:/3ds/Nuvio3DS/account.json";
constexpr const char* kSettingsPath = "sdmc:/3ds/Nuvio3DS/settings.json";

void ensureDir() {
    mkdir("sdmc:/3ds", 0777);
    mkdir("sdmc:/3ds/Nuvio3DS", 0777);
}

std::string normalizeAccent(const std::string& v) {
    if (v == "emerald" || v == "amber" || v == "violet" || v == "rose") return v;
    return "sky";
}

int normalizeBufferKb(int kb) {
    if (kb == 2048 || kb == 8192) return kb;
    return 4096;
}

} // namespace

NuvioSyncClient::NuvioSyncClient(const HttpClient& http) : http_(http) {}

std::string NuvioSyncClient::trimTrailingSlash(const std::string& s) {
    std::string out = s;
    while (!out.empty() && out.back() == '/') out.pop_back();
    return out;
}

void NuvioSyncClient::setSupabaseConfig(const std::string& url, const std::string& anonKey) {
    state_.supabaseUrl = trimTrailingSlash(url);
    state_.anonKey = anonKey;
    saveToSd();
}

bool NuvioSyncClient::loadSettingsFromSd() {
    FILE* f = std::fopen(kSettingsPath, "rb");
    if (!f) return false;

    std::fseek(f, 0, SEEK_END);
    long sz = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    if (sz <= 0 || sz > 64 * 1024) {
        std::fclose(f);
        return false;
    }

    std::string raw(static_cast<size_t>(sz), '\0');
    std::fread(&raw[0], 1, static_cast<size_t>(sz), f);
    std::fclose(f);

    json::Value root;
    if (!json::parse(raw, root) || !root.isObject()) return false;

    state_.settings.uiAccent = normalizeAccent(root.get("uiAccent").asString("sky"));
    std::string mt = root.get("defaultMediaType").asString("movie");
    state_.settings.defaultMediaType = (mt == "series") ? "series" : "movie";
    std::string pq = root.get("preferredQuality").asString("360p");
    state_.settings.preferredQuality = (pq == "480p") ? "480p" : "360p";
    state_.settings.autoPlayNext = root.get("autoPlayNext").asBool(true);
    state_.settings.preferMvdDirect = root.get("preferMvdDirect").asBool(true);
    state_.settings.subtitlesEnabled = root.get("subtitlesEnabled").asBool(true);
    state_.settings.wifiBufferKb = normalizeBufferKb(root.get("wifiBufferKb").asInt(4096));
    return true;
}

bool NuvioSyncClient::saveSettingsToSd() const {
    ensureDir();
    json::Value root;
    root.type = json::Type::Object;
    root.objVal["uiAccent"] = {json::Type::String, false, 0.0, state_.settings.uiAccent, {}, {}};
    root.objVal["defaultMediaType"] = {json::Type::String, false, 0.0, state_.settings.defaultMediaType, {}, {}};
    root.objVal["preferredQuality"] = {json::Type::String, false, 0.0, state_.settings.preferredQuality, {}, {}};
    root.objVal["autoPlayNext"] = {json::Type::Bool, state_.settings.autoPlayNext, 0.0, "", {}, {}};
    root.objVal["preferMvdDirect"] = {json::Type::Bool, state_.settings.preferMvdDirect, 0.0, "", {}, {}};
    root.objVal["subtitlesEnabled"] = {json::Type::Bool, state_.settings.subtitlesEnabled, 0.0, "", {}, {}};
    root.objVal["wifiBufferKb"] = {json::Type::Number, false, static_cast<double>(state_.settings.wifiBufferKb), "", {}, {}};

    std::string out = json::stringify(root);
    FILE* f = std::fopen(kSettingsPath, "wb");
    if (!f) return false;
    std::fwrite(out.data(), 1, out.size(), f);
    std::fclose(f);
    return true;
}

bool NuvioSyncClient::loadFromSd() {
    loadSettingsFromSd();

    FILE* f = std::fopen(kAccountPath, "rb");
    if (!f) return false;

    std::fseek(f, 0, SEEK_END);
    long sz = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    if (sz <= 0 || sz > 256 * 1024) {
        std::fclose(f);
        return false;
    }

    std::string raw(static_cast<size_t>(sz), '\0');
    std::fread(&raw[0], 1, static_cast<size_t>(sz), f);
    std::fclose(f);

    json::Value root;
    if (!json::parse(raw, root) || !root.isObject()) return false;

    state_.supabaseUrl = trimTrailingSlash(root.get("supabaseUrl").asString(""));
    state_.anonKey = root.get("anonKey").asString("");
    state_.email = root.get("email").asString("");
    state_.accessToken = root.get("accessToken").asString("");
    state_.refreshToken = root.get("refreshToken").asString("");
    state_.userId = root.get("userId").asString("");
    state_.signedIn = !state_.accessToken.empty();
    return true;
}

bool NuvioSyncClient::saveToSd() const {
    ensureDir();
    json::Value root;
    root.type = json::Type::Object;
    root.objVal["supabaseUrl"] = {json::Type::String, false, 0.0, state_.supabaseUrl, {}, {}};
    root.objVal["anonKey"] = {json::Type::String, false, 0.0, state_.anonKey, {}, {}};
    root.objVal["email"] = {json::Type::String, false, 0.0, state_.email, {}, {}};
    root.objVal["accessToken"] = {json::Type::String, false, 0.0, state_.accessToken, {}, {}};
    root.objVal["refreshToken"] = {json::Type::String, false, 0.0, state_.refreshToken, {}, {}};
    root.objVal["userId"] = {json::Type::String, false, 0.0, state_.userId, {}, {}};

    std::string out = json::stringify(root);
    FILE* f = std::fopen(kAccountPath, "wb");
    if (!f) return false;
    std::fwrite(out.data(), 1, out.size(), f);
    std::fclose(f);
    return true;
}

bool NuvioSyncClient::signIn(
    const std::string& email,
    const std::string& password,
    std::string* outError) {
    if (state_.supabaseUrl.empty() || state_.anonKey.empty()) {
        if (outError) *outError = "Set Supabase URL and Anon Key first";
        return false;
    }

    json::Value body;
    body.type = json::Type::Object;
    body.objVal["email"] = {json::Type::String, false, 0.0, email, {}, {}};
    body.objVal["password"] = {json::Type::String, false, 0.0, password, {}, {}};

    auto resp = http_.postJson(
        state_.supabaseUrl + "/auth/v1/token?grant_type=password",
        json::stringify(body),
        15,
        {{"apikey", state_.anonKey}});

    if (!resp.ok()) {
        if (outError) *outError = resp.error.empty() ? "Login rejected (HTTP " + std::to_string(resp.statusCode) + ")" : resp.error;
        return false;
    }

    json::Value parsed;
    if (!json::parse(resp.body, parsed) || !parsed.isObject()) {
        if (outError) *outError = "Malformed auth response";
        return false;
    }

    state_.accessToken = parsed.get("access_token").asString("");
    state_.refreshToken = parsed.get("refresh_token").asString("");
    state_.userId = parsed.get("user").get("id").asString("");
    state_.email = email;
    state_.signedIn = !state_.accessToken.empty();

    if (!state_.signedIn) {
        if (outError) *outError = "No access_token returned";
        return false;
    }

    saveToSd();
    return true;
}

void NuvioSyncClient::signOut() {
    state_.accessToken.clear();
    state_.refreshToken.clear();
    state_.userId.clear();
    state_.signedIn = false;
    state_.settingsSynced = false;
    state_.watchlist.clear();
    saveToSd();
}

bool NuvioSyncClient::pullSyncData(std::string* outError) {
    if (!state_.signedIn || state_.supabaseUrl.empty()) {
        if (outError) *outError = "Sign in first";
        return false;
    }

    std::vector<HttpHeader> authHeaders = {
        {"apikey", state_.anonKey},
        {"Authorization", "Bearer " + state_.accessToken},
    };

    auto resp = http_.get(
        state_.supabaseUrl + "/rest/v1/library_items?select=id,type,title,progress_seconds&limit=40",
        15,
        authHeaders);

    if (!resp.ok()) {
        if (outError) *outError = resp.error.empty() ? "Sync HTTP " + std::to_string(resp.statusCode) : resp.error;
        return false;
    }

    json::Value arr;
    if (!json::parse(resp.body, arr) || !arr.isArray()) {
        if (outError) *outError = "Unexpected sync payload";
        return false;
    }

    state_.watchlist.clear();
    for (const auto& item : arr.arrVal) {
        if (!item.isObject()) continue;
        SyncedItem si;
        si.id = item.get("id").asString("");
        si.type = item.get("type").asString("movie");
        si.title = item.get("title").asString("Untitled");
        si.progressSeconds = item.get("progress_seconds").asInt(0);
        if (!si.id.empty()) state_.watchlist.push_back(si);
    }

    // Pull account-synced UI & playback settings from user_settings (non-fatal if absent)
    auto setResp = http_.get(
        state_.supabaseUrl +
            "/rest/v1/user_settings?select=ui_accent,default_media_type,preferred_quality,auto_play_next,prefer_mvd_direct,subtitles_enabled,wifi_buffer_kb&limit=1",
        15,
        authHeaders);
    if (setResp.ok()) {
        json::Value setArr;
        if (json::parse(setResp.body, setArr) && setArr.isArray() && !setArr.arrVal.empty()) {
            const auto& row = setArr.arrVal[0];
            if (row.isObject()) {
                state_.settings.uiAccent = normalizeAccent(row.get("ui_accent").asString(state_.settings.uiAccent));
                std::string mt = row.get("default_media_type").asString(state_.settings.defaultMediaType);
                state_.settings.defaultMediaType = (mt == "series") ? "series" : "movie";
                std::string pq = row.get("preferred_quality").asString(state_.settings.preferredQuality);
                state_.settings.preferredQuality = (pq == "480p") ? "480p" : "360p";
                state_.settings.autoPlayNext = row.get("auto_play_next").asBool(state_.settings.autoPlayNext);
                state_.settings.preferMvdDirect = row.get("prefer_mvd_direct").asBool(state_.settings.preferMvdDirect);
                state_.settings.subtitlesEnabled = row.get("subtitles_enabled").asBool(state_.settings.subtitlesEnabled);
                state_.settings.wifiBufferKb = normalizeBufferKb(row.get("wifi_buffer_kb").asInt(state_.settings.wifiBufferKb));
                state_.settingsSynced = true;
                saveSettingsToSd();
            }
        }
    }

    return true;
}

bool NuvioSyncClient::updateSettings(const UserSettings& next, std::string* outError) {
    state_.settings.uiAccent = normalizeAccent(next.uiAccent);
    state_.settings.defaultMediaType = (next.defaultMediaType == "series") ? "series" : "movie";
    state_.settings.preferredQuality = (next.preferredQuality == "480p") ? "480p" : "360p";
    state_.settings.autoPlayNext = next.autoPlayNext;
    state_.settings.preferMvdDirect = next.preferMvdDirect;
    state_.settings.subtitlesEnabled = next.subtitlesEnabled;
    state_.settings.wifiBufferKb = normalizeBufferKb(next.wifiBufferKb);
    saveSettingsToSd();

    if (state_.signedIn && !state_.supabaseUrl.empty()) {
        return pushSettings(outError);
    }
    state_.settingsSynced = false;
    return true;
}

bool NuvioSyncClient::pushSettings(std::string* outError) {
    if (!state_.signedIn || state_.supabaseUrl.empty()) {
        if (outError) *outError = "Saved locally (sign in to sync)";
        state_.settingsSynced = false;
        return false;
    }

    json::Value row;
    row.type = json::Type::Object;
    if (!state_.userId.empty()) {
        row.objVal["user_id"] = {json::Type::String, false, 0.0, state_.userId, {}, {}};
    }
    row.objVal["ui_accent"] = {json::Type::String, false, 0.0, state_.settings.uiAccent, {}, {}};
    row.objVal["default_media_type"] = {json::Type::String, false, 0.0, state_.settings.defaultMediaType, {}, {}};
    row.objVal["preferred_quality"] = {json::Type::String, false, 0.0, state_.settings.preferredQuality, {}, {}};
    row.objVal["auto_play_next"] = {json::Type::Bool, state_.settings.autoPlayNext, 0.0, "", {}, {}};
    row.objVal["prefer_mvd_direct"] = {json::Type::Bool, state_.settings.preferMvdDirect, 0.0, "", {}, {}};
    row.objVal["subtitles_enabled"] = {json::Type::Bool, state_.settings.subtitlesEnabled, 0.0, "", {}, {}};
    row.objVal["wifi_buffer_kb"] = {json::Type::Number, false, static_cast<double>(state_.settings.wifiBufferKb), "", {}, {}};

    auto resp = http_.postJson(
        state_.supabaseUrl + "/rest/v1/user_settings",
        json::stringify(row),
        15,
        {
            {"apikey", state_.anonKey},
            {"Authorization", "Bearer " + state_.accessToken},
            {"Prefer", "resolution=merge-duplicates"},
        });

    if (!resp.ok()) {
        state_.settingsSynced = false;
        if (outError) *outError = resp.error.empty() ? "Settings push HTTP " + std::to_string(resp.statusCode) : resp.error;
        return false;
    }

    state_.settingsSynced = true;
    return true;
}

} // namespace nuvio::net
