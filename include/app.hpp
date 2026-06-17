#pragma once

#include "config.hpp"
#include "download.hpp"
#include "queue.hpp"
#include "history.hpp"

#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/component/component.hpp>
#include <ftxui/dom/elements.hpp>

#include <vector>
#include <string>
#include <memory>
#include <unordered_set>
#include <atomic>
#include <chrono>
#include <thread>
#include <algorithm>

namespace yt_tui {

struct LogMessage {
    enum Level { Info, Warning, Error, Success };
    Level level;
    std::string text;
    std::chrono::system_clock::time_point time;
};

class App {
public:
    App(std::filesystem::path config_dir);
    ~App();
    void run();

private:
    void setup_components();
    ftxui::Element render_header();
    ftxui::Element render_welcome();
    ftxui::Element render_url_section();
    ftxui::Element render_action_buttons();
    ftxui::Element render_output_section();
    ftxui::Element render_queue_tab();
    ftxui::Element render_history_tab();
    ftxui::Element render_log_tab();
    ftxui::Element render_tab_bar(const ftxui::Component& tq, const ftxui::Component& th, const ftxui::Component& tl);
    ftxui::Element render_status_bar();

    void add_log(LogMessage::Level level, const std::string& text);
    void clear_log();
    void save_config();
    void enqueue_from_url();
    void start_fetch_info(const std::string& url);
    void on_url_change();
    void on_download_change();
    void update_queued_titles(const std::string& url, const std::string& title);
    void open_file_browser();
    void navigate_browser(const std::filesystem::path& path);
    void select_browser_entry(const std::string& entry);

    ftxui::ScreenInteractive screen_;
    ConfigManager config_manager_;
    Config config_;
    DownloadManager download_manager_;
    DownloadQueue queue_;
    HistoryManager history_;
    std::vector<LogMessage> log_messages_;
    std::mutex log_mutex_;
    int active_downloads_{0};

    // UI state
    std::string url_input_;
    std::string output_path_input_;
    int selected_tab_{0};
    bool audio_only_{false};
    bool show_help_{false};
    bool show_welcome_{true};
    bool show_quit_confirm_{false};

    // Shared state protected by state_mutex_
    std::string status_text_;
    std::string audio_btn_label_;
    std::unordered_set<int> history_recorded_ids_;
    VideoInfo current_video_info_;
    std::atomic<bool> fetch_in_progress_{false};
    mutable std::mutex state_mutex_;

    // Tab labels (updated dynamically)
    std::string tab_q_label_;
    std::string tab_h_label_;
    std::string tab_l_label_;

    // Fetch thread
    std::string cached_url_;
    std::mutex fetch_mutex_;
    std::thread fetch_thread_;
    std::atomic<bool> fetch_running_{false};
    std::atomic<int> fetch_generation_{0};

    // Spinner state (shared across header + queue rows to avoid static locals in loops)
    int spinner_phase_{0};
    std::chrono::steady_clock::time_point spinner_last_{std::chrono::steady_clock::now()};

    // File browser state
    bool show_file_browser_{false};
    int browser_selector_{0};
    std::string browser_current_path_;
    std::vector<std::string> browser_entries_;
    int browser_selected_{0};
    int browser_scroll_{0};

    // Queue item action buttons (cancel / clear)
    // Box objects stored as members so reflect() references remain valid across frames
    std::vector<ftxui::Box> queue_cancel_boxes_;
    std::vector<int> queue_cancel_ids_;
    std::vector<ftxui::Box> queue_clear_boxes_;
    std::vector<int> queue_clear_ids_;

    // Components
    ftxui::Component url_input_comp_;
    ftxui::Component download_btn_;
    ftxui::Component queue_btn_;
    ftxui::Component audio_toggle_btn_;
    ftxui::Component quit_btn_;
    ftxui::Component path_input_comp_;
    ftxui::Component path_display_comp_;
    ftxui::Component browse_btn_;
    ftxui::Component tab_buttons_;
    ftxui::Component clear_queue_btn_;
    ftxui::Component clear_history_btn_;
    ftxui::Component clear_log_btn_;
    ftxui::Component browser_menu_;
    ftxui::Component browser_confirm_;
    ftxui::Component browser_cancel_;
    ftxui::Component quit_confirm_yes_;
    ftxui::Component quit_confirm_no_;
    ftxui::Component main_container_;
};

} // namespace yt_tui
