#include "glo/session_config.hpp"
#include "glo/cli_policy.hpp"
#include "glo/profile_hosts.hpp"

#include <charconv>
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace glo {
namespace {
class FlatJson {
public:
    enum class Kind { String, Number, Boolean, Null };
    struct Value { std::string text; Kind kind; };
private:
    std::map<std::string,Value> values_;
    static void utf8(std::string& out,unsigned v){
        if(v<128)out+=static_cast<char>(v);
        else if(v<2048){out+=static_cast<char>(0xc0|(v>>6));out+=static_cast<char>(0x80|(v&63));}
        else if(v<65536){out+=static_cast<char>(0xe0|(v>>12));out+=static_cast<char>(0x80|((v>>6)&63));out+=static_cast<char>(0x80|(v&63));}
        else{out+=static_cast<char>(0xf0|(v>>18));out+=static_cast<char>(0x80|((v>>12)&63));out+=static_cast<char>(0x80|((v>>6)&63));out+=static_cast<char>(0x80|(v&63));}
    }
public:
    explicit FlatJson(std::string_view s){
        if(s.size()>65536)throw std::runtime_error("Config is too large");
        std::size_t i=0; auto fail=[](){throw std::runtime_error("Invalid config JSON");};
        auto ws=[&](){while(i<s.size()&&(s[i]==' '||s[i]=='\n'||s[i]=='\r'||s[i]=='\t'))++i;};
        auto expect=[&](char c){ws();if(i==s.size()||s[i++]!=c)fail();};
        auto hex4=[&](){unsigned n=0;for(int k=0;k<4;++k){if(i==s.size())fail();char c=s[i++];unsigned d=c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:c>='A'&&c<='F'?c-'A'+10:16;if(d==16)fail();n=n*16+d;}return n;};
        auto str=[&](){expect('"');std::string out;bool closed=false;while(i<s.size()){unsigned char c=s[i++];if(c=='"'){closed=true;break;}if(c<32)fail();if(c!='\\'){out+=static_cast<char>(c);continue;}if(i==s.size())fail();char e=s[i++];switch(e){case '"':case '\\':case '/':out+=e;break;case 'b':out+='\b';break;case 'f':out+='\f';break;case 'n':out+='\n';break;case 'r':out+='\r';break;case 't':out+='\t';break;case 'u':{unsigned v=hex4();if(v>=0xd800&&v<=0xdbff){if(i+2>s.size()||s.substr(i,2)!="\\u")fail();i+=2;unsigned lo=hex4();if(lo<0xdc00||lo>0xdfff)fail();v=0x10000+((v-0xd800)<<10)+(lo-0xdc00);}else if(v>=0xdc00&&v<=0xdfff)fail();utf8(out,v);break;}default:fail();}}if(!closed)fail();return out;};
        expect('{');ws();if(i<s.size()&&s[i]=='}')++i;else for(;;){
            auto key=str();expect(':');ws();Value value;
            if(i<s.size()&&s[i]=='"')value={str(),Kind::String};
            else{
                auto value_start=i;while(i<s.size()&&s[i]!=','&&s[i]!='}'&&s[i]!=' '&&s[i]!='\r'&&s[i]!='\n'&&s[i]!='\t')++i;
                value.text=std::string(s.substr(value_start,i-value_start));
                if(value.text=="true"||value.text=="false")value.kind=Kind::Boolean;
                else if(value.text=="null")value.kind=Kind::Null;
                else{std::int64_t n{};auto r=std::from_chars(value.text.data(),value.text.data()+value.text.size(),n);if(value.text.empty()||r.ec!=std::errc()||r.ptr!=value.text.data()+value.text.size())fail();value.kind=Kind::Number;}
            }
            if(!values_.emplace(key,std::move(value)).second)throw std::runtime_error("Duplicate config field: "+key);
            ws();if(i<s.size()&&s[i]=='}'){++i;break;}expect(',');
        }
        ws();if(i!=s.size())fail();
    }
    const std::map<std::string,Value>& values()const{return values_;}
    std::string require_string(const char* key)const{auto it=values_.find(key);if(it==values_.end())throw std::runtime_error(std::string("Missing config field: ")+key);if(it->second.kind!=Kind::String)throw std::runtime_error(std::string("Invalid string config field: ")+key);return it->second.text;}
    std::string optional_string(const char* key)const{auto it=values_.find(key);if(it==values_.end())return {};if(it->second.kind!=Kind::String)throw std::runtime_error(std::string("Invalid string config field: ")+key);return it->second.text;}
    std::int64_t number(const char* key)const{auto it=values_.find(key);if(it==values_.end())throw std::runtime_error(std::string("Missing config field: ")+key);if(it->second.kind!=Kind::Number)throw std::runtime_error(std::string("Invalid numeric config field: ")+key);std::int64_t n{};auto r=std::from_chars(it->second.text.data(),it->second.text.data()+it->second.text.size(),n);if(r.ec!=std::errc()||r.ptr!=it->second.text.data()+it->second.text.size())throw std::runtime_error(std::string("Invalid numeric config field: ")+key);return n;}
    bool boolean(const char* key)const{auto it=values_.find(key);if(it==values_.end())throw std::runtime_error(std::string("Missing API field: ")+key);if(it->second.kind!=Kind::Boolean)throw std::runtime_error(std::string("Invalid boolean API field: ")+key);return it->second.text=="true";}
};

std::string json_quote(std::string_view in){
    static constexpr char hex[]="0123456789abcdef";
    std::string out;out.reserve(in.size()+2);out+='"';
    for(unsigned char c:in){
        switch(c){case '"':out+="\\\"";break;case '\\':out+="\\\\";break;case '\b':out+="\\b";break;case '\f':out+="\\f";break;case '\n':out+="\\n";break;case '\r':out+="\\r";break;case '\t':out+="\\t";break;default:if(c<0x20){out+="\\u00";out+=hex[(c>>4)&15];out+=hex[c&15];}else out+=static_cast<char>(c);}
    }
    out+='"';return out;
}

bool hex_decode(std::string_view in,std::vector<std::uint8_t>& out){
    if(in.empty()||(in.size()&1u))return false;auto nib=[](char c)->int{if(c>='0'&&c<='9')return c-'0';if(c>='a'&&c<='f')return c-'a'+10;if(c>='A'&&c<='F')return c-'A'+10;return -1;};out.clear();out.reserve(in.size()/2);for(std::size_t i=0;i<in.size();i+=2){int a=nib(in[i]),b=nib(in[i+1]);if(a<0||b<0){out.clear();return false;}out.push_back(static_cast<std::uint8_t>((a<<4)|b));}return true;
}
std::optional<GameId> game_id(std::string_view key){for(const auto& p:kGameProfiles)if(p.key==key&&p.supported)return p.id;return std::nullopt;}
}

bool parse_session_config_json(std::string_view json,SessionConfig& out,std::string& error){
    try{
        FlatJson r(json);
        static const std::set<std::string> allowed={"schema","relay","relay_name","relay_public_key","game","grant","timeout_message","profile_id","profile_revision","gameplay_ipv4","port_min","port_max"};
        for(const auto& [key,_]:r.values())if(!allowed.contains(key))throw std::runtime_error("Unsupported config field: "+key);
        if(r.number("schema")!=2)throw std::runtime_error("Unsupported config schema");
        const auto relay=r.require_string("relay"),relay_name=r.optional_string("relay_name"),pin=r.require_string("relay_public_key"),game=r.require_string("game"),grant=r.require_string("grant"),timeout=r.optional_string("timeout_message");
        if(!cli::hex_key(pin))throw std::runtime_error("relay_public_key must be exactly 64 hexadecimal characters");
        auto gid=game_id(game);if(!gid)throw std::runtime_error("Unsupported game profile");
        std::vector<std::uint8_t> raw;if(!hex_decode(grant,raw)||raw.size()!=152)throw std::runtime_error("grant must be one GSK2 ticket (152 bytes hex)");
        if(relay_name.size()>96)throw std::runtime_error("relay_name is too long");
        if(timeout.size()>512)throw std::runtime_error("timeout_message is too long");
        const auto profile_id=r.require_string("profile_id");
        if(profile_id.empty()||profile_id.size()>48)throw std::runtime_error("Invalid profile_id");
        const auto hosts=r.require_string("gameplay_ipv4");std::vector<std::uint32_t> validated;std::string host_error;
        if(!parse_profile_hosts(hosts,validated,host_error))throw std::runtime_error(host_error);
        const auto revision=r.number("profile_revision"), lo=r.number("port_min"),hi=r.number("port_max");
        if(revision<1||lo<1||hi>65535||lo>hi)throw std::runtime_error("Invalid profile revision or UDP ports");
        auto [host,port]=cli::endpoint(relay);SessionConfig parsed;
        parsed.profile_id=profile_id;parsed.profile_revision=static_cast<std::uint64_t>(revision);
        parsed.gameplay_ipv4=hosts;parsed.port_min=static_cast<std::uint16_t>(lo);parsed.port_max=static_cast<std::uint16_t>(hi);parsed.relay_host=std::move(host);parsed.relay_port=static_cast<std::uint16_t>(port);parsed.relay_public_key=pin;parsed.relay_name=relay_name;parsed.game_id=*gid;parsed.grant=std::move(raw);if(!timeout.empty())parsed.timeout_message=timeout;out=std::move(parsed);error.clear();return true;
    }catch(const std::exception& e){error=e.what();return false;}
}

bool unwrap_session_config_api_response(std::string_view response,std::string& config_json,std::string& error){
    try{
        FlatJson r(response);
        static const std::set<std::string> allowed={"ok","schema","relay","relay_name","relay_public_key","game","grant","timeout_message","profile_id","profile_revision","gameplay_ipv4","port_min","port_max"};
        for(const auto& [key,_]:r.values())if(!allowed.contains(key))throw std::runtime_error("Unsupported API response field: "+key);
        if(!r.boolean("ok"))throw std::runtime_error("GLO API response did not indicate success");
        const auto schema=r.number("schema");
        const auto relay=r.require_string("relay"),relay_name=r.optional_string("relay_name"),pin=r.require_string("relay_public_key"),game=r.require_string("game"),grant=r.require_string("grant"),timeout=r.optional_string("timeout_message");
        std::ostringstream out;
        out<<"{\"schema\":"<<schema<<",\"relay\":"<<json_quote(relay);if(!relay_name.empty())out<<",\"relay_name\":"<<json_quote(relay_name);out<<",\"relay_public_key\":"<<json_quote(pin)<<",\"game\":"<<json_quote(game)<<",\"grant\":"<<json_quote(grant);
        out<<",\"profile_id\":"<<json_quote(r.require_string("profile_id"))
           <<",\"profile_revision\":"<<r.number("profile_revision")
           <<",\"gameplay_ipv4\":"<<json_quote(r.require_string("gameplay_ipv4"))
           <<",\"port_min\":"<<r.number("port_min")<<",\"port_max\":"<<r.number("port_max");
        if(!timeout.empty())out<<",\"timeout_message\":"<<json_quote(timeout);
        out<<"}";
        config_json=out.str();error.clear();return true;
    }catch(const std::exception& e){config_json.clear();error=e.what();return false;}
}

}
