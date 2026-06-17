#include "download.hpp"
#include <nlohmann/json.hpp>
#include <array>
#include <cstdio>
#include <memory>
#include <regex>
#include <stdexcept>
#include <sstream>
#include <fstream>
#include <iomanip>
#include <signal.h>
#include <cstring>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>

namespace yt_tui {

std::string state_to_string(DownloadState state) {
    switch (state) {
        case DownloadState::Pending:   return "Pending";
        case DownloadState::Running:   return "Running";
        case DownloadState::Paused:    return "Paused";
        case DownloadState::Completed: return "Completed";
        case DownloadState::Failed:    return "Failed";
        case DownloadState::Cancelled: return "Cancelled";
    }
    return "Unknown";
}

const char* media_type_string(MediaType type) {
    switch (type) {
        case MediaType::Video:     return "Video";
        case MediaType::Audio:     return "Audio";
        case MediaType::VideoAudio: return "Video+Audio";
    }
    return "Unknown";
}

static std::string exec_and_capture(const std::string& cmd) {
    std::array<char, 256> buffer;
    std::string result;
    auto deleter = [](FILE* f) { if (f) pclose(f); };
    std::unique_ptr<FILE, decltype(deleter)> pipe(popen(cmd.c_str(), "r"), deleter);
    if (!pipe) {
        throw std::runtime_error("popen() failed");
    }
    while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) {
        result += buffer.data();
    }
    return result;
}

DownloadManager::DownloadManager() = default;
DownloadManager::~DownloadManager() {
    for (auto& [id, ad] : active_downloads_) {
        if (ad && ad->thread && ad->thread->joinable()) {
            ad->cancelled = true;
            ad->thread->join();
        }
    }
}

void DownloadManager::set_ytdlp_path(const std::filesystem::path& path) {
    ytdlp_path_ = path;
}

void DownloadManager::set_output_dir(const std::filesystem::path& dir) {
    output_dir_ = dir;
}

void DownloadManager::set_output_template(const std::string& tpl) {
    output_template_ = tpl;
}

std::string DownloadManager::get_ytdlp_path() const {
    return ytdlp_path_.string();
}

static std::string format_filesize(long long bytes) {
    if (bytes <= 0) return "";
    if (bytes < 1024LL * 1024) return std::to_string(bytes / 1024) + "KB";
    if (bytes < 1024LL * 1024 * 1024) return std::to_string(bytes / (1024 * 1024)) + "MB";
    return std::to_string(bytes / (1024 * 1024 * 1024)) + "GB";
}

template<typename T>
static T json_val(const nlohmann::json& j, const std::string& key, T def) {
    if (j.contains(key) && !j[key].is_null())
        return j[key].get<T>();
    return def;
}

VideoInfo DownloadManager::fetch_info(const std::string& url) {
    std::string ytdlp = ytdlp_path_.string();
    // Escape single quotes for shell safety
    std::string escaped_url = url;
    size_t pos = 0;
    while ((pos = escaped_url.find('\'', pos)) != std::string::npos) {
        escaped_url.replace(pos, 1, "'\\''");
        pos += 4;
    }
    std::string cmd = ytdlp + " --dump-json --no-download --skip-download --no-warnings " +
                      "'" + escaped_url + "' 2>&1";

    auto output = exec_and_capture(cmd);
    if (output.empty()) {
        throw std::runtime_error("yt-dlp returned no output (check network/URL)");
    }

    // Extract first JSON object from potentially mixed stdout+stderr output
    auto json_start = output.find('{');
    if (json_start == std::string::npos) {
        auto err = output;
        if (err.size() > 300) err = err.substr(0, 300) + "...";
        throw std::runtime_error("yt-dlp: " + err);
    }

    auto json_str = output.substr(json_start);
    // Trim trailing whitespace/newlines
    while (!json_str.empty() && (json_str.back() == '\n' || json_str.back() == '\r'))
        json_str.pop_back();

    nlohmann::json info;
    try {
        info = nlohmann::json::parse(json_str);
    } catch (...) {
        throw std::runtime_error("Failed to parse video info JSON from yt-dlp output");
    }

    VideoInfo vi;
    vi.id = json_val(info, "id", std::string(""));
    vi.title = json_val(info, "title", std::string(""));
    vi.duration = json_val(info, "duration_string", std::string(""));
    vi.uploader = json_val(info, "uploader", std::string(""));
    vi.upload_date = json_val(info, "upload_date", std::string(""));
    vi.description = json_val(info, "description", std::string(""));
    vi.views = json_val(info, "view_count", 0LL);

    if (info.contains("formats") && info["formats"].is_array()) {
        int max_h = 0;
        for (auto& f : info["formats"]) {
            if (!f.is_object()) continue;
            FormatInfo fi;
            fi.format_id = json_val(f, "format_id", std::string(""));
            fi.height = json_val(f, "height", 0);
            fi.width = json_val(f, "width", 0);
            fi.ext = json_val(f, "ext", std::string(""));
            fi.note = json_val(f, "format_note", std::string(""));
            fi.vcodec = json_val(f, "vcodec", std::string("none"));
            fi.acodec = json_val(f, "acodec", std::string("none"));
            fi.abr = json_val(f, "abr", 0.0);
            fi.vbr = json_val(f, "vbr", 0.0);
            fi.filesize = format_filesize(json_val(f, "filesize", 0LL));
            fi.is_audio_only = (fi.vcodec == "none");
            fi.is_video_only = (fi.acodec == "none");
            fi.has_audio = (fi.acodec != "none");
            fi.has_video = (fi.vcodec != "none");
            if (fi.height > max_h) max_h = fi.height;
            vi.formats.push_back(fi);
        }
        vi.max_height = max_h;

        // Generate display labels for the quality selector
        // Only include formats that have video (or combined)
        std::vector<int> seen_heights;
        for (auto& f : vi.formats) {
            if (f.height > 0 && f.has_video) {
                bool dup = false;
                for (int h : seen_heights) {
                    if (h == f.height) { dup = true; break; }
                }
                if (!dup) {
                    seen_heights.push_back(f.height);
                    std::string label = std::to_string(f.height) + "p";
                    if (!f.note.empty() && f.note.find(std::to_string(f.height)) == std::string::npos)
                        label += " (" + f.note + ")";
                    if (!f.filesize.empty())
                        label += " " + f.filesize;
                    vi.format_labels.push_back(label);
                }
            }
        }
        std::sort(vi.format_labels.begin(), vi.format_labels.end(),
            [](const std::string& a, const std::string& b) {
                auto get_h = [](const std::string& s) {
                    size_t p = s.find('p');
                    return p != std::string::npos ? std::stoi(s.substr(0, p)) : 0;
                };
                return get_h(a) > get_h(b);
            });
        // Also add audio-only formats at the bottom
        bool has_audio_formats = false;
        for (auto& f : vi.formats) {
            if (f.is_audio_only && f.has_audio) {
                has_audio_formats = true;
                break;
            }
        }
        if (has_audio_formats)
            vi.format_labels.push_back("Audio Only");
        if (vi.format_labels.empty())
            vi.format_labels.push_back("Best Available");
    }

    if (vi.format_labels.empty())
        vi.format_labels.push_back("Best Available");

    vi.is_playlist = info.value("playlist_count", 0) > 0;
    if (vi.is_playlist && info.contains("entries")) {
        for (auto& e : info["entries"]) {
            if (!e.is_null()) {
                vi.playlist_entries.push_back(e.value("url", ""));
            }
        }
    }

    return vi;
}

std::string DownloadManager::build_ytdlp_command(
    const DownloadItem& item,
    const std::filesystem::path& output_dir,
    const std::string& output_template,
    const std::filesystem::path& ytdlp_path)
{
    std::string cmd = ytdlp_path.string();

    cmd += " --no-warnings";
    cmd += " --newline";
    cmd += " --progress";
    cmd += " --console-title";

    std::filesystem::create_directories(output_dir);
    cmd += " -o \"" + (output_dir / output_template).string() + "\"";

    if (item.media_type == MediaType::Audio) {
        cmd += " -x";
        cmd += " --audio-format " + item.format;
        cmd += " --audio-quality 0";
    } else {
        if (!item.resolution.empty() && item.resolution != "best") {
            cmd += " -f \"bestvideo[height<=" + item.resolution + "]+bestaudio/best[height<=" + item.resolution + "]\"";
        } else {
            cmd += " -f \"bestvideo+bestaudio/best\"";
        }
        if (!item.format.empty() && item.format != "mp4") {
            cmd += " --merge-output-format " + item.format;
        } else {
            cmd += " --merge-output-format mp4";
        }
    }

    cmd += " --embed-thumbnail";
    cmd += " --embed-metadata";
    cmd += " --write-subs";
    cmd += " --sub-langs all";
    cmd += " --convert-subs srt";

    // Escape single quotes for shell safety: '"'"' terminates single-quote, adds escaped quote, reopens
    // This prevents command injection via malicious URLs containing quotes or shell metacharacters
    {
        std::string escaped = item.url;
        size_t pos = 0;
        while ((pos = escaped.find('\'', pos)) != std::string::npos) {
            escaped.replace(pos, 1, "'\\''");
            pos += 4;
        }
        cmd += " '" + escaped + "'";
    }

    return cmd;
}

// Like popen("r") but also returns the child PID so we can kill it on cancel
static FILE* popen_with_pid(const std::string& cmd, pid_t* pid_out) {
    int pipefd[2];
    if (pipe(pipefd) == -1) return nullptr;

    pid_t pid = fork();
    if (pid == -1) {
        close(pipefd[0]);
        close(pipefd[1]);
        return nullptr;
    }

    if (pid == 0) {
        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        dup2(pipefd[1], STDERR_FILENO);
        close(pipefd[1]);
        // Put child in its own process group so we can kill the entire group
        setpgid(0, 0);
        execl("/bin/sh", "sh", "-c", cmd.c_str(), nullptr);
        _exit(127);
    }

    close(pipefd[1]);
    *pid_out = pid;
    return fdopen(pipefd[0], "r");
}

// Wait for child and get its exit status, returning -1 on error
static int pclose_with_pid(FILE* pipe, pid_t pid) {
    fclose(pipe);
    int status;
    while (waitpid(pid, &status, 0) == -1) {
        if (errno != EINTR) return -1;
    }
    return status;
}

void DownloadManager::start_download(std::shared_ptr<DownloadItem> item,
                                     ProgressCallback on_progress,
                                     StateCallback on_state)
{
    int id = item->id;
    auto ad = std::make_unique<ActiveDownload>();
    ad->item = item;

    ActiveDownload* active = nullptr;
    {
        std::lock_guard<std::mutex> lock(active_mutex_);
        active_downloads_[id] = std::move(ad);
        active = active_downloads_[id].get();
    }

    auto thread_started = std::make_shared<std::atomic<bool>>(false);

    active->thread = std::make_unique<std::thread>([this, id, item, on_progress, on_state, thread_started]() {
        while (!thread_started->load()) std::this_thread::yield();

        // Detach self before erase so ~thread() doesn't call std::terminate().
        auto guard = std::shared_ptr<int>(nullptr, [this, id](int*) {
            std::lock_guard<std::mutex> lock(active_mutex_);
            auto it = active_downloads_.find(id);
            if (it != active_downloads_.end()) {
                if (it->second->thread && it->second->thread->joinable())
                    it->second->thread->detach();
            }
            active_downloads_.erase(id);
        });

        std::string cmd = build_ytdlp_command(*item, output_dir_, output_template_, ytdlp_path_);
        cmd += " 2>&1";

        item->state = DownloadState::Running;
        if (on_state) on_state(*item);

        pid_t child_pid = 0;
        FILE* pipe = popen_with_pid(cmd, &child_pid);
        if (!pipe || child_pid == 0) {
            item->state = DownloadState::Failed;
            item->error_message = "Failed to start yt-dlp process: " + std::string(std::strerror(errno));
            item->completed_time = std::chrono::system_clock::now();
            if (on_state) on_state(*item);
            return;
        }

        {
            std::lock_guard<std::mutex> lock(active_mutex_);
            auto it = active_downloads_.find(id);
            if (it != active_downloads_.end())
                it->second->child_pid.store(child_pid);
        }

        std::array<char, 1024> buffer;
        bool cancelled_by_user = false;

        while (fgets(buffer.data(), buffer.size(), pipe) != nullptr) {
            {
                ActiveDownload* ad = nullptr;
                {
                    std::lock_guard<std::mutex> lock(active_mutex_);
                    auto it = active_downloads_.find(id);
                    if (it != active_downloads_.end()) ad = it->second.get();
                }
                if (!ad || ad->cancelled) {
                    cancelled_by_user = true;
                    break;
                }
                if (ad->paused) {
                    while (ad->paused && !ad->cancelled) {
                        std::this_thread::sleep_for(std::chrono::milliseconds(100));
                    }
                    if (ad->cancelled) {
                        cancelled_by_user = true;
                        break;
                    }
                }
            }

            std::string line = buffer.data();
            while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) {
                line.pop_back();
            }

            DownloadProgress prog;
            std::regex progress_re(R"((\d+\.?\d*)%\s+of\s+[~]?([\d.]+\s*\w+)\s+at\s+([\d.]+\s*\w+/s)\s+ETA\s+([\d:]+))");
            std::smatch m;
            if (std::regex_search(line, m, progress_re)) {
                try {
                    prog.percent = std::stod(m[1]);
                } catch (...) {
                    prog.percent = 0.0;
                }
                prog.total_size = m[2];
                prog.speed = m[3];
                prog.eta = m[4];
                item->progress = prog;
                if (on_progress) on_progress(prog);
            }

            if (line.find("ERROR:") != std::string::npos) {
                item->error_message = line;
            }
        }

        if (cancelled_by_user) {
            // Kill the child process group before closing
            if (child_pid > 0) {
                kill(-child_pid, SIGTERM);
                int retries = 0;
                while (waitpid(child_pid, nullptr, WNOHANG) == 0 && retries < 50) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                    retries++;
                }
                if (retries >= 50)
                    kill(-child_pid, SIGKILL);
                waitpid(child_pid, nullptr, 0);
            }
            fclose(pipe);
            item->state = DownloadState::Cancelled;
        } else {
            int status = pclose_with_pid(pipe, child_pid);

            if (status == 0) {
                item->state = DownloadState::Completed;
                item->progress.percent = 100.0;
            } else if (WIFEXITED(status) && WEXITSTATUS(status) == 127) {
                item->state = DownloadState::Failed;
                item->error_message = "yt-dlp not found or failed to execute";
            } else {
                if (item->retries < item->max_retries) {
                    item->retries++;
                    item->state = DownloadState::Pending;
                    item->error_message = "Failed (will retry)";
                } else {
                    item->state = DownloadState::Failed;
                    if (item->error_message.empty()) {
                        item->error_message = "Download failed with exit code " + std::to_string(WEXITSTATUS(status));
                    }
                }
            }
        }

        item->completed_time = std::chrono::system_clock::now();
        if (on_state) on_state(*item);
    });

    // If item was cancelled while we were setting up, cancel in manager too
    {
        std::lock_guard<std::mutex> lock(active_mutex_);
        auto it = active_downloads_.find(id);
        if (it != active_downloads_.end()) {
            if (it->second->cancelled || item->state == DownloadState::Cancelled) {
                it->second->cancelled = true;
            }
        }
    }

    *thread_started = true;
}

void DownloadManager::cancel_download(int id) {
    pid_t pid = 0;
    {
        std::lock_guard<std::mutex> lock(active_mutex_);
        auto it = active_downloads_.find(id);
        if (it != active_downloads_.end()) {
            it->second->cancelled = true;
            pid = it->second->child_pid.load();
        }
    }
    // Kill the yt-dlp subprocess group to stop it immediately
    if (pid > 0) {
        kill(-pid, SIGTERM);
        // Give it a moment, then SIGKILL if still alive
        int retries = 0;
        while (waitpid(pid, nullptr, WNOHANG) == 0 && retries < 10) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            retries++;
        }
        if (retries >= 10)
            kill(-pid, SIGKILL);
    }
}

void DownloadManager::pause_download(int id) {
    std::lock_guard<std::mutex> lock(active_mutex_);
    auto it = active_downloads_.find(id);
    if (it != active_downloads_.end()) {
        it->second->paused = true;
    }
}

void DownloadManager::resume_download(int id) {
    std::lock_guard<std::mutex> lock(active_mutex_);
    auto it = active_downloads_.find(id);
    if (it != active_downloads_.end()) {
        it->second->paused = false;
    }
}

bool DownloadManager::is_running(int id) const {
    std::lock_guard<std::mutex> lock(active_mutex_);
    return active_downloads_.find(id) != active_downloads_.end();
}

} // namespace yt_tui
