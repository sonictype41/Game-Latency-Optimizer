#include "glo/session_config.hpp"
#include <cassert>
#include <string>
int main(){
  glo::SessionConfig c; std::string e;
  const std::string grant(304,'a');
  const std::string good="{\"schema\":1,\"relay\":\"127.0.0.1:43170\",\"relay_name\":\"Singapore-01\",\"relay_public_key\":\"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef\",\"game\":\"roblox\",\"grant\":\""+grant+"\",\"timeout_message\":\"Expired\"}";
  assert(glo::parse_session_config_json(good,c,e));
  assert(c.relay_host=="127.0.0.1" && c.relay_port==43170 && c.relay_name=="Singapore-01" && c.grant.size()==152 && c.game_id==glo::GameId::Roblox);
  auto bad=good; bad.insert(bad.size()-1,",\"issuer_seed_file\":\"C:/secret\"");
  assert(!glo::parse_session_config_json(bad,c,e));

  const std::string enveloped="{\"ok\":true,"+good.substr(1);
  std::string portable;
  assert(!glo::parse_session_config_json(enveloped,c,e));
  assert(glo::unwrap_session_config_api_response(enveloped,portable,e));
  assert(glo::parse_session_config_json(portable,c,e));
  assert(c.relay_host=="127.0.0.1" && c.relay_port==43170 && c.relay_name=="Singapore-01" && c.grant.size()==152);
  assert(!glo::unwrap_session_config_api_response("{\"ok\":false,"+good.substr(1),portable,e));
  assert(!glo::unwrap_session_config_api_response("{\"ok\":\"true\","+good.substr(1),portable,e));
  return 0;
}
