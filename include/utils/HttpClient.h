#pragma once
#include <string>
#include <vector>

class HttpClient {
public:
    struct Response {
        int         status_code = 0;
        std::string body;
        bool        success     = false;
    };

    static Response get(const std::string& url);
    static Response get(const std::string& url,
                        const std::vector<std::string>& headers);

private:
    static size_t writeCallback(void* contents, size_t size,
                                size_t nmemb, std::string* userp);
};
