#pragma once
#include <windows.h>
#include <functional>
#include <string>
#include <filesystem>

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
    Internal* internal_{nullptr};
    bool ready_{false};
};
