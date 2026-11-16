#pragma once

#include "net/http_client.hpp"
#include <string>
#include <vector>

namespace nuvio::stremio {

enum class StreamCompat {
    MvdDirect,        // Direct HTTP/HTTPS H.264 <= 480p: plays natively on New 3DS MVD
    ProxyTranscode,   // Direct HTTP/HTTPS HD/4K/HEVC/HLS: needs optional proxy transcode
    UnsupportedP2P    // Magnet / infoHash torrent: cannot stream on 3DS without debrid/proxy
};

struct CatalogRef {
    std::string type;  // "movie" or "series"
    std::string id;    // e.g. "top"
    std::string name;  // e.g. "Popular"
};

struct AddonEntry {
    std::string baseUrl;     // URL without /manifest.json
    std::string id;
    std::string name;
    std::string version;
    std::string description;
    bool enabled = true;
    std::vector<CatalogRef> catalogs;
    std::vector<std::string> resources;
};

struct MetaPreview {
    std::string id;          // IMDB / TMDB id (e.g., "tt0133093")
    std::string type;        // "movie" or "series"
    std::string name;
    std::string releaseInfo;
    std::string poster;
    std::string description;
    std::string runtime;
    std::string rating;
};

struct StreamCandidate {
    std::string addonName;
    std::string title;
    std::string name;
    std::string url;
    std::string infoHash;
    StreamCompat compat = StreamCompat::ProxyTranscode;
    std::string compatReason;
};

class AddonClient {
public:
    explicit AddonClient(const net::HttpClient& http);

    // Loads or seeds default addons at sdmc:/3ds/Nuvio3DS/addons.json
    bool loadAddonsFromSd();
    bool saveAddonsToSd() const;

    const std::vector<AddonEntry>& addons() const { return addons_; }
    std::vector<AddonEntry>& addonsMut() { return addons_; }

    const std::string& transcodeProxyUrl() const { return transcodeProxyUrl_; }
    void setTranscodeProxyUrl(const std::string& url) { transcodeProxyUrl_ = url; }

    bool installAddon(const std::string& manifestUrl, std::string* outError = nullptr);
    void toggleAddon(size_t index);
    void removeAddon(size_t index);

    bool fetchCatalog(
        const AddonEntry& addon,
        const CatalogRef& cat,
        const std::string& searchQuery,
        std::vector<MetaPreview>& outItems,
        std::string* outError = nullptr) const;

    bool fetchMetaDetail(
        const std::string& type,
        const std::string& id,
        MetaPreview& outMeta,
        std::string* outError = nullptr) const;

    bool fetchStreams(
        const std::string& type,
        const std::string& id,
        std::vector<StreamCandidate>& outStreams,
        std::string* outError = nullptr) const;

    // Resolves either direct stream URL or wraps it with transcodeProxyUrl_ if configured.
    std::string resolvePlaybackUrl(const StreamCandidate& candidate) const;

    static StreamCompat classifyStream(
        const std::string& name,
        const std::string& title,
        const std::string& url,
        const std::string& infoHash,
        std::string& outReason);

private:
    const net::HttpClient& http_;
    std::vector<AddonEntry> addons_;
    std::string transcodeProxyUrl_; // Optional: e.g. "http://192.168.1.50:8787/transcode?url="

    static std::string normalizeBaseUrl(const std::string& inputUrl);
    static std::string toLowerAscii(const std::string& s);
};

} // namespace nuvio::stremio
