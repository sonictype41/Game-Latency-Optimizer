// Pipe-driven interoperability probe; no driver or privileged routing required.
#include "glo/secure_transport.hpp"
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
static std::vector<std::uint8_t> unhex(const std::string& s){
 if(s=="-")return {};if(s.size()%2)throw std::runtime_error("hex");std::vector<std::uint8_t>b;
 for(std::size_t i=0;i<s.size();i+=2)b.push_back(static_cast<std::uint8_t>(std::stoul(s.substr(i,2),nullptr,16)));return b;
}
static std::string hex(std::span<const std::uint8_t>b){static const char*h="0123456789abcdef";std::string s;for(auto c:b){s+=h[c>>4];s+=h[c&15];}return s.empty()?"-":s;}
int main(int argc,char**argv){
 if(argc!=2)return 2;glo::secure::Client c;if(!c.begin()||!c.set_relay_key(argv[1]))return 3;
 std::string line;
 while(std::getline(std::cin,line)){try{
  std::istringstream in(line);std::string cmd,h;in>>cmd;
  if(cmd=="T"){in>>h;auto b=unhex(h);std::cout<<(c.set_ticket(b)?"OK":"FAIL");}
  else if(cmd=="H")std::cout<<hex(c.hello());
  else if(cmd=="A")std::cout<<hex(c.auth_hello());
  else if(cmd=="R"){in>>h;std::cout<<(c.retry(unhex(h))?"OK":"FAIL");}
  else if(cmd=="W"){in>>h;std::cout<<(c.welcome(unhex(h))?"OK":"FAIL");}
  else if(cmd=="S"){unsigned type;std::uint32_t flow;std::uint64_t nonce;in>>type>>flow>>nonce>>h;glo::protocol::Packet p;p.type=static_cast<glo::protocol::PacketType>(type);p.session_id=c.session_id();p.flow_id=flow;p.nonce=nonce;p.payload=unhex(h);if(p.type==glo::protocol::PacketType::Finish)p.flags=glo::protocol::kFinishFlagGLOD1;std::vector<std::uint8_t>b;std::cout<<(c.seal(p,b)?hex(b):"FAIL");}
  else if(cmd=="D"){unsigned long long seq;std::uint32_t flow;std::string mh,ph;in>>flow>>seq>>mh>>ph;auto m=unhex(mh);auto payload=unhex(ph);if(m.size()!=glo::protocol::kFlowMetaSize){std::cout<<"FAIL";}else{auto get16=[&](int o){return std::uint16_t((std::uint16_t(m[o])<<8)|m[o+1]);};std::uint32_t ip=(std::uint32_t(m[0])<<24)|(std::uint32_t(m[1])<<16)|(std::uint32_t(m[2])<<8)|m[3];std::array<std::uint8_t,glo::protocol::kDataMaxDatagram> b;std::size_t n=0;std::cout<<(glo::protocol::encode_data_c2s(c.session_id(),seq,flow,ip,get16(4),get16(6),payload,b,n)?hex(std::span<const std::uint8_t>(b.data(),n)):"FAIL");}}
  else if(cmd=="V"){in>>h;auto b=unhex(h);glo::protocol::DataView d;if(glo::protocol::decode_data_view(b,d))std::cout<<unsigned(d.direction)<<" "<<d.flow_id<<" "<<d.sequence<<" "<<hex(d.payload);else std::cout<<"FAIL";}
  else if(cmd=="O"){in>>h;glo::protocol::Packet p;if(c.open(unhex(h),p))std::cout<<unsigned(p.type)<<" "<<p.flow_id<<" "<<p.nonce<<" "<<hex(p.payload);else std::cout<<"FAIL";}
  else return 4;
  std::cout<<std::endl;
 }catch(...){std::cout<<"FAIL"<<std::endl;}}
}
