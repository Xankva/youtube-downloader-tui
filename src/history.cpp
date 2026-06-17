#include "history.hpp"
#include <fstream>
#include <algorithm>
#include <ctime>
#include <array>

namespace yt_tui {

static std::string time_point_to_string(const std::chrono::system_clock::time_point& tp) {
    auto t = std::chrono::system_clock::to_time_t(tp);
    std::tm tm;
    localtime_r(&t, &tm);
    std::array<char, 64> buf;
    std::strftime(buf.data(), buf.size(), "%Y-%m-%d %H:%M:%S", &tm);
    return buf.data();
}

nlohmann::json HistoryEntry::to_json() const {
    return {
        {"id", id},
        {"url", url},
        {"title", title},
        {"filename", filename},
        {"media_type", media_type},
        {"format", format},
        {"resolution", resolution},
        {"state", state_to_string(state)},
        {"error_message", error_message},
        {"added_time", added_time},
        {"completed_time", completed_time},
        {"file_size", file_size}
    };
}

HistoryEntry HistoryEntry::from_json(const nlohmann::json& j) {
    HistoryEntry e;
    e.id = j.value("id", 0);
    e.url = j.value("url", "");
    e.title = j.value("title", "");
    e.filename = j.value("filename", "");
    e.media_type = j.value("media_type", "");
    e.format = j.value("format", "");
    e.resolution = j.value("resolution", "");
    e.error_message = j.value("error_message", "");
    e.added_time = j.value("added_time", "");
    e.completed_time = j.value("completed_time", "");
    e.file_size = j.value("file_size", "");

    // Parse state string back to enum
    std::string state_str = j.value("state", "");
    if (state_str == "Completed")       e.state = DownloadState::Completed;
    else if (state_str == "Failed")     e.state = DownloadState::Failed;
    else if (state_str == "Cancelled")  e.state = DownloadState::Cancelled;
    else if (state_str == "Running")    e.state = DownloadState::Running;
    else if (state_str == "Paused")     e.state = DownloadState::Paused;
    else if (state_str == "Pending")    e.state = DownloadState::Pending;
    else                                e.state = DownloadState::Completed;

    return e;
}

HistoryManager::HistoryManager(std::filesystem::path history_file)
    : history_file_(std::move(history_file)) {
    load();
}

void HistoryManager::add_entry(const DownloadItem& item) {
    std::lock_guard<std::mutex> lock(mutex_);
    HistoryEntry entry;
    entry.id = item.id;
    entry.url = item.url;
    entry.title = item.title;
    entry.filename = item.filename;
    entry.media_type = media_type_string(item.media_type);
    entry.format = item.format;
    entry.resolution = item.resolution;
    entry.state = item.state;
    entry.error_message = item.error_message;
    entry.file_size = item.progress.total_size;
    entry.added_time = time_point_to_string(item.added_time);
    entry.completed_time = time_point_to_string(item.completed_time);

    entries_.push_front(entry);
    if (entries_.size() > max_entries_) {
        entries_.pop_back();
    }
    save();
}

void HistoryManager::update_entry(const DownloadItem& item) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& e : entries_) {
        if (e.id == item.id) {
            e.state = item.state;
            e.error_message = item.error_message;
            e.completed_time = time_point_to_string(item.completed_time);
            break;
        }
    }
    save();
}

std::vector<HistoryEntry> HistoryManager::get_all() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return {entries_.begin(), entries_.end()};
}

void HistoryManager::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    entries_.clear();
    save();
}

void HistoryManager::save() {
    try {
        std::filesystem::create_directories(history_file_.parent_path());
        std::ofstream f(history_file_);
        nlohmann::json j = nlohmann::json::array();
        for (const auto& e : entries_) {
            j.push_back(e.to_json());
        }
        f << j.dump(2) << std::endl;
    } catch (...) {
        // Silently fail on save error
    }
}

void HistoryManager::load() {
    try {
        if (!std::filesystem::exists(history_file_)) return;
        std::ifstream f(history_file_);
        nlohmann::json j;
        f >> j;
        if (!j.is_array()) return;
        for (const auto& elem : j) {
            entries_.push_back(HistoryEntry::from_json(elem));
        }
    } catch (...) {
        entries_.clear();
    }
}

} // namespace yt_tui
