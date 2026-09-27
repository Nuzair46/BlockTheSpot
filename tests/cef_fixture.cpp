#include "../Hook/funct_pointer.h"
#include <cstddef>
static bool partial = false;
static wchar_t filename[] = L"test.js";
static CefString* __stdcall name(void*) { return new CefString{filename, 7, nullptr}; }
static int64_t __stdcall size(void*) { return partial ? 10 : 5; }
static int64_t __stdcall tell(void*) { return 5; }
static int __stdcall read(void*, void* buffer, size_t capacity) {
    if (capacity < 5) return 0;
    const unsigned char bytes[]{0xAA, 0xBB, 0xCC, 0xDD, 0xEE};
    memcpy(buffer, bytes, 5); return 5;
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
