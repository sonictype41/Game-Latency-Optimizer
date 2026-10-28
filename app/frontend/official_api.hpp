#pragma once
#include <windows.h>
#include <filesystem>
#include <functional>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <chrono>
#include <deque>
#include <string>
#include <thread>
#include <vector>

// An app-only HTTPS client: secrets never cross the WebView2 message bridge.
class OfficialApi final {
public:
    using Notify=std::function<void(std::string)>;
    explicit OfficialApi(Notify cb);
    ~OfficialApi();
    OfficialApi(const OfficialApi&)=delete;
    OfficialApi& operator=(const OfficialApi&)=delete;
    void restore();
    void redeem(std::string code);
    void prepare(std::string game,std::string region,bool advanced=false);
    void refresh_wallet(); // on demand; successful-result TTL 15s
    void invalidate_wallet_cache() noexcept;
    void sync_cached(); // recreate WebView UI without additional network polling
    void logout();
    void stop();
private:
    void async(std::function<void()> task);
    bool request(const wchar_t* method,const wchar_t* path,const std::string& body,std::string& reply,unsigned long& code,bool authenticated);
    bool authorized(const wchar_t* method,const wchar_t* path,const std::string& body,std::string& reply,unsigned long& status);
    bool renew();
    bool fetch_details(bool report_error=true);
    void publish_cached();
    void publish_wallet(const std::string& balance);
    void send(std::string payload);
    void store_refresh();
    void load_refresh();
    std::filesystem::path token_file() const;
    std::mutex jobs_lock_;
    std::condition_variable jobs_cv_;
    std::deque<std::function<void()>> tasks_;
    std::thread worker_;
    bool closing_{false};
    unsigned long last_network_error_{0}; // worker-thread only; never exposed as a token
    std::string access_,refresh_;
    std::string cached_email_,cached_balance_,cached_catalog_games_,cached_catalog_regions_,cached_art_time_factor_{"null"};
    bool identity_verified_{false}; // worker thread only
    std::chrono::steady_clock::time_point wallet_updated_{}; // last successful /app/me
    std::chrono::steady_clock::time_point wallet_failed_{}; // short failure backoff
    std::atomic_bool wallet_requested_{false},wallet_stale_{true};
    Notify notify_;
};
