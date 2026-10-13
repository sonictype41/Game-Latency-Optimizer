#include "glo/game_detector.hpp"

#include <windows.h>
#include <tlhelp32.h>
#include <cwchar>

namespace {
std::uint32_t valorant_process_pid() {
    HANDLE snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);
    if(snapshot==INVALID_HANDLE_VALUE)return 0;
    PROCESSENTRY32W e{};e.dwSize=sizeof(e);
    std::uint32_t pid=0;
    if(Process32FirstW(snapshot,&e)){
        do {
            if(_wcsicmp(e.szExeFile,L"VALORANT-Win64-Shipping.exe")==0 && e.th32ProcessID){
                pid=e.th32ProcessID;break;
            }
        } while(Process32NextW(snapshot,&e));
    }
    CloseHandle(snapshot);return pid;
}
}


namespace glo {

GameDetector::GameDetector(GameId game_id) : game_id_(game_id) {
    if (game_id_ == GameId::Roblox) roblox_ = std::make_unique<RobloxDetector>();
    if (game_id_ == GameId::Valorant) valorant_ = true;
}

GameDetector::~GameDetector() = default;

GameState GameDetector::poll() {
    GameState out;
    out.game_id = game_id_;
    if (valorant_) {
        out.primary_pid = valorant_process_pid();
        out.process_running = out.primary_pid != 0;
        out.detail = out.process_running ? "VALORANT PID " + std::to_string(out.primary_pid) : "VALORANT not running";
        return out;
    }
    if (!roblox_) {
        out.detail = std::string(game_name(game_id_)) + " profile is not enabled in this build";
        return out;
    }
    const auto r = roblox_->poll();
    out.process_running = r.process_running;
    out.primary_pid = r.primary_pid;
    out.detail = r.detail;
    return out;
}

}  // namespace glo
