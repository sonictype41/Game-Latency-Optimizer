package secure

import (
	"bytes"
	"crypto/ecdh"
	"crypto/rand"
	"crypto/sha256"
	"encoding/binary"
	"encoding/hex"
	"golang.org/x/crypto/hkdf"
	"io"
	"sync"
	"testing"
	"time"
)

func pair(t *testing.T) (*Channel, *Channel) {
	t.Helper()
	a := bytes.Repeat([]byte{1}, 32)
	b := bytes.Repeat([]byte{2}, 32)
	x, e := NewChannel(7, a, b, time.Now().Add(time.Hour))
	if e != nil {
		t.Fatal(e)
	}
	y, e := NewChannel(7, b, a, time.Now().Add(time.Hour))
	if e != nil {
		t.Fatal(e)
	}
	return x, y
}
func TestAuthenticatedEnvelope(t *testing.T) {
	a, b := pair(t)
	p, _ := a.Seal([]byte("secret"))
	for _, i := range []int{0, 4, 8, 16, 24, len(p) - 1} {
		bad := append([]byte{}, p...)
		bad[i] ^= 1
		if _, e := b.Open(bad); e == nil {
			t.Fatalf("tamper at %d accepted", i)
		}
	}
	forged := append([]byte{}, p...)
	binary.BigEndian.PutUint64(forged[16:24], 100000)
	if _, e := b.Open(forged); e == nil {
		t.Fatal("forged high sequence accepted")
	}
	plain, e := b.Open(p)
	if e != nil || string(plain) != "secret" {
		t.Fatal("forgery poisoned replay window", e)
	}
	if _, e = b.Open(p); e == nil {
		t.Fatal("replay accepted")
	}
	if _, e = a.Open(p); e == nil {
		t.Fatal("reflection accepted")
	}
}
func TestReorderAndLimits(t *testing.T) {
	a, b := pair(t)
	var packets [][]byte
	for i := 0; i < 4100; i++ {
		p, _ := a.Seal([]byte{byte(i)})
		packets = append(packets, p)
	}
	for _, i := range []int{4099, 4098, 4, 10} {
		if _, e := b.Open(packets[i]); e != nil {
			t.Fatal(i, e)
		}
	}
	if _, e := b.Open(packets[3]); e == nil {
		t.Fatal("old packet accepted")
	}
	a.next = MaxSequence - 2
	if _, e := a.Seal(nil); e != nil {
		t.Fatal(e)
	}
	if _, e := a.Seal(nil); e == nil {
		t.Fatal("counter exhaustion accepted")
	}
	c, d := pair(t)
	c.expires = time.Now().Add(-time.Second)
	if _, e := c.Seal(nil); e == nil {
		t.Fatal("expired seal")
	}
	p, _ := d.Seal(nil)
	if _, e := c.Open(p); e == nil {
		t.Fatal("expired open")
	}
	c, d = pair(t)
	p, _ = c.Seal(make([]byte, MaxPlain))
	if len(p) != MaxDatagram {
		t.Fatal(len(p))
	}
	if _, e := d.Open(p); e != nil {
		t.Fatal(e)
	}
	if _, e := c.Seal(make([]byte, MaxPlain+1)); e == nil {
		t.Fatal("oversize")
	}
}
func TestConcurrentSequence(t *testing.T) {
	a, b := pair(t)
	out := make(chan []byte, 1000)
	var wg sync.WaitGroup
	for i := 0; i < 10; i++ {
		wg.Add(1)
		go func() {
			defer wg.Done()
			for j := 0; j < 100; j++ {
				p, e := a.Seal([]byte{1})
				if e != nil {
					t.Error(e)
				}
				out <- p
			}
		}()
	}
	wg.Wait()
	close(out)
	for p := range out {
		if _, e := b.Open(p); e != nil {
			t.Fatal(e)
		}
	}
}
func TestRFC5869(t *testing.T) {
	salt, _ := hex.DecodeString("000102030405060708090a0b0c")
	info, _ := hex.DecodeString("f0f1f2f3f4f5f6f7f8f9")
	want := "3cb25f25faacd57a90434f64d0362f2a2d2d0a90cf1a5a4c5db02d56ecc4c5bf34007208d5b887185865"
	b := make([]byte, 42)
	io.ReadFull(hkdf.New(sha256.New, bytes.Repeat([]byte{0x0b}, 22), salt, info), b)
	if hex.EncodeToString(b) != want {
		t.Fatal("HKDF vector")
	}
}
func TestLowOrderX25519Rejected(t *testing.T) {
	k, _ := ecdh.X25519().GenerateKey(rand.Reader)
	if _, e := DH(k, make([]byte, 32)); e == nil {
		t.Fatal("zero DH accepted")
	}
}
func TestDerivationContext(t *testing.T) {
	a := bytes.Repeat([]byte{1}, 32)
	b := bytes.Repeat([]byte{2}, 32)
	x, y, e := Derive(a, b, a, b, a, b, 1)
	if e != nil || bytes.Equal(x, y) {
		t.Fatal("direction separation")
	}
	z, _, _ := Derive(a, b, a, b, a, b, 2)
	if bytes.Equal(x, z) {
		t.Fatal("SID not bound")
	}
	z, _, _ = Derive(a, b, b, b, a, b, 1)
	if bytes.Equal(x, z) {
		t.Fatal("transcript not bound")
	}
}

func TestReplayBitmapBoundaryAndFootprint(t *testing.T) {
	var r Replay
	for _, n := range []uint64{1, 2, 4096, 4097, 8193} {
		if !r.eligible(n) {
			t.Fatalf("fresh sequence %d ineligible high=%d", n, r.high)
		}
		r.commit(n)
		if r.eligible(n) {
			t.Fatalf("replay %d remained eligible", n)
		}
	}
	if r.eligible(r.high - 4096) {
		t.Fatal("sequence outside replay window accepted")
	}
	if got := len(r.seen) * 8; got != 512 {
		t.Fatalf("replay bitmap storage=%d want=512", got)
	}
}

func TestOpenInPlaceAliasesCiphertextBuffer(t *testing.T) {
	a, b := pair(t)
	wire, err := a.Seal([]byte("in-place-secret"))
	if err != nil {
		t.Fatal(err)
	}
	plain, err := b.OpenInPlace(wire)
	if err != nil || string(plain) != "in-place-secret" {
		t.Fatalf("open in place failed plain=%q err=%v", plain, err)
	}
	if len(plain) == 0 || &plain[0] != &wire[HeaderSize] {
		t.Fatal("plaintext does not alias encrypted receive buffer")
	}
}

func FuzzChannelOpenRejectsMalformed(f *testing.F) {
	f.Add([]byte("GLO6"))
	f.Add(make([]byte, MaxDatagram+1))
	f.Fuzz(func(t *testing.T, in []byte) {
		if len(in) > MaxDatagram+64 {
			in = in[:MaxDatagram+64]
		}
		_, b := pair(t)
		_, _ = b.Open(append([]byte{}, in...))
	})
}

func BenchmarkSealInto512(b *testing.B) {
	a, _ := pair(&testing.T{})
	payload := make([]byte, 512)
	buf := make([]byte, 0, MaxDatagram)
	b.ReportAllocs()
	b.SetBytes(int64(len(payload)))
	for i := 0; i < b.N; i++ {
		buf = buf[:0]
		if _, err := a.SealInto(buf, payload); err != nil {
			b.Fatal(err)
		}
	}
}

func BenchmarkOpenInPlace512(b *testing.B) {
	a, c := pair(&testing.T{})
	template, err := a.Seal(make([]byte, 512))
	if err != nil {
		b.Fatal(err)
	}
	buf := make([]byte, len(template))
	b.ReportAllocs()
	b.SetBytes(512)
	for i := 0; i < b.N; i++ {
		copy(buf, template)
		c.rxMu.Lock()
		c.replay = Replay{}
		c.rxMu.Unlock()
		if _, err := c.OpenInPlace(buf); err != nil {
			b.Fatal(err)
		}
	}
}
