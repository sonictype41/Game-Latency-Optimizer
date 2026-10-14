#include "glo/udp_port_rules.hpp"
#include <cassert>
#include <string>
int main(){
  glo::UdpPortRanges r;
  assert(glo::parse_udp_port_rules("7000-8000,8181,9000-9100",r));
  assert(r.size()==3 && glo::udp_port_allowed(r,8181) && !glo::udp_port_allowed(r,8182));
  assert(glo::udp_port_allowed(r,7500) && !glo::udp_port_allowed(r,8001));
  assert(glo::parse_udp_port_rules("8181,7000-8000,7500-8100,9000-9001,9002",r));
  assert(glo::udp_port_rules_text(r)=="7000-8100,8181,9000-9002");
  assert(glo::parse_udp_port_rules("65535,1",r)&&r.size()==2);
  for(const char* bad:{"", "0", "65536", "2-1", "7000-", "1,,2", "1,", "-4", "1-2-3", "nope", "1.0", "3/4"})
    assert(!glo::parse_udp_port_rules(bad,r));
  std::string too_many;for(int i=1;i<=33;++i){if(i>1)too_many+=",";too_many+=std::to_string(i*2);}
  assert(!glo::parse_udp_port_rules(too_many,r));
}
