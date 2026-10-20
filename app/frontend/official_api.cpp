#include "official_api.hpp"
#include <windows.h>
#include <winhttp.h>
#include <wincrypt.h>
#include <algorithm>
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
std::string quoted(const std::string& text){std::string q="\"";for(unsigned char ch:text){if(ch=='"'||ch=='\\'){q+='\\';q+=char(ch);}else if(ch>=32&&ch<127)q+=char(ch);else if(ch>=128)q+=char(ch);}return q+'"';}
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
    reply.clear();code=0;
    HINTERNET session=WinHttpOpen(L"GLO/0.1.0-beta",WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,WINHTTP_NO_PROXY_NAME,WINHTTP_NO_PROXY_BYPASS,0);
    if(!session)return false;
    WinHttpSetTimeouts(session,5000,5000,5000,8000);
    HINTERNET connection=WinHttpConnect(session,L"api.gloptimizer.com",INTERNET_DEFAULT_HTTPS_PORT,0);
    if(!connection){WinHttpCloseHandle(session);return false;}
    HINTERNET req=WinHttpOpenRequest(connection,method,path,nullptr,WINHTTP_NO_REFERER,WINHTTP_DEFAULT_ACCEPT_TYPES,WINHTTP_FLAG_SECURE);
    if(!req){WinHttpCloseHandle(connection);WinHttpCloseHandle(session);return false;}
    const std::string authorization=auth?access_:std::string{};
    std::wstring headers=L"Accept: application/json\r\nContent-Type: application/json\r\n";
    if(!authorization.empty())headers+=L"Authorization: Bearer "+std::wstring(authorization.begin(),authorization.end())+L"\r\n";
    const bool post=wcscmp(method,L"POST")==0;
    BOOL ok=WinHttpSendRequest(req,headers.c_str(),static_cast<DWORD>(-1),post?(LPVOID)body.data():WINHTTP_NO_REQUEST_DATA,post?static_cast<DWORD>(body.size()):0,post?static_cast<DWORD>(body.size()):0,0)&&WinHttpReceiveResponse(req,nullptr);
    DWORD bytes=sizeof(DWORD);if(ok)WinHttpQueryHeaders(req,WINHTTP_QUERY_STATUS_CODE|WINHTTP_QUERY_FLAG_NUMBER,WINHTTP_HEADER_NAME_BY_INDEX,&code,&bytes,WINHTTP_NO_HEADER_INDEX);
    if(ok){for(;;){DWORD size=0;if(!WinHttpQueryDataAvailable(req,&size)){ok=FALSE;break;}if(!size)break;if(reply.size()+size>262144){ok=FALSE;break;}std::string temp(size,'\0');DWORD n=0;if(!WinHttpReadData(req,temp.data(),size,&n)){ok=FALSE;break;}reply.append(temp.data(),n);}}
    WinHttpCloseHandle(req);WinHttpCloseHandle(connection);WinHttpCloseHandle(session);return !!ok;
}
bool OfficialApi::renew(){if(refresh_.empty())return false;std::string reply;unsigned long code=0;
    if(!request(L"POST",L"/app/auth/refresh","{\"refresh_token\":"+quoted(refresh_)+"}",reply,code,false)||code!=200){refresh_.clear();access_.clear();store_refresh();send("{\"type\":\"auth\",\"logged\":false}");return false;}
    auto access=get_string(reply,"access_token"),refresh=get_string(reply,"refresh_token");
    if(access.empty()||refresh.empty())return false;access_=access;refresh_=refresh;store_refresh();return true;
}
bool OfficialApi::authorized(const wchar_t* method,const wchar_t* path,const std::string& body,std::string& reply,unsigned long& status){
    if(access_.empty()&&!renew())return false;
    if(!request(method,path,body,reply,status,true))return false;
    if(status==401){if(!renew())return false;return request(method,path,body,reply,status,true);}
    return true;
}
void OfficialApi::fetch_details(){std::string reply;unsigned long status=0;
    if(!authorized(L"GET",L"/app/me","",reply,status)||status!=200){send("{\"type\":\"auth\",\"logged\":false}");return;}
    const auto email=get_string(reply,"email");
    send("{\"type\":\"auth\",\"logged\":true,\"email\":"+quoted(email)+"}");
    auto balance=extract(reply,"balance",'{','}');if(!balance.empty())send("{\"type\":\"wallet\",\"wallet\":"+balance+"}");
    if(authorized(L"GET",L"/app/booster/catalog","",reply,status)&&status==200){
        auto games=extract(reply,"games",'[',']');auto regions=extract(reply,"regions",'[',']');
        if(!games.empty()&&!regions.empty())send("{\"type\":\"catalog\",\"games\":"+games+",\"regions\":"+regions+"}");
    }
}
void OfficialApi::restore(){async([this](){load_refresh();if(!refresh_.empty())fetch_details();else send("{\"type\":\"auth\",\"logged\":false}");});}
void OfficialApi::redeem(std::string code){async([this,code=std::move(code)](){std::string reply;unsigned long status=0;
    if(!request(L"POST",L"/app/auth/redeem","{\"code\":"+quoted(code)+"}",reply,status,false)||status!=200){send("{\"type\":\"error\",\"message\":\"Mã đăng nhập không hợp lệ hoặc đã hết hạn.\"}");return;}
    auto a=get_string(reply,"access_token"),r=get_string(reply,"refresh_token");if(a.empty()||r.empty()){send("{\"type\":\"error\",\"message\":\"Backend trả về phiên không hợp lệ.\"}");return;}
    access_=std::move(a);refresh_=std::move(r);store_refresh();fetch_details();send("{\"type\":\"toast\",\"message\":\"Đăng nhập thành công.\"}");
});}
void OfficialApi::refresh_wallet(){async([this](){fetch_details();});}
void OfficialApi::prepare(std::string game,std::string region){async([this,game=std::move(game),region=std::move(region)](){
    std::string reply;unsigned long status=0;
    if(!authorized(L"POST",L"/app/session/prepare","{\"game\":"+quoted(game)+",\"region\":"+quoted(region)+"}",reply,status)){
        send("{\"type\":\"error\",\"message\":\"Không thể liên lạc với GLO API.\"}");return;}
    if(status==409 && (reply.find("BALANCE_EMPTY")!=std::string::npos || reply.find("BALANCE")!=std::string::npos)){
        send("{\"type\":\"need-time\"}");return;}
    if(status!=200){send("{\"type\":\"error\",\"message\":\"Không thể tạo phiên. Kiểm tra trò chơi, region hoặc số dư.\"}");return;}
    std::string token=get_string(reply,"handoff_token");
    if(token.empty()){send("{\"type\":\"error\",\"message\":\"Thiếu handoff token.\"}");return;}
    send("{\"type\":\"prepared\",\"token\":"+quoted(token)+"}");
});}
void OfficialApi::logout(){async([this](){std::string reply;unsigned long code=0;
    if(!access_.empty())request(L"POST",L"/app/auth/logout","{\"refresh_token\":"+quoted(refresh_)+"}",reply,code,true);
    access_.clear();refresh_.clear();store_refresh();send("{\"type\":\"auth\",\"logged\":false}");
});}
