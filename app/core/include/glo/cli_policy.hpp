#pragma once
#include <algorithm>
#include <charconv>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace glo::cli {
inline constexpr char build_identity[] = "GLO v" GLO_VERSION;
struct Spec { const char* name; bool value; };
inline constexpr Spec specs[] = {
    {"--config", true},
    {"--uri", true},
    {"--force-handover-grace-ms", true},
    {"--force-direct", false},
    {"--debug", false}
};
inline unsigned number(const std::string& text, unsigned min, unsigned max) {
    unsigned n{};
    auto [p, ec] = std::from_chars(text.data(), text.data()+text.size(), n);
    if (ec != std::errc{} || p != text.data()+text.size() || n < min || n > max)
        throw std::runtime_error("Invalid numeric value: " + text);
    return n;
}
inline bool hex_key(const std::string& key) {
    return key.size()==64 && std::all_of(key.begin(),key.end(),[](unsigned char c) {
        return (c>='0'&&c<='9')||(c>='a'&&c<='f')||(c>='A'&&c<='F');
    });
}
inline std::pair<std::string,unsigned> endpoint(const std::string& text, unsigned default_port=43170) {
    auto c=text.find(':');
    std::string host=text.substr(0,c);
    if(host.empty() || host.size()>253 || host.front()=='.' || host.back()=='.' ||
       !std::all_of(host.begin(),host.end(),[](unsigned char v){return (v>='a'&&v<='z')||(v>='A'&&v<='Z')||(v>='0'&&v<='9')||v=='.'||v=='-';}))
        throw std::runtime_error("Expected an IPv4 address or DNS hostname: " + text);
    unsigned port=c==std::string::npos?default_port:number(text.substr(c+1),1,65535);
    return {host,port};
}
using Values=std::map<std::string,std::string>;
inline Values parse(const std::vector<std::string>& args) {
    Values result;
    for(std::size_t i=0;i<args.size();++i) {
        std::string name=args[i];
        if(name=="--dbg-log") name="--debug";
        const Spec* spec=nullptr;
        for(const auto& candidate:specs) if(name==candidate.name){spec=&candidate;break;}
        if(!spec) throw std::runtime_error("Unknown argument: "+name);
        if(result.contains(name)) throw std::runtime_error("Duplicate argument: "+name);
        std::string value;
        if(spec->value) {
            if(i+1==args.size() || args[i+1].empty() || args[i+1].rfind("--",0)==0)
                throw std::runtime_error("Missing value for "+name);
            value=args[++i];
        }
        if(name=="--force-handover-grace-ms") number(value,500,3000);
        result.emplace(name,value);
    }
    return result;
}
}
