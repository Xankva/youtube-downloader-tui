#pragma once

#include "download.hpp"
#include <queue>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <optional>

namespace yt_tui {

class DownloadQueue {
public:
    using ChangeCallback = std::function<void()>;

    DownloadQueue(DownloadManager& manager);
    ~DownloadQueue();

    void set_change_callback(ChangeCallback cb);

    int enqueue(std::shared_ptr<DownloadItem> item);
    bool cancel(int id);
    void cancel_all();
    void pause(int id);
    void resume(int id);
    void remove_item(int id);
    void clear_completed();

    std::vector<std::shared_ptr<DownloadItem>> all_items() const;
    std::shared_ptr<DownloadItem> get_item(int id) const;
    int active_count() const;
    int pending_count() const;
    int total_count() const;

    void process();
    void shutdown();
    void update_title(const std::string& url, const std::string& title);

private:
    void notify_change();

    DownloadManager& manager_;
    mutable std::mutex mutex_;
    std::vector<std::shared_ptr<DownloadItem>> items_;
    std::queue<int> pending_ids_;
    ChangeCallback change_callback_;
    int max_concurrent_{3};
    int next_id_{1};
    std::atomic<bool> processing_{false};
    std::atomic<bool> stop_processing_{false};
    std::optional<std::thread> processing_thread_;
};

} // namespace yt_tui
