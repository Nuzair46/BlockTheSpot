#pragma once
#include <string_view>
namespace bts {
inline std::wstring_view url_path(std::wstring_view url) {
    size_t scheme = url.find(L"://");
    if (scheme == std::wstring_view::npos) return {};
    size_t authority = scheme + 3;
    size_t start = url.find_first_of(L"/?#", authority);
    if (start == std::wstring_view::npos || url[start] != L'/') return {};
    return url.substr(start, url.find_first_of(L"?#", start) - start);
}
}
