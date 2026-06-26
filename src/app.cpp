#include "app.hpp"

#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/component/component.hpp>
#include <ftxui/component/component_options.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/color.hpp>
#include <ftxui/screen/terminal.hpp>

#include <algorithm>
#include <iomanip>
#include <sstream>
#include <ctime>
#include <cstdio>
#include <memory>
#include <regex>

namespace yt_tui {

using namespace ftxui;

namespace {

const Color c_bg        = Color::RGB(24, 24, 31);
const Color c_bg_dark   = Color::RGB(18, 18, 24);
const Color c_border    = Color::RGB(50, 50, 68);
const Color c_purple    = Color::RGB(130, 100, 250);
const Color c_purple_lt = Color::RGB(170, 140, 255);
const Color c_blue      = Color::RGB(100, 180, 255);
const Color c_green     = Color::RGB(80, 220, 130);
const Color c_red       = Color::RGB(245, 85, 95);
const Color c_red_lt    = Color::RGB(255, 120, 130);
const Color c_yellow    = Color::RGB(240, 200, 60);
const Color c_dim       = Color::RGB(120, 120, 140);
const Color c_dim_l     = Color::RGB(160, 160, 175);

const int BROWSER_VISIBLE = 14;

Element dim_text(const std::string& s) {
    return text(s) | color(c_dim);
}

Element section_label(const std::string& s) {
    return hbox(
        text(" \u2502 ") | color(c_purple) | bold,
        text(s) | color(c_dim_l) | bold,
        text(" ") | flex
    );
}

Element thin_sep() {
    return separator() | color(c_border);
}

Element bg(const Element& e) {
    return e | bgcolor(c_bg);
}

// Browser component: scroll wheel scrolls viewport only, click selects, Enter confirms.
class BrowserComponent : public ComponentBase {
public:
    using Action = std::function<void(int)>;

    BrowserComponent(std::vector<std::string>* entries, int* selected,
                     int* scroll_offset,
                     Action on_click = nullptr,
                     Action on_enter = nullptr)
        : entries_(*entries), selected_(*selected),
          scroll_offset_(*scroll_offset),
          on_click_(std::move(on_click)),
          on_enter_(std::move(on_enter)) {}

private:
    std::vector<std::string>& entries_;
    int& selected_;
    int& scroll_offset_;
    Action on_click_;
    Action on_enter_;
    std::vector<Box> boxes_;

    static void ensure_visible(int& sel, int& soff, int total, int vis) {
        if (total == 0) return;
        if (soff < 0) soff = 0;
        if (soff + vis > total) soff = std::max(0, total - vis);
        if (sel < soff) soff = sel;
        if (sel >= soff + vis) soff = sel - vis + 1;
    }

    Element Render() override {
        boxes_.resize(entries_.size());
        ensure_visible(selected_, scroll_offset_,
                       (int)entries_.size(), BROWSER_VISIBLE);

        Elements elements;
        int end = std::min((int)entries_.size(), scroll_offset_ + BROWSER_VISIBLE);

        for (int i = scroll_offset_; i < end; i++) {
            std::string label = entries_[i];
            bool is_dir = !label.empty() && label.back() == '/';
            bool is_select = label.find("[Select") == 0;

            Element line;
            if (i == selected_) {
                line = text(" \u25c9 " + label) | color(c_purple_lt) | bold;
                line = line | bgcolor(Color::RGB(38, 38, 52));
            } else if (is_select) {
                line = text("   " + label) | color(c_green);
            } else if (is_dir) {
                line = text("   " + label) | color(c_blue);
            } else {
                line = text("   " + label) | color(c_dim_l);
            }

            elements.push_back(line | reflect(boxes_[i]));
        }

        auto content = vbox(std::move(elements));

        if ((int)entries_.size() > BROWSER_VISIBLE) {
            int total = (int)entries_.size();
            int bar_h = std::max(1, BROWSER_VISIBLE * BROWSER_VISIBLE / total);
            int bar_pos = scroll_offset_ * (BROWSER_VISIBLE - bar_h) / std::max(1, total - BROWSER_VISIBLE);
            Elements scroll_bar;
            for (int r = 0; r < BROWSER_VISIBLE; r++) {
                if (r >= bar_pos && r < bar_pos + bar_h)
                    scroll_bar.push_back(text("\u2502") | color(c_purple));
                else
                    scroll_bar.push_back(text("\u2502") | color(Color::RGB(35, 35, 45)));
            }
            content = hbox(content | flex, vbox(std::move(scroll_bar)));
        }

        return content;
    }

    bool OnEvent(Event event) override {
        if (event.is_mouse()) {
            if (event.mouse().button == Mouse::WheelUp) {
                if (selected_ > 0) {
                    selected_--;
                    return true;
                }
                return true;
            }
            if (event.mouse().button == Mouse::WheelDown) {
                if (selected_ < (int)entries_.size() - 1) {
                    selected_++;
                    return true;
                }
                return true;
            }
            if (event.mouse().button == Mouse::Left &&
                event.mouse().motion == Mouse::Pressed) {
                if (!CaptureMouse(event))
                    return false;
                // BUG-15 fix: boxes_[i] stores the reflect box for entry index i.
                // Render() uses full indices starting from scroll_offset_, so we
                // iterate only the visible range and use i directly as entry_idx.
                int end_idx = std::min((int)entries_.size(), scroll_offset_ + BROWSER_VISIBLE);
                int mouse_x = event.mouse().x;
                int mouse_y = event.mouse().y;
                for (int i = scroll_offset_; i < end_idx; i++) {
                    if (boxes_[i].Contain(mouse_x, mouse_y)) {
                        selected_ = i;
                        if (on_click_)
                            on_click_(i);
                        return true;
                    }
                }
            }
            return false;
        }
        {
            if (event == Event::ArrowUp && selected_ > 0)
                selected_--;
            else if (event == Event::ArrowDown && selected_ < (int)entries_.size() - 1)
                selected_++;
            else if (event == Event::PageUp) {
                int t = selected_ - BROWSER_VISIBLE;
                selected_ = (t < 0) ? 0 : t;
            }
            else if (event == Event::PageDown) {
                int t = selected_ + BROWSER_VISIBLE;
                int total = (int)entries_.size();
                selected_ = (t >= total) ? total - 1 : t;
            }
            else if (event == Event::Home)
                selected_ = 0;
            else if (event == Event::End)
                selected_ = (int)entries_.size() - 1;
            else if (event == Event::Return) {
                if (on_enter_) on_enter_(selected_);
                return true;
            } else
                return false;
            // Ensure selection is visible (only after keyboard nav, not wheel)
            ensure_visible(selected_, scroll_offset_,
                           (int)entries_.size(), BROWSER_VISIBLE);
            return true;
        }
    }
};

} // anonymous namespace

App::App(std::filesystem::path config_dir)
    : screen_(ScreenInteractive::Fullscreen()),
      config_manager_(config_dir),
      config_(config_manager_.load()),
      queue_(download_manager_),
      history_(config_dir / "history.json")
{
    auto abs_dir = std::filesystem::absolute(config_.output_dir);
    output_path_input_ = abs_dir.string();
    download_manager_.set_output_dir(abs_dir);
    download_manager_.set_output_template(config_.output_template);

    queue_.set_change_callback([this] { on_download_change(); });

    fetch_running_ = true;
    fetch_thread_ = std::thread([this]() {
        while (fetch_running_) {
            std::string url;
            int gen;
            {
                std::unique_lock<std::mutex> lock(fetch_start_mutex_);
                fetch_cv_.wait_for(lock, std::chrono::milliseconds(600), [this] {
                    return !fetch_running_ || !fetch_pending_url_.empty();
                });
                if (!fetch_running_) break;
                if (fetch_pending_url_.empty()) continue;
                url = fetch_pending_url_;
                fetch_pending_url_.clear();
                {
                    std::lock_guard<std::mutex> lk(fetch_mutex_);
                    gen = fetch_generation_;
                }
            }

            fetch_in_progress_ = true;
            {
                std::lock_guard<std::mutex> lock(state_mutex_);
                status_text_ = "Fetching formats...";
            }

            try {
                auto vi = download_manager_.fetch_info(url);
                {
                    std::lock_guard<std::mutex> lock(fetch_mutex_);
                    if (fetch_generation_ != gen) { fetch_in_progress_ = false; continue; }
                    current_video_info_ = vi;
                    fetch_in_progress_ = false;
                }
                update_queued_titles(url, vi.title);
                add_log(LogMessage::Success, vi.title + " (" + vi.duration + ")");
                {
                    std::lock_guard<std::mutex> lock(state_mutex_);
                    status_text_ = vi.title;
                }
            } catch (const std::exception& e) {
                fetch_in_progress_ = false;
                add_log(LogMessage::Warning, "Fetch failed: " + std::string(e.what()));
                {
                    std::lock_guard<std::mutex> lock(state_mutex_);
                    status_text_ = "Info fetch failed";
                }
            }
            screen_.PostEvent(Event::Custom);
        }
        fetch_in_progress_ = false;
    });

    add_log(LogMessage::Info, "YouTube Downloader TUI started");
    add_log(LogMessage::Info, "Using yt-dlp: " + download_manager_.get_ytdlp_path());

    if (!std::filesystem::exists(download_manager_.get_ytdlp_path())) {
        add_log(LogMessage::Error, "yt-dlp not found: " + download_manager_.get_ytdlp_path());
    } else {
        add_log(LogMessage::Success, "yt-dlp found");
    }
    add_log(LogMessage::Success, "Ready");
    save_config();
}

App::~App() {
    // P1.4: Clear queue change callback so the queue thread doesn't invoke methods
    // on partially-destroyed App while we shut down.
    queue_.set_change_callback(nullptr);
    fetch_running_ = false;
    fetch_cv_.notify_one();
    if (fetch_thread_.joinable())
        fetch_thread_.join();
    queue_.shutdown();
    download_manager_.cleanup_finished();
    save_config();
}

void App::add_log(LogMessage::Level level, const std::string& text) {
    std::lock_guard<std::mutex> lock(log_mutex_);
    log_messages_.push_back({level, text, std::chrono::system_clock::now()});
    if (log_messages_.size() > 500) log_messages_.erase(log_messages_.begin());
}

void App::clear_log() {
    std::lock_guard<std::mutex> lock(log_mutex_);
    log_messages_.clear();
}

void App::save_config() {
    config_.output_dir = std::filesystem::absolute(std::filesystem::path(output_path_input_));
    config_manager_.save(config_);
}

bool App::enqueue_from_url() {
    if (url_input_.empty()) {
        std::lock_guard<std::mutex> lock(state_mutex_);
        status_text_ = "Enter a URL";
        return false;
    }

    if (queue_.has_url(url_input_)) {
        std::lock_guard<std::mutex> lock(state_mutex_);
        status_text_ = "Already in queue";
        return false;
    }

    auto item = std::make_shared<DownloadItem>();
    item->url = url_input_;
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        item->title = current_video_info_.title;
    }
    item->media_type = audio_only_ ? MediaType::Audio : MediaType::Video;
    item->format = audio_only_ ? "mp3" : "mp4";

    if (!output_path_input_.empty()) {
        auto p = std::filesystem::path(output_path_input_);
        std::filesystem::create_directories(p);
        download_manager_.set_output_dir(p);
    }

    show_welcome_ = false;
    int id = queue_.enqueue(item);
    url_input_.clear();
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        status_text_ = "Queued (ID: " + std::to_string(id) + ")";
    }
    return true;
}

void App::start_fetch_info(const std::string& url) {
    if (url.empty()) return;
    std::lock_guard<std::mutex> lock(fetch_mutex_);
    if (url == cached_url_) return;
    cached_url_ = url;
    ++fetch_generation_;
    {
        std::lock_guard<std::mutex> lk(fetch_start_mutex_);
        fetch_pending_url_ = url;
    }
    fetch_cv_.notify_one();
}

void App::on_url_change() {
    std::string input = url_input_;
    if (input.empty()) return;
    if (input.find("://") != std::string::npos ||
        input.find("youtube.com") != std::string::npos ||
        input.find("youtu.be") != std::string::npos)
    {
        show_welcome_ = false;
        start_fetch_info(input);
    }
}

void App::update_queued_titles(const std::string& url, const std::string& title) {
    if (title.empty()) return;
    queue_.update_title(url, title);
}

void App::on_download_change() {
    auto items = queue_.all_items();
    int running = 0;
    for (auto& item : items) {
        if (item->state == DownloadState::Running) running++;

        // Record completed/failed/cancelled in history (first time only)
        bool terminal = (item->state == DownloadState::Completed ||
                         item->state == DownloadState::Failed ||
                         item->state == DownloadState::Cancelled);
        if (terminal && history_recorded_ids_.find(item->id) == history_recorded_ids_.end()) {
            history_.add_entry(*item);
            history_recorded_ids_.insert(item->id);
            if (item->state == DownloadState::Completed)
                add_log(LogMessage::Success, "Complete: " + item->title);
            else if (item->state == DownloadState::Failed)
                add_log(LogMessage::Error, "Failed: " + item->title);
            else
                add_log(LogMessage::Warning, "Cancelled: " + item->title);
        }
    }
    active_downloads_ = running;
    screen_.PostEvent(Event::Custom);
}

void App::open_file_browser() {
    show_file_browser_ = true;
    browser_scroll_ = 0;
    browser_current_path_ = output_path_input_.empty()
        ? std::filesystem::current_path().string()
        : output_path_input_;

    try {
        auto p = std::filesystem::path(browser_current_path_);
        if (!std::filesystem::exists(p))
            browser_current_path_ = std::filesystem::current_path().string();
    } catch (...) {
        browser_current_path_ = std::filesystem::current_path().string();
    }
    navigate_browser(std::filesystem::path(browser_current_path_));
}

static std::string dir_entry_name(const std::filesystem::directory_entry& e) {
    auto name = e.path().filename().string();
    return e.is_directory() ? (name + "/") : name;
}

void App::navigate_browser(const std::filesystem::path& path) {
    browser_entries_.clear();
    browser_selected_ = 0;
    browser_scroll_ = 0;
    try {
        auto canonical = std::filesystem::weakly_canonical(path);
        browser_current_path_ = canonical.string();
        browser_entries_.push_back("[Select Current Dir]");
        if (canonical.has_parent_path())
            browser_entries_.push_back("..");

        std::vector<std::filesystem::directory_entry> dirs, files;
        for (auto& e : std::filesystem::directory_iterator(canonical)) {
            if (e.is_directory()) dirs.push_back(e);
            else files.push_back(e);
        }
        std::sort(dirs.begin(), dirs.end());
        std::sort(files.begin(), files.end());

        for (auto& d : dirs) browser_entries_.push_back(dir_entry_name(d));
        for (auto& f : files) browser_entries_.push_back(dir_entry_name(f));
    } catch (const std::exception& e) {
        browser_entries_.push_back("[Error: " + std::string(e.what()) + "]");
    }
    if (!browser_entries_.empty()) browser_selected_ = 0;
}

void App::select_browser_entry(const std::string& entry) {
    if (entry == "[Select Current Dir]") {
        output_path_input_ = browser_current_path_;
        show_file_browser_ = false;
        save_config();
        // BUG-06 fix: protect status_text_ writes with state_mutex_.
        std::lock_guard<std::mutex> lock(state_mutex_);
        status_text_ = "Path: " + browser_current_path_;
        return;
    }
    if (entry == "..") {
        navigate_browser(std::filesystem::path(browser_current_path_).parent_path());
        return;
    }
    std::string clean = entry;
    if (!clean.empty() && clean.back() == '/') clean.pop_back();
    auto new_path = std::filesystem::path(browser_current_path_) / clean;
    if (std::filesystem::is_directory(new_path))
        navigate_browser(new_path);
}

void App::setup_components() {
    InputOption input_style;
    input_style.transform = [](InputState const& s) {
        auto el = s.element;
        if (s.focused) {
            el = el | bgcolor(Color::RGB(38, 38, 52)) | color(c_purple_lt);
            return hbox(
                text(" ") | bgcolor(c_purple) | size(WIDTH, EQUAL, 1),
                el | flex
            );
        }
        el = el | bgcolor(Color::RGB(26, 26, 36));
        if (s.hovered)
            el = el | bgcolor(Color::RGB(32, 32, 44));
        return hbox(
            text(" ") | bgcolor(c_border) | size(WIDTH, EQUAL, 1),
            el | flex
        );
    };

    url_input_comp_ = Input(&url_input_, "paste YouTube URL here...", input_style);

    download_btn_ = Button(" \u25b6 Download ", [this] {
        bool enqueued = enqueue_from_url();
        queue_.process();
        {
            std::lock_guard<std::mutex> lock(state_mutex_);
            auto items = queue_.all_items();
            if (enqueued && !items.empty() && items.back()->state == DownloadState::Pending) {
                status_text_ = "Download started (ID: " + std::to_string(items.back()->id) + ")";
            } else if (enqueued) {
                status_text_ = "Queued (ID: " + std::to_string(items.back()->id) + ")";
            }
        }
    });
    audio_btn_label_ = "\u25a2 Audio";
    audio_toggle_btn_ = Button(&audio_btn_label_, [this] {
        audio_only_ = !audio_only_;
        audio_btn_label_ = audio_only_ ? "\u2713 Audio" : "\u25a2 Audio";
    });
    quit_btn_ = Button(" \u2715 Quit ", [this] { show_quit_confirm_ = true; });

    clear_queue_btn_ = Button(" Clear Completed ", [this] { queue_.clear_completed(); });
    clear_history_btn_ = Button(" Clear History ", [this] { history_.clear(); });
    clear_log_btn_ = Button(" Clear Log ", [this] { clear_log(); });

    // Non-editable path display + Browse button
    path_display_comp_ = Renderer([this] {
        auto p = std::filesystem::path(output_path_input_);
        std::string display = p.filename().string();
        if (display.empty()) display = output_path_input_;
        auto parent = p.parent_path().string();
        if (parent.empty() || parent == "/") parent = "";
        else parent += "/";

        return hbox(
            text(" ") | bgcolor(c_border) | size(WIDTH, EQUAL, 1),
            dim_text(parent),
            text(display) | color(c_purple_lt) | bold,
            text(" ") | flex
        ) | bgcolor(Color::RGB(26, 26, 36));
    });

    browse_btn_ = Button(" \u2026 Browse ", [this] { open_file_browser(); });

    quit_confirm_yes_ = Button("  Yes  ", [this] { screen_.ExitLoopClosure()(); });
    quit_confirm_no_ = Button("  No  ", [this] { show_quit_confirm_ = false; });

    tab_q_label_ = " Queue (0) ";
    tab_h_label_ = " History (0) ";
    tab_l_label_ = " Log (0) ";
    auto tab_q = Button(&tab_q_label_, [this] { selected_tab_ = 0; });
    auto tab_h = Button(&tab_h_label_, [this] { selected_tab_ = 1; });
    auto tab_l = Button(&tab_l_label_, [this] { selected_tab_ = 2; });
    tab_buttons_ = Container::Horizontal(Components{tab_q, tab_h, tab_l});

    // File browser using custom BrowserComponent
    browser_menu_ = std::make_shared<BrowserComponent>(
        &browser_entries_, &browser_selected_, &browser_scroll_,
        [this](int idx) {
            if (idx >= 0 && idx < (int)browser_entries_.size())
                select_browser_entry(browser_entries_[idx]);
        },
        [this](int idx) {
            if (idx >= 0 && idx < (int)browser_entries_.size())
                select_browser_entry(browser_entries_[idx]);
        }
    );

    browser_confirm_ = Button(" Select ", [this] {
        if (browser_selected_ >= 0 && browser_selected_ < (int)browser_entries_.size())
            select_browser_entry(browser_entries_[browser_selected_]);
    });
    browser_cancel_ = Button(" Cancel ", [this] { show_file_browser_ = false; });

    auto browser_layout = Container::Vertical(Components{
        browser_menu_,
        Container::Horizontal(Components{browser_confirm_, browser_cancel_}),
    });

    auto main_content = Container::Vertical(Components{
        url_input_comp_,
        Container::Horizontal(Components{download_btn_, audio_toggle_btn_, quit_btn_}),
        path_display_comp_,
        Container::Horizontal(Components{browse_btn_}),
        tab_buttons_,
        clear_queue_btn_ | Maybe([this] { return selected_tab_ == 0; }),
        clear_history_btn_ | Maybe([this] { return selected_tab_ == 1; }),
        clear_log_btn_ | Maybe([this] { return selected_tab_ == 2; }),
    });

    auto quit_confirm_layout = Container::Vertical(Components{
        Container::Horizontal(Components{quit_confirm_yes_, quit_confirm_no_}),
    });

    auto root = Container::Tab(Components{main_content, browser_layout, quit_confirm_layout}, &browser_selector_);

    auto renderer = Renderer(root, [this, tab_q, tab_h, tab_l] {
        browser_selector_ = show_quit_confirm_ ? 2 : (show_file_browser_ ? 1 : 0);

        {
            auto term = Terminal::Size();
            if (term.dimx < 90 || term.dimy < 28) {
                auto sz = std::to_string(term.dimx) + "x" + std::to_string(term.dimy);
                return bg(vbox(Elements{
                    text("") | size(HEIGHT, EQUAL, 4),
                    text(" \u26a0  Terminal Too Small  \u26a0 ") | bold | color(c_red) | center,
                    text("") | size(HEIGHT, EQUAL, 2),
                    text(" Please resize your terminal to at least 90x28 ") | center,
                    text(" Current size: " + sz + " ") | dim | center,
                    text("") | size(HEIGHT, EQUAL, 1),
                    text(" The app will automatically resume when resized. ") | color(c_dim) | center,
                    text("") | size(HEIGHT, EQUAL, 4),
                }) | borderStyled(ROUNDED) | color(c_red) | center);
            }
        }

        static std::string prev_url;
        if (url_input_ != prev_url) {
            prev_url = url_input_;
            on_url_change();
        }

        tab_q_label_ = " Queue (" + std::to_string(queue_.total_count()) + ") ";
        tab_h_label_ = " History (" + std::to_string(history_.get_all().size()) + ") ";
        // BUG-10 fix: read log_messages_.size() under the log_mutex_ lock.
        {
            std::lock_guard<std::mutex> lock(log_mutex_);
            tab_l_label_ = " Log (" + std::to_string(log_messages_.size()) + ") ";
        }

        if (show_help_) {
            return bg(vbox(
                text(" YouTube Downloader TUI ") | bold | underlined | center,
                separator(),
                text(" Enter URL, click Download or Add to Queue.") | flex,
                text(" Toggle Audio Only for mp3 download."),
                text(" Click Browse to set download directory."),
                text(" Click Quit to exit."),
                separator(),
                text(" v1.0.0") | dim | center
            ) | borderStyled(ROUNDED) | center | flex);
        }

        auto main_view = bg(vbox(
            render_header(),
            (show_welcome_ && url_input_.empty() && queue_.total_count() == 0)
                ? render_welcome() | flex
                : vbox(
                    thin_sep(),
                    render_url_section(),
                    thin_sep(),
                    render_action_buttons(),
                    thin_sep(),
                    render_output_section(),
                    thin_sep(),
                    render_tab_bar(tab_q, tab_h, tab_l),
                    thin_sep(),
                    (selected_tab_ == 0) ? render_queue_tab() | flex :
                    (selected_tab_ == 1) ? render_history_tab() | flex :
                                           render_log_tab() | flex
                  ) | flex,
            render_status_bar()
        ));

        if (show_quit_confirm_) {
            auto confirm_dialog = vbox({
                text(" Quit? ") | bold | color(c_purple_lt) | center,
                separator(),
                text(" Are you sure you want to exit?") | center,
                text(""),
                hbox({
                    quit_confirm_yes_->Render() | color(c_red) | bold,
                    text("  "),
                    quit_confirm_no_->Render() | color(c_green) | bold,
                }) | center,
            }) | borderStyled(ROUNDED) | color(c_border)
               | size(WIDTH, LESS_THAN, 40) | size(HEIGHT, LESS_THAN, 10);

            return dbox({
                main_view | dim,
                bg(confirm_dialog | center | clear_under),
            });
        }

        if (show_file_browser_) {
            auto browser_vbox = vbox(Elements{
                hbox(
                    text(" \u2502 ") | color(c_purple) | bold,
                    text(" Select Output Directory ") | bold | color(c_purple_lt),
                    text(" ") | flex,
                    dim_text("\u2191\u2193/\u21c7\u21c1 nav  \u23ce open  esc back")
                ),
                thin_sep(),
                dim_text(" " + browser_current_path_),
                thin_sep(),
                browser_menu_->Render() | flex,
                thin_sep(),
                hbox(
                    text("  "),
                    browser_confirm_->Render() | color(c_green) | bold,
                    text("  "),
                    browser_cancel_->Render() | color(c_dim),
                    text(" ") | flex
                ) | center
            });

            return dbox({
                main_view | dim,
                bg(browser_vbox
                    | borderStyled(ROUNDED) | color(c_border)
                    | center | clear_under
                    | size(WIDTH, LESS_THAN, 62)
                    | size(HEIGHT, LESS_THAN, 26))
            });
        }

        return main_view;
    });

    // Outer CatchEvent handles cancel clicks and welcome-screen dismiss
    main_container_ = renderer | CatchEvent([this](Event e) {
        // Cancel button clicks in queue tab
        if (selected_tab_ == 0 && e.is_mouse()) {
            auto& m = e.mouse();
            // Only accept explicit Left-click press (not hover/release)
            if (m.button == Mouse::Left && m.motion == Mouse::Pressed) {
                for (size_t i = 0; i < queue_cancel_boxes_.size(); i++) {
                    if (queue_cancel_boxes_[i].Contain(m.x, m.y)) {
                        if (i < queue_cancel_ids_.size()) {
                            add_log(LogMessage::Info,
                                "Cancel click on ID " +
                                std::to_string(queue_cancel_ids_[i]));
                            queue_.cancel(queue_cancel_ids_[i]);
                        }
                        return true;
                    }
                }
                // Clear button clicks for completed/failed/cancelled items
                for (size_t i = 0; i < queue_clear_boxes_.size(); i++) {
                    if (queue_clear_boxes_[i].Contain(m.x, m.y)) {
                        if (i < queue_clear_ids_.size()) {
                            queue_.remove_item(queue_clear_ids_[i]);
                        }
                        return true;
                    }
                }
            }
        }
        // Quit confirmation: keyboard shortcuts
        if (show_quit_confirm_) {
            if (!e.is_mouse()) {
                if (e == Event::Return || e == Event::Character('y') || e == Event::Character('Y')) {
                    screen_.ExitLoopClosure()();
                    return true;
                }
                if (e == Event::Escape || e == Event::Character('n') || e == Event::Character('N')) {
                    show_quit_confirm_ = false;
                    return true;
                }
                // Let Tab/arrows through for button navigation
            }
            // Mouse events pass through to the Tab (confirm buttons handle them)
        }

        // Welcome screen: dismiss on keyboard input only
        if (!e.is_mouse() && show_welcome_ &&
            url_input_.empty() && queue_.total_count() == 0) {
            show_welcome_ = false;
            return true;
        }
        return false;

    });
}

Element App::render_header() {
    std::string subtitle;
    bool fetching = false;
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        if (!current_video_info_.title.empty()) {
            subtitle = current_video_info_.title;
            if (subtitle.size() > 32) subtitle = subtitle.substr(0, 29) + "...";
        }
        fetching = fetch_in_progress_;
    }

    Elements left;
    left.push_back(text(" ") | color(c_purple));
    left.push_back(text("\u2502") | color(c_purple));
    left.push_back(text(" ytd ") | bold | color(c_purple_lt));
    if (!subtitle.empty()) {
        left.push_back(text("\u2502") | color(c_border));
        left.push_back(text(" " + subtitle + " ") | color(c_dim_l));
    }
    if (fetching) {
        static const char* sp = "\u25d4\u25d1\u25d5\u25d2";
        auto now = std::chrono::steady_clock::now();
        // BUG-02 fix: use member variables instead of static locals so the
        // spinner state is not shared with per-item spinners in the queue tab.
        if (now - spinner_last_ > std::chrono::milliseconds(250)) {
            spinner_phase_ = (spinner_phase_ + 1) % 4;
            spinner_last_ = now;
        }
        left.push_back(text(std::string(1, sp[spinner_phase_])) | color(c_yellow));
    }

    Elements right;
    if (active_downloads_ > 0) {
        right.push_back(text(" \u25cf " + std::to_string(active_downloads_) + " ") | color(c_green) | bold);
    }
    right.push_back(text(" v1.0.0 ") | dim);

    return hbox(
        hbox(std::move(left)) | flex,
        hbox(std::move(right))
    ) | bgcolor(c_bg_dark);
}

Element App::render_welcome() {
    return vbox({
        text("") | size(HEIGHT, EQUAL, 2),
        hbox({
            text("") | flex,
            vbox({
                text("  YT-TUI  ") | color(c_purple_lt) | bold | underlined | center,
                text("") | size(HEIGHT, EQUAL, 2),
                text("  YouTube Downloader TUI  ") | color(c_purple_lt) | bold | center,
                text("  v" + std::string("1.0.0") + "  ") | color(c_dim) | center,
                text("") | size(HEIGHT, EQUAL, 1),
                text("  \u25b8 Paste a URL and press Download  \u25c2  ") | color(c_dim) | center,
                text("") | size(HEIGHT, EQUAL, 2),
                text("  Press any key to begin  ") | color(Color::RGB(120, 120, 140)) | center,
            }),
            text("") | flex,
        }),
        text("") | size(HEIGHT, EQUAL, 3),
    }) | center;
}

Element App::render_url_section() {
    std::string info;
    bool fetching = false;
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        if (!current_video_info_.title.empty() && !url_input_.empty()) {
            info = current_video_info_.title;
            if (!current_video_info_.duration.empty())
                info += " (" + current_video_info_.duration + ")";
            if (info.size() > 50) info = info.substr(0, 47) + "...";
        }
        fetching = fetch_in_progress_;
    }

    Elements lines;
    lines.push_back(hbox(text("  "), url_input_comp_->Render() | flex));
    if (!info.empty()) {
        lines.push_back(hbox(
            text("    "),
            text("\u2713") | color(c_green) | bold,
            text(" ") | color(c_green),
            dim_text(info)
        ));
    } else if (fetching) {
        lines.push_back(hbox(
            text("    "),
            dim_text("fetching...")
        ));
    }

    return vbox(
        section_label("URL"),
        vbox(std::move(lines))
    );
}

Element App::render_action_buttons() {
    Element space = text(" ") | size(WIDTH, EQUAL, 1);

    auto dl_el = download_btn_->Render();
    auto au_el = audio_toggle_btn_->Render();
    auto qt_el = quit_btn_->Render();

    return hbox(
        text("   "),
        dl_el | color(audio_only_ ? c_green : c_purple) | bold, space,
        au_el | (audio_only_ ? (color(c_green) | bold) : color(c_dim)), space,
        text(" ") | flex,
        qt_el | color(c_red) | bold
    );
}

Element App::render_output_section() {
    return vbox(
        section_label("Output"),
        hbox(
            text("  "),
            path_display_comp_->Render() | flex,
            text(" "),
            browse_btn_->Render() | color(c_blue)
        )
    );
}

Element App::render_tab_bar(const Component& tq, const Component& th, const Component& tl) {
    auto make_tab = [this](int idx, const Component& c, Color active_color) -> Element {
        auto e = c->Render();
        if (selected_tab_ == idx) {
            return hbox(
                text(" ") | bgcolor(active_color) | size(WIDTH, EQUAL, 1),
                text(" ") | bgcolor(c_bg_dark),
                e | color(active_color) | bold,
                text(" ")
            );
        }
        return hbox(
            text(" ") | dim,
            e | dim,
            text(" ")
        );
    };

    return hbox(
        text(" "),
        make_tab(0, tq, c_purple_lt),
        make_tab(1, th, c_blue),
        make_tab(2, tl, c_dim_l),
        text(" ") | flex
    ) | bgcolor(c_bg_dark);
}

Element App::render_queue_tab() {
    auto items = queue_.all_items();
    if (items.empty()) {
        return vbox(Elements{
            text("") | size(HEIGHT, EQUAL, 2),
            hbox(text("   "), dim_text("\u2205 queue is empty")) | center | flex,
            text("") | size(HEIGHT, EQUAL, 1),
            hbox(text("   "), clear_queue_btn_->Render()) | center,
        }) | flex;
    }

    queue_cancel_boxes_.clear();
    queue_cancel_ids_.clear();
    queue_clear_boxes_.clear();
    queue_clear_ids_.clear();
    Elements rows;

    rows.push_back(hbox(
        text("   "),
        text(" ID") | bold | color(c_purple_lt) | size(WIDTH, EQUAL, 4),
        text(" Title") | bold | color(c_purple_lt) | flex,
        text(" Status") | bold | color(c_purple_lt) | size(WIDTH, EQUAL, 9),
        text(" Progress") | bold | color(c_purple_lt) | size(WIDTH, EQUAL, 14),
        text(" Speed") | bold | color(c_purple_lt) | size(WIDTH, EQUAL, 9),
        text("  ")
    ) | bgcolor(c_bg_dark));

    for (auto& item : items) {
        Color sc;
        std::string status_icon;
        switch (item->state) {
            case DownloadState::Running:
                sc = c_green; status_icon = "\u25b6"; break;
            case DownloadState::Completed:
                sc = c_green; status_icon = "\u2713"; break;
            case DownloadState::Failed:
                sc = c_red; status_icon = "\u2717"; break;
            case DownloadState::Cancelled:
                sc = c_yellow; status_icon = "\u2715"; break;
            case DownloadState::Paused:
                sc = c_yellow; status_icon = "\u23f8"; break;
            default:
                sc = c_dim; status_icon = "\u25cb"; break;
        }

        auto title = item->title.empty() ? item->url : item->title;
        if (title.empty()) title = "(no title)";
        if (!item->resolution.empty() && !item->title.empty())
            title += " - " + item->resolution;
        if (title.size() > 36) title = title.substr(0, 33) + "...";

        std::string pstr;
        Element ge;
        if (item->state == DownloadState::Running) {
            std::ostringstream ss;
            ss << std::fixed << std::setprecision(1) << item->progress.percent << "%";
            pstr = ss.str();
            ge = gauge(item->progress.percent / 100.0) | color(c_purple);
        } else if (item->state == DownloadState::Completed) {
            pstr = "100%"; ge = gauge(1.0) | color(c_green);
        } else if (item->state == DownloadState::Failed) {
            pstr = "FAIL"; ge = gauge(0.0) | color(c_red);
        } else if (item->state == DownloadState::Cancelled) {
            pstr = "CNCL"; ge = gauge(0.0) | color(c_yellow);
        } else {
            pstr = "\u2014"; ge = gauge(0.0) | color(c_dim);
        }

        std::string speed = (item->state == DownloadState::Running && !item->progress.speed.empty())
            ? item->progress.speed : "\u2014";

        std::string sstr = status_icon + " " + state_to_string(item->state);
        if (item->state == DownloadState::Running) {
            // BUG-02 fix: use member spinner state rather than static locals
            // inside this per-item loop (statics would be shared across iterations).
            static const char* sp = "\u25d4\u25d1\u25d5\u25d2";
            auto now = std::chrono::steady_clock::now();
            if (now - spinner_last_ > std::chrono::milliseconds(250)) {
                spinner_phase_ = (spinner_phase_ + 1) % 4;
                spinner_last_ = now;
            }
            sstr = status_icon + " " + std::string(1, sp[spinner_phase_]);
        }

        rows.push_back(separator() | color(c_border));

        bool is_terminal = (item->state == DownloadState::Completed ||
                            item->state == DownloadState::Failed ||
                            item->state == DownloadState::Cancelled);

        Element action_el;
        if (is_terminal) {
            auto& clear_box = queue_clear_boxes_.emplace_back();
            action_el = hbox(
                text("\u2716") | color(c_dim) | bold | bgcolor(c_bg_dark),
                text(" ") | bgcolor(c_bg_dark) | size(WIDTH, EQUAL, 1)
            ) | reflect(clear_box);
            queue_clear_ids_.push_back(item->id);
        } else {
            auto& cancel_box = queue_cancel_boxes_.emplace_back();
            action_el = hbox(
                text(" ") | bgcolor(Color::RGB(60, 30, 30)) | size(WIDTH, EQUAL, 1),
                text("\u2715") | color(c_red) | bold | bgcolor(Color::RGB(60, 30, 30)),
                text(" ") | bgcolor(Color::RGB(60, 30, 30)) | size(WIDTH, EQUAL, 1)
            ) | reflect(cancel_box);
            queue_cancel_ids_.push_back(item->id);
        }

        rows.push_back(hbox(
            text("  "),
            text(" " + std::to_string(item->id)) | dim | size(WIDTH, EQUAL, 4),
            text(" " + title) | flex,
            text(" " + sstr + " ") | color(sc) | bold | size(WIDTH, EQUAL, 9),
            hbox(
                ge | size(WIDTH, EQUAL, 8),
                text(" " + pstr) | size(WIDTH, EQUAL, 5)
            ),
            text(" " + speed + " ") | dim | size(WIDTH, EQUAL, 9),
            action_el,
            text("  ")
        ));
    }

    return vbox(Elements{
        vbox(std::move(rows)) | yframe | vscroll_indicator | flex,
        thin_sep(),
        hbox(text("   "), clear_queue_btn_->Render()) | center,
    });
}

Element App::render_history_tab() {
    auto e = history_.get_all();
    if (e.empty())
        return vbox(Elements{
            text("") | size(HEIGHT, EQUAL, 2),
            hbox(text("   "), dim_text("\u2205 no history")) | center | flex,
            text("") | size(HEIGHT, EQUAL, 1),
            hbox(text("   "), clear_history_btn_->Render()) | center,
        }) | flex;

    Elements rows;
    rows.push_back(hbox(
        text("  "),
        text(" Title") | bold | color(c_purple_lt) | flex,
        text(" Type") | bold | color(c_purple_lt) | size(WIDTH, EQUAL, 8),
        text(" Status") | bold | color(c_purple_lt) | size(WIDTH, EQUAL, 10),
        text(" Completed") | bold | color(c_purple_lt) | size(WIDTH, EQUAL, 18),
        text("  ")
    ) | bgcolor(c_bg_dark));

    for (auto& h : e) {
        auto t = h.title.empty() ? (h.url.empty() ? "(unknown)" : h.url.substr(0, 40)) : h.title.substr(0, 40);
        if (t.size() >= 40) t += "...";
        Color sc = (h.state == DownloadState::Completed) ? c_green
                 : (h.state == DownloadState::Cancelled)  ? c_yellow
                 : c_red;  // BUG-12 fix: cancelled → yellow, failed → red
        rows.push_back(separator() | color(c_border));
        rows.push_back(hbox(
            text("  "),
            text(" " + t) | flex,
            text(" " + h.media_type) | dim | size(WIDTH, EQUAL, 8),
            text(" " + state_to_string(h.state) + " ") | color(sc) | size(WIDTH, EQUAL, 10),
            text(" " + h.completed_time) | dim | size(WIDTH, EQUAL, 18),
            text("  ")
        ));
    }
    return vbox(Elements{
        vbox(std::move(rows)) | yframe | vscroll_indicator | flex,
        thin_sep(),
        hbox(text("   "), clear_history_btn_->Render()) | center,
    }) | flex;
}

Element App::render_log_tab() {
    // BUG-10 fix: read log_messages_ under the log_mutex_ lock.
    std::vector<LogMessage> copy;
    { std::lock_guard<std::mutex> lock(log_mutex_); copy = log_messages_; }
    if (copy.empty())
        return vbox(Elements{
            text("") | size(HEIGHT, EQUAL, 2),
            hbox(text("   "), dim_text("\u2205 no logs")) | center | flex,
            text("") | size(HEIGHT, EQUAL, 1),
            hbox(text("   "), clear_log_btn_->Render()) | center,
        }) | flex;

    Elements lines;
    // (copy already captured under lock above)

    for (auto& msg : copy) {
        auto t = std::chrono::system_clock::to_time_t(msg.time);
        std::tm tm;
#ifdef _WIN32
        localtime_s(&tm, &t);
#else
        localtime_r(&t, &tm);
#endif
        std::array<char, 32> buf;
        std::strftime(buf.data(), buf.size(), "%H:%M:%S", &tm);
        Color c; std::string p;
        switch (msg.level) {
            case LogMessage::Info:    c = c_blue;   p = "\u2139"; break;
            case LogMessage::Warning: c = c_yellow; p = "\u26a0"; break;
            case LogMessage::Error:   c = c_red;    p = "\u2717"; break;
            case LogMessage::Success: c = c_green;  p = "\u2713"; break;
        }
        lines.push_back(hbox(
            text("  "),
            text(p) | color(c) | bold | size(WIDTH, EQUAL, 2),
            dim_text(" " + std::string(buf.data()) + " "),
            text(msg.text) | color(Color::White)
        ));
    }
    return vbox(Elements{
        vbox(std::move(lines)) | yframe | vscroll_indicator | flex,
        thin_sep(),
        hbox(text("   "), clear_log_btn_->Render()) | center,
    }) | flex;
}

Element App::render_status_bar() {
    std::string s;
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        s = status_text_;
    }
    bool has_active = false;
    if (s.empty()) {
        auto items = queue_.all_items();
        int p = 0, r = 0;
        for (auto& it : items) {
            if (it->state == DownloadState::Pending) p++;
            if (it->state == DownloadState::Running) r++;
        }
        has_active = (r > 0);
        if (r > 0) {
            s = "\u25cf " + std::to_string(r) + " active";
            if (p > 0) s += ", " + std::to_string(p) + " waiting";
        } else if (p > 0) {
            s = "\u25cb " + std::to_string(p) + " queued";
        } else {
            s = "ready \u25c9";
        }
    }

    Color fg = has_active || active_downloads_ > 0 ? c_green : c_dim;

    return hbox(
        text(" "),
        text(s) | color(fg),
        text(" ") | flex,
        // BUG-19 fix: use Ctrl+C instead of macOS ⌘ symbol on all platforms.
        dim_text("Ctrl+C quit")
    ) | bgcolor(c_bg_dark);
}

void App::run() {
    setup_components();
    screen_.Loop(main_container_);
}

} // namespace yt_tui
