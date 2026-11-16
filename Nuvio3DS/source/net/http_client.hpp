#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace nuvio::net {

struct HttpHeader {
    std::string name;
    std::string value;
};

struct HttpResponse {
    long statusCode = 0;
    std::string body;
    std::string error;
    bool ok() const { return error.empty() && statusCode >= 200 && statusCode < 300; }
};

class HttpClient {
public:
    static bool globalInit();
    static void globalCleanup();

    explicit HttpClient(bool verifyTls = false);

    void setVerifyTls(bool verify) { verifyTls_ = verify; }
    void setCaBundlePath(const std::string& path) { caBundlePath_ = path; }

    HttpResponse get(
        const std::string& url,
        long timeoutSec = 15,
        const std::vector<HttpHeader>& headers = {}) const;

    HttpResponse postJson(
        const std::string& url,
        const std::string& jsonBody,
        long timeoutSec = 15,
        const std::vector<HttpHeader>& headers = {}) const;

    // Streams an HTTP/HTTPS resource in chunks for MVD H.264 playback or large downloads.
    bool streamRange(
        const std::string& url,
        uint64_t startByte,
        uint64_t endByte,
        const std::function<bool(const uint8_t* data, size_t len)>& onChunk,
        long timeoutSec = 20,
        std::string* outError = nullptr) const;

    static std::string urlEncode(const std::string& value);

private:
    bool verifyTls_ = false;
    std::string caBundlePath_ = "romfs:/cacert.pem";
};

} // namespace nuvio::net
