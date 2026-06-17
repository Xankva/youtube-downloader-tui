#pragma once

#include <string>
#include <functional>
#include <thread>
#include <atomic>
#include <chrono>
#include <vector>
#include <filesystem>
#include <mutex>
#include <unordered_map>
#include <cstdio>

namespace yt_tui {

enum class DownloadState {
    Pending,
    Running,
    Paused,
    Completed,
    Failed,
    Cancelled
};

enum class MediaType {
    Video,
    Audio,
    VideoAudio
};

struct DownloadProgress {
    double percent{0.0};
    std::string speed;
    std::string eta;
    std::string downloaded;
    std::string total_size;
    std::string status_line;
};

struct FormatInfo {
    std::string format_id;
    int height{0};
    int width{0};
    std::string ext;
    std::string note;
    std::string vcodec;
    std::string acodec;
    double abr{0};
    double vbr{0};
    std::string filesize;
    bool is_audio_only{false};
    bool is_video_only{false};
    bool has_audio{false};
    bool has_video{false};
};

struct VideoInfo {
    std::string id;
    std::string title;
    std::string duration;
    std::string uploader;
    std::string upload_date;
    std::string description;
    long long views{0};
    int max_height{0};
    std::vector<FormatInfo> formats;
    std::vector<std::string> format_labels;
    std::vector<std::string> thumbnails;
    bool is_playlist{false};
    std::vector<std::string> playlist_entries;
};

struct DownloadItem {
    int id{0};
    std::string url;
    std::string title;
    std::string filename;
    MediaType media_type{MediaType::Video};
    std::string format;
    std::string resolution;
    DownloadState state{DownloadState::Pending};
    DownloadProgress progress;
    int retries{0};
    int max_retries{3};
    std::string error_message;
    std::filesystem::path output_path;
    std::chrono::system_clock::time_point added_time;
    std::chrono::system_clock::time_point completed_time;
};

using ProgressCallback = std::function<void(const DownloadProgress&)>;
using StateCallback = std::function<void(const DownloadItem&)>;

class DownloadManager {
public:
    DownloadManager();
    ~DownloadManager();

    void set_ytdlp_path(const std::filesystem::path& path);
    void set_output_dir(const std::filesystem::path& dir);
    void set_output_template(const std::string& tpl);

    std::string get_ytdlp_path() const;

    VideoInfo fetch_info(const std::string& url);
    void start_download(std::shared_ptr<DownloadItem> item,
                        ProgressCallback on_progress,
                        StateCallback on_state);
    void cancel_download(int id);
    void pause_download(int id);
    void resume_download(int id);

    bool is_running(int id) const;
    static std::string build_ytdlp_command(
        const DownloadItem& item,
        const std::filesystem::path& output_dir,
        const std::string& output_template,
        const std::filesystem::path& ytdlp_path);

private:
    std::filesystem::path ytdlp_path_{"bin/yt-dlp"};
    std::filesystem::path output_dir_{"downloads"};
    std::string output_template_{"%(title)s.%(ext)s"};

    struct ActiveDownload {
        std::shared_ptr<DownloadItem> item;
        std::unique_ptr<std::thread> thread;
        std::atomic<bool> cancelled{false};
        std::atomic<bool> paused{false};
        std::atomic<pid_t> child_pid{0};
    };

    mutable std::mutex active_mutex_;
    std::unordered_map<int, std::unique_ptr<ActiveDownload>> active_downloads_;
    std::atomic<int> next_id_{1};
};

std::string state_to_string(DownloadState state);
const char* media_type_string(MediaType type);

} // namespace yt_tui
