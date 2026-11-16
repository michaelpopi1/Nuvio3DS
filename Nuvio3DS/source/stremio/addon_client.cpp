#include "stremio/addon_client.hpp"
#include "util/mini_json.hpp"

#include <3ds.h>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <sys/stat.h>

namespace nuvio::stremio {

namespace {

constexpr const char* kConfigDir = "sdmc:/3ds/Nuvio3DS";
constexpr const char* kAddonsPath = "sdmc:/3ds/Nuvio3DS/addons.json";

void ensureConfigDirectory() {
    mkdir("sdmc:/3ds", 0777);
    mkdir(kConfigDir, 0777);
}

bool containsAny(const std::string& haystack, const std::vector<const char*>& needles) {
    for (const char* n : needles) {
        if (haystack.find(n) != std::string::npos) return true;
    }
    return false;
}

} // namespace

AddonClient::AddonClient(const net::HttpClient& http) : http_(http) {}

std::string AddonClient::toLowerAscii(const std::string& s) {
    std::string out = s;
    for (char& c : out) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return out;
}

std::string AddonClient::normalizeBaseUrl(const std::string& inputUrl) {
    std::string u = inputUrl;
    while (!u.empty() && u.back() == '/') u.pop_back();
    const std::string suffix = "/manifest.json";
    if (u.size() > suffix.size() &&
        u.compare(u.size() - suffix.size(), suffix.size(), suffix) == 0) {
        u.erase(u.size() - suffix.size());
    }
    return u;
}

bool AddonClient::loadAddonsFromSd() {
    addons_.clear();
    FILE* f = std::fopen(kAddonsPath, "rb");
    if (f) {
        std::fseek(f, 0, SEEK_END);
        long sz = std::ftell(f);
        std::fseek(f, 0, SEEK_SET);
        if (sz > 0 && sz < 512 * 1024) {
            std::string raw(static_cast<size_t>(sz), '\0');
            std::fread(&raw[0], 1, static_cast<size_t>(sz), f);
            std::fclose(f);

            json::Value root;
            if (json::parse(raw, root) && root.isObject()) {
                transcodeProxyUrl_ = root.get("transcodeProxyUrl").asString("");
                const auto& arr = root.get("addons");
                if (arr.isArray()) {
                    for (const auto& item : arr.arrVal) {
                        AddonEntry entry;
                        entry.baseUrl = item.get("baseUrl").asString();
                        entry.id = item.get("id").asString();
                        entry.name = item.get("name").asString();
                        entry.version = item.get("version").asString("1.0.0");
                        entry.description = item.get("description").asString();
                        entry.enabled = item.get("enabled").asBool(true);
                        const auto& cats = item.get("catalogs");
                        if (cats.isArray()) {
                            for (const auto& c : cats.arrVal) {
                                entry.catalogs.push_back({
                                    c.get("type").asString("movie"),
                                    c.get("id").asString("top"),
                                    c.get("name").asString("Popular"),
                                });
                            }
                        }
                        const auto& res = item.get("resources");
                        if (res.isArray()) {
                            for (const auto& r : res.arrVal) {
                                if (r.isString()) entry.resources.push_back(r.strVal);
                            }
                        }
                        if (!entry.baseUrl.empty()) addons_.push_back(entry);
                    }
                }
            }
        } else {
            std::fclose(f);
        }
    }

    if (addons_.empty()) {
        // Seed Cinemeta (standard public Stremio catalog addon) by default.
        AddonEntry cinemeta;
        cinemeta.baseUrl = "https://v3-cinemeta.strem.io";
        cinemeta.id = "com.linvo.cinemeta";
        cinemeta.name = "Cinemeta";
        cinemeta.version = "3.0.0";
        cinemeta.description = "Official TMDB/IMDB catalog & metadata provider";
        cinemeta.enabled = true;
        cinemeta.resources = {"catalog", "meta"};
        cinemeta.catalogs = {
            {"movie", "top", "Popular Movies"},
            {"series", "top", "Popular Series"},
        };
        addons_.push_back(cinemeta);
        saveAddonsToSd();
    }

    return true;
}

bool AddonClient::saveAddonsToSd() const {
    ensureConfigDirectory();

    json::Value root;
    root.type = json::Type::Object;

    json::Value proxyVal;
    proxyVal.type = json::Type::String;
    proxyVal.strVal = transcodeProxyUrl_;
    root.objVal["transcodeProxyUrl"] = proxyVal;

    json::Value arr;
    arr.type = json::Type::Array;
    for (const auto& a : addons_) {
        json::Value obj;
        obj.type = json::Type::Object;
        obj.objVal["baseUrl"] = {json::Type::String, false, 0.0, a.baseUrl, {}, {}};
        obj.objVal["id"] = {json::Type::String, false, 0.0, a.id, {}, {}};
        obj.objVal["name"] = {json::Type::String, false, 0.0, a.name, {}, {}};
        obj.objVal["version"] = {json::Type::String, false, 0.0, a.version, {}, {}};
        obj.objVal["description"] = {json::Type::String, false, 0.0, a.description, {}, {}};
        obj.objVal["enabled"] = {json::Type::Bool, a.enabled, 0.0, "", {}, {}};

        json::Value cats;
        cats.type = json::Type::Array;
        for (const auto& c : a.catalogs) {
            json::Value co;
            co.type = json::Type::Object;
            co.objVal["type"] = {json::Type::String, false, 0.0, c.type, {}, {}};
            co.objVal["id"] = {json::Type::String, false, 0.0, c.id, {}, {}};
            co.objVal["name"] = {json::Type::String, false, 0.0, c.name, {}, {}};
            cats.arrVal.push_back(co);
        }
        obj.objVal["catalogs"] = cats;

        json::Value res;
        res.type = json::Type::Array;
        for (const auto& r : a.resources) {
            res.arrVal.push_back({json::Type::String, false, 0.0, r, {}, {}});
        }
        obj.objVal["resources"] = res;
        arr.arrVal.push_back(obj);
    }
    root.objVal["addons"] = arr;

    std::string serialized = json::stringify(root);
    FILE* f = std::fopen(kAddonsPath, "wb");
    if (!f) return false;
    std::fwrite(serialized.data(), 1, serialized.size(), f);
    std::fclose(f);
    return true;
}

bool AddonClient::installAddon(const std::string& manifestUrl, std::string* outError) {
    std::string base = normalizeBaseUrl(manifestUrl);
    if (base.empty()) {
        if (outError) *outError = "Invalid addon URL";
        return false;
    }

    auto resp = http_.get(base + "/manifest.json");
    if (!resp.ok()) {
        if (outError) *outError = resp.error.empty() ? "Manifest HTTP error" : resp.error;
        return false;
    }

    json::Value root;
    if (!json::parse(resp.body, root) || !root.isObject()) {
        if (outError) *outError = "Invalid manifest.json format";
        return false;
    }

    AddonEntry entry;
    entry.baseUrl = base;
    entry.id = root.get("id").asString(base);
    entry.name = root.get("name").asString("Custom Addon");
    entry.version = root.get("version").asString("1.0.0");
    entry.description = root.get("description").asString("");
    entry.enabled = true;

    const auto& cats = root.get("catalogs");
    if (cats.isArray()) {
        for (const auto& c : cats.arrVal) {
            entry.catalogs.push_back({
                c.get("type").asString("movie"),
                c.get("id").asString("top"),
                c.get("name").asString("Catalog"),
            });
        }
    }

    const auto& resources = root.get("resources");
    if (resources.isArray()) {
        for (const auto& r : resources.arrVal) {
            if (r.isString()) {
                entry.resources.push_back(r.strVal);
            } else if (r.isObject() && r.has("name")) {
                entry.resources.push_back(r.get("name").asString());
            }
        }
    }

    for (auto& existing : addons_) {
        if (existing.baseUrl == entry.baseUrl) {
            existing = entry;
            saveAddonsToSd();
            return true;
        }
    }
    addons_.push_back(entry);
    saveAddonsToSd();
    return true;
}

void AddonClient::toggleAddon(size_t index) {
    if (index < addons_.size()) {
        addons_[index].enabled = !addons_[index].enabled;
        saveAddonsToSd();
    }
}

void AddonClient::removeAddon(size_t index) {
    if (index < addons_.size()) {
        addons_.erase(addons_.begin() + static_cast<long>(index));
        saveAddonsToSd();
    }
}

bool AddonClient::fetchCatalog(
    const AddonEntry& addon,
    const CatalogRef& cat,
    const std::string& searchQuery,
    std::vector<MetaPreview>& outItems,
    std::string* outError) const {
    outItems.clear();
    std::string url = addon.baseUrl + "/catalog/" + cat.type + "/" + cat.id;
    if (!searchQuery.empty()) {
        url += "/search=" + net::HttpClient::urlEncode(searchQuery);
    }
    url += ".json";

    auto resp = http_.get(url);
    if (!resp.ok()) {
        if (outError) *outError = resp.error.empty() ? "Catalog HTTP error" : resp.error;
        return false;
    }

    json::Value root;
    if (!json::parse(resp.body, root) || !root.isObject()) {
        if (outError) *outError = "Malformed catalog JSON";
        return false;
    }

    const auto& metas = root.get("metas");
    if (!metas.isArray()) return true;

    for (const auto& m : metas.arrVal) {
        if (outItems.size() >= 40) break; // Cap per-page results for 3DS FCRAM safety
        MetaPreview item;
        item.id = m.get("id").asString();
        item.type = m.get("type").asString(cat.type);
        item.name = m.get("name").asString("Untitled");
        item.releaseInfo = m.get("releaseInfo").asString("");
        item.poster = m.get("poster").asString("");
        item.description = m.get("description").asString("");
        item.runtime = m.get("runtime").asString("");
        item.rating = m.get("imdbRating").asString("");
        if (!item.id.empty()) outItems.push_back(item);
    }
    return true;
}

bool AddonClient::fetchMetaDetail(
    const std::string& type,
    const std::string& id,
    MetaPreview& outMeta,
    std::string* outError) const {
    for (const auto& addon : addons_) {
        if (!addon.enabled) continue;
        bool hasMeta = std::find(addon.resources.begin(), addon.resources.end(), "meta") !=
                       addon.resources.end();
        if (!hasMeta) continue;

        std::string url = addon.baseUrl + "/meta/" + type + "/" + net::HttpClient::urlEncode(id) + ".json";
        auto resp = http_.get(url);
        if (!resp.ok()) continue;

        json::Value root;
        if (!json::parse(resp.body, root)) continue;
        const auto& m = root.get("meta");
        if (!m.isObject()) continue;

        outMeta.id = m.get("id").asString(id);
        outMeta.type = m.get("type").asString(type);
        outMeta.name = m.get("name").asString(outMeta.name);
        outMeta.releaseInfo = m.get("releaseInfo").asString(outMeta.releaseInfo);
        outMeta.description = m.get("description").asString(outMeta.description);
        outMeta.runtime = m.get("runtime").asString(outMeta.runtime);
        outMeta.rating = m.get("imdbRating").asString(outMeta.rating);
        return true;
    }
    if (outError) *outError = "No enabled meta addon returned details";
    return false;
}

StreamCompat AddonClient::classifyStream(
    const std::string& name,
    const std::string& title,
    const std::string& url,
    const std::string& infoHash,
    std::string& outReason) {
    if (!infoHash.empty() || url.rfind("magnet:", 0) == 0) {
        outReason = "P2P torrent requires off-device proxy/debrid";
        return StreamCompat::UnsupportedP2P;
    }

    std::string meta = toLowerAscii(name + " " + title + " " + url);
    if (containsAny(meta, {"hevc", "h265", "x265", "av1", "vp9", "10bit", "4k", "2160p", "1080p", "720p"})) {
        outReason = "Exceeds New 3DS MVD (H.264 <= 480p); use transcode proxy";
        return StreamCompat::ProxyTranscode;
    }

    if (containsAny(meta, {".m3u8", "hls"})) {
        outReason = "HLS playlist needs remux/transcode proxy to Annex-B H.264";
        return StreamCompat::ProxyTranscode;
    }

    if (containsAny(meta, {"480p", "360p", "240p", "h264", "x264", "avc", ".mp4", ".h264"})) {
        outReason = "Compatible with New 3DS MVD hardware H.264 decoder";
        return StreamCompat::MvdDirect;
    }

    outReason = "Unknown stream profile; transcode proxy recommended";
    return StreamCompat::ProxyTranscode;
}

bool AddonClient::fetchStreams(
    const std::string& type,
    const std::string& id,
    std::vector<StreamCandidate>& outStreams,
    std::string* outError) const {
    outStreams.clear();
    int queried = 0;

    for (const auto& addon : addons_) {
        if (!addon.enabled) continue;
        bool hasStream = std::find(addon.resources.begin(), addon.resources.end(), "stream") !=
                         addon.resources.end();
        if (!hasStream) continue;
        ++queried;

        std::string url = addon.baseUrl + "/stream/" + type + "/" + net::HttpClient::urlEncode(id) + ".json";
        auto resp = http_.get(url);
        if (!resp.ok()) continue;

        json::Value root;
        if (!json::parse(resp.body, root)) continue;
        const auto& streams = root.get("streams");
        if (!streams.isArray()) continue;

        for (const auto& s : streams.arrVal) {
            StreamCandidate cand;
            cand.addonName = addon.name;
            cand.name = s.get("name").asString("Stream");
            cand.title = s.get("title").asString(s.get("description").asString(""));
            cand.url = s.get("url").asString("");
            cand.infoHash = s.get("infoHash").asString("");
            cand.compat = classifyStream(cand.name, cand.title, cand.url, cand.infoHash, cand.compatReason);
            outStreams.push_back(cand);
        }
    }

    // Prioritize MvdDirect streams at the top of the list for 3DS playback.
    std::stable_sort(outStreams.begin(), outStreams.end(), [](const StreamCandidate& a, const StreamCandidate& b) {
        return static_cast<int>(a.compat) < static_cast<int>(b.compat);
    });

    if (queried == 0 && outError) {
        *outError = "No stream-capable addons enabled";
        return false;
    }
    return true;
}

std::string AddonClient::resolvePlaybackUrl(const StreamCandidate& candidate) const {
    if (candidate.compat == StreamCompat::MvdDirect || transcodeProxyUrl_.empty()) {
        return candidate.url;
    }
    return transcodeProxyUrl_ + net::HttpClient::urlEncode(candidate.url);
}

} // namespace nuvio::stremio
