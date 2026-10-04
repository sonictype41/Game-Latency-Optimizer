#include "glo/secure_transport.hpp"
#include <sodium.h>
#include <algorithm>
#include <cstring>

namespace glo::secure {
namespace {
void put16(unsigned char* p,std::uint16_t n){p[0]=static_cast<unsigned char>(n>>8);p[1]=static_cast<unsigned char>(n);}
std::uint32_t get32(const unsigned char* p){return (std::uint32_t(p[0])<<24)|(std::uint32_t(p[1])<<16)|(std::uint32_t(p[2])<<8)|p[3];}
void put64(unsigned char* p,std::uint64_t n){for(int i=7;i>=0;--i){p[i]=static_cast<unsigned char>(n);n>>=8;}}
std::uint64_t get64(const unsigned char* p){std::uint64_t n=0;for(int i=0;i<8;++i)n=(n<<8)|p[i];return n;}
void hmac256(const unsigned char* key,std::size_t kl,const unsigned char* p,std::size_t n,unsigned char* out){
 crypto_auth_hmacsha256_state st;
 crypto_auth_hmacsha256_init(&st,key,kl);crypto_auth_hmacsha256_update(&st,p,n);crypto_auth_hmacsha256_final(&st,out);sodium_memzero(&st,sizeof st);
}
}
Client::~Client(){std::scoped_lock lock(state_mu_,tx_mu_,rx_mu_);sodium_memzero(secret_.data(),32);sodium_memzero(tx_.data(),32);sodium_memzero(rx_.data(),32);if(!ticket_.empty())sodium_memzero(ticket_.data(),ticket_.size());}
bool Client::begin(){
 std::scoped_lock lock(state_mu_);
 if(started_||sodium_init()<0)return false;
 randombytes_buf(secret_.data(),32);randombytes_buf(random_.data(),32);
 if(crypto_scalarmult_curve25519_base(pub_.data(),secret_.data())!=0)return false;
 started_=true;return true;
}
bool Client::set_relay_key(const std::string& pinned_hex){
 std::scoped_lock lock(state_mu_);if(!started_||ready_.load(std::memory_order_acquire)||pinned_hex.size()!=64)return false;
 std::size_t written=0;if(sodium_hex2bin(pin_.data(),32,pinned_hex.data(),pinned_hex.size(),nullptr,&written,nullptr)!=0||written!=32)return false;pin_set_=true;return true;
}
bool Client::set_ticket(std::span<const std::uint8_t> ticket){
 std::scoped_lock lock(state_mu_);if(!started_||ready_.load(std::memory_order_acquire)||ticket.empty()||ticket.size()>65535)return false;
 ticket_.assign(ticket.begin(),ticket.end());return true;
}
std::vector<std::uint8_t> Client::hello() const{
 std::scoped_lock lock(state_mu_);if(!started_)return {};
 std::vector<std::uint8_t> b(kHelloSize);std::memcpy(b.data(),"GLH6",4);b[4]=1;
 std::copy(pub_.begin(),pub_.end(),b.begin()+8);std::copy(random_.begin(),random_.end(),b.begin()+40);std::copy(cookie_.begin(),cookie_.end(),b.begin()+72);return b;
}
std::vector<std::uint8_t> Client::auth_hello() const{
 std::scoped_lock lock(state_mu_);if(!started_||!challenged_||ticket_.empty())return {};
 std::vector<std::uint8_t> b(kAuthHelloPrefix+ticket_.size());std::memcpy(b.data(),"GLH6",4);b[4]=4;
 std::copy(pub_.begin(),pub_.end(),b.begin()+8);std::copy(random_.begin(),random_.end(),b.begin()+40);std::copy(cookie_.begin(),cookie_.end(),b.begin()+72);
 put16(b.data()+104,static_cast<std::uint16_t>(ticket_.size()));std::copy(ticket_.begin(),ticket_.end(),b.begin()+106);return b;
}
bool Client::retry(std::span<const std::uint8_t>b){
 std::scoped_lock lock(state_mu_);
 if(!started_||ready_.load(std::memory_order_acquire)||b.size()!=kHelloSize||std::memcmp(b.data(),"GLH6",4)||b[4]!=2||b[5]||b[6]||b[7]||sodium_memcmp(b.data()+8,pub_.data(),32)||sodium_memcmp(b.data()+40,random_.data(),32))return false;
 std::copy(b.begin()+72,b.end(),cookie_.begin());challenged_=true;return true;
}
bool Client::welcome(std::span<const std::uint8_t>b){
 std::scoped_lock lock(state_mu_);
 if(!started_||!pin_set_||ticket_.empty()||ready_.load(std::memory_order_acquire)||b.size()<184||std::memcmp(b.data(),"GLH6",4)||b[4]!=3||b[5]||b[6]||b[7]||sodium_memcmp(b.data()+8,pub_.data(),32)||sodium_memcmp(b.data()+40,random_.data(),32))return false;
 const auto id=get64(b.data()+104);if(!id)return false;
 std::array<unsigned char,64> dh{},keys{};
 if(crypto_scalarmult_curve25519(dh.data(),secret_.data(),pin_.data())!=0||crypto_scalarmult_curve25519(dh.data()+32,secret_.data(),b.data()+72)!=0){sodium_memzero(dh.data(),dh.size());return false;}
 constexpr char label[]="GLO6 X25519 ChaCha20Poly1305 HKDF-SHA256 v1";
 crypto_hash_sha256_state hs;crypto_hash_sha256_init(&hs);crypto_hash_sha256_update(&hs,reinterpret_cast<const unsigned char*>(label),sizeof(label)-1);
 crypto_hash_sha256_update(&hs,pub_.data(),32);crypto_hash_sha256_update(&hs,random_.data(),32);crypto_hash_sha256_update(&hs,pin_.data(),32);crypto_hash_sha256_update(&hs,b.data()+72,32);crypto_hash_sha256_update(&hs,b.data()+104,8);
 std::array<unsigned char,32> salt{},prk{};crypto_hash_sha256_final(&hs,salt.data());hmac256(salt.data(),32,dh.data(),64,prk.data());
 constexpr char info[]="GLO6 c2s|s2c traffic keys";
 std::vector<unsigned char> msg(info,info+sizeof(info)-1);msg.push_back(1);hmac256(prk.data(),32,msg.data(),msg.size(),keys.data());
 msg.assign(keys.begin(),keys.begin()+32);msg.insert(msg.end(),info,info+sizeof(info)-1);msg.push_back(2);hmac256(prk.data(),32,msg.data(),msg.size(),keys.data()+32);
 sodium_memzero(dh.data(),64);sodium_memzero(prk.data(),32);sodium_memzero(msg.data(),msg.size());
 const auto enc=b.subspan(112);bool valid=enc.size()>=72&&std::memcmp(enc.data(),"GLO6",4)==0&&enc[4]==0&&enc[5]==0&&enc[6]==0&&enc[7]==0&&get64(enc.data()+8)==id&&get64(enc.data()+16)==1;
 std::array<unsigned char,12> nonce{};put64(nonce.data()+4,1);std::vector<unsigned char> plain(enc.size()-40);unsigned long long size=0;
 if(valid)valid=crypto_aead_chacha20poly1305_ietf_decrypt(plain.data(),&size,nullptr,enc.data()+24,enc.size()-24,enc.data(),24,nonce.data(),keys.data()+32)==0;
 protocol::Packet p;if(valid)valid=protocol::decode(std::span<const std::uint8_t>(plain.data(),static_cast<std::size_t>(size)),p)&&p.type==protocol::PacketType::Welcome&&p.session_id==id&&p.flags==0&&p.nonce==0&&p.flow_id==0&&p.payload.size()==4;
 std::uint32_t ttl=valid?get32(p.payload.data()):0;if(!ttl)valid=false;
 if(valid){std::copy(keys.begin(),keys.begin()+32,tx_.begin());std::copy(keys.begin()+32,keys.end(),rx_.begin());sid_=id;high_=1;std::fill(seen_.begin(),seen_.end(),0);seen_[(1&4095)>>6]|=std::uint64_t{1}<<(1&63);expires_=std::chrono::steady_clock::now()+std::chrono::seconds(ttl);sodium_memzero(secret_.data(),32);ready_.store(true,std::memory_order_release);}
 sodium_memzero(keys.data(),64);return valid;
}
bool Client::expired() const {std::scoped_lock lock(tx_mu_);return ready_.load(std::memory_order_acquire)&&(std::chrono::steady_clock::now()>=expires_||next_>=kMaxSequence-1);}
std::uint32_t Client::remaining_seconds() const {
 std::scoped_lock lock(tx_mu_);
 if(!ready_.load(std::memory_order_acquire))return 0;
 const auto now=std::chrono::steady_clock::now();if(now>=expires_)return 0;
 const auto s=std::chrono::duration_cast<std::chrono::seconds>(expires_-now).count();
 return static_cast<std::uint32_t>(s+1);
}
bool Client::seal(const protocol::Packet&p,std::vector<std::uint8_t>&out){
 std::scoped_lock lock(tx_mu_);out.clear();if(!ready_.load(std::memory_order_acquire)||p.session_id!=sid_||std::chrono::steady_clock::now()>=expires_||next_>=kMaxSequence-1)return false;
 std::vector<std::uint8_t> plain;if(!protocol::encode(p,plain))return false;out.resize(24+plain.size()+16);std::memcpy(out.data(),"GLO6",4);std::fill(out.begin()+4,out.begin()+8,0);put64(out.data()+8,sid_);put64(out.data()+16,++next_);
 std::array<unsigned char,12> nonce{};put64(nonce.data()+4,next_);unsigned long long len=0;return crypto_aead_chacha20poly1305_ietf_encrypt(out.data()+24,&len,plain.data(),plain.size(),out.data(),24,nullptr,nonce.data(),tx_.data())==0&&len==plain.size()+16;
}
bool Client::open(std::span<const std::uint8_t>b,protocol::Packet&p){
 std::scoped_lock lock(rx_mu_);if(!ready_.load(std::memory_order_acquire)||std::chrono::steady_clock::now()>=expires_||b.size()<72||b.size()>kMaxDatagram||std::memcmp(b.data(),"GLO6",4)||b[4]||b[5]||b[6]||b[7]||get64(b.data()+8)!=sid_)return false;
 auto seq=get64(b.data()+16);if(!seq||seq>=kMaxSequence||(seq<=high_&&high_-seq>=4096))return false;
 auto bit_set=[this](std::uint64_t n){auto i=n&4095;return(seen_[i>>6]&(std::uint64_t{1}<<(i&63)))!=0;};auto bit_clear=[this](std::uint64_t n){auto i=n&4095;seen_[i>>6]&=~(std::uint64_t{1}<<(i&63));};auto bit_mark=[this](std::uint64_t n){auto i=n&4095;seen_[i>>6]|=(std::uint64_t{1}<<(i&63));};if(seq<=high_&&bit_set(seq))return false;
 std::array<unsigned char,12> nonce{};put64(nonce.data()+4,seq);std::vector<unsigned char> plain(b.size()-40);unsigned long long len=0;if(crypto_aead_chacha20poly1305_ietf_decrypt(plain.data(),&len,nullptr,b.data()+24,b.size()-24,b.data(),24,nonce.data(),rx_.data())!=0)return false;
 if(!protocol::decode(std::span<const std::uint8_t>(plain.data(),static_cast<std::size_t>(len)),p)||p.session_id!=sid_)return false;if(seq>high_){auto delta=seq-high_;if(delta>=4096)std::fill(seen_.begin(),seen_.end(),0);else for(auto n=high_+1;n<=seq;++n)bit_clear(n);high_=seq;}bit_mark(seq);return true;
}
}
