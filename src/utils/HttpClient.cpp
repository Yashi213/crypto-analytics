#include "utils/HttpClient.h"
#include "utils/Logger.h"
#include <curl/curl.h>
#include <stdexcept>

size_t HttpClient::writeCallback(void* contents, size_t size,
                                  size_t nmemb, std::string* userp) {
    size_t total = size * nmemb;
    userp->append(static_cast<char*>(contents), total);
    return total;
}

HttpClient::Response HttpClient::get(const std::string& url) {
    return get(url, {});
}

HttpClient::Response HttpClient::get(const std::string& url,
                                      const std::vector<std::string>& headers) {
    CURL* curl = curl_easy_init();
    if (!curl) return {0, "", false};

    Response resp;

    curl_easy_setopt(curl, CURLOPT_URL,           url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION,  writeCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA,      &resp.body);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT,        30L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT,      "CryptoAnalytics/1.0");

    struct curl_slist* hlist = nullptr;
    for (const auto& h : headers)
        hlist = curl_slist_append(hlist, h.c_str());
    if (hlist)
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, hlist);

    CURLcode res = curl_easy_perform(curl);
    if (res == CURLE_OK) {
        long code = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
        resp.status_code = static_cast<int>(code);
        resp.success     = (code >= 200 && code < 300);
    } else {
        LOG_ERROR("curl error " + std::to_string(res) + ": " + curl_easy_strerror(res));
    }

    if (hlist) curl_slist_free_all(hlist);
    curl_easy_cleanup(curl);
    return resp;
}
