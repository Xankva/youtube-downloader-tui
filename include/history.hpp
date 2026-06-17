#pragma once

#include "download.hpp"
#include <deque>
#include <filesystem>
#include <mutex>
#include <string>
#include <nlohmann/json.hpp>

namespace yt_tui {

struct HistoryEntry {
    int id;
    std::string url;
    std::string title;
    std::string filename;
    std::string media_type;
    std::string format;
    std::string resolution;
    DownloadState state;
    std::string error_message;
    std::string added_time;
    std::string completed_time;
    std::string file_size;

    nlohmann::json to_json() const;
    static HistoryEntry from_json(const nlohmann::json& j);
};

class HistoryManager {
public:
    HistoryManager(std::filesystem::path history_file);

    void add_entry(const DownloadItem& item);
    void update_entry(const DownloadItem& item);
    std::vector<HistoryEntry> get_all() const;
    void clear();
    void save();
    void load();

private:
    std::filesystem::path history_file_;
    std::deque<HistoryEntry> entries_;
    mutable std::mutex mutex_;
    static constexpr size_t max_entries_{100};
};

} // namespace yt_tui
