#include "glo/network_worker.hpp"
#include "glo/game_profile.hpp"

#include <sodium.h>
#include <windows.h>
#include <shellapi.h>
#include <sddl.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <charconv>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <optional>
#include <sstream>
#include <span>
#include <string>
#include <thread>
#include <vector>

namespace glo {
namespace {
constexpr wchar_t kWorkerFlag[] = L"--network-worker";
constexpr wchar_t kPipeFlag[] = L"--worker-pipe";
constexpr wchar_t kSecretFlag[] = L"--worker-secret";
constexpr char kProtocolHello[] = "GIPC1";

std::string narrow(const std::wstring& s) {
    if (s.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, s.data(), static_cast<int>(s.size()), nullptr, 0, nullptr, nullptr);
    if (n <= 0) return {};
    std::string out(static_cast<std::size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, s.data(), static_cast<int>(s.size()), out.data(), n, nullptr, nullptr);
    return out;
}
std::wstring widen(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), static_cast<int>(s.size()), nullptr, 0);
    if (n <= 0) return {};
    std::wstring out(static_cast<std::size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), static_cast<int>(s.size()), out.data(), n);
    return out;
}

std::string hex_encode(std::span<const std::uint8_t> bytes) {
    static constexpr char h[] = "0123456789abcdef";
    std::string out; out.resize(bytes.size()*2);
    for (std::size_t i=0;i<bytes.size();++i) { out[2*i]=h[bytes[i]>>4]; out[2*i+1]=h[bytes[i]&15]; }
    return out;
}
std::string hex_encode(const std::string& s) {
    return hex_encode(std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(s.data()), s.size()));
}
bool hex_decode(const std::string& in, std::vector<std::uint8_t>& out) {
    if (in.size() & 1) return false;
    auto nib=[](char c)->int{if(c>='0'&&c<='9')return c-'0';if(c>='a'&&c<='f')return c-'a'+10;if(c>='A'&&c<='F')return c-'A'+10;return -1;};
    out.clear(); out.reserve(in.size()/2);
    for(std::size_t i=0;i<in.size();i+=2){int a=nib(in[i]),b=nib(in[i+1]);if(a<0||b<0){out.clear();return false;}out.push_back(static_cast<std::uint8_t>((a<<4)|b));}
    return true;
}
bool hex_decode_string(const std::string& in, std::string& out) {
    std::vector<std::uint8_t> b; if(!hex_decode(in,b)) return false;
    out.assign(reinterpret_cast<const char*>(b.data()),b.size()); return true;
}
std::vector<std::string> split_tabs(const std::string& line) {
    std::vector<std::string> out; std::size_t pos=0;
    for(;;){auto n=line.find('\t',pos);out.push_back(line.substr(pos,n==std::string::npos?std::string::npos:n-pos));if(n==std::string::npos)break;pos=n+1;}
    return out;
}
template<class T> bool parse_int(const std::string& s,T& out){auto [p,e]=std::from_chars(s.data(),s.data()+s.size(),out);return e==std::errc{}&&p==s.data()+s.size();}

constexpr DWORD kIpcWriteTimeoutMs = 2000;
constexpr DWORD kIpcHelloTimeoutMs = 3000;
constexpr DWORD kIpcConnectTimeoutMs = 12000;

// GIPC1 is full-duplex: the command reader may have a ReadFile pending while a
// ClientCore callback sends STATE/config in the opposite direction. A named
// pipe opened for overlapped I/O must therefore use OVERLAPPED for *every*
// ReadFile/WriteFile. This prevents a synchronous read from serializing and
// starving an unrelated write on the same duplex handle.
bool wait_overlapped(HANDLE pipe, OVERLAPPED& ov, DWORD timeout_ms, DWORD& transferred) {
    const DWORD wait = WaitForSingleObject(ov.hEvent, timeout_ms);
    if (wait != WAIT_OBJECT_0) {
        // OVERLAPPED storage must stay alive until cancellation completes.
        CancelIoEx(pipe, &ov);
        WaitForSingleObject(ov.hEvent, INFINITE);
        DWORD ignored = 0;
        GetOverlappedResult(pipe, &ov, &ignored, FALSE);
        return false;
    }
    return GetOverlappedResult(pipe, &ov, &transferred, FALSE) != FALSE;
}

bool write_line(HANDLE pipe, std::mutex& mu, const std::string& line, DWORD timeout_ms = kIpcWriteTimeoutMs) {
    std::scoped_lock lock(mu);
    std::string framed = line;
    framed.push_back('\n');
    const char* p = framed.data();
    DWORD left = static_cast<DWORD>(framed.size());
    while (left) {
        HANDLE event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!event) return false;
        OVERLAPPED ov{};
        ov.hEvent = event;
        DWORD n = 0;
        BOOL ok = WriteFile(pipe, p, left, &n, &ov);
        if (!ok) {
            const DWORD e = GetLastError();
            if (e != ERROR_IO_PENDING || !wait_overlapped(pipe, ov, timeout_ms, n)) {
                CloseHandle(event);
                return false;
            }
        }
        CloseHandle(event);
        if (n == 0) return false;
        p += n;
        left -= n;
    }
    return true;
}

bool read_one(HANDLE pipe, char& c, DWORD timeout_ms) {
    HANDLE event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!event) return false;
    OVERLAPPED ov{};
    ov.hEvent = event;
    DWORD n = 0;
    BOOL ok = ReadFile(pipe, &c, 1, &n, &ov);
    if (!ok) {
        const DWORD e = GetLastError();
        if (e != ERROR_IO_PENDING || !wait_overlapped(pipe, ov, timeout_ms, n)) {
            CloseHandle(event);
            return false;
        }
    }
    CloseHandle(event);
    return n == 1;
}

bool read_line(HANDLE pipe, std::string& line) {
    line.clear();
    while (line.size() < 1024 * 1024) {
        char c = 0;
        if (!read_one(pipe, c, INFINITE)) return false;
        if (c == '\n') return true;
        if (c != '\r') line.push_back(c);
    }
    return false;
}

bool read_line_until(HANDLE pipe, std::string& line, DWORD timeout_ms, const std::atomic_bool& stop) {
    line.clear();
    const ULONGLONG deadline = GetTickCount64() + timeout_ms;
    while (line.size() < 1024 * 1024) {
        if (stop.load(std::memory_order_acquire)) return false;
        const ULONGLONG now = GetTickCount64();
        if (now >= deadline) return false;
        char c = 0;
        const DWORD remaining = static_cast<DWORD>(std::min<ULONGLONG>(deadline - now, 250));
        if (!read_one(pipe, c, remaining)) {
            if (GetLastError() == ERROR_OPERATION_ABORTED && stop.load(std::memory_order_acquire)) return false;
            continue;
        }
        if (c == '\n') return true;
        if (c != '\r') line.push_back(c);
    }
    return false;
}

std::string random_hex(std::size_t bytes) {
    std::vector<std::uint8_t> b(bytes); randombytes_buf(b.data(),b.size()); return hex_encode(b);
}
std::wstring module_path() {
    std::wstring p(32768,L'\0'); DWORD n=GetModuleFileNameW(nullptr,p.data(),static_cast<DWORD>(p.size()));
    if(!n||n>=p.size())return {};p.resize(n);return p;
}
std::string default_wintun_path() {
    const auto exe=module_path(); if(exe.empty()) return "wintun.dll";
    return narrow((std::filesystem::path(exe).parent_path()/L"wintun.dll").wstring());
}

bool verify_wintun_resource(const std::string& path, std::string& error) {
    std::ifstream in(std::filesystem::path(widen(path)), std::ios::binary);
    if (!in) { error = "Wintun runtime is missing"; return false; }
    crypto_hash_sha256_state state{};
    crypto_hash_sha256_init(&state);
    std::array<unsigned char, 64 * 1024> block{};
    while (in) {
        in.read(reinterpret_cast<char*>(block.data()), static_cast<std::streamsize>(block.size()));
        const auto n = in.gcount();
        if (n > 0) crypto_hash_sha256_update(&state, block.data(), static_cast<unsigned long long>(n));
    }
    if (!in.eof()) { error = "Could not read the Wintun runtime"; return false; }
    std::array<unsigned char, crypto_hash_sha256_BYTES> digest{};
    crypto_hash_sha256_final(&state, digest.data());
    const auto actual = hex_encode(std::span<const std::uint8_t>(digest.data(), digest.size()));
    if (actual != GLO_WINTUN_SHA256) {
        error = "Wintun runtime integrity check failed";
        return false;
    }
    return true;
}

std::optional<std::wstring> arg_value(const wchar_t* name) {
    int argc=0; LPWSTR* argv=CommandLineToArgvW(GetCommandLineW(),&argc); if(!argv)return std::nullopt;
    std::optional<std::wstring> out;
    for(int i=1;i<argc;++i){if(wcscmp(argv[i],name)==0&&i+1<argc){out=argv[i+1];break;}}
    LocalFree(argv);return out;
}
bool has_arg(const wchar_t* name) {
    int argc=0; LPWSTR* argv=CommandLineToArgvW(GetCommandLineW(),&argc); if(!argv)return false;
    bool found=false;for(int i=1;i<argc;++i)if(wcscmp(argv[i],name)==0){found=true;break;}LocalFree(argv);return found;
}

std::wstring current_user_sid() {
    HANDLE token=nullptr; if(!OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&token)) return {};
    DWORD n=0; GetTokenInformation(token,TokenUser,nullptr,0,&n); std::vector<std::uint8_t> buf(n);
    if(!n||!GetTokenInformation(token,TokenUser,buf.data(),n,&n)){CloseHandle(token);return {};}
    LPWSTR sid=nullptr; auto* tu=reinterpret_cast<TOKEN_USER*>(buf.data());
    if(!ConvertSidToStringSidW(tu->User.Sid,&sid)){CloseHandle(token);return {};}
    std::wstring out=sid; LocalFree(sid); CloseHandle(token); return out;
}

struct SecurityDescriptor {
    PSECURITY_DESCRIPTOR ptr{nullptr};
    ~SecurityDescriptor(){if(ptr)LocalFree(ptr);}
};

HANDLE create_pipe_server(const std::wstring& name, SecurityDescriptor& sd) {
    const auto sid=current_user_sid(); if(sid.empty()) return INVALID_HANDLE_VALUE;
    const std::wstring sddl=L"D:P(A;;GA;;;SY)(A;;GA;;;BA)(A;;GA;;;"+sid+L")";
    if(!ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(),SDDL_REVISION_1,&sd.ptr,nullptr)) return INVALID_HANDLE_VALUE;
    SECURITY_ATTRIBUTES sa{sizeof(sa),sd.ptr,FALSE};
    return CreateNamedPipeW(name.c_str(), PIPE_ACCESS_DUPLEX|FILE_FLAG_FIRST_PIPE_INSTANCE|FILE_FLAG_OVERLAPPED,
                            PIPE_TYPE_BYTE|PIPE_READMODE_BYTE|PIPE_WAIT|PIPE_REJECT_REMOTE_CLIENTS,
                            1, 1<<20, 1<<20, 0, &sa);
}

std::string serialize_state(const ClientSnapshot& s) {
    std::ostringstream o;
    o<<"STATE\t"<<static_cast<int>(s.state)<<'\t'<<static_cast<int>(s.phase)<<'\t'<<static_cast<int>(s.active_route)
     <<'\t'<<static_cast<int>(s.direct_reason)<<'\t'<<s.session_id<<'\t'<<static_cast<int>(s.game_id)
     <<'\t'<<(s.game_running?1:0)<<'\t'<<(s.gameplay_active?1:0)<<'\t'<<s.game_pid<<'\t'<<s.gameplay_host_ipv4
     <<'\t'<<(s.gameplay_ping_ms?std::to_string(*s.gameplay_ping_ms):"-")
     <<'\t'<<(s.gameplay_loss_pct?std::to_string(*s.gameplay_loss_pct):"-")
     <<'\t'<<s.session_remaining_seconds
     <<'\t'<<hex_encode(s.message);
    if(s.error)o<<'\t'<<s.error->generation<<'\t'<<static_cast<int>(s.error->code)<<'\t'<<hex_encode(s.error->title)<<'\t'<<hex_encode(s.error->message);
    else o<<"\t0\t0\t\t";
    return o.str();
}

bool parse_state(const std::vector<std::string>& f, ClientSnapshot& s) {
    if(f.size()<19||f[0]!="STATE")return false;
    int state=0,phase=0,route=0,reason=0,game=0,gr=0,ga=0,ecode=0;std::uint64_t sid=0,egen=0;std::uint32_t pid=0,host=0,remaining=0;
    if(!parse_int(f[1],state)||!parse_int(f[2],phase)||!parse_int(f[3],route)||!parse_int(f[4],reason)||
       !parse_int(f[5],sid)||!parse_int(f[6],game)||!parse_int(f[7],gr)||!parse_int(f[8],ga)||!parse_int(f[9],pid)||
       !parse_int(f[10],host)||!parse_int(f[13],remaining)||!parse_int(f[15],egen)||!parse_int(f[16],ecode))return false;
    s.state=static_cast<ConnectionState>(state);s.phase=static_cast<UiPhase>(phase);s.active_route=static_cast<ActiveRoute>(route);
    s.direct_reason=static_cast<DirectReason>(reason);s.session_id=sid;s.game_id=static_cast<GameId>(game);s.game_name=std::string(game_name(s.game_id));
    s.game_running=gr!=0;s.gameplay_active=ga!=0;s.game_pid=pid;s.gameplay_host_ipv4=host;
    s.gameplay_ping_ms=f[11]=="-"?std::nullopt:std::optional<double>(std::stod(f[11]));
    s.gameplay_loss_pct=f[12]=="-"?std::nullopt:std::optional<double>(std::stod(f[12]));
    s.session_remaining_seconds=remaining;
    hex_decode_string(f[14],s.message);
    if(egen!=0){ClientError e;e.generation=egen;e.code=static_cast<ClientErrorCode>(ecode);hex_decode_string(f[17],e.title);hex_decode_string(f[18],e.message);s.error=std::move(e);}else s.error.reset();
    return true;
}

struct WorkerRuntime {
    HANDLE pipe{INVALID_HANDLE_VALUE};
    std::mutex write_mu;
    std::atomic_bool stop{false};
    ClientCore core;

    bool send(const std::string& line){return write_line(pipe,write_mu,line);}
};

int worker_main(const std::wstring& pipe_name,const std::string& secret){
    SecurityDescriptor sd;
    HANDLE pipe=create_pipe_server(pipe_name,sd);
    if(pipe==INVALID_HANDLE_VALUE)return 71;
    HANDLE connect_event=CreateEventW(nullptr,TRUE,FALSE,nullptr);
    if(!connect_event){CloseHandle(pipe);return 72;}
    OVERLAPPED connect_ov{};connect_ov.hEvent=connect_event;
    BOOL connected=ConnectNamedPipe(pipe,&connect_ov);
    if(!connected){
        const DWORD e=GetLastError();
        if(e==ERROR_PIPE_CONNECTED) connected=TRUE;
        else if(e==ERROR_IO_PENDING){DWORD ignored=0;connected=wait_overlapped(pipe,connect_ov,kIpcConnectTimeoutMs,ignored)?TRUE:FALSE;}
    }
    CloseHandle(connect_event);
    if(!connected){CloseHandle(pipe);return 72;}
    WorkerRuntime rt;rt.pipe=pipe;
    if(!rt.send(std::string(kProtocolHello)+"\t"+secret)){CloseHandle(pipe);return 73;}
    std::string line;
    while(read_line(pipe,line)){
        const auto f=split_tabs(line);if(f.empty())continue;
        if(f[0]=="CONNECT"&&f.size()==15){
            int game=0,direct=0,debug=0;unsigned grace=0,port=0;
            std::string host,pin,timeout,profile_id,profile_ips;std::vector<std::uint8_t> grant;unsigned lo=0,hi=0;std::uint64_t revision=0;
            if(!parse_int(f[1],game)||!parse_int(f[2],direct)||!parse_int(f[3],debug)||!parse_int(f[4],grace)||
               !hex_decode_string(f[5],host)||!parse_int(f[6],port)||!hex_decode_string(f[7],pin)||
               !hex_decode(f[8],grant)||!hex_decode_string(f[9],timeout)||
               !hex_decode_string(f[10],profile_id)||!hex_decode_string(f[11],profile_ips)||
               !parse_int(f[12],revision)||!parse_int(f[13],lo)||!parse_int(f[14],hi))continue;
            if(game!=static_cast<int>(GameId::Roblox)){rt.send("WORKER_ERROR\t"+hex_encode("Unsupported game profile"));continue;}
            ClientOptions opt;opt.game_id=GameId::Roblox;opt.routing_policy=direct?RoutingPolicy::DirectOnly:RoutingPolicy::RelayPreferred;
            opt.dbg_log=debug!=0;opt.force_handover_grace_ms=std::clamp(grace,500u,3000u);opt.wintun_path=default_wintun_path();
            opt.game_exe_path.clear(); // privileged helper never trusts a caller-supplied executable path
            if(!direct){
                std::string wintun_error;
                if(!verify_wintun_resource(opt.wintun_path,wintun_error)){rt.send("WORKER_ERROR\t"+hex_encode(wintun_error));continue;}
                if(host.empty()||port==0||port>65535||pin.empty()||grant.empty()){rt.send("WORKER_ERROR\t"+hex_encode("Invalid session config"));continue;}
                opt.relay_host=std::move(host);opt.relay_port=static_cast<std::uint16_t>(port);opt.relay_public_key=std::move(pin);
                opt.session_grant=std::move(grant);opt.timeout_message=std::move(timeout);
                if(profile_id.empty()||revision==0||lo==0||hi>65535||lo>hi){rt.send("WORKER_ERROR\t"+hex_encode("Invalid profile"));continue;}
                opt.profile_id=std::move(profile_id);opt.gameplay_ipv4=std::move(profile_ips);opt.profile_revision=revision;
                opt.port_min=static_cast<std::uint16_t>(lo);opt.port_max=static_cast<std::uint16_t>(hi);
            }
            rt.core.connect_async(opt,[&rt](const ClientSnapshot& s){rt.send(serialize_state(s));});
        }else if(f[0]=="DEBUG"&&f.size()>=2){int on=0;if(parse_int(f[1],on))rt.core.set_debug_logging(on!=0);
        }else if(f[0]=="DISCONNECT"){
            rt.core.disconnect();rt.send(serialize_state(rt.core.snapshot()));
        }else if(f[0]=="SHUTDOWN"){
            rt.stop=true;break;
        }
    }
    rt.stop=true;rt.core.disconnect();CloseHandle(pipe);return 0;
}

} // namespace

struct NetworkWorkerClient::Impl {
    mutable std::mutex mu;
    std::mutex write_mu;
    HANDLE pipe{INVALID_HANDLE_VALUE};
    HANDLE process{nullptr};
    std::thread reader;
    std::thread starter;
    std::atomic_bool starter_running{false};
    std::atomic_bool stopping{false};
    ClientSnapshot snapshot;
    UpdateCallback callback;
    std::unique_ptr<ClientCore> direct_core;

    ~Impl(){shutdown();}
    void publish_local(const ClientSnapshot& s){UpdateCallback cb;{std::scoped_lock lock(mu);snapshot=s;cb=callback;}if(cb)cb(s);}
    bool send(const std::string& l){return pipe!=INVALID_HANDLE_VALUE&&write_line(pipe,write_mu,l);}
    bool start_worker(){
        if(pipe!=INVALID_HANDLE_VALUE)return true;
        const auto exe=module_path();if(exe.empty())return false;
        const std::wstring pipe_name=L"\\\\.\\pipe\\GLO-Net-"+widen(random_hex(16));const std::string secret=random_hex(32);
        std::wstring params=std::wstring(kWorkerFlag)+L" "+kPipeFlag+L" \""+pipe_name+L"\" "+kSecretFlag+L" "+widen(secret);
        SHELLEXECUTEINFOW sei{sizeof(sei)};sei.fMask=SEE_MASK_NOCLOSEPROCESS;sei.lpVerb=L"runas";sei.lpFile=exe.c_str();sei.lpParameters=params.c_str();sei.nShow=SW_HIDE;
        if(!ShellExecuteExW(&sei))return false;
        HANDLE client=INVALID_HANDLE_VALUE;
        const ULONGLONG deadline=GetTickCount64()+10000;
        while(!stopping.load(std::memory_order_acquire)&&GetTickCount64()<deadline){
            if(WaitForSingleObject(sei.hProcess,0)==WAIT_OBJECT_0)break;
            if(WaitNamedPipeW(pipe_name.c_str(),250)){
                client=CreateFileW(pipe_name.c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_EXISTING,FILE_FLAG_OVERLAPPED,nullptr);
                if(client!=INVALID_HANDLE_VALUE)break;
            }
            Sleep(25);
        }
        if(client==INVALID_HANDLE_VALUE){
            if(WaitForSingleObject(sei.hProcess,0)==WAIT_TIMEOUT)TerminateProcess(sei.hProcess,74);
            CloseHandle(sei.hProcess);return false;
        }
        std::string hello;
        if(!read_line_until(client,hello,3000,stopping)){
            CloseHandle(client);if(WaitForSingleObject(sei.hProcess,0)==WAIT_TIMEOUT)TerminateProcess(sei.hProcess,74);CloseHandle(sei.hProcess);return false;
        }
        const auto f=split_tabs(hello);
        if(f.size()!=2||f[0]!=kProtocolHello||f[1]!=secret||stopping.load(std::memory_order_acquire)){
            CloseHandle(client);if(WaitForSingleObject(sei.hProcess,0)==WAIT_TIMEOUT)TerminateProcess(sei.hProcess,74);CloseHandle(sei.hProcess);return false;
        }
        pipe=client;process=sei.hProcess;
        reader=std::thread([this]{reader_loop();});return true;
    }
    void reader_loop(){
        std::string line;
        while(!stopping&&read_line(pipe,line)){
            const auto f=split_tabs(line);if(f.empty())continue;
            if(f[0]=="STATE"){
                ClientSnapshot s;if(parse_state(f,s))publish_local(s);
            }else if(f[0]=="WORKER_ERROR"){
                ClientSnapshot s;{std::scoped_lock lock(mu);s=snapshot;}s.state=ConnectionState::Disconnected;s.phase=UiPhase::Disconnected;
                std::string msg;if(f.size()>1)hex_decode_string(f[1],msg);ClientError e;e.code=ClientErrorCode::WorkerStartFailed;e.generation=1;e.title="GLO - Network worker";e.message=msg;s.error=e;s.message="Not connected";publish_local(s);
            }
        }
        if(!stopping){ClientSnapshot s;{std::scoped_lock lock(mu);s=snapshot;}s.state=ConnectionState::Disconnected;s.phase=UiPhase::Disconnected;s.message="Network worker stopped";publish_local(s);}
    }
    void shutdown(){
        stopping.store(true,std::memory_order_release);
        if(starter.joinable()&&starter.get_id()!=std::this_thread::get_id())starter.join();
        if(direct_core){direct_core->disconnect();direct_core.reset();}
        if(pipe!=INVALID_HANDLE_VALUE){send("SHUTDOWN");CancelIoEx(pipe,nullptr);CloseHandle(pipe);pipe=INVALID_HANDLE_VALUE;}
        if(reader.joinable()&&reader.get_id()!=std::this_thread::get_id())reader.join();
        if(process){
            if(WaitForSingleObject(process,2500)==WAIT_TIMEOUT){TerminateProcess(process,75);WaitForSingleObject(process,500);}
            CloseHandle(process);process=nullptr;
        }
    }
};

NetworkWorkerClient::NetworkWorkerClient():impl_(std::make_unique<Impl>()){}
NetworkWorkerClient::~NetworkWorkerClient(){shutdown();}

bool NetworkWorkerClient::connect_async(const ClientOptions& options,UpdateCallback cb){
    if(options.routing_policy==RoutingPolicy::DirectOnly){
        impl_->shutdown();impl_->stopping.store(false,std::memory_order_release);impl_->direct_core=std::make_unique<ClientCore>();return impl_->direct_core->connect_async(options,std::move(cb));
    }
    if(impl_->direct_core){impl_->direct_core->disconnect();impl_->direct_core.reset();}
    if(impl_->starter.joinable()) {
        if(impl_->starter_running.load(std::memory_order_acquire)) {
            // Idempotent duplicate Connect while UAC/IPC startup is still in flight.
            // Never join a live starter on the Win32 UI thread.
            return true;
        }
        impl_->starter.join();
    }
    impl_->stopping.store(false,std::memory_order_release);
    {std::scoped_lock lock(impl_->mu);impl_->callback=std::move(cb);impl_->snapshot=ClientSnapshot{};impl_->snapshot.state=ConnectionState::Connecting;impl_->snapshot.message="Starting network worker...";}
    const std::string command="CONNECT\t"+std::to_string(static_cast<int>(options.game_id))+"\t0\t"+(options.dbg_log?"1":"0")+"\t"+std::to_string(std::clamp(options.force_handover_grace_ms,500u,3000u))+"\t"+hex_encode(options.relay_host)+"\t"+std::to_string(options.relay_port)+"\t"+hex_encode(options.relay_public_key)+"\t"+hex_encode(options.session_grant)+"\t"+hex_encode(options.timeout_message)+"\t"+hex_encode(options.profile_id)+"\t"+
        hex_encode(options.gameplay_ipv4)+"\t"+std::to_string(options.profile_revision)+"\t"+
        std::to_string(options.port_min)+"\t"+std::to_string(options.port_max);
    try{
        impl_->starter_running.store(true,std::memory_order_release);
        impl_->starter=std::thread([impl=impl_.get(),command]{
            struct Done { std::atomic_bool& flag; ~Done(){flag.store(false,std::memory_order_release);} } done{impl->starter_running};
            if(!impl->start_worker()){
                if(impl->stopping.load(std::memory_order_acquire))return;
                ClientSnapshot s;s.state=ConnectionState::Disconnected;s.phase=UiPhase::Disconnected;s.message="Not connected";ClientError e;e.code=ClientErrorCode::WorkerStartFailed;e.generation=1;e.title="GLO - Administrator permission required";e.message="GLO needs administrator permission only while game routing is enabled. Permission was cancelled or the network worker could not start.";s.error=e;impl->publish_local(s);return;
            }
            if(impl->stopping.load(std::memory_order_acquire))return;
            if(!impl->send(command)){
                ClientSnapshot s;s.state=ConnectionState::Disconnected;s.phase=UiPhase::Disconnected;s.message="Not connected";ClientError e;e.code=ClientErrorCode::WorkerStartFailed;e.generation=1;e.title="GLO - Network worker";e.message="The elevated network worker started but its IPC channel closed before the connection could begin.";s.error=e;impl->publish_local(s);
            }
        });
    }catch(...){
        impl_->starter_running.store(false,std::memory_order_release);
        ClientSnapshot s;s.state=ConnectionState::Disconnected;s.phase=UiPhase::Disconnected;s.message="Not connected";ClientError e;e.code=ClientErrorCode::WorkerStartFailed;e.generation=1;e.title="GLO - Network worker";e.message="GLO could not start the network-worker launcher thread.";s.error=e;impl_->publish_local(s);return false;
    }
    return true;
}
void NetworkWorkerClient::disconnect(){if(impl_->direct_core){impl_->direct_core->disconnect();return;}impl_->send("DISCONNECT");}
void NetworkWorkerClient::shutdown(){if(impl_)impl_->shutdown();}
void NetworkWorkerClient::set_debug_logging(bool enabled) noexcept {if(impl_->direct_core)impl_->direct_core->set_debug_logging(enabled);else impl_->send(std::string("DEBUG\t")+(enabled?"1":"0"));}
ClientSnapshot NetworkWorkerClient::snapshot() const {if(impl_->direct_core)return impl_->direct_core->snapshot();std::scoped_lock lock(impl_->mu);return impl_->snapshot;}

std::optional<int> run_network_worker_if_requested(){
    if(!has_arg(kWorkerFlag))return std::nullopt;
    const auto pipe=arg_value(kPipeFlag),secret=arg_value(kSecretFlag);if(!pipe||!secret)return 64;
    const auto sec=narrow(*secret);if(sec.size()!=64)return 64;
    return worker_main(*pipe,sec);
}

}  // namespace glo
