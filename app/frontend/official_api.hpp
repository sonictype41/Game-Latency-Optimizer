#pragma once
#include <windows.h>
#include <filesystem>
#include <functional>
#include <mutex>
#include <condition_variable>
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
    void prepare(std::string game,std::string region);
    void refresh_wallet();
    void logout();
    void stop();
private:
    void async(std::function<void()> task);
    bool request(const wchar_t* method,const wchar_t* path,const std::string& body,std::string& reply,unsigned long& code,bool authenticated);
    bool authorized(const wchar_t* method,const wchar_t* path,const std::string& body,std::string& reply,unsigned long& status);
    bool renew();
    void fetch_details();
    void send(std::string payload);
    void store_refresh();
    void load_refresh();
    std::filesystem::path token_file() const;
    std::mutex jobs_lock_;
    std::condition_variable jobs_cv_;
    std::deque<std::function<void()>> tasks_;
    std::thread worker_;
    bool closing_{false};
    std::string access_,refresh_;
    Notify notify_;
};
