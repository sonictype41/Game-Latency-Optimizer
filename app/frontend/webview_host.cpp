#include "webview_host.hpp"
#include <WebView2.h>
#include <windows.h>
#include <wincrypt.h>
#include <algorithm>
#include <atomic>
#include <memory>
#include <string>
#include <utility>

namespace {
template<typename I> class HandlerBase : public I {
    std::atomic<ULONG> refs_{1};
public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** out) override {
        if(!out) return E_POINTER;
        *out=nullptr;
        if(iid==IID_IUnknown || iid==__uuidof(I)) { *out=static_cast<I*>(this);AddRef();return S_OK; }
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs_; }
    ULONG STDMETHODCALLTYPE Release() override { ULONG r=--refs_;if(!r) delete this;return r; }
protected: virtual ~HandlerBase()=default;
};
std::wstring from_utf8(const std::string& s) {
    int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),static_cast<int>(s.size()),nullptr,0);
    if(n<=0) return {};
    std::wstring out(static_cast<size_t>(n),L'\0');
    MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),static_cast<int>(s.size()),out.data(),n);
    return out;
}
}

class WebviewHost::Internal {
public:
    HWND parent{};
    ICoreWebView2Controller* controller{};
    ICoreWebView2* view{};
    HMODULE loader{};
    bool* ready{};
    bool closing{false};
    std::filesystem::path folder;
    MessageCallback callback;
    EventRegistrationToken msg_token{}, nav_token{};
    bool msg_registered{false},nav_registered{false};

    struct Navigation final:HandlerBase<ICoreWebView2NavigationStartingEventHandler> {
        HRESULT STDMETHODCALLTYPE Invoke(ICoreWebView2*,ICoreWebView2NavigationStartingEventArgs* args) override {
            LPWSTR uri=nullptr;
            if(SUCCEEDED(args->get_Uri(&uri))){
                std::wstring v=uri?uri:L""; if(uri)CoTaskMemFree(uri);
                if(v!=L"https://glo.local/index.html" && v.rfind(L"https://glo.local/",0)!=0)args->put_Cancel(TRUE);
            }else args->put_Cancel(TRUE);
            return S_OK;
        }
    };
    struct Message final:HandlerBase<ICoreWebView2WebMessageReceivedEventHandler> {
        Internal* owner;
        explicit Message(Internal* p):owner(p){}
        HRESULT STDMETHODCALLTYPE Invoke(ICoreWebView2*,ICoreWebView2WebMessageReceivedEventArgs* args) override {
            if(owner->closing) return S_OK;
            LPWSTR src=nullptr;
            if(FAILED(args->get_Source(&src)))return S_OK;
            std::wstring origin=src?src:L"";if(src)CoTaskMemFree(src);
            if(origin.rfind(L"https://glo.local/",0)!=0)return S_OK;
            LPWSTR json=nullptr;
            if(SUCCEEDED(args->get_WebMessageAsJson(&json))&&json){
                std::wstring text(json);CoTaskMemFree(json);
                if(text.size()<=16384&&owner->callback)owner->callback(text);
            }
            return S_OK;
        }
    };
    struct Controller final:HandlerBase<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler> {
        Internal* owner;
        explicit Controller(Internal* p):owner(p){}
        HRESULT STDMETHODCALLTYPE Invoke(HRESULT result,ICoreWebView2Controller* ctrl) override {
            if(owner->closing)return S_OK;
            if(FAILED(result)||!ctrl)return S_OK;
            owner->controller=ctrl;ctrl->AddRef();
            if(FAILED(ctrl->get_CoreWebView2(&owner->view)))return S_OK;
            ICoreWebView2Settings* settings=nullptr;
            if(SUCCEEDED(owner->view->get_Settings(&settings))&&settings){
                settings->put_AreDefaultContextMenusEnabled(FALSE);
                settings->put_AreDevToolsEnabled(FALSE);
                settings->put_AreHostObjectsAllowed(FALSE);
                settings->put_IsWebMessageEnabled(TRUE);
                settings->Release();
            }
            auto* msg=new Message(owner);
            if(SUCCEEDED(owner->view->add_WebMessageReceived(msg,&owner->msg_token)))owner->msg_registered=true;
            msg->Release();
            auto* nav=new Navigation();
            if(SUCCEEDED(owner->view->add_NavigationStarting(nav,&owner->nav_token)))owner->nav_registered=true;
            nav->Release();
            ICoreWebView2_3* view3=nullptr;
            if(SUCCEEDED(owner->view->QueryInterface(IID_PPV_ARGS(&view3)))&&view3){
                const HRESULT mapping=view3->SetVirtualHostNameToFolderMapping(L"glo.local",owner->folder.c_str(),COREWEBVIEW2_HOST_RESOURCE_ACCESS_KIND_DENY_CORS);
                view3->Release();
                if(SUCCEEDED(mapping)){
                    RECT r{};GetClientRect(owner->parent,&r);ctrl->put_Bounds(r);ctrl->put_IsVisible(TRUE);
                    owner->view->Navigate(L"https://glo.local/index.html");
                    if(owner->ready)*owner->ready=true;
                }
            }
            return S_OK;
        }
    };
    struct Environment final:HandlerBase<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler> {
        Internal* owner;
        explicit Environment(Internal* p):owner(p){}
        HRESULT STDMETHODCALLTYPE Invoke(HRESULT hr,ICoreWebView2Environment* env) override {
            if(owner->closing||FAILED(hr)||!env)return S_OK;
            auto* cb=new Controller(owner);
            env->CreateCoreWebView2Controller(owner->parent,cb);
            cb->Release();return S_OK;
        }
    };
    void shutdown() {
        closing=true;
        if(view){if(msg_registered)view->remove_WebMessageReceived(msg_token);if(nav_registered)view->remove_NavigationStarting(nav_token);view->Release();view=nullptr;}
        if(controller){controller->Close();controller->Release();controller=nullptr;}
        if(loader){FreeLibrary(loader);loader=nullptr;}
    }
};

WebviewHost::~WebviewHost(){stop();}
bool WebviewHost::start(HWND hwnd,const std::filesystem::path& folder,MessageCallback cb,std::wstring& error){
    stop();
    if(!std::filesystem::is_regular_file(folder/L"index.html")){error=L"GLO UI assets were not found. Please reinstall the app.";return false;}
    internal_=new Internal;
    internal_->parent=hwnd;internal_->folder=folder;internal_->callback=std::move(cb);internal_->ready=&ready_;
    internal_->loader=LoadLibraryExW(L"WebView2Loader.dll",nullptr,LOAD_LIBRARY_SEARCH_APPLICATION_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if(!internal_->loader){error=L"WebView2Loader.dll is missing. Please reinstall GLO.";stop();return false;}
    using CreateFn=HRESULT(STDAPICALLTYPE*)(PCWSTR,PCWSTR,ICoreWebView2EnvironmentOptions*,ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler*);
    auto fn=reinterpret_cast<CreateFn>(GetProcAddress(internal_->loader,"CreateCoreWebView2EnvironmentWithOptions"));
    if(!fn){error=L"Invalid WebView2 loader.";stop();return false;}
    using VersionFn=HRESULT(STDAPICALLTYPE*)(PCWSTR,LPWSTR*);
    auto version_fn=reinterpret_cast<VersionFn>(GetProcAddress(internal_->loader,"GetAvailableCoreWebView2BrowserVersionString"));
    LPWSTR version=nullptr;
    if(!version_fn||FAILED(version_fn(nullptr,&version))||!version){error=L"Microsoft WebView2 Evergreen Runtime is not installed.";stop();return false;}
    CoTaskMemFree(version);
    wchar_t local[MAX_PATH]{};
    if(!GetEnvironmentVariableW(L"LOCALAPPDATA",local,MAX_PATH)) {error=L"LOCALAPPDATA is unavailable.";stop();return false;}
    auto user=std::filesystem::path(local)/L"GLO"/L"WebView2";
    std::error_code ec;std::filesystem::create_directories(user,ec);
    if(ec){error=L"Cannot create WebView2 profile folder.";stop();return false;}
    auto* cb_env=new Internal::Environment(internal_);
    HRESULT hr=fn(nullptr,user.c_str(),nullptr,cb_env);
    cb_env->Release();
    if(FAILED(hr)){error=L"WebView2 Evergreen Runtime is required. Install it from Microsoft and restart GLO.";stop();return false;}
    return true;
}
void WebviewHost::resize() const {if(internal_&&internal_->controller){RECT r{};GetClientRect(internal_->parent,&r);internal_->controller->put_Bounds(r);}}
void WebviewHost::post_json(const std::string& json) const {if(!internal_||!internal_->view)return;const auto wide=from_utf8(json);if(!wide.empty())internal_->view->PostWebMessageAsJson(wide.c_str());}
void WebviewHost::stop(){ready_=false;if(internal_){internal_->shutdown();delete internal_;internal_=nullptr;}}
