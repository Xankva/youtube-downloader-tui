#include "app.hpp"
#include <cstdlib>
#include <iostream>
#include <filesystem>

namespace {

void print_usage() {
    std::cout << "YouTube Downloader TUI v1.0.0" << std::endl;
    std::cout << std::endl;
    std::cout << "Usage: ytd [OPTIONS]" << std::endl;
    std::cout << std::endl;
    std::cout << "Options:" << std::endl;
    std::cout << "  --help, -h     Show this help message" << std::endl;
    std::cout << "  --version, -v  Show version information" << std::endl;
    std::cout << std::endl;
    std::cout << "A modern terminal UI for downloading YouTube videos." << std::endl;
    std::cout << "Uses yt-dlp as the download backend." << std::endl;
}

} // anonymous namespace

int main(int argc, char* argv[]) {
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            print_usage();
            return 0;
        }
        if (arg == "--version" || arg == "-v") {
            std::cout << "ytd v1.0.0" << std::endl;
            return 0;
        }
    }

#ifdef _WIN32
    const char* home_env = std::getenv("USERPROFILE");
    const char* home_drive = std::getenv("HOMEDRIVE");
    const char* home_path = std::getenv("HOMEPATH");
    std::string home_str;
    if (home_env) {
        home_str = home_env;
    } else if (home_drive && home_path) {
        home_str = std::string(home_drive) + home_path;
    }
    if (home_str.empty()) {
        std::cerr << "Cannot determine home directory" << std::endl;
        return 1;
    }
    std::filesystem::path home_dir(home_str);
#else
    auto home = std::getenv("HOME");
    if (!home) {
        std::cerr << "HOME environment variable not set" << std::endl;
        return 1;
    }
    std::filesystem::path home_dir(home);
#endif
    auto config_dir = home_dir / ".config" / "ytd";
    std::filesystem::create_directories(config_dir);
    std::filesystem::create_directories(config_dir / "downloads");

    try {
        yt_tui::App app(config_dir);
        app.run();
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
