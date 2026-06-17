#include "queue.hpp"
#include <algorithm>

namespace yt_tui {

DownloadQueue::DownloadQueue(DownloadManager& manager)
    : manager_(manager) {}

void DownloadQueue::set_change_callback(ChangeCallback cb) {
    std::lock_guard<std::mutex> lock(mutex_);
    change_callback_ = std::move(cb);
}

int DownloadQueue::enqueue(std::shared_ptr<DownloadItem> item) {
    ChangeCallback cb;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        item->id = next_id_++;
        item->added_time = std::chrono::system_clock::now();
        items_.push_back(item);
        pending_ids_.push(item->id);
        cb = change_callback_;
    }
    if (cb) cb();
    return item->id;
}

bool DownloadQueue::cancel(int id) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = std::find_if(items_.begin(), items_.end(),
            [id](const auto& item) { return item->id == id; });
        if (it == items_.end()) return false;

        manager_.cancel_download(id);
        (*it)->state = DownloadState::Cancelled;
    }
    notify_change();
    return true;
}

void DownloadQueue::cancel_all() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& item : items_) {
            if (item->state == DownloadState::Pending) {
                item->state = DownloadState::Cancelled;
            }
            manager_.cancel_download(item->id);
        }
    }
    notify_change();
}

void DownloadQueue::pause(int id) {
    manager_.pause_download(id);
}

void DownloadQueue::resume(int id) {
    manager_.resume_download(id);
}

void DownloadQueue::remove_item(int id) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        items_.erase(std::remove_if(items_.begin(), items_.end(),
            [id](const auto& item) { return item->id == id; }), items_.end());
    }
    notify_change();
}

void DownloadQueue::clear_completed() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        items_.erase(std::remove_if(items_.begin(), items_.end(), [](const auto& item) {
            return item->state == DownloadState::Completed ||
                   item->state == DownloadState::Cancelled ||
                   item->state == DownloadState::Failed;
        }), items_.end());
    }
    notify_change();
}

std::vector<std::shared_ptr<DownloadItem>> DownloadQueue::all_items() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return items_;
}

std::shared_ptr<DownloadItem> DownloadQueue::get_item(int id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = std::find_if(items_.begin(), items_.end(),
        [id](const auto& item) { return item->id == id; });
    return it != items_.end() ? *it : nullptr;
}

int DownloadQueue::active_count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return static_cast<int>(std::count_if(items_.begin(), items_.end(),
        [](const auto& item) { return item->state == DownloadState::Running; }));
}

int DownloadQueue::pending_count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return static_cast<int>(std::count_if(items_.begin(), items_.end(),
        [](const auto& item) { return item->state == DownloadState::Pending; }));
}

int DownloadQueue::total_count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return static_cast<int>(items_.size());
}

void DownloadQueue::process() {
    if (processing_.exchange(true)) return;

    std::thread([this]() {
        while (true) {
            std::shared_ptr<DownloadItem> item;

            {
                std::unique_lock<std::mutex> lock(mutex_);
                int active = 0;
                for (auto& i : items_) {
                    if (i->state == DownloadState::Running) active++;
                }

                while (active >= max_concurrent_ && !pending_ids_.empty()) {
                    lock.unlock();
                    std::this_thread::sleep_for(std::chrono::milliseconds(200));
                    lock.lock();
                    active = 0;
                    for (auto& i : items_) {
                        if (i->state == DownloadState::Running) active++;
                    }
                }

                while (!pending_ids_.empty()) {
                    int id = pending_ids_.front();
                    pending_ids_.pop();
                    auto it = std::find_if(items_.begin(), items_.end(),
                        [id](const auto& i) { return i->id == id; });
                    if (it != items_.end() && (*it)->state == DownloadState::Pending) {
                        item = *it;
                        break;
                    }
                }
            }

            if (!item) {
                bool all_done = true;
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    for (auto& i : items_) {
                        if (i->state == DownloadState::Pending ||
                            i->state == DownloadState::Running) {
                            all_done = false;
                            break;
                        }
                    }
                }
                if (all_done) {
                    processing_ = false;
                    return;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
                continue;
            }

            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (item->state != DownloadState::Pending)
                    continue;
                manager_.start_download(
                    item,
                    [this](const DownloadProgress&) { notify_change(); },
                    [this](const DownloadItem&) { notify_change(); }
                );
                // Handle race: item may have been cancelled between the state check
                // and start_download. If so, cancel it in the manager immediately.
                if (item->state == DownloadState::Cancelled) {
                    manager_.cancel_download(item->id);
                }
            }
        }
    }).detach();
}

void DownloadQueue::notify_change() {
    ChangeCallback cb;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        cb = change_callback_;
    }
    if (cb) cb();
}

} // namespace yt_tui
