#include "download.hpp"
#include <nlohmann/json.hpp>
#include <array>
#include <cstdio>
#include <memory>
#include <regex>
#include <stdexcept>
#include <format>
#include <fstream>
#include <cstring>
#include <cerrno>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <io.h>
#include <fcntl.h>
#include <process.h>
#else
#include <signal.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
#endif

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

// ---------------------------------------------------------------------------
// Platform-specific process helpers
// ---------------------------------------------------------------------------

// Shell-quote a string argument: single-quote on POSIX, double-quote on Windows
static std::string shell_quote(const std::string& s) {
#ifdef _WIN32
    // cmd.exe: wrap in double quotes; escape embedded double quotes as ""
    std::string r = "\"";
    for (char c : s) {
        if (c == '"') r += "\"\"";
        else r += c;
    }
    return r + "\"";
#else
    // POSIX sh: wrap in single quotes; escape embedded single quotes as '\''
    std::string r;
    r.reserve(s.size() + 4);
    r = "'";
    for (char c : s) {
        if (c == '\'') r += "'\\''";
        else r += c;
    }
    return r + "'";
#endif
}

// Simple popen wrapper that doesn't need extra headers beyond <cstdio>
static std::string exec_and_capture(const std::string& cmd) {
    std::array<char, 256> buffer;
    std::string result;
#ifdef _WIN32
    auto pipe = _popen(cmd.c_str(), "r");
#else
    auto pipe = popen(cmd.c_str(), "r");
#endif
    if (!pipe) {
        throw std::runtime_error("popen() failed");
    }
    while (fgets(buffer.data(), buffer.size(), pipe) != nullptr) {
        result += buffer.data();
    }
#ifdef _WIN32
    int rc = _pclose(pipe);
#else
    int rc = pclose(pipe);
#endif
    // BUG-14 fix: throw on non-zero exit regardless of whether there is output,
    // unless output already contains JSON (caller will detect and throw anyway).
    if (rc != 0 && result.find('{') == std::string::npos) {
        std::string err = result.empty() ? "yt-dlp command failed" :
            "yt-dlp error: " + result.substr(0, 200);
        throw std::runtime_error(err);
    }
    return result;
}

// Like popen("r") but also captures a handle usable for cancellation.
// Returns FILE* (caller must fclose).  On POSIX the handle is a pid_t;
// on Windows it is a HANDLE (both stored as uintptr_t).
static FILE* popen_with_pid(const std::string& cmd, uintptr_t* handle_out) {
    *handle_out = 0;

#ifdef _WIN32
    HANDLE hStdoutRead, hStdoutWrite;
    SECURITY_ATTRIBUTES sa = { sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE };

    if (!CreatePipe(&hStdoutRead, &hStdoutWrite, &sa, 0))
        return nullptr;
    if (!SetHandleInformation(hStdoutRead, HANDLE_FLAG_INHERIT, 0)) {
        CloseHandle(hStdoutRead);
        CloseHandle(hStdoutWrite);
        return nullptr;
    }

    // Wrap the command so cmd.exe runs it.
    std::string full_cmd = "cmd.exe /c " + cmd;

    PROCESS_INFORMATION pi;
    ZeroMemory(&pi, sizeof(pi));
    STARTUPINFO si;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    si.hStdOutput  = hStdoutWrite;
    si.hStdError   = hStdoutWrite;
    si.dwFlags    |= STARTF_USESTDHANDLES;

    if (!CreateProcessA(nullptr, &full_cmd[0], nullptr, nullptr, TRUE,
                        CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        CloseHandle(hStdoutWrite);
        CloseHandle(hStdoutRead);
        return nullptr;
    }

    CloseHandle(hStdoutWrite);
    CloseHandle(pi.hThread);

    int fd = _open_osfhandle(reinterpret_cast<intptr_t>(hStdoutRead), _O_RDONLY);
    FILE* pipe = _fdopen(fd, "r");

    *handle_out = reinterpret_cast<uintptr_t>(pi.hProcess);
    return pipe;

#else
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
        (void)setpgid(0, 0);
        execl("/bin/sh", "sh", "-c", cmd.c_str(), nullptr);
        _exit(127);
    }

    close(pipefd[1]);
    *handle_out = static_cast<uintptr_t>(pid);
    return fdopen(pipefd[0], "r");
#endif
}

// Close the pipe and wait for the child process to finish.
// Returns exit status, or -1 on error.
static int pclose_with_pid(FILE* pipe, uintptr_t handle) {
    if (!pipe) return -1;
    fclose(pipe);

#ifdef _WIN32
    HANDLE hProcess = reinterpret_cast<HANDLE>(handle);
    if (!hProcess) return -1;
    WaitForSingleObject(hProcess, INFINITE);
    DWORD exit_code = 0;
    GetExitCodeProcess(hProcess, &exit_code);
    CloseHandle(hProcess);
    return static_cast<int>(exit_code);
#else
    pid_t pid = static_cast<pid_t>(handle);
    if (pid <= 0) return -1;
    int status;
    while (waitpid(pid, &status, 0) == -1) {
        if (errno != EINTR) return -1;
    }
    return status;
#endif
}

// Kill the subprocess (and its group on POSIX).
static void kill_subprocess(uintptr_t handle) {
    if (handle == 0) return;

#ifdef _WIN32
    HANDLE hProcess = reinterpret_cast<HANDLE>(handle);
    TerminateProcess(hProcess, 1);
#else
    pid_t pid = static_cast<pid_t>(handle);
    kill(-pid, SIGTERM);
    // Give it a moment, then SIGKILL if still alive
    int retries = 0;
    while (waitpid(pid, nullptr, WNOHANG) == 0 && retries < 50) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        retries++;
    }
    if (retries >= 50)
        kill(-pid, SIGKILL);
    waitpid(pid, nullptr, 0);
#endif
}

// ---------------------------------------------------------------------------
// DownloadManager
// ---------------------------------------------------------------------------

DownloadManager::DownloadManager() = default;

DownloadManager::~DownloadManager() {
    cleanup_finished();
    {
        std::lock_guard<std::mutex> lock(active_mutex_);
        for (auto& [id, ad] : active_downloads_) {
            if (ad) {
                ad->cancelled = true;
                uintptr_t h = ad->child_handle_.load();
                if (h != 0)
                    kill_subprocess(h);
            }
        }
    }
    // BUG-03 fix: poll without holding the lock so download threads can set
    // finished=true without being starved by the destructor spinning.
    for (int i = 0; i < 100; i++) {
        bool all_finished = true;
        {
            std::lock_guard<std::mutex> lock(active_mutex_);
            for (auto& [id, ad] : active_downloads_) {
                if (ad && !ad->finished) { all_finished = false; break; }
            }
        }
        if (all_finished) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    std::vector<std::unique_ptr<std::thread>> threads_to_join;
    {
        std::lock_guard<std::mutex> lock(active_mutex_);
        for (auto& [id, ad] : active_downloads_) {
            if (ad && ad->thread)
                threads_to_join.push_back(std::move(ad->thread));
        }
        active_downloads_.clear();
    }
    for (auto& t : threads_to_join) {
        if (t->joinable()) t->join();
    }
}

void DownloadManager::cleanup_finished() {
    std::lock_guard<std::mutex> lock(active_mutex_);
    for (auto it = active_downloads_.begin(); it != active_downloads_.end(); ) {
        if (it->second && it->second->finished) {
            if (it->second->thread && it->second->thread->joinable())
                it->second->thread->join();
            it = active_downloads_.erase(it);
        } else {
            ++it;
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

// ---------------------------------------------------------------------------
// File-size formatting
// ---------------------------------------------------------------------------

static std::string format_filesize(long long bytes) {
    if (bytes <= 0) return "";
    if (bytes < 1024LL * 1024)
        return std::to_string(bytes / 1024) + "KB";
    if (bytes < 1024LL * 1024 * 1024)
        return std::format("{:.1f}MB", bytes / (1024.0 * 1024.0));
    return std::format("{:.1f}GB", bytes / (1024.0 * 1024.0 * 1024.0));
}

template<typename T>
static T json_val(const nlohmann::json& j, const std::string& key, T def) {
    if (j.contains(key) && !j[key].is_null())
        return j[key].get<T>();
    return def;
}

// ---------------------------------------------------------------------------
// Fetch video info
// ---------------------------------------------------------------------------

VideoInfo DownloadManager::fetch_info(const std::string& url) {
    std::string ytdlp = ytdlp_path_.string();
    std::string cmd = ytdlp + " --dump-json --no-download --skip-download --no-warnings " +
                      shell_quote(url) + " 2>&1";

    auto output = exec_and_capture(cmd);
    if (output.empty()) {
        throw std::runtime_error("yt-dlp returned no output (check network/URL)");
    }

    auto json_start = output.find('{');
    if (json_start == std::string::npos) {
        auto err = output;
        if (err.size() > 300) err = err.substr(0, 300) + "...";
        throw std::runtime_error("yt-dlp: " + err);
    }

    auto json_str = output.substr(json_start);
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

// ---------------------------------------------------------------------------
// Build yt-dlp command line
// ---------------------------------------------------------------------------

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
    cmd += " --retries 10";
    cmd += " --retry-sleep 5";
    cmd += " --socket-timeout 30";
    cmd += " --extractor-retries 3";

    std::filesystem::create_directories(output_dir);
    // BUG-07 fix: use shell_quote() for paths and format strings to handle
    // spaces, special characters and potential shell metacharacters safely.
    cmd += " -o " + shell_quote((output_dir / output_template).string());

    if (item.media_type == MediaType::Audio) {
        cmd += " -x";
        cmd += " --audio-format " + shell_quote(item.format);
        cmd += " --audio-quality 0";
    } else {
        if (!item.resolution.empty() && item.resolution != "best") {
            std::string fmt = "bestvideo[height<=" + item.resolution + "]+bestaudio/best[height<=" + item.resolution + "]";
            cmd += " -f " + shell_quote(fmt);
        } else {
            cmd += " -f " + shell_quote("bestvideo+bestaudio/best");
        }
        if (!item.format.empty() && item.format != "mp4") {
            cmd += " --merge-output-format " + shell_quote(item.format);
        } else {
            cmd += " --merge-output-format mp4";
        }
    }

    cmd += " --embed-thumbnail";
    cmd += " --embed-metadata";
    cmd += " --write-subs";
    cmd += " --sub-langs all";
    cmd += " --convert-subs srt";

    cmd += " " + shell_quote(item.url);

    return cmd;
}

// ---------------------------------------------------------------------------
// Start download
// ---------------------------------------------------------------------------

void DownloadManager::start_download(std::shared_ptr<DownloadItem> item,
                                     ProgressCallback on_progress,
                                     StateCallback on_state,
                                     RetryCallback on_retry)
{
    int id = item->id;
    auto ad = std::make_unique<ActiveDownload>();
    ad->item = item;

    // BUG-01 fix: create the thread *before* moving ad into the map so we
    // hold a valid pointer through the entire setup. Then insert under the lock.
    ad->thread = std::make_unique<std::thread>([this, id, item, on_progress, on_state, on_retry]() {
        auto cleanup = [this, id]() {
            std::lock_guard<std::mutex> lock(active_mutex_);
            auto it = active_downloads_.find(id);
            if (it != active_downloads_.end())
                it->second->finished = true;
        };

        std::string cmd = build_ytdlp_command(*item, output_dir_, output_template_, ytdlp_path_);
        cmd += " 2>&1";

        item->state = DownloadState::Running;
        if (on_state) on_state(*item);

        uintptr_t child_handle = 0;
        FILE* pipe = popen_with_pid(cmd, &child_handle);
        if (!pipe || child_handle == 0) {
            item->state = DownloadState::Failed;
            item->error_message = "Failed to start yt-dlp process";
            item->completed_time = std::chrono::system_clock::now();
            if (on_state) on_state(*item);
            cleanup();
            return;
        }

        {
            std::lock_guard<std::mutex> lock(active_mutex_);
            auto it = active_downloads_.find(id);
            if (it != active_downloads_.end())
                it->second->child_handle_.store(child_handle);
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
            while (!line.empty() && (line.back() == '\n' || line.back() == '\r'))
                line.pop_back();

            DownloadProgress prog;
            static const std::regex progress_re(R"((\d+\.?\d*)%\s+of\s+[~]?([\d.]+\s*\w+)\s+at\s+([\d.]+\s*\w+/s)\s+ETA\s+([\d:]+))");
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
            kill_subprocess(child_handle);
            fclose(pipe);
            item->state = DownloadState::Cancelled;
        } else {
            int status = pclose_with_pid(pipe, child_handle);

#ifdef _WIN32
            if (status == 0) {
                item->state = DownloadState::Completed;
                item->progress.percent = 100.0;
            } else {
                if (item->retries < item->max_retries) {
                    item->retries++;
                    item->state = DownloadState::Pending;
                    item->error_message = "Failed (will retry)";
                    if (on_retry) on_retry(item->id);
                } else {
                    item->state = DownloadState::Failed;
                    if (item->error_message.empty()) {
                        item->error_message = "Download failed with exit code " + std::to_string(status);
                    }
                }
            }
#else
            if (status == 0) {
                item->state = DownloadState::Completed;
                item->progress.percent = 100.0;
            } else if (WIFEXITED(status) && WEXITSTATUS(status) == 127) {
                item->state = DownloadState::Failed;
                item->error_message = "yt-dlp not found or failed to execute";
            } else {
                int exit_code = -1;
                if (WIFEXITED(status))
                    exit_code = WEXITSTATUS(status);
                else if (WIFSIGNALED(status))
                    exit_code = -WTERMSIG(status);
                if (item->retries < item->max_retries) {
                    item->retries++;
                    item->state = DownloadState::Pending;
                    item->error_message = "Failed (will retry)";
                    if (on_retry) on_retry(item->id);
                } else {
                    item->state = DownloadState::Failed;
                    if (item->error_message.empty()) {
                        item->error_message = "Download failed with exit code " + std::to_string(exit_code);
                    }
                }
            }
#endif
        }

        item->completed_time = std::chrono::system_clock::now();
        if (on_state) on_state(*item);
        cleanup();
    });

    {
        std::lock_guard<std::mutex> lock(active_mutex_);
        active_downloads_[id] = std::move(ad);
    }
}

// ---------------------------------------------------------------------------
// Cancel / pause / resume
// ---------------------------------------------------------------------------

void DownloadManager::cancel_download(int id) {
    uintptr_t handle = 0;
    {
        std::lock_guard<std::mutex> lock(active_mutex_);
        auto it = active_downloads_.find(id);
        if (it != active_downloads_.end()) {
            it->second->cancelled = true;
            handle = it->second->child_handle_.load();
        }
    }
    if (handle != 0) {
        // Kill the subprocess (process group on POSIX, process on Windows).
#ifdef _WIN32
        HANDLE hProcess = reinterpret_cast<HANDLE>(handle);
        TerminateProcess(hProcess, 1);
        WaitForSingleObject(hProcess, 1000);
#else
        pid_t pid = static_cast<pid_t>(handle);
        kill(-pid, SIGTERM);
        int retries = 0;
        while (waitpid(pid, nullptr, WNOHANG) == 0 && retries < 10) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            retries++;
        }
        if (retries >= 10) {
            kill(-pid, SIGKILL);
            // BUG-08 fix: reap the process after SIGKILL to prevent zombie accumulation.
            waitpid(pid, nullptr, 0);
        }
#endif
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