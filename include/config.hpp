#pragma once

#include <string>
#include <filesystem>
#include <vector>
#include <nlohmann/json.hpp>

namespace yt_tui {

struct FormatQuality {
    std::string label;
    std::string ytdlp_format;
};

struct Config {
    std::filesystem::path output_dir{"downloads"};
    std::string output_template{"%(title)s.%(ext)s"};
    int max_concurrent{3};
    int max_retries{3};
    bool prefer_mp4{true};
    std::string default_audio_format{"mp3"};
    std::string default_video_format{"mp4"};
    std::string default_resolution{"1080p"};

    static std::vector<FormatQuality> video_qualities();
    static std::vector<std::string> audio_formats();
    static std::vector<std::string> video_extensions();

    nlohmann::json to_json() const;
    static Config from_json(const nlohmann::json& j);
};

class ConfigManager {
public:
    ConfigManager(std::filesystem::path config_dir);
    Config load();
    void save(const Config& config);
private:
    std::filesystem::path config_file_;
    Config default_config_;
};

} // namespace yt_tui
