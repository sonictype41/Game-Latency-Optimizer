#include "official_api.hpp"
#include <windows.h>
#include <winhttp.h>
#include <wincrypt.h>
#include <algorithm>
#include <chrono>
#include <thread>
#include <cwchar>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <utility>

namespace {
std::string get_string(const std::string& data,const std::string& name){
    std::string key="\""+name+"\"";
    size_t p=data.find(key);if(p==std::string::npos)return {};
    p=data.find(':',p+key.size());if(p==std::string::npos)return {};
    p=data.find('"',p+1);if(p==std::string::npos)return {};
    std::string out;
    for(++p;p<data.size();++p){char c=data[p];if(c=='"')return out;if(c=='\\'){if(++p>=data.size())break;c=data[p];if(c=='n'||c=='r')c=' ';else if(c=='t')c=' ';}out+=c;if(out.size()>8192)break;}
    return {};
}
std::string json_quote(const std::string& text){std::string q="\"";for(unsigned char ch:text){if(ch=='"'||ch=='\\'){q+='\\';q+=char(ch);}else if(ch>=32&&ch<127)q+=char(ch);else if(ch>=128)q+=char(ch);}return q+'"';}
std::string extract(const std::string& s,const std::string& key,char open,char close){
    size_t p=s.find('"'+key+'"');if(p==std::string::npos)return {};
    p=s.find(':',p+key.size()+2);if(p==std::string::npos)return {};
    p=s.find(open,p+1);if(p==std::string::npos)return {};
    bool string=false,escaped=false;int depth=0;
    for(size_t i=p;i<s.size();++i){char c=s[i];if(string){if(escaped)escaped=false;else if(c=='\\')escaped=true;else if(c=='"')string=false;continue;}if(c=='"'){string=true;continue;}if(c==open)++depth;else if(c==close&&!--depth)return s.substr(p,i-p+1);}
    return {};
}
std::filesystem::path appdata(){wchar_t buf[32768]{};const DWORD n=GetEnvironmentVariableW(L"LOCALAPPDATA",buf,32768);if(!n||n>=32768)return {};return std::filesystem::path(buf)/L"GLO";}
}
OfficialApi::OfficialApi(Notify cb):notify_(std::move(cb)){
    worker_=std::thread([this](){
        while(true){std::function<void()> work;
            {std::unique_lock lock(jobs_lock_);jobs_cv_.wait(lock,[this]{return closing_||!tasks_.empty();});
             if(closing_)break;work=std::move(tasks_.front());tasks_.pop_front();}
            work();
        }
    });
}
OfficialApi::~OfficialApi(){stop();}
void OfficialApi::send(std::string json){if(notify_)notify_(std::move(json));}
void OfficialApi::async(std::function<void()> work){{std::lock_guard lock(jobs_lock_);if(closing_)return;tasks_.push_back(std::move(work));}jobs_cv_.notify_one();}
void OfficialApi::stop(){{std::lock_guard lock(jobs_lock_);closing_=true;tasks_.clear();}jobs_cv_.notify_all();if(worker_.joinable())worker_.join();}
std::filesystem::path OfficialApi::token_file() const {const auto d=appdata();return d.empty()?std::filesystem::path{}:d/L"native_auth.dat";}
void OfficialApi::store_refresh(){auto path=token_file();if(path.empty())return;if(refresh_.empty()){std::error_code ec;std::filesystem::remove(path,ec);return;}
    DATA_BLOB src{static_cast<DWORD>(refresh_.size()),reinterpret_cast<BYTE*>(refresh_.data())},encrypted{};
    if(!CryptProtectData(&src,L"GLO native session",nullptr,nullptr,nullptr,CRYPTPROTECT_UI_FORBIDDEN,&encrypted))return;
    std::error_code ec;std::filesystem::create_directories(path.parent_path(),ec);
    if(!ec){std::ofstream out(path,std::ios::binary|std::ios::trunc);if(out)out.write(reinterpret_cast<const char*>(encrypted.pbData),encrypted.cbData);}
    LocalFree(encrypted.pbData);
}
void OfficialApi::load_refresh(){auto path=token_file();if(path.empty())return;std::ifstream in(path,std::ios::binary);if(!in)return;
    std::string cipher(std::istreambuf_iterator<char>{in},{});if(cipher.empty()||cipher.size()>16384)return;
    DATA_BLOB src{static_cast<DWORD>(cipher.size()),reinterpret_cast<BYTE*>(cipher.data())},raw{};
    if(CryptUnprotectData(&src,nullptr,nullptr,nullptr,nullptr,CRYPTPROTECT_UI_FORBIDDEN,&raw)){
        if(raw.cbData<=512)refresh_.assign(reinterpret_cast<const char*>(raw.pbData),raw.cbData);
        LocalFree(raw.pbData);
    }
}
bool OfficialApi::request(const wchar_t* method,const wchar_t* path,const std::string& body,std::string& reply,unsigned long& code,bool auth){
    reply.clear();code=0;last_network_error_=0;
    HINTERNET session=WinHttpOpen(L"GLO/0.1.5-beta",WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,WINHTTP_NO_PROXY_NAME,WINHTTP_NO_PROXY_BYPASS,0);
    if(!session){last_network_error_=GetLastError();return false;}
    WinHttpSetTimeouts(session,5000,5000,5000,8000);
    HINTERNET connection=WinHttpConnect(session,L"api.gloptimizer.com",INTERNET_DEFAULT_HTTPS_PORT,0);
    if(!connection){last_network_error_=GetLastError();WinHttpCloseHandle(session);return false;}
    HINTERNET req=WinHttpOpenRequest(connection,method,path,nullptr,WINHTTP_NO_REFERER,WINHTTP_DEFAULT_ACCEPT_TYPES,WINHTTP_FLAG_SECURE);
    if(!req){last_network_error_=GetLastError();WinHttpCloseHandle(connection);WinHttpCloseHandle(session);return false;}
    const std::string authorization=auth?access_:std::string{};
    std::wstring headers=L"Accept: application/json\r\nContent-Type: application/json\r\n";
    if(!authorization.empty())headers+=L"Authorization: Bearer "+std::wstring(authorization.begin(),authorization.end())+L"\r\n";
    const bool post=wcscmp(method,L"POST")==0;
    BOOL ok=WinHttpSendRequest(req,headers.c_str(),static_cast<DWORD>(-1),post?(LPVOID)body.data():WINHTTP_NO_REQUEST_DATA,post?static_cast<DWORD>(body.size()):0,post?static_cast<DWORD>(body.size()):0,0)&&WinHttpReceiveResponse(req,nullptr);
    DWORD bytes=sizeof(DWORD);if(ok)WinHttpQueryHeaders(req,WINHTTP_QUERY_STATUS_CODE|WINHTTP_QUERY_FLAG_NUMBER,WINHTTP_HEADER_NAME_BY_INDEX,&code,&bytes,WINHTTP_NO_HEADER_INDEX);
    if(ok){for(;;){DWORD size=0;if(!WinHttpQueryDataAvailable(req,&size)){ok=FALSE;break;}if(!size)break;if(reply.size()+size>262144){ok=FALSE;break;}std::string temp(size,'\0');DWORD n=0;if(!WinHttpReadData(req,temp.data(),size,&n)){ok=FALSE;break;}reply.append(temp.data(),n);}}
    if(!ok)last_network_error_=GetLastError();
    WinHttpCloseHandle(req);WinHttpCloseHandle(connection);WinHttpCloseHandle(session);return !!ok;
}
bool OfficialApi::renew(){if(refresh_.empty())return false;std::string reply;unsigned long code=0;
    const bool reached=request(L"POST",L"/app/auth/refresh","{\"refresh_token\":"+json_quote(refresh_)+"}",reply,code,false);
    if(!reached||code!=200){
        // Retain DPAPI credentials across network errors. Only a definitive auth
        // rejection is proof the stored session is invalid.
        if(reached&&(code==401||code==403)){refresh_.clear();access_.clear();store_refresh();}
        return false;
    }
    auto access=get_string(reply,"access_token"),refresh=get_string(reply,"refresh_token");
    if(access.empty()||refresh.empty())return false;access_=access;refresh_=refresh;store_refresh();return true;
}
bool OfficialApi::authorized(const wchar_t* method,const wchar_t* path,const std::string& body,std::string& reply,unsigned long& status){
    if(access_.empty()&&!renew())return false;
    if(!request(method,path,body,reply,status,true))return false;
    if(status==401){
        if(!renew()){
            // A failed refresh due to a transient network/server error does not
            // establish that the existing session has expired.
            status=refresh_.empty()?401:0;
            return false;
        }
        return request(method,path,body,reply,status,true);
    }
    return true;
}
void OfficialApi::publish_wallet(const std::string& balance){
    if(balance.empty())return;
    cached_balance_=balance;
    wallet_updated_=std::chrono::steady_clock::now();
    wallet_stale_.store(false);
    send("{\"type\":\"wallet\",\"wallet\":"+balance+"}");
    send("{\"type\":\"wallet-status\",\"status\":\"success\"}");
}
void OfficialApi::publish_cached(){
    if(identity_verified_){
        send("{\"type\":\"auth\",\"logged\":true,\"email\":"+json_quote(cached_email_)+"}");
        if(!cached_balance_.empty())send("{\"type\":\"wallet\",\"wallet\":"+cached_balance_+"}");
        if(!cached_catalog_games_.empty()&&!cached_catalog_regions_.empty())
            send("{\"type\":\"catalog\",\"games\":"+cached_catalog_games_+",\"regions\":"+cached_catalog_regions_+",\"art_time_factor\":"+cached_art_time_factor_+"}");
    }
}
void OfficialApi::sync_cached(){async([this](){
    if(identity_verified_)publish_cached();
    else if(!refresh_.empty())fetch_details();
    else send("{\"type\":\"auth\",\"logged\":false}");
});}
bool OfficialApi::fetch_details(bool report_error){
    std::string reply;unsigned long status=0;
    if(!authorized(L"GET",L"/app/me","",reply,status)||status!=200){
        if(refresh_.empty()){
            // Only confirmed refresh rejection, or no credentials, is logout.
            access_.clear();refresh_.clear();identity_verified_=false;
            cached_email_.clear();cached_balance_.clear();cached_catalog_games_.clear();cached_catalog_regions_.clear();cached_art_time_factor_="null";
            wallet_stale_.store(true);store_refresh();
            send("{\"type\":\"auth\",\"logged\":false}");
        }else if(report_error&&!identity_verified_){
            send("{\"type\":\"auth\",\"status\":\"offline\"}");
        }else if(report_error){
            send("{\"type\":\"wallet-status\",\"status\":\"error\"}");
        }
        return false;
    }
    cached_email_=get_string(reply,"email");
    identity_verified_=true;
    send("{\"type\":\"auth\",\"logged\":true,\"email\":"+json_quote(cached_email_)+"}");
    publish_wallet(extract(reply,"balance",'{','}'));
    // Catalog is not a wallet dependency. It is loaded once at sign-in/restore.
    if(authorized(L"GET",L"/app/booster/catalog","",reply,status)&&status==200){
        auto games=extract(reply,"games",'[',']'),regions=extract(reply,"regions",'[',']');
        if(!games.empty()&&!regions.empty()){
            cached_catalog_games_=games;cached_catalog_regions_=regions;
            // Only numeric config advertised by the first-party backend is surfaced.
            std::string factor="null";const auto key=reply.find("\"art_time_factor\"");
            if(key!=std::string::npos){const auto colon=reply.find(':',key+17);if(colon!=std::string::npos){
                const auto first=reply.find_first_not_of(" \t\r\n",colon+1);
                if(first!=std::string::npos){const auto last=reply.find_first_not_of("0123456789.",first);
                    const auto raw=reply.substr(first,last==std::string::npos?std::string::npos:last-first);
                    try{const auto num=std::stod(raw);if(num>=1.0&&num<=10.0)factor=raw;}catch(...){}}
                }}
            cached_art_time_factor_=factor;
            send("{\"type\":\"catalog\",\"games\":"+games+",\"regions\":"+regions+",\"art_time_factor\":"+factor+"}");
        }
    }
    return true;
}
void OfficialApi::restore(){async([this](){
    if(refresh_.empty())load_refresh();
    if(refresh_.empty()){send("{\"type\":\"auth\",\"logged\":false}");return;}
    // Only startup/explicit Retry verifies identity. A transient failure should
    // not throw away the last successfully verified UI/session.
    for(int attempt=0;attempt<2;++attempt){
        if(fetch_details(false))return;
        if(refresh_.empty())return; // definitive token rejection
        if(attempt==0)std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
    if(!identity_verified_)send("{\"type\":\"auth\",\"status\":\"offline\"}");
    else send("{\"type\":\"wallet-status\",\"status\":\"error\"}");
});}
void OfficialApi::redeem(std::string code){async([this,code=std::move(code)](){std::string reply;unsigned long status=0;
    if(!request(L"POST",L"/app/auth/redeem","{\"code\":"+json_quote(code)+"}",reply,status,false)||status!=200){send("{\"type\":\"error\",\"message\":\"Mã đăng nhập không hợp lệ hoặc đã hết hạn.\"}");return;}
    auto a=get_string(reply,"access_token"),r=get_string(reply,"refresh_token");if(a.empty()||r.empty()){send("{\"type\":\"error\",\"message\":\"Backend trả về phiên không hợp lệ.\"}");return;}
    access_=std::move(a);refresh_=std::move(r);identity_verified_=false;wallet_stale_=true;wallet_updated_={};wallet_failed_={};
    cached_email_.clear();cached_balance_.clear();cached_catalog_games_.clear();cached_catalog_regions_.clear();cached_art_time_factor_="null";
    store_refresh();fetch_details();send("{\"type\":\"toast\",\"message\":\"Đăng nhập thành công.\"}");
});}
void OfficialApi::invalidate_wallet_cache() noexcept{wallet_stale_.store(true);}
void OfficialApi::refresh_wallet(){
    // Collapse repeated menu opens *before* enqueueing tasks. Do not poll.
    if(wallet_requested_.exchange(true))return;
    async([this](){
        const auto now=std::chrono::steady_clock::now();
        const bool fresh=!wallet_stale_.load()&&!cached_balance_.empty()&&
            now-wallet_updated_<std::chrono::seconds(15);
        if(fresh){
            send("{\"type\":\"wallet-status\",\"status\":\"cached\"}");
            wallet_requested_=false;return;
        }
        if(wallet_failed_!=std::chrono::steady_clock::time_point{} &&
           now-wallet_failed_<std::chrono::seconds(5)){
            send("{\"type\":\"wallet-status\",\"status\":\"error\"}");
            wallet_requested_=false;return;
        }
        send("{\"type\":\"wallet-status\",\"status\":\"loading\"}");
        wallet_failed_=now; // until a successful response clears it
        std::string reply;unsigned long status=0;
        const bool ok=authorized(L"GET",L"/app/me","",reply,status);
        if(ok&&status==200){
            const auto balance=extract(reply,"balance",'{','}');
            if(!balance.empty()){wallet_failed_={};publish_wallet(balance);}
            else send("{\"type\":\"wallet-status\",\"status\":\"error\"}");
        }else{
            // Do not alter the verified auth UI on network/5xx/429 errors.
            // Definitive credential invalidity still invalidates authenticated UI.
            if(refresh_.empty()){
                access_.clear();refresh_.clear();identity_verified_=false;store_refresh();
                cached_balance_.clear();cached_email_.clear();cached_catalog_games_.clear();cached_catalog_regions_.clear();cached_art_time_factor_="null";
                send("{\"type\":\"auth\",\"logged\":false}");
            }else send("{\"type\":\"wallet-status\",\"status\":\"error\"}");
        }
        wallet_requested_=false;
    });
}
// Map only known provider error codes to public, non-sensitive explanations.
// Never include HTTP response bodies, handoff tokens or Authorization headers in diagnostics.
void OfficialApi::prepare(std::string game,std::string region,bool advanced){async([this,game=std::move(game),region=std::move(region),advanced](){
    std::string reply;unsigned long status=0;
    const bool reached=authorized(L"POST",L"/app/session/prepare","{\"game\":"+json_quote(game)+",\"region\":"+json_quote(region)+",\"routing_preference\":\""+(advanced?"advanced":"standard")+"\"}",reply,status);
    const auto code=get_string(reply,"code");
    if(reached&&status==409&&(code=="BALANCE_EMPTY"||code=="BALANCE_INSUFFICIENT"||reply.find("BALANCE_EMPTY")!=std::string::npos)){
        send("{\"type\":\"need-time\",\"stage\":\"prepare\",\"error_code\":\"BALANCE_EMPTY\"}");return;
    }
    if(!reached||status!=200){
        std::string safe_code="PREPARE_FAILED";
        std::string message="Unable to prepare a GLO session. Please try again.";
        if(status==401||status==403){
            safe_code="AUTH_REQUIRED";message="Sign-in expired. Please sign in again.";
        }else if(!reached){
            safe_code=last_network_error_==12002?"NETWORK_TIMEOUT":"NETWORK_UNAVAILABLE";
            message=last_network_error_==12002?"GLO API connection timed out.":"Could not reach GLO API. Check your connection.";
        }else if(status==429||code=="RATE_LIMITED"){
            safe_code="RATE_LIMITED";message="Too many requests. Please wait before retrying.";
        }else if(code=="ART_NO_ELIGIBLE_ROUTE"||code=="ART_ADMISSION_FAILED"||code=="ART_NO_REACHABLE_ROUTE"){
            safe_code=code;message="Could not establish an Advanced Routing session. No usage has started.";
        }else if(code=="SERVICE_MAINTENANCE"){
            safe_code="SERVICE_MAINTENANCE";message="GLO Services is temporarily unavailable.";
        }else if(code=="PROFILE_UNAVAILABLE"){
            safe_code="PROFILE_UNAVAILABLE";message="The selected game or region is unavailable.";
        }else if(code=="HANDOFF_CANDIDATES_UNAVAILABLE"){
            safe_code="HANDOFF_CANDIDATES_UNAVAILABLE";message="No relay candidates are currently available.";
        }else if(status>=500){safe_code="SERVER_ERROR";message="GLO API returned a server error.";}
        send("{\"type\":\"error\",\"stage\":\"prepare\",\"error_code\":"+json_quote(safe_code)+",\"http_status\":"+std::to_string(status)+",\"network_error\":"+std::to_string(last_network_error_)+",\"message\":"+json_quote(message)+"}");
        return;
    }
    std::string token=get_string(reply,"handoff_token");
    if(token.empty()){
        send("{\"type\":\"error\",\"stage\":\"prepare\",\"error_code\":\"MISSING_HANDOFF_TOKEN\",\"message\":\"Provider response was incomplete.\"}");return;
    }
    send("{\"type\":\"prepared\",\"token\":"+json_quote(token)+"}");
});}
void OfficialApi::logout(){async([this](){std::string reply;unsigned long code=0;
    if(!access_.empty())request(L"POST",L"/app/auth/logout","{\"refresh_token\":"+json_quote(refresh_)+"}",reply,code,true);
    access_.clear();refresh_.clear();identity_verified_=false;wallet_stale_.store(true);wallet_updated_={};wallet_failed_={};
    cached_email_.clear();cached_balance_.clear();cached_catalog_games_.clear();cached_catalog_regions_.clear();cached_art_time_factor_="null";
    store_refresh();send("{\"type\":\"auth\",\"logged\":false,\"reason\":\"logout\"}");
});}
