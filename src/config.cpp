#include "config.hpp"
#include <fstream>
#include <algorithm>

namespace yt_tui {

std::vector<FormatQuality> Config::video_qualities() {
    return {
        {"Best", "best"},
        {"2160p (4K)", "bestvideo[height<=2160]+bestaudio/best[height<=2160]"},
        {"1440p (2K)", "bestvideo[height<=1440]+bestaudio/best[height<=1440]"},
        {"1080p (Full HD)", "bestvideo[height<=1080]+bestaudio/best[height<=1080]"},
        {"720p (HD)", "bestvideo[height<=720]+bestaudio/best[height<=720]"},
        {"480p", "bestvideo[height<=480]+bestaudio/best[height<=480]"},
        {"360p", "bestvideo[height<=360]+bestaudio/best[height<=360]"},
        {"240p", "bestvideo[height<=240]+bestaudio/best[height<=240]"},
        {"144p", "bestvideo[height<=144]+bestaudio/best[height<=144]"},
    };
}

std::vector<std::string> Config::audio_formats() {
    return {"mp3", "m4a", "opus", "flac", "wav"};
}

std::vector<std::string> Config::video_extensions() {
    return {"mp4", "mkv", "webm", "avi"};
}

nlohmann::json Config::to_json() const {
    return {
        {"output_dir", output_dir.string()},
        {"output_template", output_template},
        {"max_concurrent", max_concurrent},
        {"max_retries", max_retries},
        {"prefer_mp4", prefer_mp4},
        {"default_audio_format", default_audio_format},
        {"default_video_format", default_video_format},
        {"default_resolution", default_resolution}
    };
}

Config Config::from_json(const nlohmann::json& j) {
    Config c;
    if (j.contains("output_dir")) c.output_dir = j["output_dir"].get<std::string>();
    if (j.contains("output_template")) c.output_template = j["output_template"].get<std::string>();
    if (j.contains("max_concurrent")) c.max_concurrent = std::clamp(j["max_concurrent"].get<int>(), 1, 16);
    if (j.contains("max_retries")) c.max_retries = std::clamp(j["max_retries"].get<int>(), 0, 20);
    if (j.contains("prefer_mp4")) c.prefer_mp4 = j["prefer_mp4"].get<bool>();
    if (j.contains("default_audio_format")) {
        auto fmt = j["default_audio_format"].get<std::string>();
        auto valid = audio_formats();
        if (std::find(valid.begin(), valid.end(), fmt) != valid.end())
            c.default_audio_format = fmt;
    }
    if (j.contains("default_video_format")) {
        auto fmt = j["default_video_format"].get<std::string>();
        auto valid = video_extensions();
        if (std::find(valid.begin(), valid.end(), fmt) != valid.end())
            c.default_video_format = fmt;
    }
    if (j.contains("default_resolution")) c.default_resolution = j["default_resolution"].get<std::string>();
    return c;
}

ConfigManager::ConfigManager(std::filesystem::path config_dir) {
    std::filesystem::create_directories(config_dir);
    config_file_ = config_dir / "config.json";
}

Config ConfigManager::load() {
    try {
        if (std::filesystem::exists(config_file_)) {
            std::ifstream f(config_file_);
            nlohmann::json j;
            f >> j;
            return Config::from_json(j);
        }
    } catch (...) {
        // Fall through to defaults
    }
    return default_config_;
}

void ConfigManager::save(const Config& config) {
    std::filesystem::create_directories(config_file_.parent_path());
    std::ofstream f(config_file_);
    f << config.to_json().dump(2) << std::endl;
}

} // namespace yt_tui
