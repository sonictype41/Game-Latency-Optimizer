#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <commdlg.h>
#include <shellapi.h>
#include <winhttp.h>

#include "glo/cli_policy.hpp"
#include "glo/client_core.hpp"
#include "glo/client_log.hpp"
#include "glo/secure_transport.hpp"
#include "glo/network_worker.hpp"
#include "glo/user_paths.hpp"
#include "glo/session_config.hpp"
#include "glo/ui_resources.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <chrono>
#include <cstdint>
#include <cwchar>
#include <filesystem>
#include <fstream>
#include <future>
#include <iomanip>
#include <iterator>
#include <memory>
#include <map>
#include <random>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {
constexpr UINT WM_GLO_UPDATE = WM_APP + 1;
constexpr UINT WM_GLO_DISCONNECT_DONE = WM_APP + 2;
constexpr UINT WM_GLO_SHUTDOWN_DONE = WM_APP + 3;
constexpr UINT WM_GLO_TRAY = WM_APP + 4;
constexpr UINT kTrayIconId = 1;
constexpr UINT kTrayOpenId = 41001;
constexpr UINT kTrayDisconnectId = 41002;
constexpr UINT kTrayQuitId = 41003;
constexpr int kClientWidth = 620;
constexpr int kClientHeight = 540;
constexpr wchar_t kMainWindowClass[] = L"GLOGenericClient";
#define GLO_WIDEN_IMPL(x) L##x
#define GLO_WIDEN(x) GLO_WIDEN_IMPL(x)
constexpr wchar_t kDisplayVersion[] = L"v" GLO_WIDEN(GLO_RELEASE);
constexpr wchar_t kOfficialWebUrl[] = L"https://gloptimizer.com";
constexpr wchar_t kOfficialApiOrigin[] = L"https://api.gloptimizer.com";
constexpr wchar_t kSingleInstanceMutex[] = L"Local\\GLOClientSingleInstance";
constexpr ULONG_PTR kHandoffCopyData = 0x474C4F55;
constexpr wchar_t kHowToUseUrl[] = L"https://gloptimizer.com/auth/";
// Set these to the project's exact public links when they are published.
constexpr wchar_t kGithubUrl[] = L"https://github.com/sonictype41/Game-Latency-Optimizer";
constexpr wchar_t kFacebookUrl[] = L"https://www.facebook.com/an.nguyen.671146/";

enum class ThemeMode { Light, Dark };
enum class Language { English, Vietnamese };
enum class UiPage { Main, Settings };
enum class ActionFeedback { None, Pasted, Imported };
enum class InAppModal { None, HandoffConfirm, DisconnectConfirm, SelfHosted };

struct Palette {
    COLORREF background, surface, surface_alt, text, muted, border, accent, relay, direct, danger;
};

enum class ConfigSource { Manual, ThirdPartyHandoff, OfficialHandoff };

struct UiState {
    HWND window{};
    RECT settings_rect{}, theme_rect{}, back_rect{}, language_en_rect{}, language_vi_rect{}, debug_toggle_rect{}, debug_log_link_rect{};
    RECT paste_rect{}, import_rect{}, connect_rect{}, how_to_use_rect{};
    RECT official_rect{}, github_rect{}, facebook_rect{}, relay_eye_rect{};
    RECT modal_cancel_rect{}, modal_primary_rect{}, modal_uri_rect{}, modal_paste_rect{}, modal_import_rect{};
    HFONT font_title{}, font_hero{}, font_body{}, font_label{}, font_metric{}, font_link{};
    HICON icon_settings_light{}, icon_settings_dark{}, icon_sun_light{}, icon_sun_dark{};
    HICON icon_moon_light{}, icon_moon_dark{}, icon_back_light{}, icon_back_dark{};
    HICON icon_official_light{}, icon_official_dark{}, icon_github_light{}, icon_github_dark{};
    HICON icon_facebook_light{}, icon_facebook_dark{};
    HICON icon_paste_light{}, icon_paste_dark{}, icon_import_light{}, icon_import_dark{};
    HICON icon_connect_light{}, icon_connect_dark{}, icon_disconnect_light{}, icon_disconnect_dark{};
    HICON icon_app_big{}, icon_app_small{}, icon_tray_connected{};
    bool tray_added{false};
    bool tray_connected{false};
    ThemeMode theme{ThemeMode::Light};
    Language language{Language::English};
    UiPage page{UiPage::Main};
    bool debug_logging{false};
    bool config_consumed{false};
    bool relay_endpoint_visible{true};
    ConfigSource config_source{ConfigSource::Manual};
    ActionFeedback action_feedback{ActionFeedback::None};
    InAppModal modal{InAppModal::None};
    std::wstring pending_handoff_uri;
    std::wstring pending_handoff_game;
    std::wstring pending_handoff_source;
    std::uint64_t action_feedback_until_ms{0};
    std::uint64_t last_error_generation{0};
    std::atomic_bool disconnecting{false};
    std::atomic_bool closing{false};
    std::thread disconnect_thread;
    std::thread shutdown_thread;
    glo::NetworkWorkerClient client;
    glo::ClientLog app_log;
    glo::ClientOptions options;
    std::optional<glo::SessionConfig> config;
};
UiState* g_ui = nullptr;

bool point_in(const RECT& r, LPARAM lp) {
    POINT p{static_cast<short>(LOWORD(lp)), static_cast<short>(HIWORD(lp))};
    return PtInRect(&r, p) != FALSE;
}
bool point_in(const RECT& r, POINT p) { return PtInRect(&r,p) != FALSE; }

bool system_looks_dark() {
    const COLORREF c = GetSysColor(COLOR_WINDOW);
    return static_cast<int>(GetRValue(c)) + static_cast<int>(GetGValue(c)) + static_cast<int>(GetBValue(c)) < 384;
}

Palette palette(ThemeMode mode) {
    if (mode == ThemeMode::Dark) {
        return {RGB(8,12,18), RGB(14,20,29), RGB(19,27,39), RGB(235,244,255), RGB(128,145,166),
                RGB(38,52,69), RGB(86,123,255), RGB(88,224,165), RGB(255,179,71), RGB(255,91,113)};
    }
    return {RGB(242,246,251), RGB(255,255,255), RGB(247,250,253), RGB(26,35,48), RGB(95,112,135),
            RGB(211,221,233), RGB(54,95,230), RGB(24,157,105), RGB(213,126,24), RGB(211,62,82)};
}

HFONT make_font(int height, int weight, bool underline=false) {
    return CreateFontW(-height,0,0,0,weight,FALSE,underline?TRUE:FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,
                       CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH|FF_DONTCARE,L"Segoe UI");
}
void select_font(HDC dc, HFONT font, COLORREF color) { SelectObject(dc,font); SetBkMode(dc,TRANSPARENT); SetTextColor(dc,color); }
void draw_text(HDC dc, const std::wstring& text, RECT rect, UINT flags, HFONT font, COLORREF color) {
    select_font(dc,font,color); DrawTextW(dc,text.c_str(),static_cast<int>(text.size()),&rect,flags);
}
void draw_text(HDC dc, const wchar_t* text, RECT rect, UINT flags, HFONT font, COLORREF color) {
    draw_text(dc,std::wstring(text?text:L""),rect,flags,font,color);
}
void rounded_fill(HDC dc, const RECT& r, COLORREF fill, COLORREF border, int radius=14) {
    HBRUSH b=CreateSolidBrush(fill); HPEN pen=CreatePen(PS_SOLID,1,border);
    HGDIOBJ ob=SelectObject(dc,b), op=SelectObject(dc,pen);
    RoundRect(dc,r.left,r.top,r.right,r.bottom,radius,radius);
    SelectObject(dc,op); SelectObject(dc,ob); DeleteObject(pen); DeleteObject(b);
}
void draw_pill(HDC dc,const wchar_t* text,RECT r,COLORREF color){rounded_fill(dc,r,color,color,18);draw_text(dc,text,r,DT_CENTER|DT_VCENTER|DT_SINGLELINE,g_ui->font_label,RGB(255,255,255));}
void draw_busy_spinner(HDC dc,const RECT&r,COLORREF color){
    static const int dx[8]={0,5,7,5,0,-5,-7,-5},dy[8]={-7,-5,0,5,7,5,0,-5};
    const int phase=static_cast<int>((GetTickCount64()/80)%8),cx=r.left+20,cy=(r.top+r.bottom)/2;
    for(int i=0;i<8;i++){HBRUSH b=CreateSolidBrush(i==phase?color:RGB(GetRValue(color)/2,GetGValue(color)/2,GetBValue(color)/2));auto o=SelectObject(dc,b);int rad=i==phase?2:1;Ellipse(dc,cx+dx[i]-rad,cy+dy[i]-rad,cx+dx[i]+rad+1,cy+dy[i]+rad+1);SelectObject(dc,o);DeleteObject(b);}
}

std::wstring widen(const std::string& s) {
    if(s.empty()) return {};
    const int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),static_cast<int>(s.size()),nullptr,0);
    if(n<=0) return {};
    std::wstring out(static_cast<std::size_t>(n),L'\0');
    MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),static_cast<int>(s.size()),out.data(),n); return out;
}
std::string narrow(const std::wstring& w) {
    if(w.empty()) return {};
    const int n=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,w.data(),static_cast<int>(w.size()),nullptr,0,nullptr,nullptr);
    if(n<=0) return {};
    std::string out(static_cast<std::size_t>(n),'\0');
    WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,w.data(),static_cast<int>(w.size()),out.data(),n,nullptr,nullptr); return out;
}
std::wstring format_duration(std::uint32_t total) {
    const auto h=total/3600,m=(total%3600)/60,s=total%60; std::wostringstream out;
    out<<std::setfill(L'0')<<std::setw(2)<<h<<L':'<<std::setw(2)<<m<<L':'<<std::setw(2)<<s; return out.str();
}

void load_settings() {
    std::ifstream in(glo::settings_path(),std::ios::binary); if(!in) return;
    const std::string text((std::istreambuf_iterator<char>(in)),{});
    auto has_after=[&](const char* key,const char* value){auto k=text.find(key); return k!=std::string::npos&&text.find(value,k)!=std::string::npos;};
    if(has_after("\"language\"","\"vi\"")) g_ui->language=Language::Vietnamese;
    else if(has_after("\"language\"","\"en\"")) g_ui->language=Language::English;
    if(has_after("\"theme\"","\"dark\"")) g_ui->theme=ThemeMode::Dark;
    else if(has_after("\"theme\"","\"light\"")) g_ui->theme=ThemeMode::Light;
    auto k=text.find("\"debug_logging\""); if(k!=std::string::npos){auto c=text.find(':',k);if(c!=std::string::npos){auto v=text.find_first_not_of(" \t\r\n",c+1);if(v!=std::string::npos)g_ui->debug_logging=text.compare(v,4,"true")==0;}}
}
bool save_settings() {
    const auto path=glo::settings_path(); std::error_code ec; std::filesystem::create_directories(path.parent_path(),ec); if(ec) return false;
    const auto final_path=path.wstring(); const auto temp=final_path+L".tmp"; {std::ofstream out(std::filesystem::path(temp),std::ios::binary|std::ios::trunc); if(!out)return false;
        out<<"{\n  \"language\": \""<<(g_ui->language==Language::Vietnamese?"vi":"en")<<"\",\n"
           <<"  \"theme\": \""<<(g_ui->theme==ThemeMode::Dark?"dark":"light")<<"\",\n"
           <<"  \"debug_logging\": "<<(g_ui->debug_logging?"true":"false")<<"\n}\n"; if(!out)return false;}
    if(!MoveFileExW(temp.c_str(),final_path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)){DeleteFileW(temp.c_str());return false;} return true;
}
bool sync_frontend_debug_log(bool report_error=false){
    if(g_ui->debug_logging){
        if(!g_ui->app_log.enabled()){
            std::string error;
            if(!g_ui->app_log.enable_debug_file(error)){
                OutputDebugStringA(("GLO frontend debug log: "+error+"\r\n").c_str());
                if(report_error&&g_ui->window)MessageBoxW(g_ui->window,g_ui->language==Language::Vietnamese?L"Không thể mở tệp nhật ký gỡ lỗi trong LocalAppData.":L"Could not open the debug log in LocalAppData.",g_ui->language==Language::Vietnamese?L"GLO - Nhật ký":L"GLO - Debug log",MB_OK|MB_ICONERROR);
                return false;
            }
            g_ui->app_log.info("APP003","event=debug_logging enabled=true source=frontend");
        }
    }else if(g_ui->app_log.enabled()){
        g_ui->app_log.info("APP003","event=debug_logging enabled=false source=frontend");
        g_ui->app_log.disable();
    }
    return true;
}
void persist_settings(HWND hwnd,ThemeMode old_theme,Language old_lang,bool old_debug){
    if(save_settings()){g_ui->options.dbg_log=g_ui->debug_logging;if(old_debug!=g_ui->debug_logging){sync_frontend_debug_log(true);g_ui->client.set_debug_logging(g_ui->debug_logging);}return;}
    g_ui->theme=old_theme;g_ui->language=old_lang;g_ui->debug_logging=old_debug;
    MessageBoxW(hwnd,g_ui->language==Language::Vietnamese?L"Không thể lưu cài đặt vào LocalAppData.":L"Could not save settings to LocalAppData.",L"GLO - Settings",MB_OK|MB_ICONERROR);
}

HICON load_ui_icon(int id){return static_cast<HICON>(LoadImageW(GetModuleHandleW(nullptr),MAKEINTRESOURCEW(id),IMAGE_ICON,20,20,LR_DEFAULTCOLOR));}
void draw_asset_icon(HDC dc,const RECT&r,HICON icon){if(!icon)return;const int x=r.left+((r.right-r.left)-20)/2,y=r.top+((r.bottom-r.top)-20)/2;DrawIconEx(dc,x,y,icon,20,20,0,nullptr,DI_NORMAL);}
void draw_icon_button(HDC dc,const RECT&r,const Palette&p){rounded_fill(dc,r,p.surface,p.border,10);}
void draw_settings_icon(HDC dc,const RECT&r){draw_asset_icon(dc,r,g_ui->theme==ThemeMode::Dark?g_ui->icon_settings_dark:g_ui->icon_settings_light);}
void draw_theme_icon(HDC dc,const RECT&r){draw_asset_icon(dc,r,g_ui->theme==ThemeMode::Dark?g_ui->icon_sun_dark:g_ui->icon_moon_light);}
void draw_back_icon(HDC dc,const RECT&r){draw_asset_icon(dc,r,g_ui->theme==ThemeMode::Dark?g_ui->icon_back_dark:g_ui->icon_back_light);}
void draw_toggle(HDC dc,const RECT&r,bool enabled,const Palette&p){const COLORREF fill=enabled?p.accent:p.surface_alt;rounded_fill(dc,r,fill,enabled?p.accent:p.border,18);const int d=(r.bottom-r.top)-8,x=enabled?r.right-d-4:r.left+4;RECT knob{x,r.top+4,x+d,r.bottom-4};HBRUSH b=CreateSolidBrush(enabled?RGB(255,255,255):p.muted);auto old=SelectObject(dc,b);Ellipse(dc,knob.left,knob.top,knob.right,knob.bottom);SelectObject(dc,old);DeleteObject(b);}
COLORREF mix_color(COLORREF a,COLORREF b,int pct){pct=std::clamp(pct,0,100);auto mix=[&](BYTE x,BYTE y){return static_cast<BYTE>((static_cast<int>(x)*(100-pct)+static_cast<int>(y)*pct)/100);};return RGB(mix(GetRValue(a),GetRValue(b)),mix(GetGValue(a),GetGValue(b)),mix(GetBValue(a),GetBValue(b)));}
void draw_status_dot(HDC dc,int x,int y,COLORREF color){HBRUSH b=CreateSolidBrush(color);auto old=SelectObject(dc,b);HPEN pen=CreatePen(PS_SOLID,1,color);auto op=SelectObject(dc,pen);Ellipse(dc,x-4,y-4,x+5,y+5);SelectObject(dc,op);SelectObject(dc,old);DeleteObject(pen);DeleteObject(b);}
void draw_metric_tile(HDC dc,RECT box,const std::wstring&value,const wchar_t*label,const Palette&p,COLORREF value_color=CLR_INVALID){rounded_fill(dc,box,p.surface_alt,p.border,12);RECT bar{box.left+12,box.top+10,box.left+42,box.top+13};HBRUSH b=CreateSolidBrush(p.accent);FillRect(dc,&bar,b);DeleteObject(b);RECT lr{box.left+12,box.top+18,box.right-12,box.top+39};RECT vr{box.left+12,box.top+37,box.right-12,box.bottom-8};draw_text(dc,label,lr,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS,g_ui->font_label,p.muted);draw_text(dc,value,vr,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS,g_ui->font_metric,value_color==CLR_INVALID?p.text:value_color);}

std::wstring mask_endpoint(const std::string& host, std::uint16_t port){
    const std::wstring port_part=L":"+std::to_wstring(port);
    const bool ipv4=!host.empty()&&host.find_first_not_of("0123456789.")==std::string::npos&&host.find('.')!=std::string::npos;
    if(ipv4)return L"***.***.***.***"+port_part;
    return std::wstring(8,L'*')+port_part;
}

void draw_eye_toggle(HDC dc,const RECT& r,const Palette& p,bool visible){
    const COLORREF border=mix_color(p.border,p.background,15);
    rounded_fill(dc,r,p.surface_alt,border,9);
    HPEN pen=CreatePen(PS_SOLID,2,visible?p.text:p.muted);
    auto old_pen=SelectObject(dc,pen);
    auto old_brush=SelectObject(dc,GetStockObject(HOLLOW_BRUSH));
    const int cx=(r.left+r.right)/2, cy=(r.top+r.bottom)/2;
    Arc(dc,cx-9,cy-5,cx+9,cy+5,cx-9,cy,cx+9,cy);
    Arc(dc,cx-9,cy-5,cx+9,cy+5,cx+9,cy,cx-9,cy);
    if(visible){Ellipse(dc,cx-3,cy-3,cx+4,cy+4);}else{MoveToEx(dc,cx-8,cy+7,nullptr);LineTo(dc,cx+8,cy-7);}    
    SelectObject(dc,old_brush);
    SelectObject(dc,old_pen);
    DeleteObject(pen);
}

void draw_metric_tile_with_eye(HDC dc,RECT box,const std::wstring&value,const wchar_t*label,const Palette&p,const RECT&eye_rect,bool visible,COLORREF value_color=CLR_INVALID){
    rounded_fill(dc,box,p.surface_alt,p.border,12);
    RECT bar{box.left+12,box.top+10,box.left+42,box.top+13};HBRUSH b=CreateSolidBrush(p.accent);FillRect(dc,&bar,b);DeleteObject(b);
    RECT lr{box.left+12,box.top+18,eye_rect.left-8,box.top+39};
    RECT vr{box.left+12,box.top+37,box.right-12,box.bottom-8};
    draw_text(dc,label,lr,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS,g_ui->font_label,p.muted);
    draw_text(dc,value,vr,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS,g_ui->font_metric,value_color==CLR_INVALID?p.text:value_color);
    draw_eye_toggle(dc,eye_rect,p,visible);
}

void draw_link_button(HDC dc,const RECT&r,const Palette&p,HICON icon,bool enabled){const COLORREF fill=enabled?p.surface:p.surface_alt;const COLORREF border=enabled?p.border:mix_color(p.border,p.background,45);rounded_fill(dc,r,fill,border,10);draw_asset_icon(dc,r,icon);}
void open_external_link(HWND hwnd,const wchar_t* url,const wchar_t* label){
    const bool vi=g_ui&&g_ui->language==Language::Vietnamese;
    if(!url||!*url){
        std::wstring msg=vi?(std::wstring(L"Liên kết ")+label+L" chưa được cấu hình trong bundle OSS này."):(std::wstring(label)+L" link is not configured in this OSS bundle yet.");
        MessageBoxW(hwnd,msg.c_str(),vi?L"GL Optimizer - Liên kết":L"GL Optimizer - Links",MB_OK|MB_ICONINFORMATION);
        return;
    }
    const auto rc=reinterpret_cast<INT_PTR>(ShellExecuteW(hwnd,L"open",url,nullptr,nullptr,SW_SHOWNORMAL));
    if(rc<=32)MessageBoxW(hwnd,vi?L"Windows không thể mở liên kết này.":L"Windows could not open this link.",vi?L"GL Optimizer - Liên kết":L"GL Optimizer - Links",MB_OK|MB_ICONERROR);
}
void draw_footer(HDC dc,const RECT&client,const Palette&p){
    const int w=client.right,h=client.bottom;
    // Footer metadata sits directly on the app background. Only the three
    // clickable icons keep their small hit-target boxes.
    RECT version{28,h-44,250,h-10};
    draw_text(dc,kDisplayVersion,version,DT_LEFT|DT_VCENTER|DT_SINGLELINE,g_ui->font_label,p.muted);
    const int top=h-44;
    g_ui->official_rect=RECT{w-142,top,w-110,top+32};
    g_ui->github_rect=RECT{w-100,top,w-68,top+32};
    g_ui->facebook_rect=RECT{w-58,top,w-26,top+32};
    const bool dark=g_ui->theme==ThemeMode::Dark;
    draw_link_button(dc,g_ui->official_rect,p,dark?g_ui->icon_official_dark:g_ui->icon_official_light,*kOfficialWebUrl!=L'\0');
    draw_link_button(dc,g_ui->github_rect,p,dark?g_ui->icon_github_dark:g_ui->icon_github_light,*kGithubUrl!=L'\0');
    draw_link_button(dc,g_ui->facebook_rect,p,dark?g_ui->icon_facebook_dark:g_ui->icon_facebook_light,*kFacebookUrl!=L'\0');
}

void draw_idle_help(HDC dc,RECT rect,const Palette&p){
    const wchar_t* body=g_ui->language==Language::Vietnamese
        ?L"Mở liên kết GLO từ dịch vụ của bạn, hoặc dùng kết nối self-hosted bên thứ ba."
        :L"Open a GLO link from your service, or use a self-hosted third-party connection.";
    const wchar_t* link=g_ui->language==Language::Vietnamese?L"Sử dụng dịch vụ chính thức của GLO":L"Use the official GLO service";
    select_font(dc,g_ui->font_body,p.muted);
    SIZE bs{},ls{};
    GetTextExtentPoint32W(dc,body,static_cast<int>(wcslen(body)),&bs);
    select_font(dc,g_ui->font_link,p.accent);
    GetTextExtentPoint32W(dc,link,static_cast<int>(wcslen(link)),&ls);
    // The official-service action is intentionally a dedicated line. It must
    // remain clickable and readable when the UI is resized or localized.
    RECT body_rect{rect.left,rect.top,rect.right,rect.top+21};
    draw_text(dc,body,body_rect,DT_LEFT|DT_TOP|DT_SINGLELINE|DT_END_ELLIPSIS,g_ui->font_body,p.muted);
    const int link_y=rect.top+21;
    g_ui->how_to_use_rect=RECT{rect.left,link_y,rect.right,link_y+22};
    draw_text(dc,link,g_ui->how_to_use_rect,DT_LEFT|DT_TOP|DT_SINGLELINE|DT_END_ELLIPSIS,g_ui->font_link,p.accent);
}

std::wstring ping_value(const glo::ClientSnapshot&s){if(s.phase==glo::UiPhase::Gameplay&&s.active_route==glo::ActiveRoute::Direct)return L"N/A";if(!s.gameplay_ping_ms||!std::isfinite(*s.gameplay_ping_ms)||*s.gameplay_ping_ms<=0)return L"--";return std::to_wstring(std::lround(std::clamp(*s.gameplay_ping_ms,0.0,9999.0)))+L" ms";}
std::wstring loss_value(const glo::ClientSnapshot&s){if(s.phase==glo::UiPhase::Gameplay&&s.active_route==glo::ActiveRoute::Direct)return L"N/A";if(!s.gameplay_loss_pct||!std::isfinite(*s.gameplay_loss_pct))return L"--";std::wostringstream o;o<<std::fixed<<std::setprecision(1)<<std::clamp(*s.gameplay_loss_pct,0.0,100.0)<<L'%';return o.str();}
const wchar_t* direct_eyebrow(glo::DirectReason r,Language l){const bool vi=l==Language::Vietnamese;switch(r){case glo::DirectReason::CapacityFull:return vi?L"NHU CẦU CAO":L"HIGH DEMAND";case glo::DirectReason::Maintenance:return vi?L"BẢO TRÌ":L"MAINTENANCE";case glo::DirectReason::RelayUnavailable:return vi?L"RELAY KHÔNG KHẢ DỤNG":L"RELAY UNAVAILABLE";case glo::DirectReason::RelayDegraded:return vi?L"RELAY SUY GIẢM":L"RELAY DEGRADED";case glo::DirectReason::LocalFallback:return L"APP FALLBACK";case glo::DirectReason::DirectSelected:return vi?L"CHỈ DIRECT":L"DIRECT ONLY";case glo::DirectReason::None:return L"DIRECT";}return L"DIRECT";}

void draw_metric(HDC dc,RECT box,const std::wstring&value,const wchar_t*label,const Palette&p,COLORREF value_color=CLR_INVALID){rounded_fill(dc,box,p.surface_alt,p.border,11);RECT accent{box.left,box.top+8,box.left+3,box.bottom-8};HBRUSH ab=CreateSolidBrush(p.accent);FillRect(dc,&accent,ab);DeleteObject(ab);RECT lr{box.left+16,box.top,box.right-150,box.bottom},vr{box.right-164,box.top,box.right-16,box.bottom};draw_text(dc,label,lr,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS,g_ui->font_body,p.muted);draw_text(dc,value,vr,DT_RIGHT|DT_VCENTER|DT_SINGLELINE,g_ui->font_metric,value_color==CLR_INVALID?p.text:value_color);}
void draw_action(HDC dc,const RECT&r,const wchar_t*text,HICON icon,const Palette&p,bool primary,bool enabled=true){
    const COLORREF fill=primary&&enabled?p.accent:p.surface, border=primary&&enabled?p.accent:p.border, color=!enabled?p.muted:(primary?RGB(255,255,255):p.text);
    rounded_fill(dc,r,fill,border,11);
    select_font(dc,g_ui->font_body,color);
    SIZE ts{};GetTextExtentPoint32W(dc,text,static_cast<int>(wcslen(text)),&ts);
    const int icon_size=18,gap=icon?8:0,total=ts.cx+(icon?icon_size+gap:0),cy=(r.top+r.bottom)/2,x=(r.left+r.right-total)/2;
    int tx=x;
    if(icon){
        const int iy=cy-icon_size/2;
        // Keep SVG-derived ICOs crisp even when the button is disabled.
        // DrawStateW/DSS_DISABLED destroys alpha contrast on thin dark-mode glyphs.
        DrawIconEx(dc,x,iy,icon,icon_size,icon_size,0,nullptr,DI_NORMAL);
        tx+=icon_size+gap;
    }
    RECT tr{tx,r.top,r.right-10,r.bottom};draw_text(dc,text,tr,DT_LEFT|DT_VCENTER|DT_SINGLELINE,g_ui->font_body,color);
}


void paint_in_app_modal(HDC dc,const RECT&client,const Palette&p){
    g_ui->modal_cancel_rect={};g_ui->modal_primary_rect={};g_ui->modal_uri_rect={};g_ui->modal_paste_rect={};g_ui->modal_import_rect={};
    if(g_ui->modal==InAppModal::None)return;
    const int w=client.right,h=client.bottom,card_w=500;
    RECT card{(w-card_w)/2,92,(w+card_w)/2,h-82};rounded_fill(dc,card,p.surface,p.border,18);
    RECT title{card.left+26,card.top+22,card.right-26,card.top+56};
    if(g_ui->modal==InAppModal::HandoffConfirm){
        draw_text(dc,g_ui->language==Language::Vietnamese?L"Xác nhận kết nối":L"Confirm connection",title,DT_LEFT|DT_VCENTER|DT_SINGLELINE,g_ui->font_title,p.text);
        RECT game_l{card.left+28,card.top+78,card.left+120,card.top+104};draw_text(dc,L"Game",game_l,DT_LEFT|DT_VCENTER|DT_SINGLELINE,g_ui->font_label,p.muted);
        RECT game_v{card.left+132,card.top+78,card.right-28,card.top+104};draw_text(dc,g_ui->pending_handoff_game,game_v,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS,g_ui->font_body,p.text);
        RECT src_l{card.left+28,card.top+112,card.left+120,card.top+138};draw_text(dc,g_ui->language==Language::Vietnamese?L"Nguồn":L"Source",src_l,DT_LEFT|DT_VCENTER|DT_SINGLELINE,g_ui->font_label,p.muted);
        RECT src_v{card.left+132,card.top+112,card.right-28,card.top+138};draw_text(dc,g_ui->pending_handoff_source,src_v,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS,g_ui->font_body,p.text);
        RECT q{card.left+28,card.top+166,card.right-28,card.top+206};draw_text(dc,g_ui->language==Language::Vietnamese?L"Bạn có muốn kết nối?":L"Do you want to connect?",q,DT_LEFT|DT_VCENTER|DT_SINGLELINE,g_ui->font_body,p.text);
        g_ui->modal_cancel_rect=RECT{card.left+28,card.bottom-64,card.left+210,card.bottom-22};
        g_ui->modal_primary_rect=RECT{card.right-210,card.bottom-64,card.right-28,card.bottom-22};
        draw_action(dc,g_ui->modal_cancel_rect,g_ui->language==Language::Vietnamese?L"Hủy":L"Cancel",nullptr,p,false,true);
        draw_action(dc,g_ui->modal_primary_rect,g_ui->language==Language::Vietnamese?L"Kết nối":L"Connect",g_ui->theme==ThemeMode::Dark?g_ui->icon_connect_dark:g_ui->icon_connect_light,p,true,true);
        return;
    }
    if(g_ui->modal==InAppModal::DisconnectConfirm){
        draw_text(dc,g_ui->language==Language::Vietnamese?L"Ngắt kết nối":L"Disconnect",title,DT_LEFT|DT_VCENTER|DT_SINGLELINE,g_ui->font_title,p.text);
        RECT q{card.left+28,card.top+92,card.right-28,card.top+144};draw_text(dc,g_ui->language==Language::Vietnamese?L"Bạn có muốn ngắt kết nối GLO?":L"Do you want to disconnect GLO?",q,DT_LEFT|DT_VCENTER|DT_SINGLELINE,g_ui->font_body,p.text);
        g_ui->modal_cancel_rect=RECT{card.left+28,card.bottom-64,card.left+210,card.bottom-22};
        g_ui->modal_primary_rect=RECT{card.right-210,card.bottom-64,card.right-28,card.bottom-22};
        draw_action(dc,g_ui->modal_cancel_rect,g_ui->language==Language::Vietnamese?L"Ở lại":L"Stay",nullptr,p,false,true);
        draw_action(dc,g_ui->modal_primary_rect,g_ui->language==Language::Vietnamese?L"Ngắt kết nối":L"Disconnect",g_ui->theme==ThemeMode::Dark?g_ui->icon_disconnect_dark:g_ui->icon_disconnect_light,p,true,true);
        return;
    }
    draw_text(dc,g_ui->language==Language::Vietnamese?L"Kết nối self-hosted bên thứ ba":L"Third-party self-hosted connection",title,DT_LEFT|DT_VCENTER|DT_SINGLELINE,g_ui->font_title,p.text);
    RECT warning{card.left+28,card.top+64,card.right-28,card.top+142};
    draw_text(dc,g_ui->language==Language::Vietnamese?L"Chỉ tiếp tục nếu bạn tin tưởng provider/config này. GLO Official không phát hành JSON config.":L"Continue only if you trust this provider/config. GLO Official does not issue JSON configs.",warning,DT_LEFT|DT_WORDBREAK,g_ui->font_body,p.muted);
    const bool dark=g_ui->theme==ThemeMode::Dark;
    g_ui->modal_uri_rect=RECT{card.left+28,card.top+150,card.right-28,card.top+190};
    g_ui->modal_paste_rect=RECT{card.left+28,card.top+198,card.right-28,card.top+238};
    g_ui->modal_import_rect=RECT{card.left+28,card.top+246,card.right-28,card.top+286};
    draw_action(dc,g_ui->modal_uri_rect,g_ui->language==Language::Vietnamese?L"Dán URI từ provider":L"Paste provider URI",dark?g_ui->icon_connect_dark:g_ui->icon_connect_light,p,false,true);
    draw_action(dc,g_ui->modal_paste_rect,g_ui->language==Language::Vietnamese?L"Dán JSON config":L"Paste JSON config",dark?g_ui->icon_paste_dark:g_ui->icon_paste_light,p,false,true);
    draw_action(dc,g_ui->modal_import_rect,g_ui->language==Language::Vietnamese?L"Nhập file JSON":L"Import JSON file",dark?g_ui->icon_import_dark:g_ui->icon_import_light,p,false,true);
    g_ui->modal_cancel_rect=RECT{card.right-168,card.bottom-58,card.right-28,card.bottom-20};
    draw_action(dc,g_ui->modal_cancel_rect,g_ui->language==Language::Vietnamese?L"Đóng":L"Close",nullptr,p,false,true);
}

void set_action_feedback(ActionFeedback feedback){
    g_ui->action_feedback=feedback;
    g_ui->action_feedback_until_ms=GetTickCount64()+2000;
    InvalidateRect(g_ui->window,nullptr,FALSE);
}

bool read_text_file(const std::filesystem::path& path,std::string& out,std::string& error){std::ifstream in(path,std::ios::binary);if(!in){error="Could not open config file";return false;}in.seekg(0,std::ios::end);const auto n=in.tellg();if(n<0||n>65536){error="Config file is too large";return false;}in.seekg(0);out.assign(std::istreambuf_iterator<char>(in),{});return true;}
bool clipboard_text(std::string&out,std::string&error){if(!OpenClipboard(g_ui?g_ui->window:nullptr)){error="Clipboard is unavailable";return false;}struct G{~G(){CloseClipboard();}}g;HANDLE h=GetClipboardData(CF_UNICODETEXT);if(!h){error="Clipboard does not contain text";return false;}auto*p=static_cast<const wchar_t*>(GlobalLock(h));if(!p){error="Could not read clipboard";return false;}std::wstring t(p);GlobalUnlock(h);out=narrow(t);if(out.empty()){error="Clipboard config is empty";return false;}return true;}

void apply_config(glo::SessionConfig c,ConfigSource source=ConfigSource::Manual){g_ui->config=std::move(c);g_ui->config_source=source;g_ui->config_consumed=false;const auto&cfg=*g_ui->config;g_ui->options.relay_host=cfg.relay_host;g_ui->options.relay_port=cfg.relay_port;g_ui->options.relay_public_key=cfg.relay_public_key;g_ui->options.session_grant=cfg.grant;g_ui->options.timeout_message=cfg.timeout_message;g_ui->options.game_id=cfg.game_id;g_ui->options.profile_id=cfg.profile_id;g_ui->options.gameplay_ipv4=cfg.gameplay_ipv4;g_ui->options.profile_revision=cfg.profile_revision;g_ui->options.port_min=cfg.port_min;g_ui->options.port_max=cfg.port_max;InvalidateRect(g_ui->window,nullptr,FALSE);}
bool parse_and_apply(const std::string&text,ConfigSource source=ConfigSource::Manual){glo::SessionConfig c;std::string error;if(!glo::parse_session_config_json(text,c,error)){MessageBoxW(g_ui->window,widen(error).c_str(),L"GLO - Invalid config",MB_OK|MB_ICONERROR);return false;}apply_config(std::move(c),source);return true;}
void paste_config(){std::string text,error;if(!clipboard_text(text,error)){MessageBoxW(g_ui->window,widen(error).c_str(),L"GLO",MB_OK|MB_ICONERROR);return;}if(parse_and_apply(text))set_action_feedback(ActionFeedback::Pasted);}
void import_config(){wchar_t file[32768]{};OPENFILENAMEW ofn{};ofn.lStructSize=sizeof(ofn);ofn.hwndOwner=g_ui->window;ofn.lpstrFilter=L"GLO session config (*.json)\0*.json\0All files\0*.*\0\0";ofn.lpstrFile=file;ofn.nMaxFile=32768;ofn.Flags=OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST|OFN_NOCHANGEDIR;if(!GetOpenFileNameW(&ofn))return;std::string text,error;if(!read_text_file(file,text,error)){MessageBoxW(g_ui->window,widen(error).c_str(),L"GLO",MB_OK|MB_ICONERROR);return;}if(parse_and_apply(text))set_action_feedback(ActionFeedback::Imported);}
void clear_config_after_use(){g_ui->options.session_grant.clear();if(g_ui->config){g_ui->config->grant.clear();}g_ui->config.reset();g_ui->config_source=ConfigSource::Manual;g_ui->config_consumed=false;}

void connect_loaded(){
    if(g_ui->options.routing_policy!=glo::RoutingPolicy::DirectOnly&&(!g_ui->config||g_ui->config_consumed)){MessageBoxW(g_ui->window,g_ui->language==Language::Vietnamese?L"Hãy dán hoặc nhập một config phiên mới trước.":L"Paste or import a fresh session config first.",L"GL Optimizer",MB_OK|MB_ICONINFORMATION);return;}
    g_ui->options.dbg_log=g_ui->debug_logging;
    const bool ok=g_ui->client.connect_async(g_ui->options,[](const glo::ClientSnapshot&){if(g_ui&&g_ui->window)PostMessageW(g_ui->window,WM_GLO_UPDATE,0,0);});
    if(!ok)MessageBoxW(g_ui->window,L"Could not start the GLO network worker.",L"GLO",MB_OK|MB_ICONERROR);
    InvalidateRect(g_ui->window,nullptr,FALSE);
}

struct HandoffLink { std::wstring issuer, endpoint; std::string token; };

enum class HandoffParseError {
    None, InvalidSchemeOrAction, MalformedQuery, DuplicateParameter, InvalidEncoding,
    UnsupportedVersion, InvalidIssuer, InvalidEndpoint, MissingOrInvalidToken
};

struct HandoffParseResult {
    std::optional<HandoffLink> link;
    HandoffParseError error{HandoffParseError::None};
};

std::wstring handoff_error_reason(HandoffParseError error){
    switch(error){
        case HandoffParseError::InvalidSchemeOrAction: return L"Invalid GLO link scheme or action.";
        case HandoffParseError::MalformedQuery: return L"The GLO link query is malformed.";
        case HandoffParseError::DuplicateParameter: return L"The GLO link contains a duplicate parameter.";
        case HandoffParseError::InvalidEncoding: return L"The GLO link contains invalid URL encoding.";
        case HandoffParseError::UnsupportedVersion: return L"Unsupported GLO link version.";
        case HandoffParseError::InvalidIssuer: return L"Invalid or missing link issuer.";
        case HandoffParseError::InvalidEndpoint: return L"Invalid or missing GLO API endpoint.";
        case HandoffParseError::MissingOrInvalidToken: return L"Invalid or missing session token.";
        default: return L"Unknown GLO link error.";
    }
}

const char* handoff_error_name(HandoffParseError error){
    switch(error){
        case HandoffParseError::None:return "none";
        case HandoffParseError::InvalidSchemeOrAction:return "invalid_scheme_or_action";
        case HandoffParseError::MalformedQuery:return "malformed_query";
        case HandoffParseError::DuplicateParameter:return "duplicate_parameter";
        case HandoffParseError::InvalidEncoding:return "invalid_encoding";
        case HandoffParseError::UnsupportedVersion:return "unsupported_version";
        case HandoffParseError::InvalidIssuer:return "invalid_issuer";
        case HandoffParseError::InvalidEndpoint:return "invalid_endpoint";
        case HandoffParseError::MissingOrInvalidToken:return "missing_or_invalid_token";
    }
    return "unknown";
}

std::string handoff_uri_safe_summary(const std::wstring& uri){
    const std::wstring_view whole(uri);
    const bool canonical=whole.starts_with(L"glo://connect/?");
    const bool legacy=whole.starts_with(L"glo://connect?");
    bool token_present=false;std::size_t token_length=0;
    const auto query=whole.find(L'?');
    if(query!=std::wstring_view::npos){
        std::wstring_view rest=whole.substr(query+1);std::size_t pos=0;
        while(pos<=rest.size()){
            const auto amp=rest.find(L'&',pos);
            const auto part=rest.substr(pos,amp==std::wstring_view::npos?rest.size()-pos:amp-pos);
            if(part.starts_with(L"token=")){token_present=true;token_length=part.size()-6;break;}
            if(amp==std::wstring_view::npos)break;pos=amp+1;
        }
    }
    std::ostringstream out;out<<"uri_length="<<uri.size()<<" canonical_form="<<(canonical?"true":"false")<<" legacy_form="<<(legacy?"true":"false")<<" token_present="<<(token_present?"true":"false")<<" token_length="<<token_length;return out.str();
}

std::string json_escape(const std::string& in){std::string o;o.reserve(in.size()+8);for(unsigned char c:in){if(c=='"'||c=='\\'){o+='\\';o+=static_cast<char>(c);}else if(c=='\n')o+="\\n";else if(c=='\r')o+="\\r";else if(c=='\t')o+="\\t";else if(c>=0x20)o+=static_cast<char>(c);}return o;}

std::optional<std::string> json_string_field(const std::string& text,const std::string& key){
    const std::string needle="\""+key+"\"";auto p=text.find(needle);if(p==std::string::npos)return std::nullopt;p=text.find(':',p+needle.size());if(p==std::string::npos)return std::nullopt;++p;while(p<text.size()&&std::isspace(static_cast<unsigned char>(text[p])))++p;if(p>=text.size()||text[p++]!='"')return std::nullopt;std::string out;
    while(p<text.size()){char c=text[p++];if(c=='"')return out;if(c!='\\'){if(static_cast<unsigned char>(c)<0x20)return std::nullopt;out+=c;continue;}if(p>=text.size())return std::nullopt;char e=text[p++];switch(e){case '"':case '\\':case '/':out+=e;break;case 'b':out+='\b';break;case 'f':out+='\f';break;case 'n':out+='\n';break;case 'r':out+='\r';break;case 't':out+='\t';break;default:return std::nullopt;}}
    return std::nullopt;
}

struct RelayCandidate { std::string id,relay,relay_public_key; double rtt_ms{1e30}; };

std::vector<RelayCandidate> json_relay_candidates(const std::string& text){
    std::vector<RelayCandidate> out;const std::string key="\"candidates\"";auto p=text.find(key);if(p==std::string::npos)return out;p=text.find('[',p+key.size());if(p==std::string::npos)return out;
    bool in_string=false,escape=false;int depth=0;std::size_t begin=std::string::npos;
    for(std::size_t i=p+1;i<text.size();++i){char ch=text[i];if(in_string){if(escape)escape=false;else if(ch=='\\')escape=true;else if(ch=='"')in_string=false;continue;}if(ch=='"'){in_string=true;continue;}if(ch=='{'){if(depth++==0)begin=i;continue;}if(ch=='}'){if(depth<=0)return {};if(--depth==0&&begin!=std::string::npos){const std::string obj=text.substr(begin,i-begin+1);auto id=json_string_field(obj,"candidate_id"),relay=json_string_field(obj,"relay"),pin=json_string_field(obj,"relay_public_key");if(!id||!relay||!pin||id->empty()||id->size()>128||!glo::cli::hex_key(*pin))return {};try{glo::cli::endpoint(*relay);}catch(...){return {};}out.push_back({*id,*relay,*pin});if(out.size()>16)return {};begin=std::string::npos;}continue;}if(ch==']'&&depth==0)break;}
    return out;
}

std::optional<double> probe_relay_candidate(const RelayCandidate& candidate){
    WSADATA wsa{};if(WSAStartup(MAKEWORD(2,2),&wsa)!=0)return std::nullopt;struct WsaGuard{~WsaGuard(){WSACleanup();}} guard;
    std::string host;unsigned port=0;try{auto ep=glo::cli::endpoint(candidate.relay);host=ep.first;port=ep.second;}catch(...){return std::nullopt;}
    addrinfo hints{};hints.ai_family=AF_INET;hints.ai_socktype=SOCK_DGRAM;hints.ai_protocol=IPPROTO_UDP;addrinfo* result=nullptr;const std::string service=std::to_string(port);if(getaddrinfo(host.c_str(),service.c_str(),&hints,&result)!=0||!result)return std::nullopt;struct AddrGuard{addrinfo*p;~AddrGuard(){if(p)freeaddrinfo(p);}} ag{result};
    SOCKET sock=socket(result->ai_family,result->ai_socktype,result->ai_protocol);if(sock==INVALID_SOCKET)return std::nullopt;struct SockGuard{SOCKET s;~SockGuard(){if(s!=INVALID_SOCKET)closesocket(s);}} sg{sock};DWORD timeout=650;setsockopt(sock,SOL_SOCKET,SO_RCVTIMEO,reinterpret_cast<const char*>(&timeout),sizeof(timeout));
    glo::secure::Client client;if(!client.begin())return std::nullopt;const auto hello=client.hello();if(hello.empty())return std::nullopt;const auto start=std::chrono::steady_clock::now();if(sendto(sock,reinterpret_cast<const char*>(hello.data()),static_cast<int>(hello.size()),0,result->ai_addr,static_cast<int>(result->ai_addrlen))==SOCKET_ERROR)return std::nullopt;
    std::array<std::uint8_t,glo::secure::kMaxDatagram> buf{};sockaddr_storage from{};int fromlen=sizeof(from);const int n=recvfrom(sock,reinterpret_cast<char*>(buf.data()),static_cast<int>(buf.size()),0,reinterpret_cast<sockaddr*>(&from),&fromlen);if(n<=0||!client.retry(std::span<const std::uint8_t>(buf.data(),static_cast<std::size_t>(n))))return std::nullopt;const auto elapsed=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();return elapsed;
}

std::vector<RelayCandidate> measure_relay_candidates(std::vector<RelayCandidate> candidates){
    std::vector<std::future<std::optional<double>>> jobs;jobs.reserve(candidates.size());
    for(const auto& c:candidates){jobs.emplace_back(std::async(std::launch::async,[c]()->std::optional<double>{
        std::vector<double> samples;for(int attempt=0;attempt<3;++attempt){if(auto rtt=probe_relay_candidate(c))samples.push_back(*rtt);}
        if(samples.empty())return std::nullopt;std::sort(samples.begin(),samples.end());return samples[samples.size()/2];
    }));}
    for(std::size_t i=0;i<candidates.size();++i){auto rtt=jobs[i].get();if(rtt)candidates[i].rtt_ms=*rtt;const bool reachable=candidates[i].rtt_ms<1e29;std::string detail="event=candidate_probe candidate="+candidates[i].id+" reachable="+(reachable?std::string("true"):std::string("false"));if(reachable)detail+=" rtt_ms="+std::to_string(candidates[i].rtt_ms);g_ui->app_log.info("URI020",detail);}
    return candidates;
}

bool url_decode_component(std::wstring_view in,std::wstring& out){
    out.clear();out.reserve(in.size());
    auto hex=[](wchar_t c)->int{if(c>=L'0'&&c<=L'9')return c-L'0';if(c>=L'a'&&c<=L'f')return c-L'a'+10;if(c>=L'A'&&c<=L'F')return c-L'A'+10;return -1;};
    std::string bytes;
    auto flush=[&](){if(!bytes.empty()){out+=widen(bytes);bytes.clear();}};
    for(std::size_t i=0;i<in.size();++i){
        const wchar_t c=in[i];
        if(c==L'%'){
            if(i+2>=in.size())return false;
            const int a=hex(in[i+1]),b=hex(in[i+2]);if(a<0||b<0)return false;
            bytes.push_back(static_cast<char>((a<<4)|b));i+=2;continue;
        }
        flush();out+=c==L'+'?L' ':c;
    }
    flush();return true;
}

bool canonical_https_origin(const std::wstring& raw,std::wstring& canonical,std::wstring& host){
    std::wstring url=raw;while(url.size()>8&&url.back()==L'/')url.pop_back();URL_COMPONENTSW u{};u.dwStructSize=sizeof(u);u.dwSchemeLength=u.dwHostNameLength=u.dwUrlPathLength=u.dwExtraInfoLength=DWORD(-1);if(!WinHttpCrackUrl(url.c_str(),0,0,&u)||u.nScheme!=INTERNET_SCHEME_HTTPS||!u.lpszHostName||!u.dwHostNameLength)return false;
    std::wstring path(u.lpszUrlPath?u.lpszUrlPath:L"",u.dwUrlPathLength==DWORD(-1)?0:u.dwUrlPathLength);std::wstring extra(u.lpszExtraInfo?u.lpszExtraInfo:L"",u.dwExtraInfoLength==DWORD(-1)?0:u.dwExtraInfoLength);if((!path.empty()&&path!=L"/")||!extra.empty())return false;host.assign(u.lpszHostName,u.dwHostNameLength);std::transform(host.begin(),host.end(),host.begin(),::towlower);if(host.find(L'@')!=std::wstring::npos)return false;canonical=L"https://"+host;if(u.nPort!=INTERNET_DEFAULT_HTTPS_PORT)canonical+=L":"+std::to_wstring(u.nPort);return true;
}

HandoffParseResult parse_handoff_uri(const std::wstring& uri){
    // Chromium/Windows canonicalizes a custom URI with an authority into a slash
    // before the query (glo://connect/?...). Keep the pre-r7 form for compatibility.
    constexpr std::wstring_view canonical_prefix=L"glo://connect/?";
    constexpr std::wstring_view legacy_prefix=L"glo://connect?";
    const std::wstring_view whole(uri);
    std::wstring_view rest;
    if(whole.size()>canonical_prefix.size()&&whole.substr(0,canonical_prefix.size())==canonical_prefix)
        rest=whole.substr(canonical_prefix.size());
    else if(whole.size()>legacy_prefix.size()&&whole.substr(0,legacy_prefix.size())==legacy_prefix)
        rest=whole.substr(legacy_prefix.size());
    else
        return {{},HandoffParseError::InvalidSchemeOrAction};
    std::map<std::wstring,std::wstring> q;
    std::size_t pos=0;
    while(pos<=rest.size()){
        const auto amp=rest.find(L'&',pos);
        const auto part=rest.substr(pos,amp==std::wstring_view::npos?rest.size()-pos:amp-pos);
        const auto eq=part.find(L'=');
        if(eq==std::wstring_view::npos)return {{},HandoffParseError::MalformedQuery};
        auto k=std::wstring(part.substr(0,eq));std::wstring v;
        if(k.empty()||part.substr(eq+1).empty())return {{},HandoffParseError::MalformedQuery};
        if(q.contains(k))return {{},HandoffParseError::DuplicateParameter};
        if(!url_decode_component(part.substr(eq+1),v))return {{},HandoffParseError::InvalidEncoding};
        if(v.empty())return {{},HandoffParseError::MalformedQuery};
        q.emplace(std::move(k),std::move(v));
        if(amp==std::wstring_view::npos)break;pos=amp+1;
    }
    const auto v=q.find(L"v");if(v==q.end()||v->second!=L"1")return {{},HandoffParseError::UnsupportedVersion};
    const auto issuer_it=q.find(L"issuer");if(issuer_it==q.end())return {{},HandoffParseError::InvalidIssuer};
    const auto endpoint_it=q.find(L"endpoint");if(endpoint_it==q.end())return {{},HandoffParseError::InvalidEndpoint};
    const auto token_it=q.find(L"token");if(token_it==q.end())return {{},HandoffParseError::MissingOrInvalidToken};
    std::wstring issuer,endpoint,ih,eh;
    if(!canonical_https_origin(issuer_it->second,issuer,ih))return {{},HandoffParseError::InvalidIssuer};
    if(!canonical_https_origin(endpoint_it->second,endpoint,eh))return {{},HandoffParseError::InvalidEndpoint};
    std::string token=narrow(token_it->second);
    if(token.size()<20||token.size()>256||!std::all_of(token.begin(),token.end(),[](unsigned char c){return std::isalnum(c)||c=='_'||c=='-';}))return {{},HandoffParseError::MissingOrInvalidToken};
    return {HandoffLink{issuer,endpoint,std::move(token)},HandoffParseError::None};
}


bool http_post_json(const std::wstring& origin,const std::wstring& path,const std::string& body,DWORD& status,std::string& response,std::wstring& error){
    std::wstring url=origin+path;URL_COMPONENTSW u{};u.dwStructSize=sizeof(u);u.dwSchemeLength=u.dwHostNameLength=u.dwUrlPathLength=u.dwExtraInfoLength=DWORD(-1);if(!WinHttpCrackUrl(url.c_str(),0,0,&u)||u.nScheme!=INTERNET_SCHEME_HTTPS){error=L"Invalid HTTPS endpoint.";return false;}std::wstring host(u.lpszHostName,u.dwHostNameLength);std::wstring target(u.lpszUrlPath,u.dwUrlPathLength);if(u.lpszExtraInfo&&u.dwExtraInfoLength)target.append(u.lpszExtraInfo,u.dwExtraInfoLength);
    HINTERNET ses=WinHttpOpen(L"GLO/" GLO_WIDEN(GLO_VERSION),WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,WINHTTP_NO_PROXY_NAME,WINHTTP_NO_PROXY_BYPASS,0);if(!ses){error=L"Could not initialize HTTPS.";return false;}WinHttpSetTimeouts(ses,5000,5000,5000,7000);HINTERNET con=WinHttpConnect(ses,host.c_str(),u.nPort,0);if(!con){WinHttpCloseHandle(ses);error=L"Could not reach the provider.";return false;}HINTERNET req=WinHttpOpenRequest(con,L"POST",target.c_str(),nullptr,WINHTTP_NO_REFERER,WINHTTP_DEFAULT_ACCEPT_TYPES,WINHTTP_FLAG_SECURE);if(!req){WinHttpCloseHandle(con);WinHttpCloseHandle(ses);error=L"Could not create the HTTPS request.";return false;}
    const wchar_t* hdr=L"Content-Type: application/json\r\nAccept: application/json\r\n";BOOL ok=WinHttpSendRequest(req,hdr,DWORD(-1L),(LPVOID)body.data(),static_cast<DWORD>(body.size()),static_cast<DWORD>(body.size()),0)&&WinHttpReceiveResponse(req,nullptr);status=0;DWORD sl=sizeof(status);if(ok)WinHttpQueryHeaders(req,WINHTTP_QUERY_STATUS_CODE|WINHTTP_QUERY_FLAG_NUMBER,WINHTTP_HEADER_NAME_BY_INDEX,&status,&sl,WINHTTP_NO_HEADER_INDEX);response.clear();if(ok){for(;;){DWORD avail=0;if(!WinHttpQueryDataAvailable(req,&avail)){ok=FALSE;break;}if(!avail)break;if(response.size()+avail>131072){ok=FALSE;error=L"Provider response is too large.";break;}std::string chunk(avail,'\0');DWORD got=0;if(!WinHttpReadData(req,chunk.data(),avail,&got)){ok=FALSE;break;}chunk.resize(got);response+=chunk;}}WinHttpCloseHandle(req);WinHttpCloseHandle(con);WinHttpCloseHandle(ses);if(!ok){if(error.empty())error=L"HTTPS request failed.";return false;}return true;
}

std::string random_nonce(){std::random_device rd;static const char h[]="0123456789abcdef";std::string out;out.reserve(32);for(int i=0;i<16;++i){unsigned v=rd()&0xffu;out+=h[v>>4];out+=h[v&15];}return out;}

void self_host_menu(){
    g_ui->modal=InAppModal::SelfHosted;
    InvalidateRect(g_ui->window,nullptr,FALSE);
}


void perform_handoff_connect(const std::wstring& uri){
    const auto safe=handoff_uri_safe_summary(uri);auto parsed=parse_handoff_uri(uri);
    if(!parsed.link){g_ui->app_log.error("URI002",std::string("event=handoff_parse_failed reason=")+handoff_error_name(parsed.error)+" "+safe);return;}
    auto link=std::move(parsed.link);DWORD status=0;std::string resp;std::wstring err;const std::string inspect="{\"token\":\""+json_escape(link->token)+"\"}";
    if(!http_post_json(link->endpoint,L"/app/handoff/inspect",inspect,status,resp,err)||status/100!=2){g_ui->app_log.error("URI005","event=handoff_inspect_failed http_status="+std::to_string(status));MessageBoxW(g_ui->window,(err.empty()?(g_ui->language==Language::Vietnamese?L"Liên kết đã hết hạn hoặc provider từ chối yêu cầu.":L"The link expired or the provider rejected it."):err).c_str(),L"GLO - Handoff",MB_OK|MB_ICONERROR);return;}
    auto game=json_string_field(resp,"game"),returned_issuer=json_string_field(resp,"issuer");auto candidates=json_relay_candidates(resp);if(!game||!returned_issuer||candidates.empty()){g_ui->app_log.error("URI006","event=handoff_metadata_invalid");MessageBoxW(g_ui->window,L"Provider returned invalid handoff candidates.",L"GLO - Handoff",MB_OK|MB_ICONERROR);return;}
    std::wstring canonical_returned,returned_host;if(!canonical_https_origin(widen(*returned_issuer),canonical_returned,returned_host)||canonical_returned!=link->issuer){g_ui->app_log.error("URI007","event=handoff_identity_mismatch");MessageBoxW(g_ui->window,L"Provider identity mismatch.",L"GLO - Handoff",MB_OK|MB_ICONERROR);return;}
    std::wstring official,oh;canonical_https_origin(kOfficialWebUrl,official,oh);std::wstring official_api,oah;canonical_https_origin(kOfficialApiOrigin,official_api,oah);bool trusted=link->issuer==official&&link->endpoint==official_api;
    candidates=measure_relay_candidates(std::move(candidates));const bool any_reachable=std::any_of(candidates.begin(),candidates.end(),[](const RelayCandidate& c){return c.rtt_ms<1e29;});if(!any_reachable){g_ui->app_log.error("URI021","event=candidate_probe_all_failed");MessageBoxW(g_ui->window,g_ui->language==Language::Vietnamese?L"Không có relay nào của provider phản hồi probe.":L"None of the provider relay candidates responded to the probe.",L"GLO - Handoff",MB_OK|MB_ICONERROR);return;}
    std::string measurements="[";for(std::size_t i=0;i<candidates.size();++i){if(i)measurements+=",";measurements+="{\"candidate_id\":\""+json_escape(candidates[i].id)+"\",\"rtt_ms\":"+(candidates[i].rtt_ms<1e29?std::to_string(candidates[i].rtt_ms):std::string("null"))+"}";}measurements+="]";
    const std::string nonce=random_nonce();const std::string redeem="{\"token\":\""+json_escape(link->token)+"\",\"client_nonce\":\""+nonce+"\",\"measurements\":"+measurements+"}";resp.clear();err.clear();status=0;if(!http_post_json(link->endpoint,L"/app/handoff/redeem",redeem,status,resp,err)||status/100!=2){g_ui->app_log.warn("URI009","event=handoff_measurements_redeem_failed measured_candidates="+std::to_string(candidates.size())+" http_status="+std::to_string(status));const std::wstring redeem_error=(g_ui->language==Language::Vietnamese?L"Không thể cấp phiên từ các relay đã đo.\n":L"Could not redeem a session from the measured relays.\n")+err;MessageBoxW(g_ui->window,redeem_error.c_str(),L"GLO - Handoff",MB_OK|MB_ICONERROR);return;}g_ui->app_log.info("URI010","event=handoff_redeem_ok measured_candidates="+std::to_string(candidates.size()));std::string config_json,envelope_error;if(!glo::unwrap_session_config_api_response(resp,config_json,envelope_error)){g_ui->app_log.error("URI011","event=handoff_redeem_response_invalid reason="+envelope_error);MessageBoxW(g_ui->window,widen(envelope_error).c_str(),L"GLO - Handoff",MB_OK|MB_ICONERROR);return;}if(parse_and_apply(config_json,trusted?ConfigSource::OfficialHandoff:ConfigSource::ThirdPartyHandoff)){connect_loaded();return;}

}

void handle_handoff_uri(const std::wstring& uri){
    const auto safe=handoff_uri_safe_summary(uri);g_ui->app_log.info("URI001","event=handoff_received "+safe);const auto snap=g_ui->client.snapshot();
    if(snap.phase!=glo::UiPhase::Disconnected||snap.state==glo::ConnectionState::Connecting||g_ui->disconnecting.load()){g_ui->app_log.warn("URI003","event=handoff_rejected reason=active_connection "+safe);MessageBoxW(g_ui->window,g_ui->language==Language::Vietnamese?L"GLO đang có phiên kết nối. Hãy ngắt kết nối trước khi mở liên kết khác.":L"GLO already has an active connection. Disconnect before opening another link.",L"GLO",MB_OK|MB_ICONINFORMATION);return;}
    auto parsed=parse_handoff_uri(uri);if(!parsed.link){g_ui->app_log.error("URI002",std::string("event=handoff_parse_failed reason=")+handoff_error_name(parsed.error)+" "+safe);const std::wstring reason=handoff_error_reason(parsed.error);const std::wstring message=(g_ui->language==Language::Vietnamese?L"Không thể mở liên kết GLO này.\nLý do: ":L"Unable to open this GLO link.\nReason: ")+reason;MessageBoxW(g_ui->window,message.c_str(),L"GLO",MB_OK|MB_ICONERROR);return;}
    auto link=std::move(parsed.link);g_ui->app_log.info("URI004","event=handoff_parse_ok token_present=true token_length="+std::to_string(link->token.size()));DWORD status=0;std::string resp;std::wstring err;const std::string inspect="{\"token\":\""+json_escape(link->token)+"\"}";
    if(!http_post_json(link->endpoint,L"/app/handoff/inspect",inspect,status,resp,err)||status/100!=2){g_ui->app_log.error("URI005","event=handoff_inspect_failed http_status="+std::to_string(status));MessageBoxW(g_ui->window,(err.empty()?(g_ui->language==Language::Vietnamese?L"Liên kết đã hết hạn hoặc provider từ chối yêu cầu.":L"The link expired or the provider rejected it."):err).c_str(),L"GLO - Handoff",MB_OK|MB_ICONERROR);return;}
    auto game=json_string_field(resp,"game"),returned_issuer=json_string_field(resp,"issuer");auto candidates=json_relay_candidates(resp);if(!game||!returned_issuer||candidates.empty()){g_ui->app_log.error("URI006","event=handoff_metadata_invalid");MessageBoxW(g_ui->window,L"Provider returned invalid handoff candidates.",L"GLO - Handoff",MB_OK|MB_ICONERROR);return;}
    std::wstring canonical_returned,returned_host;if(!canonical_https_origin(widen(*returned_issuer),canonical_returned,returned_host)||canonical_returned!=link->issuer){g_ui->app_log.error("URI007","event=handoff_identity_mismatch");MessageBoxW(g_ui->window,L"Provider identity mismatch.",L"GLO - Handoff",MB_OK|MB_ICONERROR);return;}
    std::wstring source_host,canon;canonical_https_origin(link->issuer,canon,source_host);
    g_ui->pending_handoff_uri=uri;g_ui->pending_handoff_game=widen(*game);g_ui->pending_handoff_source=source_host;g_ui->modal=InAppModal::HandoffConfirm;
    InvalidateRect(g_ui->window,nullptr,FALSE);
}


void begin_disconnect(HWND hwnd){if(g_ui->disconnecting.exchange(true))return;if(g_ui->disconnect_thread.joinable())g_ui->disconnect_thread.join();g_ui->disconnect_thread=std::thread([hwnd]{g_ui->client.disconnect();PostMessageW(hwnd,WM_GLO_DISCONNECT_DONE,0,0);});InvalidateRect(hwnd,nullptr,FALSE);}

std::pair<std::wstring,std::wstring> localized_error(const glo::ClientError&e){if(g_ui->language==Language::English)return{widen(e.title),widen(e.message)};const wchar_t*body=L"GLO không thể hoàn tất kết nối.";switch(e.code){case glo::ClientErrorCode::WorkerStartFailed:body=L"GLO không thể khởi động tác vụ kết nối nền.";break;case glo::ClientErrorCode::WinsockInitFailed:body=L"GLO không thể khởi tạo hệ thống mạng của Windows.";break;case glo::ClientErrorCode::RelayResolveFailed:body=L"GLO không thể phân giải địa chỉ relay.";break;case glo::ClientErrorCode::RelaySocketFailed:case glo::ClientErrorCode::RelaySocketConfigFailed:case glo::ClientErrorCode::RelayConnectFailed:body=L"GLO không thể chuẩn bị kết nối tới relay.";break;case glo::ClientErrorCode::RelayHandshakeSendFailed:body=L"GLO không thể gửi yêu cầu kết nối tới relay.";break;case glo::ClientErrorCode::RelayHandshakeTimeout:body=L"Không xác thực được relay trong thời gian cho phép.";break;case glo::ClientErrorCode::RelayKeyMissing:body=L"Session config thiếu relay public key.";break;case glo::ClientErrorCode::RelayKeyInvalid:body=L"Relay public key không hợp lệ.";break;case glo::ClientErrorCode::SessionAdmissionFailed:body=L"Session grant đã hết hạn, đã dùng hoặc bị relay từ chối.";break;case glo::ClientErrorCode::SessionExpired:body=L"Phiên GLO đã hết hạn. Hãy bắt đầu kết nối mới từ provider.";break;case glo::ClientErrorCode::WintunInitFailed:body=L"GLO không thể khởi tạo Wintun.";break;case glo::ClientErrorCode::ProfileRouteInitFailed:body=L"GLO không thể thiết lập định tuyến theo game profile.";break;}return{L"GLO - Lỗi kết nối",body};}


void open_debug_log_file(HWND hwnd){
    const auto path=glo::debug_log_path();std::error_code ec;
    if(!std::filesystem::exists(path,ec)||ec||!std::filesystem::is_regular_file(path,ec)){
        MessageBoxW(hwnd,g_ui->language==Language::Vietnamese?L"Chưa có nhật ký gỡ lỗi nào được ghi.":L"No debug log has been written yet.",g_ui->language==Language::Vietnamese?L"GLO - Nhật ký":L"GLO - Log file",MB_OK|MB_ICONINFORMATION);return;
    }
    const auto rc=reinterpret_cast<INT_PTR>(ShellExecuteW(hwnd,L"open",path.wstring().c_str(),nullptr,nullptr,SW_SHOWNORMAL));
    if(rc<=32)MessageBoxW(hwnd,g_ui->language==Language::Vietnamese?L"Windows không thể mở tệp nhật ký.":L"Windows could not open the log file.",g_ui->language==Language::Vietnamese?L"GLO - Nhật ký":L"GLO - Log file",MB_OK|MB_ICONERROR);
}

void paint_settings(HDC dc,const RECT&client,const Palette&p){
    const int w=client.right;
    g_ui->debug_log_link_rect=RECT{};
    g_ui->back_rect=RECT{28,20,62,54};draw_icon_button(dc,g_ui->back_rect,p);draw_back_icon(dc,g_ui->back_rect);
    RECT title{76,18,w-150,56};draw_text(dc,g_ui->language==Language::Vietnamese?L"Cài đặt":L"Settings",title,DT_LEFT|DT_VCENTER|DT_SINGLELINE,g_ui->font_title,p.text);
    g_ui->theme_rect=RECT{w-62,20,w-28,54};draw_icon_button(dc,g_ui->theme_rect,p);draw_theme_icon(dc,g_ui->theme_rect);

    RECT appearance_card{28,78,w-28,160};rounded_fill(dc,appearance_card,p.surface,p.border,16);
    RECT appearance{48,91,w-48,115};draw_text(dc,g_ui->language==Language::Vietnamese?L"GIAO DIỆN":L"APPEARANCE",appearance,DT_LEFT|DT_VCENTER|DT_SINGLELINE,g_ui->font_label,p.accent);
    RECT theme_label{48,120,180,148};draw_text(dc,g_ui->language==Language::Vietnamese?L"Chủ đề":L"Theme",theme_label,DT_LEFT|DT_VCENTER|DT_SINGLELINE,g_ui->font_body,p.text);
    RECT theme_value{190,120,w-48,148};draw_text(dc,g_ui->theme==ThemeMode::Dark?(g_ui->language==Language::Vietnamese?L"Tối":L"Dark"):(g_ui->language==Language::Vietnamese?L"Sáng":L"Light"),theme_value,DT_RIGHT|DT_VCENTER|DT_SINGLELINE,g_ui->font_body,p.muted);

    RECT language_card{28,172,w-28,272};rounded_fill(dc,language_card,p.surface,p.border,16);
    RECT lang_title{48,185,w-48,209};draw_text(dc,g_ui->language==Language::Vietnamese?L"NGÔN NGỮ":L"LANGUAGE",lang_title,DT_LEFT|DT_VCENTER|DT_SINGLELINE,g_ui->font_label,p.accent);
    g_ui->language_en_rect=RECT{48,220,208,258};g_ui->language_vi_rect=RECT{218,220,378,258};
    rounded_fill(dc,g_ui->language_en_rect,g_ui->language==Language::English?p.accent:p.surface_alt,g_ui->language==Language::English?p.accent:p.border,10);
    rounded_fill(dc,g_ui->language_vi_rect,g_ui->language==Language::Vietnamese?p.accent:p.surface_alt,g_ui->language==Language::Vietnamese?p.accent:p.border,10);
    draw_text(dc,L"English",g_ui->language_en_rect,DT_CENTER|DT_VCENTER|DT_SINGLELINE,g_ui->font_label,g_ui->language==Language::English?RGB(255,255,255):p.text);
    draw_text(dc,L"Tiếng Việt",g_ui->language_vi_rect,DT_CENTER|DT_VCENTER|DT_SINGLELINE,g_ui->font_label,g_ui->language==Language::Vietnamese?RGB(255,255,255):p.text);

    RECT diagnostics_card{28,284,w-28,438};rounded_fill(dc,diagnostics_card,p.surface,p.border,16);
    RECT dbg_title{48,297,w-48,321};draw_text(dc,g_ui->language==Language::Vietnamese?L"CHẨN ĐOÁN":L"DIAGNOSTICS",dbg_title,DT_LEFT|DT_VCENTER|DT_SINGLELINE,g_ui->font_label,p.accent);
    RECT dbg{48,328,w-132,356};draw_text(dc,g_ui->language==Language::Vietnamese?L"Nhật ký gỡ lỗi chi tiết":L"Detailed debug logging",dbg,DT_LEFT|DT_VCENTER|DT_SINGLELINE,g_ui->font_body,p.text);
    g_ui->debug_toggle_rect=RECT{w-112,326,w-50,358};draw_toggle(dc,g_ui->debug_toggle_rect,g_ui->debug_logging,p);
    RECT hint{48,364,w-48,394};draw_text(dc,g_ui->language==Language::Vietnamese?L"Log trong LocalAppData\\GLO\\logs, tối đa 3 tệp x 5 MiB.":L"Logs in LocalAppData\\GLO\\logs, up to 3 files x 5 MiB.",hint,DT_LEFT|DT_TOP|DT_WORDBREAK,g_ui->font_label,p.muted);
    if(g_ui->debug_logging){g_ui->debug_log_link_rect=RECT{48,398,210,423};draw_text(dc,g_ui->language==Language::Vietnamese?L"Mở tệp nhật ký":L"Open log file",g_ui->debug_log_link_rect,DT_LEFT|DT_VCENTER|DT_SINGLELINE,g_ui->font_link,p.accent);}

    draw_footer(dc,client,p);
}

void paint_ui(HWND hwnd,HDC target){
    RECT client{};GetClientRect(hwnd,&client);const int w=client.right,h=client.bottom;
    HDC dc=CreateCompatibleDC(target);HBITMAP bitmap=CreateCompatibleBitmap(target,std::max(1,w),std::max(1,h));auto old_bitmap=SelectObject(dc,bitmap);
    const Palette p=palette(g_ui->theme);HBRUSH bg=CreateSolidBrush(p.background);FillRect(dc,&client,bg);DeleteObject(bg);g_ui->how_to_use_rect=RECT{};g_ui->relay_eye_rect=RECT{};

    if(g_ui->page==UiPage::Settings){paint_settings(dc,client,p);BitBlt(target,0,0,w,h,dc,0,0,SRCCOPY);SelectObject(dc,old_bitmap);DeleteObject(bitmap);DeleteDC(dc);return;}

    RECT title{28,18,w-170,58};draw_text(dc,L"Gaming. Low-latency. Open.",title,DT_LEFT|DT_VCENTER|DT_SINGLELINE,g_ui->font_title,p.text);
    g_ui->settings_rect=RECT{w-104,20,w-70,54};draw_icon_button(dc,g_ui->settings_rect,p);draw_settings_icon(dc,g_ui->settings_rect);
    g_ui->theme_rect=RECT{w-62,20,w-28,54};draw_icon_button(dc,g_ui->theme_rect,p);draw_theme_icon(dc,g_ui->theme_rect);

    const auto s=g_ui->client.snapshot();const bool connecting=s.state==glo::ConnectionState::Connecting;const bool disconnecting=g_ui->disconnecting.load();const bool connected=s.phase!=glo::UiPhase::Disconnected||connecting||disconnecting;const bool ready=!connected&&g_ui->config&&!g_ui->config_consumed;const bool gameplay=s.phase==glo::UiPhase::Gameplay&&!connecting&&!disconnecting;const bool show_session=s.phase!=glo::UiPhase::Disconnected&&!connecting&&!disconnecting;
    const std::wstring selected_game=widen(s.game_name.empty()?std::string(glo::game_name(s.game_id)):s.game_name);

    const int card_bottom=gameplay?410:(show_session||ready?326:294);
    RECT card{28,78,w-28,card_bottom};rounded_fill(dc,card,p.surface,p.border,18);
    RECT eyebrow{64,98,w-50,122},hero{50,126,w-50,165},detail{50,169,w-50,199};

    COLORREF status_color=p.muted;
    if(connecting||ready)status_color=p.accent;else if(gameplay)status_color=s.active_route==glo::ActiveRoute::Relay?p.relay:p.direct;else if(connected)status_color=p.relay;
    draw_status_dot(dc,52,110,status_color);

    if(connecting){
        draw_text(dc,g_ui->language==Language::Vietnamese?L"ĐANG KẾT NỐI":L"CONNECTING",eyebrow,DT_LEFT|DT_VCENTER|DT_SINGLELINE,g_ui->font_label,p.accent);
        draw_text(dc,g_ui->language==Language::Vietnamese?L"Đang dựng route...":L"Building route...",hero,DT_LEFT|DT_VCENTER|DT_SINGLELINE,g_ui->font_hero,p.text);
        draw_text(dc,widen(s.message),detail,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS,g_ui->font_body,p.muted);
    }else if(ready){
        draw_text(dc,g_ui->language==Language::Vietnamese?L"CẤU HÌNH PHIÊN SẴN SÀNG":L"SESSION CONFIG READY",eyebrow,DT_LEFT|DT_VCENTER|DT_SINGLELINE,g_ui->font_label,p.accent);
        draw_text(dc,g_ui->language==Language::Vietnamese?L"Sẵn sàng tối ưu":L"Ready to optimize",hero,DT_LEFT|DT_VCENTER|DT_SINGLELINE,g_ui->font_hero,p.text);
        draw_text(dc,g_ui->language==Language::Vietnamese?L"Kiểm tra game và relay, sau đó kết nối.":L"Review the game and relay, then connect.",detail,DT_LEFT|DT_VCENTER|DT_SINGLELINE,g_ui->font_body,p.muted);
        const auto&c=*g_ui->config;const int gap=10,tile_top=218,tile_h=86,tile_w=(w-110-gap)/2;RECT a{50,tile_top,50+tile_w,tile_top+tile_h},b{60+tile_w,tile_top,w-50,tile_top+tile_h};
        draw_metric_tile(dc,a,widen(std::string(glo::game_name(c.game_id))),g_ui->language==Language::Vietnamese?L"GAME ĐÃ CHỌN":L"SELECTED GAME",p);
        if(g_ui->config_source==ConfigSource::OfficialHandoff){g_ui->relay_eye_rect={};draw_metric_tile(dc,b,widen(c.relay_name.empty()?std::string("Relay"):c.relay_name),g_ui->language==Language::Vietnamese?L"NODE":L"NODE",p,p.relay);}else{RECT eye{b.right-38,b.top+12,b.right-12,b.top+38};g_ui->relay_eye_rect=eye;draw_metric_tile_with_eye(dc,b,g_ui->relay_endpoint_visible?widen(c.relay_host+":"+std::to_string(c.relay_port)):mask_endpoint(c.relay_host,c.relay_port),L"RELAY",p,eye,g_ui->relay_endpoint_visible);}
    }else if(!connected){
        draw_text(dc,g_ui->language==Language::Vietnamese?L"SẴN SÀNG":L"READY",eyebrow,DT_LEFT|DT_VCENTER|DT_SINGLELINE,g_ui->font_label,p.muted);
        draw_text(dc,g_ui->language==Language::Vietnamese?L"Chưa có route":L"No route loaded",hero,DT_LEFT|DT_VCENTER|DT_SINGLELINE,g_ui->font_hero,p.text);
        if(g_ui->options.routing_policy==glo::RoutingPolicy::DirectOnly)draw_text(dc,g_ui->language==Language::Vietnamese?L"Direct-only debug mode đang bật.":L"Direct-only debug mode is enabled.",detail,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS,g_ui->font_body,p.muted);else draw_idle_help(dc,detail,p);
    }else{
        switch(s.phase){
            case glo::UiPhase::Disconnected:break;
            case glo::UiPhase::ConnectedWaitingForGame:{
                draw_text(dc,g_ui->language==Language::Vietnamese?L"ĐÃ KẾT NỐI":L"CONNECTED",eyebrow,DT_LEFT|DT_VCENTER|DT_SINGLELINE,g_ui->font_label,p.relay);
                const std::wstring t=selected_game+(g_ui->language==Language::Vietnamese?L" chưa chạy":L" is not running");draw_text(dc,t,hero,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS,g_ui->font_hero,p.text);
                draw_text(dc,g_ui->language==Language::Vietnamese?L"GLO đang chờ game đã chọn.":L"GLO is waiting for the selected game.",detail,DT_LEFT|DT_VCENTER|DT_SINGLELINE,g_ui->font_body,p.muted);break;}
            case glo::UiPhase::ConnectedStandby:{
                draw_text(dc,g_ui->language==Language::Vietnamese?L"ĐÃ PHÁT HIỆN GAME":L"GAME DETECTED",eyebrow,DT_LEFT|DT_VCENTER|DT_SINGLELINE,g_ui->font_label,p.relay);
                const std::wstring t=selected_game+(g_ui->language==Language::Vietnamese?L" đang chạy":L" is running");draw_text(dc,t,hero,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS,g_ui->font_hero,p.text);
                draw_text(dc,g_ui->language==Language::Vietnamese?L"Đang chờ gameplay, hãy vào trận để bắt đầu tối ưu.":L"Waiting for gameplay. Enter a match to start optimization.",detail,DT_LEFT|DT_VCENTER|DT_SINGLELINE,g_ui->font_body,p.muted);break;}
            case glo::UiPhase::Gameplay:{
                const bool relayed=s.active_route==glo::ActiveRoute::Relay;
                draw_text(dc,relayed?(g_ui->language==Language::Vietnamese?L"ĐÃ KẾT NỐI":L"CONNECTED"):direct_eyebrow(s.direct_reason,g_ui->language),eyebrow,DT_LEFT|DT_VCENTER|DT_SINGLELINE,g_ui->font_label,relayed?p.relay:p.direct);
                const std::wstring t=(g_ui->language==Language::Vietnamese?L"Đang chơi ":L"Playing ")+selected_game;draw_text(dc,t,hero,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS,g_ui->font_hero,p.text);
                draw_text(dc,L"",detail,DT_LEFT|DT_VCENTER|DT_SINGLELINE,g_ui->font_body,p.muted);
                RECT pill{w-146,99,w-50,129};draw_pill(dc,relayed?L"RELAY":L"DIRECT",pill,relayed?p.relay:p.direct);break;}
        }
    }

    if(show_session&&g_ui->config){
        const bool official_service=g_ui->config_source==ConfigSource::OfficialHandoff;const auto endpoint=g_ui->relay_endpoint_visible?widen(g_ui->config->relay_host+":"+std::to_string(g_ui->config->relay_port)):mask_endpoint(g_ui->config->relay_host,g_ui->config->relay_port);const auto left=s.session_remaining_seconds?format_duration(s.session_remaining_seconds):L"00:00:00";const int gap=10,tile_w=(w-110-gap)/2;
        if(gameplay){
            const int top=218,tile_h=86;RECT a{50,top,50+tile_w,top+tile_h},b{60+tile_w,top,w-50,top+tile_h},c{50,top+tile_h+10,50+tile_w,top+2*tile_h+10},d{60+tile_w,top+tile_h+10,w-50,top+2*tile_h+10};
            draw_metric_tile(dc,a,ping_value(s),g_ui->language==Language::Vietnamese?L"ĐỘ TRỄ RELAY":L"RELAY LATENCY",p,s.active_route==glo::ActiveRoute::Relay?p.relay:p.text);
            draw_metric_tile(dc,b,loss_value(s),g_ui->language==Language::Vietnamese?L"MẤT GÓI":L"PACKET LOSS",p);
            draw_metric_tile(dc,c,left,g_ui->language==Language::Vietnamese?L"THỜI GIAN CÒN LẠI":L"SESSION LEFT",p,s.session_remaining_seconds>0&&s.session_remaining_seconds<=600?p.danger:p.text);
            if(official_service){g_ui->relay_eye_rect={};draw_metric_tile(dc,d,widen(g_ui->config->relay_name.empty()?std::string("Relay"):g_ui->config->relay_name),L"NODE",p,p.relay);}else{RECT eye{d.right-38,d.top+12,d.right-12,d.top+38};g_ui->relay_eye_rect=eye;draw_metric_tile_with_eye(dc,d,endpoint,g_ui->language==Language::Vietnamese?L"ĐIỂM RELAY":L"RELAY ENDPOINT",p,eye,g_ui->relay_endpoint_visible);}
        }else{
            const int top=218,tile_h=86;RECT a{50,top,50+tile_w,top+tile_h},b{60+tile_w,top,w-50,top+tile_h};
            if(official_service){g_ui->relay_eye_rect={};draw_metric_tile(dc,a,widen(g_ui->config->relay_name.empty()?std::string("Relay"):g_ui->config->relay_name),L"NODE",p,p.relay);}else{RECT eye{a.right-38,a.top+12,a.right-12,a.top+38};g_ui->relay_eye_rect=eye;draw_metric_tile_with_eye(dc,a,endpoint,g_ui->language==Language::Vietnamese?L"ĐIỂM RELAY":L"RELAY ENDPOINT",p,eye,g_ui->relay_endpoint_visible);}draw_metric_tile(dc,b,left,g_ui->language==Language::Vietnamese?L"THỜI GIAN CÒN LẠI":L"SESSION LEFT",p,s.session_remaining_seconds>0&&s.session_remaining_seconds<=600?p.danger:p.text);
        }
    }

    const bool can_load=!connected&&!connecting&&!disconnecting;
    int action_top=card_bottom+14;
    if(can_load&&!ready){
        const int load_top=card_bottom+42;
        RECT load_label{28,card_bottom+14,w-28,card_bottom+36};
        draw_text(dc,g_ui->language==Language::Vietnamese?L"TÙY CHỌN NÂNG CAO":L"ADVANCED",load_label,DT_LEFT|DT_VCENTER|DT_SINGLELINE,g_ui->font_label,p.muted);
        g_ui->paste_rect=RECT{52,load_top,w-52,load_top+42};g_ui->import_rect=RECT{};
        draw_action(dc,g_ui->paste_rect,g_ui->language==Language::Vietnamese?L"Kết nối đến máy chủ bên thứ ba được self-host":L"Connect to a self-hosted third-party server",nullptr,p,false,true);
        action_top=load_top+54;
    }else{g_ui->paste_rect=RECT{};g_ui->import_rect=RECT{};}

    const bool active=connected||connecting||disconnecting;const bool can_connect=ready||g_ui->options.routing_policy==glo::RoutingPolicy::DirectOnly;
    if(active||can_connect){g_ui->connect_rect=RECT{78,action_top,w-78,action_top+44};const wchar_t*bt=disconnecting?(g_ui->language==Language::Vietnamese?L"Đang ngắt kết nối...":L"Disconnecting..."):connecting?(g_ui->language==Language::Vietnamese?L"Đang kết nối...":L"Connecting..."):active?(g_ui->language==Language::Vietnamese?L"Ngắt kết nối":L"Disconnect"):(g_ui->language==Language::Vietnamese?L"Kết nối":L"Connect");const bool dark_actions=g_ui->theme==ThemeMode::Dark;HICON action_icon=nullptr;if(!connecting&&!disconnecting)action_icon=active?(dark_actions?g_ui->icon_disconnect_dark:g_ui->icon_disconnect_light):(dark_actions?g_ui->icon_connect_dark:g_ui->icon_connect_light);draw_action(dc,g_ui->connect_rect,bt,action_icon,p,!active,active||can_connect);if(connecting||disconnecting)draw_busy_spinner(dc,g_ui->connect_rect,p.muted);}else g_ui->connect_rect=RECT{};

    draw_footer(dc,client,p);
    paint_in_app_modal(dc,client,p);
    BitBlt(target,0,0,w,h,dc,0,0,SRCCOPY);SelectObject(dc,old_bitmap);DeleteObject(bitmap);DeleteDC(dc);
}

struct CommandArgs{std::optional<std::filesystem::path> config;std::optional<std::wstring> uri;};
CommandArgs parse_args(){int argc=0;LPWSTR*argv=CommandLineToArgvW(GetCommandLineW(),&argc);if(!argv)throw std::runtime_error("Cannot read command line");std::vector<std::string>args;for(int i=1;i<argc;++i)args.push_back(narrow(argv[i]));LocalFree(argv);const auto values=glo::cli::parse(args);CommandArgs out;for(const auto&[name,value]:values){if(name=="--config")out.config=std::filesystem::path(widen(value));else if(name=="--uri")out.uri=widen(value);else if(name=="--force-handover-grace-ms")g_ui->options.force_handover_grace_ms=glo::cli::number(value,500,3000);else if(name=="--force-direct")g_ui->options.routing_policy=glo::RoutingPolicy::DirectOnly;else if(name=="--debug")g_ui->debug_logging=true;else if(name=="--dark")g_ui->theme=ThemeMode::Dark;else if(name=="--light")g_ui->theme=ThemeMode::Light;}return out;}



bool connection_active() {
    if(!g_ui) return false;
    const auto snap=g_ui->client.snapshot();
    return snap.phase!=glo::UiPhase::Disconnected||snap.state==glo::ConnectionState::Connecting||g_ui->disconnecting.load();
}

bool connection_established() {
    if(!g_ui) return false;
    const auto snap=g_ui->client.snapshot();
    return snap.phase!=glo::UiPhase::Disconnected&&snap.state!=glo::ConnectionState::Connecting&&!g_ui->disconnecting.load();
}

HICON make_connected_tray_icon(HICON base) {
    if(!base) return nullptr;
    ICONINFO source{};
    if(!GetIconInfo(base,&source) || !source.hbmColor || !source.hbmMask) {
        if(source.hbmColor)DeleteObject(source.hbmColor);
        if(source.hbmMask)DeleteObject(source.hbmMask);
        return nullptr;
    }
    BITMAP bm{};
    if(!GetObjectW(source.hbmColor,sizeof(bm),&bm) || bm.bmWidth<=0 || bm.bmHeight<=0) {
        DeleteObject(source.hbmColor);DeleteObject(source.hbmMask);return nullptr;
    }
    HDC screen=GetDC(nullptr),src=CreateCompatibleDC(screen),dst=CreateCompatibleDC(screen),msrc=CreateCompatibleDC(screen),mdst=CreateCompatibleDC(screen);
    HBITMAP color=CreateCompatibleBitmap(screen,bm.bmWidth,bm.bmHeight);
    HBITMAP mask=CreateBitmap(bm.bmWidth,bm.bmHeight,1,1,nullptr);
    HICON result=nullptr;
    if(src&&dst&&msrc&&mdst&&color&&mask){
        auto os=SelectObject(src,source.hbmColor),od=SelectObject(dst,color),oms=SelectObject(msrc,source.hbmMask),omd=SelectObject(mdst,mask);
        BitBlt(dst,0,0,bm.bmWidth,bm.bmHeight,src,0,0,SRCCOPY);
        BitBlt(mdst,0,0,bm.bmWidth,bm.bmHeight,msrc,0,0,SRCCOPY);
        const LONG radius=std::max<LONG>(2,std::min(bm.bmWidth,bm.bmHeight)/6);
        const LONG cx=bm.bmWidth-radius-1,cy=bm.bmHeight-radius-1;
        HBRUSH green=CreateSolidBrush(RGB(32,201,116));HPEN edge=CreatePen(PS_SOLID,1,RGB(8,80,48));
        auto ob=SelectObject(dst,green),op=SelectObject(dst,edge);
        Ellipse(dst,cx-radius,cy-radius,cx+radius+1,cy+radius+1);
        SelectObject(dst,op);SelectObject(dst,ob);DeleteObject(edge);DeleteObject(green);
        HBRUSH black=static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));HPEN black_pen=static_cast<HPEN>(GetStockObject(BLACK_PEN));
        ob=SelectObject(mdst,black);op=SelectObject(mdst,black_pen);
        Ellipse(mdst,cx-radius,cy-radius,cx+radius+1,cy+radius+1);
        SelectObject(mdst,op);SelectObject(mdst,ob);
        SelectObject(src,os);SelectObject(dst,od);SelectObject(msrc,oms);SelectObject(mdst,omd);
        ICONINFO made{};made.fIcon=TRUE;made.hbmColor=color;made.hbmMask=mask;made.xHotspot=0;made.yHotspot=0;
        result=CreateIconIndirect(&made);
    }
    if(color)DeleteObject(color);if(mask)DeleteObject(mask);
    if(src)DeleteDC(src);if(dst)DeleteDC(dst);if(msrc)DeleteDC(msrc);if(mdst)DeleteDC(mdst);if(screen)ReleaseDC(nullptr,screen);
    DeleteObject(source.hbmColor);DeleteObject(source.hbmMask);
    return result;
}

void update_tray_icon(bool force=false) {
    if(!g_ui||!g_ui->window)return;
    const bool active=connection_established();
    if(g_ui->tray_added&&!force&&g_ui->tray_connected==active)return;
    NOTIFYICONDATAW nid{};nid.cbSize=sizeof(nid);nid.hWnd=g_ui->window;nid.uID=kTrayIconId;
    nid.uFlags=NIF_MESSAGE|NIF_ICON|NIF_TIP;nid.uCallbackMessage=WM_GLO_TRAY;
    nid.hIcon=active&&g_ui->icon_tray_connected?g_ui->icon_tray_connected:g_ui->icon_app_small;
    lstrcpynW(nid.szTip,L"Game Latency Optimizer (GLO)",static_cast<int>(sizeof(nid.szTip)/sizeof(nid.szTip[0])));
    if(!g_ui->tray_added){
        if(Shell_NotifyIconW(NIM_ADD,&nid)){g_ui->tray_added=true;g_ui->tray_connected=active;}
    }else if(Shell_NotifyIconW(NIM_MODIFY,&nid)){g_ui->tray_connected=active;}
}

void remove_tray_icon(){
    if(!g_ui||!g_ui->tray_added||!g_ui->window)return;
    NOTIFYICONDATAW nid{};nid.cbSize=sizeof(nid);nid.hWnd=g_ui->window;nid.uID=kTrayIconId;
    Shell_NotifyIconW(NIM_DELETE,&nid);g_ui->tray_added=false;
}

void show_main_window(HWND hwnd){
    ShowWindow(hwnd,SW_RESTORE);SetForegroundWindow(hwnd);
}

void begin_quit(HWND hwnd){
    if(!g_ui||g_ui->closing.load())return;
    if(connection_active()){
        const wchar_t* text=g_ui->language==Language::Vietnamese?L"GLO đang kết nối. Thoát ứng dụng sẽ ngắt phiên hiện tại. Bạn có chắc muốn thoát?":L"GLO is currently connected. Quitting will disconnect the active session. Are you sure you want to quit?";
        const int answer=MessageBoxW(hwnd,text,L"Game Latency Optimizer",MB_YESNO|MB_ICONWARNING|MB_DEFBUTTON2|MB_TASKMODAL);
        if(answer!=IDYES)return;
    }
    if(g_ui->closing.exchange(true))return;
    EnableWindow(hwnd,FALSE);ShowWindow(hwnd,SW_HIDE);
    g_ui->shutdown_thread=std::thread([hwnd]{if(g_ui->disconnect_thread.joinable())g_ui->disconnect_thread.join();g_ui->client.shutdown();PostMessageW(hwnd,WM_GLO_SHUTDOWN_DONE,0,0);});
}

void show_tray_menu(HWND hwnd){
    HMENU menu=CreatePopupMenu();if(!menu)return;
    AppendMenuW(menu,MF_STRING,kTrayOpenId,L"Open Game Latency Optimizer");
    if(connection_established())AppendMenuW(menu,MF_STRING,kTrayDisconnectId,L"Disconnect");
    AppendMenuW(menu,MF_SEPARATOR,0,nullptr);AppendMenuW(menu,MF_STRING,kTrayQuitId,L"Quit");
    POINT pt{};GetCursorPos(&pt);SetForegroundWindow(hwnd);
    const UINT command=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_RIGHTBUTTON|TPM_NONOTIFY,pt.x,pt.y,0,hwnd,nullptr);
    DestroyMenu(menu);PostMessageW(hwnd,WM_NULL,0,0);
    if(command==kTrayOpenId)show_main_window(hwnd);
    else if(command==kTrayDisconnectId){if(connection_established())begin_disconnect(hwnd);}
    else if(command==kTrayQuitId)begin_quit(hwnd);
}

LRESULT CALLBACK wnd_proc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp){switch(msg){case WM_CREATE:g_ui->window=hwnd;g_ui->font_title=make_font(26,FW_BOLD);g_ui->font_hero=make_font(27,FW_BOLD);g_ui->font_body=make_font(14,FW_NORMAL);g_ui->font_label=make_font(11,FW_BOLD);g_ui->font_metric=make_font(18,FW_BOLD);g_ui->font_link=make_font(14,FW_NORMAL,true);g_ui->icon_app_big=static_cast<HICON>(LoadImageW(GetModuleHandleW(nullptr),MAKEINTRESOURCEW(IDI_GLO_APP),IMAGE_ICON,GetSystemMetrics(SM_CXICON),GetSystemMetrics(SM_CYICON),LR_DEFAULTCOLOR));g_ui->icon_app_small=static_cast<HICON>(LoadImageW(GetModuleHandleW(nullptr),MAKEINTRESOURCEW(IDI_GLO_APP),IMAGE_ICON,GetSystemMetrics(SM_CXSMICON),GetSystemMetrics(SM_CYSMICON),LR_DEFAULTCOLOR));if(g_ui->icon_app_big)SendMessageW(hwnd,WM_SETICON,ICON_BIG,reinterpret_cast<LPARAM>(g_ui->icon_app_big));if(g_ui->icon_app_small)SendMessageW(hwnd,WM_SETICON,ICON_SMALL,reinterpret_cast<LPARAM>(g_ui->icon_app_small));g_ui->icon_settings_light=load_ui_icon(IDI_GLO_SETTINGS_LIGHT);g_ui->icon_settings_dark=load_ui_icon(IDI_GLO_SETTINGS_DARK);g_ui->icon_sun_light=load_ui_icon(IDI_GLO_SUN_LIGHT);g_ui->icon_sun_dark=load_ui_icon(IDI_GLO_SUN_DARK);g_ui->icon_moon_light=load_ui_icon(IDI_GLO_MOON_LIGHT);g_ui->icon_moon_dark=load_ui_icon(IDI_GLO_MOON_DARK);g_ui->icon_back_light=load_ui_icon(IDI_GLO_BACK_LIGHT);g_ui->icon_back_dark=load_ui_icon(IDI_GLO_BACK_DARK);g_ui->icon_official_light=load_ui_icon(IDI_GLO_OFFICIAL_LIGHT);g_ui->icon_official_dark=load_ui_icon(IDI_GLO_OFFICIAL_DARK);g_ui->icon_github_light=load_ui_icon(IDI_GLO_GITHUB_LIGHT);g_ui->icon_github_dark=load_ui_icon(IDI_GLO_GITHUB_DARK);g_ui->icon_facebook_light=load_ui_icon(IDI_GLO_FACEBOOK_LIGHT);g_ui->icon_facebook_dark=load_ui_icon(IDI_GLO_FACEBOOK_DARK);g_ui->icon_paste_light=load_ui_icon(IDI_GLO_PASTE_LIGHT);g_ui->icon_paste_dark=load_ui_icon(IDI_GLO_PASTE_DARK);g_ui->icon_import_light=load_ui_icon(IDI_GLO_IMPORT_LIGHT);g_ui->icon_import_dark=load_ui_icon(IDI_GLO_IMPORT_DARK);g_ui->icon_connect_light=load_ui_icon(IDI_GLO_CONNECT_LIGHT);g_ui->icon_connect_dark=load_ui_icon(IDI_GLO_CONNECT_DARK);g_ui->icon_disconnect_light=load_ui_icon(IDI_GLO_DISCONNECT_LIGHT);g_ui->icon_disconnect_dark=load_ui_icon(IDI_GLO_DISCONNECT_DARK);g_ui->icon_tray_connected=make_connected_tray_icon(g_ui->icon_app_small);update_tray_icon(true);SetTimer(hwnd,42,250,nullptr);return 0;
    case WM_COPYDATA:{auto*cds=reinterpret_cast<COPYDATASTRUCT*>(lp);if(!cds||cds->dwData!=kHandoffCopyData||!cds->lpData||cds->cbData<sizeof(wchar_t)||cds->cbData>16384)return FALSE;std::wstring uri(static_cast<const wchar_t*>(cds->lpData));ShowWindow(hwnd,SW_RESTORE);SetForegroundWindow(hwnd);handle_handoff_uri(uri);return TRUE;}
    case WM_TIMER:if(wp==42){const auto snap=g_ui->client.snapshot();bool repaint=snap.state==glo::ConnectionState::Connecting||g_ui->disconnecting.load();if(g_ui->action_feedback!=ActionFeedback::None){if(GetTickCount64()>=g_ui->action_feedback_until_ms){g_ui->action_feedback=ActionFeedback::None;g_ui->action_feedback_until_ms=0;}repaint=true;}if(repaint)InvalidateRect(hwnd,nullptr,FALSE);update_tray_icon();return 0;}break;
    case WM_PAINT:{PAINTSTRUCT ps{};HDC dc=BeginPaint(hwnd,&ps);paint_ui(hwnd,dc);EndPaint(hwnd,&ps);return 0;}case WM_ERASEBKGND:return 1;
    case WM_SETCURSOR:if(LOWORD(lp)==HTCLIENT){POINT pt{};GetCursorPos(&pt);ScreenToClient(hwnd,&pt);if(g_ui->modal!=InAppModal::None){const bool hot=point_in(g_ui->modal_cancel_rect,pt)||point_in(g_ui->modal_primary_rect,pt)||point_in(g_ui->modal_uri_rect,pt)||point_in(g_ui->modal_paste_rect,pt)||point_in(g_ui->modal_import_rect,pt);if(hot){SetCursor(LoadCursor(nullptr,IDC_HAND));return TRUE;}break;}const bool common=point_in(g_ui->official_rect,pt)||point_in(g_ui->github_rect,pt)||point_in(g_ui->facebook_rect,pt)||point_in(g_ui->theme_rect,pt);const bool page_hot=g_ui->page==UiPage::Settings?(point_in(g_ui->back_rect,pt)||point_in(g_ui->language_en_rect,pt)||point_in(g_ui->language_vi_rect,pt)||point_in(g_ui->debug_toggle_rect,pt)||point_in(g_ui->debug_log_link_rect,pt)):(point_in(g_ui->settings_rect,pt)||point_in(g_ui->paste_rect,pt)||point_in(g_ui->import_rect,pt)||point_in(g_ui->connect_rect,pt)||point_in(g_ui->how_to_use_rect,pt)||point_in(g_ui->relay_eye_rect,pt));if(common||page_hot){SetCursor(LoadCursor(nullptr,IDC_HAND));return TRUE;}}break;
    case WM_LBUTTONUP:{
        if(g_ui->modal!=InAppModal::None){
            if(point_in(g_ui->modal_cancel_rect,lp)){g_ui->modal=InAppModal::None;InvalidateRect(hwnd,nullptr,FALSE);return 0;}
            if(g_ui->modal==InAppModal::HandoffConfirm&&point_in(g_ui->modal_primary_rect,lp)){
                const std::wstring uri=g_ui->pending_handoff_uri;g_ui->modal=InAppModal::None;g_ui->pending_handoff_uri.clear();g_ui->pending_handoff_game.clear();g_ui->pending_handoff_source.clear();InvalidateRect(hwnd,nullptr,FALSE);perform_handoff_connect(uri);return 0;
            }
            if(g_ui->modal==InAppModal::DisconnectConfirm&&point_in(g_ui->modal_primary_rect,lp)){
                g_ui->modal=InAppModal::None;InvalidateRect(hwnd,nullptr,FALSE);begin_disconnect(hwnd);return 0;
            }
            if(g_ui->modal==InAppModal::SelfHosted){
                if(point_in(g_ui->modal_uri_rect,lp)){
                    std::string text,error;if(!clipboard_text(text,error)){MessageBoxW(hwnd,widen(error).c_str(),L"GLO",MB_OK|MB_ICONERROR);return 0;}
                    const std::wstring uri=widen(text);g_ui->modal=InAppModal::None;InvalidateRect(hwnd,nullptr,FALSE);handle_handoff_uri(uri);return 0;
                }
                if(point_in(g_ui->modal_paste_rect,lp)){g_ui->modal=InAppModal::None;InvalidateRect(hwnd,nullptr,FALSE);paste_config();return 0;}
                if(point_in(g_ui->modal_import_rect,lp)){g_ui->modal=InAppModal::None;InvalidateRect(hwnd,nullptr,FALSE);import_config();return 0;}
            }
            return 0;
        }
        const ThemeMode ot=g_ui->theme;const Language ol=g_ui->language;const bool od=g_ui->debug_logging;
        if(point_in(g_ui->official_rect,lp)){open_external_link(hwnd,kOfficialWebUrl,L"Official web");return 0;}
        if(point_in(g_ui->github_rect,lp)){open_external_link(hwnd,kGithubUrl,L"GitHub");return 0;}
        if(point_in(g_ui->facebook_rect,lp)){open_external_link(hwnd,kFacebookUrl,L"Facebook");return 0;}
        if(g_ui->page==UiPage::Settings){
            if(point_in(g_ui->back_rect,lp)){g_ui->page=UiPage::Main;InvalidateRect(hwnd,nullptr,FALSE);return 0;}
            if(point_in(g_ui->theme_rect,lp)){g_ui->theme=g_ui->theme==ThemeMode::Dark?ThemeMode::Light:ThemeMode::Dark;persist_settings(hwnd,ot,ol,od);InvalidateRect(hwnd,nullptr,FALSE);return 0;}
            if(point_in(g_ui->language_en_rect,lp)||point_in(g_ui->language_vi_rect,lp)){g_ui->language=point_in(g_ui->language_vi_rect,lp)?Language::Vietnamese:Language::English;persist_settings(hwnd,ot,ol,od);InvalidateRect(hwnd,nullptr,FALSE);return 0;}
            if(point_in(g_ui->debug_toggle_rect,lp)){g_ui->debug_logging=!g_ui->debug_logging;persist_settings(hwnd,ot,ol,od);InvalidateRect(hwnd,nullptr,FALSE);return 0;}
            if(g_ui->debug_logging&&point_in(g_ui->debug_log_link_rect,lp)){open_debug_log_file(hwnd);return 0;}return 0;
        }
        if(point_in(g_ui->settings_rect,lp)){g_ui->page=UiPage::Settings;InvalidateRect(hwnd,nullptr,FALSE);return 0;}
        if(point_in(g_ui->theme_rect,lp)){g_ui->theme=g_ui->theme==ThemeMode::Dark?ThemeMode::Light:ThemeMode::Dark;persist_settings(hwnd,ot,ol,od);InvalidateRect(hwnd,nullptr,FALSE);return 0;}
        if(point_in(g_ui->how_to_use_rect,lp)){open_external_link(hwnd,kHowToUseUrl,g_ui->language==Language::Vietnamese?L"Sử dụng dịch vụ chính thức của GLO":L"Use the official GLO service");return 0;}
        if(point_in(g_ui->relay_eye_rect,lp)){g_ui->relay_endpoint_visible=!g_ui->relay_endpoint_visible;InvalidateRect(hwnd,nullptr,FALSE);return 0;}
        const auto snap=g_ui->client.snapshot();const bool active=snap.phase!=glo::UiPhase::Disconnected||snap.state==glo::ConnectionState::Connecting||g_ui->disconnecting.load();
        if(!active&&point_in(g_ui->paste_rect,lp)){self_host_menu();return 0;}
        if(point_in(g_ui->connect_rect,lp)){if(active){if(!g_ui->disconnecting.load()){g_ui->modal=InAppModal::DisconnectConfirm;InvalidateRect(hwnd,nullptr,FALSE);}}else connect_loaded();return 0;}
        break;
    }
    case WM_KEYDOWN:
        if(wp==VK_ESCAPE&&g_ui->modal!=InAppModal::None){g_ui->modal=InAppModal::None;InvalidateRect(hwnd,nullptr,FALSE);return 0;}
        if(g_ui->modal!=InAppModal::None)return 0;
        if(wp==VK_ESCAPE&&g_ui->page==UiPage::Settings){g_ui->page=UiPage::Main;InvalidateRect(hwnd,nullptr,FALSE);return 0;}
        if(wp==VK_RETURN&&g_ui->page==UiPage::Main){const auto snap=g_ui->client.snapshot();if(snap.phase==glo::UiPhase::Disconnected&&snap.state!=glo::ConnectionState::Connecting)connect_loaded();return 0;}break;
    case WM_GLO_UPDATE:{const auto s=g_ui->client.snapshot();if(s.session_id!=0&&!g_ui->config_consumed){g_ui->config_consumed=true;g_ui->options.session_grant.clear();if(g_ui->config)g_ui->config->grant.clear();}if(s.phase==glo::UiPhase::Disconnected&&g_ui->config_consumed&&!g_ui->disconnecting.load())clear_config_after_use();InvalidateRect(hwnd,nullptr,FALSE);UpdateWindow(hwnd);update_tray_icon();if(s.error&&s.error->generation&&s.error->generation!=g_ui->last_error_generation){g_ui->last_error_generation=s.error->generation;const auto[t,m]=localized_error(*s.error);MessageBoxW(hwnd,m.c_str(),t.c_str(),MB_OK|MB_ICONERROR|MB_TASKMODAL);}return 0;}
    case WM_GLO_DISCONNECT_DONE:if(g_ui->disconnect_thread.joinable())g_ui->disconnect_thread.join();g_ui->disconnecting=false;if(g_ui->config_consumed)clear_config_after_use();InvalidateRect(hwnd,nullptr,FALSE);update_tray_icon(true);return 0;
    case WM_GLO_TRAY:
        if(lp==WM_LBUTTONUP||lp==WM_LBUTTONDBLCLK){show_main_window(hwnd);return 0;}
        if(lp==WM_RBUTTONUP||lp==WM_CONTEXTMENU){show_tray_menu(hwnd);return 0;}
        break;
    case WM_POWERBROADCAST:if(wp==PBT_APMSUSPEND){if(!g_ui->disconnecting.load())begin_disconnect(hwnd);return TRUE;}break;
    case WM_CLOSE:ShowWindow(hwnd,SW_HIDE);return 0;
    case WM_GLO_SHUTDOWN_DONE:if(g_ui->shutdown_thread.joinable())g_ui->shutdown_thread.join();DestroyWindow(hwnd);return 0;
    case WM_DESTROY:remove_tray_icon();for(HFONT f:{g_ui->font_title,g_ui->font_hero,g_ui->font_body,g_ui->font_label,g_ui->font_metric,g_ui->font_link})if(f)DeleteObject(f);for(HICON i:{g_ui->icon_settings_light,g_ui->icon_settings_dark,g_ui->icon_sun_light,g_ui->icon_sun_dark,g_ui->icon_moon_light,g_ui->icon_moon_dark,g_ui->icon_back_light,g_ui->icon_back_dark,g_ui->icon_official_light,g_ui->icon_official_dark,g_ui->icon_github_light,g_ui->icon_github_dark,g_ui->icon_facebook_light,g_ui->icon_facebook_dark,g_ui->icon_paste_light,g_ui->icon_paste_dark,g_ui->icon_import_light,g_ui->icon_import_dark,g_ui->icon_connect_light,g_ui->icon_connect_dark,g_ui->icon_disconnect_light,g_ui->icon_disconnect_dark,g_ui->icon_app_big,g_ui->icon_app_small,g_ui->icon_tray_connected})if(i)DestroyIcon(i);PostQuitMessage(0);return 0;}return DefWindowProcW(hwnd,msg,wp,lp);}
}

int WINAPI WinMain(HINSTANCE instance,HINSTANCE,LPSTR,int show){if(auto worker_exit=glo::run_network_worker_if_requested())return *worker_exit;SetProcessDPIAware();auto ui=std::make_unique<UiState>();g_ui=ui.get();g_ui->theme=system_looks_dark()?ThemeMode::Dark:ThemeMode::Light;load_settings();CommandArgs command;try{command=parse_args();}catch(const std::exception&e){MessageBoxW(nullptr,widen(e.what()).c_str(),L"GLO - Command line",MB_OK|MB_ICONERROR);return 64;}g_ui->options.dbg_log=g_ui->debug_logging;sync_frontend_debug_log(false);g_ui->app_log.info("APP001",std::string("event=start version=")+GLO_VERSION+" process=frontend");
HANDLE instance_mutex=CreateMutexW(nullptr,FALSE,kSingleInstanceMutex);if(!instance_mutex){MessageBoxW(nullptr,L"Could not create the GLO instance lock.",L"GLO",MB_OK|MB_ICONERROR);return 1;}if(GetLastError()==ERROR_ALREADY_EXISTS){HWND existing=nullptr;for(int i=0;i<30&&!existing;++i){existing=FindWindowW(kMainWindowClass,nullptr);if(!existing)Sleep(50);}if(existing){ShowWindow(existing,SW_RESTORE);SetForegroundWindow(existing);if(command.uri){COPYDATASTRUCT cds{};cds.dwData=kHandoffCopyData;cds.cbData=static_cast<DWORD>((command.uri->size()+1)*sizeof(wchar_t));cds.lpData=command.uri->data();DWORD_PTR forwarded=0;SendMessageTimeoutW(existing,WM_COPYDATA,0,reinterpret_cast<LPARAM>(&cds),SMTO_ABORTIFHUNG|SMTO_BLOCK,2000,&forwarded);}}CloseHandle(instance_mutex);g_ui=nullptr;return 0;}
WNDCLASSW wc{};wc.lpfnWndProc=wnd_proc;wc.hInstance=instance;wc.lpszClassName=kMainWindowClass;wc.hCursor=LoadCursor(nullptr,IDC_ARROW);wc.hIcon=static_cast<HICON>(LoadImageW(instance,MAKEINTRESOURCEW(IDI_GLO_APP),IMAGE_ICON,GetSystemMetrics(SM_CXICON),GetSystemMetrics(SM_CYICON),LR_DEFAULTCOLOR));wc.hbrBackground=nullptr;if(!RegisterClassW(&wc)){g_ui->app_log.error("APP900","event=window_class_register_failed");CloseHandle(instance_mutex);return 1;}RECT wr{0,0,kClientWidth,kClientHeight};AdjustWindowRect(&wr,WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX,FALSE);HWND hwnd=CreateWindowExW(0,kMainWindowClass,L"Game Latency Optimizer",WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX,CW_USEDEFAULT,CW_USEDEFAULT,wr.right-wr.left,wr.bottom-wr.top,nullptr,nullptr,instance,nullptr);if(!hwnd){g_ui->app_log.error("APP901","event=window_create_failed");CloseHandle(instance_mutex);return 2;}ShowWindow(hwnd,show);UpdateWindow(hwnd);if(command.config){std::string text,error;if(read_text_file(*command.config,text,error)){parse_and_apply(text);}else MessageBoxW(hwnd,widen(error).c_str(),L"GLO - Config",MB_OK|MB_ICONERROR);}if(command.uri)handle_handoff_uri(*command.uri);MSG msg{};while(GetMessageW(&msg,nullptr,0,0)>0){TranslateMessage(&msg);DispatchMessageW(&msg);}g_ui->app_log.info("APP002","event=shutdown process=frontend");CloseHandle(instance_mutex);g_ui=nullptr;return static_cast<int>(msg.wParam);}
