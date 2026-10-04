#include "glo/cli_policy.hpp"
#include <cstdlib>
#include <functional>
#include <iostream>
void require(bool b){if(!b){std::cerr<<"FAILED\n";std::exit(1);}}
void rejects(std::function<void()> f){try{f();}catch(const std::exception&){return;}require(false);}
int main(){
 using namespace glo::cli;
 require(parse({}).empty());
 require(parse({"--config","session.json"}).at("--config")=="session.json");
 require(parse({"--force-direct"}).contains("--force-direct"));
 require(parse({"--debug"}).contains("--debug"));
 require(parse({"--force-handover-grace-ms","1800"}).contains("--force-handover-grace-ms"));
 rejects([]{parse({"--unknown"});});rejects([]{parse({"--config"});});rejects([]{parse({"--debug","--dbg-log"});});
 rejects([]{number("500junk",500,3000);});rejects([]{number("-1",0,3000);});
 require(number("500",500,3000)==500);require(number("3000",500,3000)==3000);
 for(auto v:{"host:0","host:65536","host:abc","host:43junk","", "http://host", "[::1]:80"}) rejects([&]{endpoint(v);});
 require(!hex_key(std::string(64,'z')));require(hex_key(std::string(64,'F')));
 std::cout<<build_identity<<" generic CLI behavior PASS\n";
}
