// Package secure implements GLO6's authenticated UDP envelope.
// Cryptographic primitives are provided by Go and golang.org/x/crypto.
package secure

import (
	"crypto/cipher"
	"crypto/ecdh"
	"crypto/hmac"
	"crypto/sha256"
	"encoding/binary"
	"errors"
	"golang.org/x/crypto/chacha20poly1305"
	"golang.org/x/crypto/hkdf"
	"io"
	"sync"
	"time"
)

const HeaderSize = 24
const MaxPlain = 1432
const MaxDatagram = HeaderSize + MaxPlain + 16
const MaxSequence = uint64(1) << 32 // re-handshake before exhaustion; never wrap
const replayWindow = uint64(4096)

var ErrInvalid = errors.New("invalid secure datagram")

// Replay is a 4096-packet authenticated replay window. The bitmap is circular:
// 4096 bits (512 bytes) replace the old 4096x uint64 sequence table (32 KiB).
// Only authenticated packets may commit and advance the window.
type Replay struct {
	high uint64
	seen [replayWindow / 64]uint64
}

func replaySlot(n uint64) (int, uint64) {
	i := n & (replayWindow - 1)
	return int(i >> 6), uint64(1) << (i & 63)
}
func (r *Replay) bit(n uint64) bool {
	w, m := replaySlot(n)
	return r.seen[w]&m != 0
}
func (r *Replay) clearBit(n uint64) {
	w, m := replaySlot(n)
	r.seen[w] &^= m
}
func (r *Replay) setBit(n uint64) {
	w, m := replaySlot(n)
	r.seen[w] |= m
}
func (r *Replay) eligible(n uint64) bool {
	if n == 0 || n >= MaxSequence {
		return false
	}
	if n > r.high {
		return true
	}
	return r.high-n < replayWindow && !r.bit(n)
}
func (r *Replay) commit(n uint64) {
	if n > r.high {
		delta := n - r.high
		if delta >= replayWindow {
			clear(r.seen[:])
		} else {
			// Clear circular slots that are being reused by the advancing window.
			// Normal UDP traffic advances by one, while a gap is bounded to 4095.
			for seq := r.high + 1; seq <= n; seq++ {
				r.clearBit(seq)
			}
		}
		r.high = n
	}
	r.setBit(n)
}

type Channel struct {
	txMu, rxMu sync.Mutex
	tx, rx     cipher.AEAD
	sid        uint64
	next       uint64
	replay     Replay
	expires    time.Time
}

func NewChannel(sid uint64, tx, rx []byte, deadline time.Time) (*Channel, error) {
	if sid == 0 || deadline.IsZero() {
		return nil, ErrInvalid
	}
	a, e := chacha20poly1305.New(tx)
	if e != nil {
		return nil, e
	}
	b, e := chacha20poly1305.New(rx)
	if e != nil {
		return nil, e
	}
	return &Channel{tx: a, rx: b, sid: sid, expires: deadline}, nil
}

// Seal appends an authenticated GLO6 envelope into a newly allocated buffer.
func (c *Channel) Seal(p []byte) ([]byte, error) { return c.SealInto(nil, p) }

// SealInto appends an authenticated GLO6 envelope to dst. Callers may pass a
// zero-length pooled buffer with sufficient capacity to avoid per-packet heap
// allocation on the relay egress path.
func (c *Channel) SealInto(dst, p []byte) ([]byte, error) {
	c.txMu.Lock()
	defer c.txMu.Unlock()
	if len(p) > MaxPlain || time.Now().After(c.expires) || c.next >= MaxSequence-1 {
		return nil, ErrInvalid
	}
	c.next++
	start := len(dst)
	need := HeaderSize + len(p) + chacha20poly1305.Overhead
	if cap(dst)-len(dst) < need {
		grown := make([]byte, len(dst), len(dst)+need)
		copy(grown, dst)
		dst = grown
	}
	dst = dst[:start+HeaderSize]
	h := dst[start : start+HeaderSize]
	copy(h, "GLO6")
	clear(h[4:8])
	binary.BigEndian.PutUint64(h[8:16], c.sid)
	binary.BigEndian.PutUint64(h[16:24], c.next)
	var nonce [12]byte
	binary.BigEndian.PutUint64(nonce[4:], c.next)
	return c.tx.Seal(dst, nonce[:], p, h), nil
}

func (c *Channel) validateHeader(b []byte) (uint64, error) {
	if len(b) < HeaderSize+chacha20poly1305.Overhead || len(b) > MaxDatagram ||
		string(b[:4]) != "GLO6" || binary.BigEndian.Uint32(b[4:8]) != 0 ||
		binary.BigEndian.Uint64(b[8:16]) != c.sid || time.Now().After(c.expires) {
		return 0, ErrInvalid
	}
	seq := binary.BigEndian.Uint64(b[16:24])
	if !c.replay.eligible(seq) {
		return 0, ErrInvalid
	}
	return seq, nil
}

// Open preserves the historical API by returning an owned plaintext buffer.
func (c *Channel) Open(b []byte) ([]byte, error) {
	c.rxMu.Lock()
	defer c.rxMu.Unlock()
	seq, e := c.validateHeader(b)
	if e != nil {
		return nil, e
	}
	var nonce [12]byte
	binary.BigEndian.PutUint64(nonce[4:], seq)
	p, e := c.rx.Open(nil, nonce[:], b[HeaderSize:], b[:HeaderSize])
	if e != nil {
		return nil, ErrInvalid
	}
	c.replay.commit(seq)
	return p, nil
}

// OpenInPlace authenticates and decrypts ciphertext directly over the encrypted
// region of b. The returned plaintext aliases b[HeaderSize:] and is valid until
// the caller reuses/modifies b. Replay state advances only after AEAD succeeds.
func (c *Channel) OpenInPlace(b []byte) ([]byte, error) {
	c.rxMu.Lock()
	defer c.rxMu.Unlock()
	seq, e := c.validateHeader(b)
	if e != nil {
		return nil, e
	}
	var nonce [12]byte
	binary.BigEndian.PutUint64(nonce[4:], seq)
	dst := b[HeaderSize:HeaderSize]
	p, e := c.rx.Open(dst, nonce[:], b[HeaderSize:], b[:HeaderSize])
	if e != nil {
		return nil, ErrInvalid
	}
	c.replay.commit(seq)
	return p, nil
}

// Derive binds both DH results to both public keys, client randomness and SID.
// Static DH authenticates the pinned responder; ephemeral DH gives forward
// secrecy after ephemeral private keys and session keys have been erased.
func Derive(staticDH, ephemeralDH, clientPub, clientRandom, serverStatic, serverEphemeral []byte, sid uint64) ([]byte, []byte, error) {
	for _, v := range [][]byte{staticDH, ephemeralDH, clientPub, clientRandom, serverStatic, serverEphemeral} {
		if len(v) != 32 {
			return nil, nil, ErrInvalid
		}
	}
	h := sha256.New()
	h.Write([]byte("GLO6 X25519 ChaCha20Poly1305 HKDF-SHA256 v1"))
	for _, v := range [][]byte{clientPub, clientRandom, serverStatic, serverEphemeral} {
		h.Write(v)
	}
	var id [8]byte
	binary.BigEndian.PutUint64(id[:], sid)
	h.Write(id[:])
	ikm := make([]byte, 0, 64)
	ikm = append(ikm, staticDH...)
	ikm = append(ikm, ephemeralDH...)
	defer clear(ikm)
	keys := make([]byte, 64)
	_, e := io.ReadFull(hkdf.New(sha256.New, ikm, h.Sum(nil), []byte("GLO6 c2s|s2c traffic keys")), keys)
	if e != nil {
		clear(keys)
		return nil, nil, e
	}
	return keys[:32], keys[32:], nil
}
func DH(key *ecdh.PrivateKey, pub []byte) ([]byte, error) {
	p, e := ecdh.X25519().NewPublicKey(pub)
	if e != nil {
		return nil, e
	}
	return key.ECDH(p) // rejects all-zero secret
}
func mac(key, data []byte) []byte { h := hmac.New(sha256.New, key); h.Write(data); return h.Sum(nil) }
