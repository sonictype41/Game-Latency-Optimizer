#include "glo/profile_hosts.hpp"
#include <charconv>
#include <array>
namespace glo {
std::string bundled_profile_hosts(std::string_view game) {
 return game=="roblox" ? "128.116.46.33/32,128.116.50.33/32,128.116.54.33/32,128.116.97.33/32" : "";
}
bool parse_profile_hosts(std::string_view csv,std::vector<Ipv4Cidr>& out,std::string& error) {
 out.clear();error.clear();if(csv.empty()||csv.size()>2048){error="Missing/oversized gameplay IP profile";return false;}
 auto invalid=[&](std::string msg){error=std::move(msg);out.clear();return false;};
 for(std::size_t pos=0;pos<csv.size();) {
  const auto end=csv.find(',',pos);auto value=csv.substr(pos,end==std::string_view::npos?csv.size()-pos:end-pos);
  unsigned prefix=32;
  const auto slash=value.find('/');
  if(slash!=std::string_view::npos){
   auto digits=value.substr(slash+1);if(digits.empty()||digits.size()>2)return invalid("Invalid CIDR prefix");
   auto parsed=std::from_chars(digits.data(),digits.data()+digits.size(),prefix);
   if(parsed.ec!=std::errc{}||parsed.ptr!=digits.data()+digits.size()||prefix<16||prefix>32)return invalid("CIDR prefix must be /16 through /32");
   value=value.substr(0,slash);
  }
  if(value.empty()||value.size()>15)return invalid("Invalid IPv4 network");
  std::array<unsigned,4> octets{};std::size_t offset=0;
  for(std::size_t i=0;i<4;++i){auto dot=value.find('.',offset);if((i<3&&dot==std::string_view::npos)||(i==3&&dot!=std::string_view::npos))return invalid("Invalid IPv4 network");
   const auto word=value.substr(offset,(dot==std::string_view::npos?value.size():dot)-offset);
   if(word.empty()||word.size()>3||(word.size()>1&&word.front()=='0'))return invalid("Invalid IPv4 octet");
   unsigned x=0;auto parsed=std::from_chars(word.data(),word.data()+word.size(),x);
   if(parsed.ec!=std::errc{}||parsed.ptr!=word.data()+word.size()||x>255)return invalid("Invalid IPv4 octet");octets[i]=x;offset=dot+1;
  }
  const unsigned a=octets[0],b=octets[1];
  if(a==0||a==10||a==127||a>=224||(a==169&&b==254)||(a==172&&b>=16&&b<=31)||(a==192&&b==168)||(a==100&&b>=64&&b<=127))return invalid("Non-public routing network");
  const auto ip=(a<<24)|(b<<16)|(octets[2]<<8)|octets[3];
  const auto mask=cidr_mask(static_cast<std::uint8_t>(prefix));
  if((ip&mask)!=ip)return invalid("CIDR has nonzero host bits");
  const Ipv4Cidr route{ip,static_cast<std::uint8_t>(prefix)};
  for(const auto& prev:out){if(cidr_contains(prev,ip)||cidr_contains(route,prev.network_host))return invalid("Overlapping gameplay CIDRs");}
  out.push_back(route);if(out.size()>32)return invalid("Too many gameplay CIDRs");
  if(end==std::string_view::npos)break;
  pos=end+1;if(pos==csv.size())return invalid("Empty gameplay CIDR");
 }
 return true;
}
}
