#include "../Hook/funct_pointer.h"
#include "../Hook/log_thread.h"
#include "../Common/windows.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>
#include <vector>
#include <stdexcept>
#define CHECK(x) do { if (!(x)) throw std::runtime_error(#x); } while (false)
static int first() { return 1; }
static int second() { return 2; }

int main(int argc, char** argv) {
    CHECK(argc == 2);
    install_directory = std::filesystem::absolute(argv[1]).wstring();
    std::filesystem::create_directories(install_directory);
    using fn = int(*)();
    struct Object { size_t size; fn call; } object{sizeof(Object), first};
    CHECK(!get_funct_t<fn>(nullptr, 0));
    CHECK(!get_funct_t<fn>(&object, sizeof(Object)));
    CHECK(get_funct_t<fn>(&object, offsetof(Object, call))() == 1);
    CHECK(overwrite_funct_t(&object, offsetof(Object, call), second));
    CHECK(object.call() == 2);
    CHECK(!overwrite_funct_t(&object, sizeof(Object), first));
    init_log(2);
    std::vector<std::thread> writers;
    for (int i = 0; i < 8; ++i) writers.emplace_back([i] {
        for (int n = 0; n < 2500; ++n) log_debug(("worker " + std::to_string(i) + " bounded concurrent logging test").c_str());
    });
    for (auto& thread : writers) thread.join();
    set_status("test", "failed", "expected failure");
    set_status("test", "applied");
    std::string status;
    CHECK(bts::read_text(install_directory + L"\\blockthespot-status.txt", status));
    CHECK(status.find("test: failed") != std::string::npos);
    CHECK(std::filesystem::file_size(install_directory + L"\\blockthespot.log") <= 1024 * 1024 + 2048);
    CHECK(std::filesystem::exists(install_directory + L"\\blockthespot.log.1"));
    std::cout << "Windows: CEF member bounds, callback writes, concurrent logging, rotation, and health report passed\n";
}
