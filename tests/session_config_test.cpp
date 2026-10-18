#include "glo/session_config.hpp"
#include <cassert>
#include <string>
int main(){
  glo::SessionConfig c; std::string e;
  const std::string grant(304,'a');
  const std::string good="{\"schema\":2,\"relay\":\"127.0.0.1:43170\",\"relay_name\":\"Singapore-01\",\"relay_public_key\":\"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef\",\"game\":\"roblox\",\"grant\":\""+grant+"\",\"profile_id\":\"roblox-sg\",\"profile_revision\":1,\"gameplay_ipv4\":\"128.116.97.33/32\",\"port_min\":49152,\"port_max\":65535,\"timeout_message\":\"Expired\"}";
  assert(glo::parse_session_config_json(good,c,e));
  assert(c.profile_id=="roblox-sg" && c.gameplay_ipv4=="128.116.97.33/32" && c.port_min==49152);
  assert(c.relay_host=="127.0.0.1" && c.relay_port==43170 && c.relay_name=="Singapore-01" && c.grant.size()==152 && c.game_id==glo::GameId::Roblox);
  auto vg=good;
  const auto old_game=std::string("\"game\":\"roblox\"");
  vg.replace(vg.find(old_game),old_game.size(),"\"game\":\"valorant\"");
  assert(glo::parse_session_config_json(vg,c,e));
  assert(c.game_id==glo::GameId::Valorant);
  // New PUBG profile is accepted with CIDRs, without any hardcoded PUBG endpoint.
  auto pg=good;
  pg.replace(pg.find("\"game\":\"roblox\""),std::string("\"game\":\"roblox\"").size(),"\"game\":\"pubg\"");
  pg.replace(pg.find("128.116.97.33/32"),std::string("128.116.97.33/32").size(),"20.212.0.0/16,57.155.0.0/16");
  assert(glo::parse_session_config_json(pg,c,e));
  assert(c.game_id==glo::GameId::PUBG && c.gameplay_ipv4=="20.212.0.0/16,57.155.0.0/16");
  // The parser is still strict for unsupported games.
  vg.replace(vg.find("\"game\":\"valorant\""),std::string("\"game\":\"valorant\"").size(),"\"game\":\"not-supported\"");
  assert(!glo::parse_session_config_json(vg,c,e));
  auto complex=good;
  complex.replace(complex.find("\"schema\":2"),std::string("\"schema\":2").size(),"\"schema\":3");
  complex.insert(complex.size()-1,",\"udp_port_rules\":\"7000-8000,8181\"");
  assert(glo::parse_session_config_json(complex,c,e));
  assert(c.port_rules.size()==2 && glo::udp_port_allowed(c.port_rules,8181) && !glo::udp_port_allowed(c.port_rules,8182));
  assert(!glo::parse_session_config_json(complex.substr(0,complex.size()-1)+",\"unknown\":1}",c,e));
  std::string portable;
  assert(glo::unwrap_session_config_api_response("{\"ok\":true,"+complex.substr(1),portable,e));
  assert(glo::parse_session_config_json(portable,c,e)&&c.port_rules.size()==2);
  auto bad=good; bad.insert(bad.size()-1,",\"issuer_seed_file\":\"C:/secret\"");
  assert(!glo::parse_session_config_json(bad,c,e));

  const std::string enveloped="{\"ok\":true,"+good.substr(1);
  assert(!glo::parse_session_config_json(enveloped,c,e));
  assert(glo::unwrap_session_config_api_response(enveloped,portable,e));
  assert(glo::parse_session_config_json(portable,c,e));
  assert(c.relay_host=="127.0.0.1" && c.relay_port==43170 && c.relay_name=="Singapore-01" && c.grant.size()==152);
  assert(!glo::unwrap_session_config_api_response("{\"ok\":false,"+good.substr(1),portable,e));
  assert(!glo::unwrap_session_config_api_response("{\"ok\":\"true\","+good.substr(1),portable,e));
  return 0;
}
