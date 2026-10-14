#pragma once
#include <algorithm>
#include <charconv>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
namespace glo {
using UdpPortRange=std::pair<std::uint16_t,std::uint16_t>;
using UdpPortRanges=std::vector<UdpPortRange>;
inline bool parse_udp_port_rules(std::string_view text,UdpPortRanges& out){
    out.clear();if(text.empty()||text.size()>512)return false;
    auto number=[](std::string_view s,unsigned& n)->bool{
        while(!s.empty()&&(s.front()==' '||s.front()=='\t'))s.remove_prefix(1);
        while(!s.empty()&&(s.back()==' '||s.back()=='\t'))s.remove_suffix(1);
        if(s.empty())return false;
        auto r=std::from_chars(s.data(),s.data()+s.size(),n);
        return r.ec==std::errc()&&r.ptr==s.data()+s.size()&&n>=1&&n<=65535;
    };
    std::size_t pos=0;unsigned count=0;
    while(pos<text.size()){
        auto end=text.find(',',pos);if(end==std::string_view::npos)end=text.size();
        auto item=text.substr(pos,end-pos);auto dash=item.find('-');
        unsigned lo=0,hi=0;
        if(dash==std::string_view::npos){if(!number(item,lo))return false;hi=lo;}
        else if(item.find('-',dash+1)!=std::string_view::npos||!number(item.substr(0,dash),lo)||!number(item.substr(dash+1),hi))return false;
        if(lo>hi||++count>32)return false;
        out.emplace_back(static_cast<std::uint16_t>(lo),static_cast<std::uint16_t>(hi));
        pos=end+1;if(end==text.size())break;
        if(pos==text.size())return false;
    }
    std::sort(out.begin(),out.end());UdpPortRanges normalized;
    for(auto [a,b]:out){if(!normalized.empty()&&unsigned(a)<=unsigned(normalized.back().second)+1)
        normalized.back().second=std::max(normalized.back().second,b);
        else normalized.emplace_back(a,b);}
    out=std::move(normalized);return !out.empty();
}
inline std::string udp_port_rules_text(const UdpPortRanges& ranges){
    std::string result;for(auto [a,b]:ranges){if(!result.empty())result+=',';result+=std::to_string(a);if(a!=b){result+='-';result+=std::to_string(b);}}
    return result;
}
inline bool udp_port_allowed(const UdpPortRanges& ranges,std::uint16_t port){
    for(auto [a,b]:ranges)if(port>=a&&port<=b)return true;return false;
}
}
