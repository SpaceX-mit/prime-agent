// tests/test_http.cpp
#define TEST_FRAMEWORK_IMPLEMENT
#include "test_framework.hpp"

#include "pi_http/http_types.hpp"

#include <string>

using namespace pi::http;

TEST_CASE("full_url no query") {
    HttpRequest r;
    r.url = "https://api.example.com/v1/chat";
    CHECK(r.full_url() == "https://api.example.com/v1/chat");
}

TEST_CASE("full_url with query") {
    HttpRequest r;
    r.url = "https://api.example.com/v1/chat";
    r.query["a"] = "1";
    r.query["b"] = "hello world";
    auto u = r.full_url();
    CHECK(u.find("a=1") != std::string::npos);
    CHECK(u.find("b=hello%20world") != std::string::npos);
}

TEST_CASE("full_url url-encode special chars") {
    HttpRequest r;
    r.url = "https://api.example.com/x";
    r.query["k"] = "a/b+c";
    auto u = r.full_url();
    CHECK(u.find("k=a%2Fb%2Bc") != std::string::npos);
}
