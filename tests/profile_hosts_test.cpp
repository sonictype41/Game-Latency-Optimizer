#include "glo/profile_hosts.hpp"
#include <cassert>
#include <string>
#include <vector>
int main(){std::vector<glo::Ipv4Cidr> v;std::string e;
 assert(glo::parse_profile_hosts(glo::bundled_profile_hosts("roblox"),v,e)&&v.size()==4);
 assert(!glo::parse_profile_hosts("0.0.0.0/0",v,e));
 assert(!glo::parse_profile_hosts("128.116.97.33,128.116.97.33",v,e));
 assert(!glo::parse_profile_hosts("192.168.1.2",v,e));
 assert(!glo::parse_profile_hosts("128.116.97.33/24",v,e));
 assert(glo::parse_profile_hosts("20.212.0.0/16,57.155.0.0/16",v,e)&&v.size()==2);
 assert(!glo::parse_profile_hosts("20.212.0.0/15",v,e));
 assert(!glo::parse_profile_hosts("20.212.0.0/16,20.212.244.0/24",v,e));
 assert(!glo::parse_profile_hosts("20.212.1.0/16",v,e));
 assert(glo::parse_profile_hosts("128.116.97.33/32",v,e)&&v.size()==1);
}
