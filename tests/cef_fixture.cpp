#include "../Hook/funct_pointer.h"
#include <cstddef>
#include <string>
#include <vector>
static bool partial = false;
static std::wstring filename = L"test.js", url;
static std::vector<unsigned char> bytes{0xAA, 0xBB, 0xCC, 0xDD, 0xEE};
static CefString* __stdcall name(void*) { return new CefString{filename.data(), filename.size(), nullptr}; }
static int64_t __stdcall size(void*) { return static_cast<int64_t>(bytes.size()) * (partial ? 2 : 1); }
static int64_t __stdcall tell(void*) { return static_cast<int64_t>(bytes.size()); }
static int __stdcall read(void*, void* buffer, size_t capacity) {
    if (capacity < bytes.size()) return 0;
    memcpy(buffer, bytes.data(), bytes.size()); return static_cast<int>(bytes.size());
}
struct Reader {
    size_t bytes = sizeof(Reader);
    char reserved1[64]{};
    decltype(&name) get_name = name;
    decltype(&size) get_size = size;
    char reserved2[24]{};
    decltype(&read) read_file = read;
    decltype(&tell) position = tell;
};
static_assert(offsetof(Reader, get_name) == 72 && offsetof(Reader, read_file) == 112);
static Reader reader;
extern "C" __declspec(dllexport) void* cef_zip_reader_create(void*) { reader.read_file = read; return &reader; }
extern "C" __declspec(dllexport) void cef_string_userfree_utf16_free(CefString* string) { delete string; }
extern "C" __declspec(dllexport) void bts_test_partial(int value) { partial = value != 0; }

extern "C" __declspec(dllexport) void bts_test_file(const wchar_t* file, const unsigned char* data, size_t count) {
    filename = file; bytes.assign(data, data + count);
}
static CefString* __stdcall get_url(void*) { return new CefString{url.data(), url.size(), nullptr}; }
struct Request {
    size_t bytes = sizeof(Request);
    char reserved[40]{};
    decltype(&get_url) read_url = get_url;
};
static_assert(offsetof(Request, read_url) == 48);
static Request request;
extern "C" __declspec(dllexport) void* bts_test_request(const wchar_t* value) { url = value; return &request; }
extern "C" __declspec(dllexport) void* cef_urlrequest_create(void* value, void*, void*) { return value; }
