#include "glo/profile_hosts.hpp"
#include <charconv>
#include <set>
#include <array>
namespace glo {
std::string bundled_profile_hosts(std::string_view game) {
 return game=="roblox" ? "128.116.46.33/32,128.116.50.33/32,128.116.54.33/32,128.116.97.33/32" : "";
}
bool parse_profile_hosts(std::string_view csv, std::vector<std::uint32_t>& out, std::string& error) {
 out.clear();error.clear();if(csv.empty()||csv.size()>2048){error="Missing/oversized gameplay IP profile";return false;}
 std::set<std::uint32_t> seen;
 for(std::size_t pos=0;pos<csv.size();) {
  const auto end=csv.find(',',pos);auto value=csv.substr(pos,end==std::string_view::npos?csv.size()-pos:end-pos);
  if(value.ends_with("/32")) value.remove_suffix(3);
  if(value.empty()||value.size()>15){error="Invalid /32 host";return false;}
  std::array<unsigned,4> octets{};std::size_t offset=0;
  for(std::size_t i=0;i<4;++i){auto dot=value.find('.',offset);if((i<3&&dot==std::string_view::npos)||(i==3&&dot!=std::string_view::npos)){error="Invalid IPv4 profile";return false;}
   const auto word=value.substr(offset,(dot==std::string_view::npos?value.size():dot)-offset);
   if(word.empty()||word.size()>3||(word.size()>1&&word.front()=='0')){error="Invalid IPv4 octet";return false;}
   unsigned x=0;auto r=std::from_chars(word.data(),word.data()+word.size(),x);if(r.ec!=std::errc{}||r.ptr!=word.data()+word.size()||x>255){error="Invalid IPv4 octet";return false;}octets[i]=x;offset=dot+1;
  }
  const unsigned a=octets[0],b=octets[1];
  if(a==0||a==10||a==127||a>=224||(a==169&&b==254)||(a==172&&b>=16&&b<=31)||(a==192&&b==168)||(a==100&&b>=64&&b<=127)) {error="Non-public routing host";return false;}
  const auto ip=(a<<24)|(b<<16)|(octets[2]<<8)|octets[3];
  if(!seen.insert(ip).second){error="Duplicate gameplay host";return false;}
  out.push_back(ip);if(out.size()>32){error="Too many gameplay hosts";return false;}
  if(end==std::string_view::npos) break;
  pos=end+1;if(pos==csv.size()){error="Empty gameplay host";return false;}
 }
 return true;
}
}
