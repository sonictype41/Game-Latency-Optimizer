#pragma once
#include <windows.h>
#include <functional>
#include <string>
#include <filesystem>
#include <memory>

class WebviewHost final {
public:
    using MessageCallback=std::function<void(const std::wstring&)>;
    WebviewHost()=default;
    ~WebviewHost();
    WebviewHost(const WebviewHost&)=delete;
    WebviewHost& operator=(const WebviewHost&)=delete;
    bool start(HWND parent, const std::filesystem::path& ui_folder, MessageCallback callback, std::wstring& error);
    void resize() const;
    void post_json(const std::string& utf8_json) const;
    void stop();
    bool ready() const noexcept { return ready_; }
private:
    class Internal;
    std::shared_ptr<Internal> internal_;
    bool ready_{false};
};
