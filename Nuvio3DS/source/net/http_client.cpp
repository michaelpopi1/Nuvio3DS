#include "net/http_client.hpp"

#include <3ds.h>
#include <curl/curl.h>
#include <cstdio>
#include <cstring>

namespace nuvio::net {

namespace {

size_t writeToStringCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    size_t total = size * nmemb;
    auto* str = static_cast<std::string*>(userp);
    if (str->size() + total > 4 * 1024 * 1024) {
        // Guard against 3DS FCRAM exhaustion on oversized JSON payloads.
        return 0;
    }
    str->append(static_cast<const char*>(contents), total);
    return total;
}

struct StreamContext {
    const std::function<bool(const uint8_t*, size_t)>* cb;
    bool aborted = false;
};

size_t writeToStreamCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    size_t total = size * nmemb;
    auto* ctx = static_cast<StreamContext*>(userp);
    if (!ctx || !ctx->cb || !(*ctx->cb)(static_cast<const uint8_t*>(contents), total)) {
        if (ctx) ctx->aborted = true;
        return 0;
    }
    return total;
}

} // namespace

bool HttpClient::globalInit() {
    return curl_global_init(CURL_GLOBAL_DEFAULT) == CURLE_OK;
}

void HttpClient::globalCleanup() {
    curl_global_cleanup();
}

HttpClient::HttpClient(bool verifyTls) : verifyTls_(verifyTls) {}

HttpResponse HttpClient::get(
    const std::string& url,
    long timeoutSec,
    const std::vector<HttpHeader>& headers) const {
    HttpResponse resp;
    CURL* curl = curl_easy_init();
    if (!curl) {
        resp.error = "curl_easy_init failed";
        return resp;
    }

    struct curl_slist* headerList = nullptr;
    for (const auto& h : headers) {
        std::string line = h.name + ": " + h.value;
        headerList = curl_slist_append(headerList, line.c_str());
    }
    if (headerList) {
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headerList);
    }

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "Nuvio3DS/1.0 (New3DS; libctru)");
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 5L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, timeoutSec);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeToStringCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &resp.body);

    // Enable TLS 1.2+ via mbedTLS bundled in 3ds-curl portlib.
    if (verifyTls_) {
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
        curl_easy_setopt(curl, CURLOPT_CAINFO, caBundlePath_.c_str());
    } else {
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    }

    CURLcode res = curl_easy_perform(curl);
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &resp.statusCode);

    if (res != CURLE_OK) {
        resp.error = curl_easy_strerror(res);
    }

    if (headerList) curl_slist_free_all(headerList);
    curl_easy_cleanup(curl);
    return resp;
}

HttpResponse HttpClient::postJson(
    const std::string& url,
    const std::string& jsonBody,
    long timeoutSec,
    const std::vector<HttpHeader>& headers) const {
    HttpResponse resp;
    CURL* curl = curl_easy_init();
    if (!curl) {
        resp.error = "curl_easy_init failed";
        return resp;
    }

    struct curl_slist* headerList = nullptr;
    headerList = curl_slist_append(headerList, "Content-Type: application/json");
    for (const auto& h : headers) {
        std::string line = h.name + ": " + h.value;
        headerList = curl_slist_append(headerList, line.c_str());
    }

    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headerList);
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "Nuvio3DS/1.0 (New3DS; libctru)");
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, jsonBody.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(jsonBody.size()));
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, timeoutSec);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeToStringCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &resp.body);

    if (verifyTls_) {
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
        curl_easy_setopt(curl, CURLOPT_CAINFO, caBundlePath_.c_str());
    } else {
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    }

    CURLcode res = curl_easy_perform(curl);
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &resp.statusCode);
    if (res != CURLE_OK) {
        resp.error = curl_easy_strerror(res);
    }

    if (headerList) curl_slist_free_all(headerList);
    curl_easy_cleanup(curl);
    return resp;
}

bool HttpClient::streamRange(
    const std::string& url,
    uint64_t startByte,
    uint64_t endByte,
    const std::function<bool(const uint8_t* data, size_t len)>& onChunk,
    long timeoutSec,
    std::string* outError) const {
    CURL* curl = curl_easy_init();
    if (!curl) {
        if (outError) *outError = "curl_easy_init failed";
        return false;
    }

    StreamContext ctx{&onChunk, false};
    char rangeHdr[64];
    if (endByte >= startByte && endByte > 0) {
        std::snprintf(rangeHdr, sizeof(rangeHdr), "%llu-%llu",
                      static_cast<unsigned long long>(startByte),
                      static_cast<unsigned long long>(endByte));
        curl_easy_setopt(curl, CURLOPT_RANGE, rangeHdr);
    }

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "Nuvio3DS/1.0 (New3DS; libctru)");
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, timeoutSec);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeToStreamCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &ctx);

    if (verifyTls_) {
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
        curl_easy_setopt(curl, CURLOPT_CAINFO, caBundlePath_.c_str());
    } else {
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    }

    CURLcode res = curl_easy_perform(curl);
    long httpCode = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);
    curl_easy_cleanup(curl);

    if (ctx.aborted) return true;
    if (res != CURLE_OK) {
        if (outError) *outError = curl_easy_strerror(res);
        return false;
    }
    if (httpCode < 200 || httpCode >= 300) {
        if (outError) *outError = "HTTP status " + std::to_string(httpCode);
        return false;
    }
    return true;
}

std::string HttpClient::urlEncode(const std::string& value) {
    static const char kHex[] = "0123456789ABCDEF";
    std::string out;
    out.reserve(value.size() * 3);
    for (unsigned char c : value) {
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') {
            out.push_back(static_cast<char>(c));
        } else {
            out.push_back('%');
            out.push_back(kHex[(c >> 4) & 0x0F]);
            out.push_back(kHex[c & 0x0F]);
        }
    }
    return out;
}

} // namespace nuvio::net
