#include "app.hpp"
#include <cstdlib>
#include <iostream>
#include <filesystem>
#include <signal.h>

namespace {

void handle_signal(int) {
    // Signal handler placeholder - FTXUI handles graceful shutdown internally
}

void print_usage() {
    std::cout << "YouTube TUI Downloader v1.0.0" << std::endl;
    std::cout << std::endl;
    std::cout << "Usage: yt-tui [OPTIONS]" << std::endl;
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
            std::cout << "yt-tui v1.0.0" << std::endl;
            return 0;
        }
    }

    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    auto home = std::getenv("HOME");
    if (!home) {
        std::cerr << "HOME environment variable not set" << std::endl;
        return 1;
    }

    std::filesystem::path home_dir(home);
    auto config_dir = home_dir / ".config" / "yt-tui";
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
