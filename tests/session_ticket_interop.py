#!/usr/bin/env python3
"""C++ client / Python issuer / Go relay ticket-handshake interoperability.
Usage: session_ticket_interop.py RELAY CPP_PROBE
"""
import hashlib, os, socket, struct, subprocess, sys, tempfile, time, threading
from pathlib import Path
from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey
from cryptography.hazmat.primitives.serialization import Encoding, PublicFormat

MAGIC=b"GSK2"; VERSION=2; FMT=">4sBBH16s8s32sQQII"

def bind():
    s=socket.socket(socket.AF_INET,socket.SOCK_DGRAM);s.bind(("127.0.0.1",0));s.settimeout(2);return s

def free_port():
    s=bind();p=s.getsockname()[1];s.close();return p

def ticket(private,pub,relay_pub,ttl=30,redeem=60):
    now=int(time.time());tid=os.urandom(16);keyid=hashlib.sha256(pub).digest()[:8]
    unsigned=struct.pack(FMT,MAGIC,VERSION,0,0,tid,keyid,hashlib.sha256(relay_pub).digest(),
                         now,now+redeem,ttl,0)
    assert len(unsigned)==88
    return unsigned+private.sign(unsigned)

class Probe:
    def __init__(self,path,pin):
        self.p=subprocess.Popen([path,pin.hex()],stdin=subprocess.PIPE,stdout=subprocess.PIPE,text=True)
    def rpc(self,line):
        self.p.stdin.write(line+"\n");self.p.stdin.flush();out=self.p.stdout.readline().strip();assert out,(line,self.p.poll());return out
    def close(self):
        try:self.p.stdin.close()
        except Exception:pass
        self.p.wait(timeout=3)

def main():
    if len(sys.argv)!=3: raise SystemExit(__doc__)
    relay,probe=map(lambda p:str(Path(p).resolve()),sys.argv[1:])
    with tempfile.TemporaryDirectory() as td:
        td=Path(td);relay_key=td/'relay.key';issuer_pub_file=td/'issuer.pub';spent=td/'spent.log'
        pin=bytes.fromhex(subprocess.check_output([relay,'--keygen',str(relay_key)],text=True).strip())
        issuer=Ed25519PrivateKey.generate();issuer_pub=issuer.public_key().public_bytes(Encoding.Raw,PublicFormat.Raw)
        issuer_pub_file.write_text(issuer_pub.hex()+'\n');issuer_pub_file.chmod(0o600)
        port=free_port();log=(td/'relay.log').open('w+')
        proc=subprocess.Popen([relay,'--listen',f'127.0.0.1:{port}','--key-file',str(relay_key),
                               '--issuer-key-file',str(issuer_pub_file),'--redeemed-ticket-journal',str(spent),
                               '--stats-interval-sec','1','--clear-default-allowlist','--allow-cidr','127.0.0.0/8','--allow-private-targets'],stdout=log,stderr=log)
        try:
            time.sleep(.25);assert proc.poll() is None
            echo=bind();echo.settimeout(.1);running=True
            def echo_loop():
                while running:
                    try:
                        b,a=echo.recvfrom(2048);echo.sendto(b,a)
                    except socket.timeout:pass
                    except OSError:break
            echo_thread=threading.Thread(target=echo_loop);echo_thread.start()
            c=Probe(probe,pin);sock=bind();sock.connect(('127.0.0.1',port))
            try:
                grant=ticket(issuer,issuer_pub,pin)
                assert c.rpc('T '+grant.hex())=='OK'
                sock.send(bytes.fromhex(c.rpc('H')));retry=sock.recv(2048);assert c.rpc('R '+retry.hex())=='OK'
                auth=bytes.fromhex(c.rpc('A'));assert auth[:5]==b'GLH6\x04' and len(auth)==106+152
                sock.send(auth);welcome=sock.recv(2048)
                assert c.rpc('W '+welcome.hex())=='OK'
                sock.send(bytes.fromhex(c.rpc('S 16 0 0 -')))
                assert c.rpc('O '+sock.recv(2048).hex()).startswith('17 ')
                payload=b'glo-ticket-flow';meta=socket.inet_aton('127.0.0.1')+struct.pack('!HH',echo.getsockname()[1],50000)
                sock.send(bytes.fromhex(c.rpc('D 1 1 '+meta.hex()+' '+payload.hex())))
                opened=c.rpc('V '+sock.recv(2048).hex())
                assert opened.endswith(payload.hex()),opened
            finally:
                sock.close();c.close();running=False;echo.close();echo_thread.join(timeout=1)
            # The bearer grant is claimed by the first successful transport session.
            # A fresh C++ client cannot reuse it while the redeem window is open.
            c2=Probe(probe,pin);s2=bind();s2.connect(('127.0.0.1',port));s2.settimeout(.35)
            try:
                assert c2.rpc('T '+grant.hex())=='OK'
                s2.send(bytes.fromhex(c2.rpc('H')));assert c2.rpc('R '+s2.recv(2048).hex())=='OK'
                s2.send(bytes.fromhex(c2.rpc('A')))
                try:s2.recv(2048);raise AssertionError('cross-handshake ticket replay accepted')
                except socket.timeout:pass
            finally:s2.close();c2.close()
            assert spent.exists() and spent.stat().st_size>0
            print('PASS C++/Python/Go GSK2 bearer-grant handshake and replay rejection')
        finally:
            proc.terminate();proc.wait(timeout=5);log.close()

if __name__=='__main__':main()
