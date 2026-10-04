package sessionkey

import (
	"crypto/ed25519"
	"crypto/rand"
	"testing"
	"time"
)

func TestTicketVerifyRedeemBearerGrant(t *testing.T) {
	pub, priv, _ := ed25519.GenerateKey(rand.Reader)
	relay := make([]byte, 32)
	rand.Read(relay)
	var id [16]byte
	rand.Read(id[:])
	now := time.Unix(1000, 0)
	ticket := Ticket{ID: id, IssuerKeyID: KeyID(pub), RelayKeyHash: RelayKeyHash(relay), IssuedAt: 1000, RedeemBefore: 1060, SessionTTL: 3000}
	raw, err := Sign(ticket, priv)
	if err != nil {
		t.Fatal(err)
	}
	v, err := NewVerifier([]ed25519.PublicKey{pub}, relay, "", now)
	if err != nil {
		t.Fatal(err)
	}
	identity := []byte("peer-a\x00transport-identity")
	got, err := v.VerifyAndRedeem(raw, identity, now)
	if err != nil || got.SessionTTL != 3000 {
		t.Fatalf("verify: %#v %v", got, err)
	}
	if _, err = v.VerifyAndRedeem(raw, identity, now.Add(time.Second)); err != nil {
		t.Fatalf("idempotent retry: %v", err)
	}
	if _, err = v.VerifyAndRedeem(raw, []byte("peer-b\x00transport-identity"), now.Add(2*time.Second)); err != ErrRedeemed {
		t.Fatalf("copied grant replay=%v", err)
	}
}

func TestVerifyAllocateRedeemDoesNotConsumeOnFailedAllocation(t *testing.T) {
	pub, priv, _ := ed25519.GenerateKey(rand.Reader)
	relay := make([]byte, 32)
	_, _ = rand.Read(relay)
	var id [16]byte
	_, _ = rand.Read(id[:])
	now := time.Unix(2000, 0)
	ticket := Ticket{ID: id, IssuerKeyID: KeyID(pub), RelayKeyHash: RelayKeyHash(relay), IssuedAt: 2000, RedeemBefore: 2060, SessionTTL: 3000}
	raw, _ := Sign(ticket, priv)
	v, _ := NewVerifier([]ed25519.PublicKey{pub}, relay, "", now)
	verified, err := v.Verify(raw, now)
	if err != nil {
		t.Fatal(err)
	}
	// Allocation failure does not call Redeem, so another Verify remains valid.
	if _, err = v.Verify(raw, now.Add(time.Second)); err != nil {
		t.Fatalf("verify retry: %v", err)
	}
	identity := []byte("peer-a\x00transport-identity")
	if err = v.Redeem(verified, identity, now.Add(time.Second)); err != nil {
		t.Fatalf("redeem: %v", err)
	}
	if err = v.Redeem(verified, []byte("peer-b\x00transport-identity"), now.Add(2*time.Second)); err != ErrRedeemed {
		t.Fatalf("cross-peer replay=%v", err)
	}
}

func TestGrantExpiryAndRelayPin(t *testing.T) {
	pub, priv, _ := ed25519.GenerateKey(rand.Reader)
	relay := make([]byte, 32)
	_, _ = rand.Read(relay)
	otherRelay := make([]byte, 32)
	_, _ = rand.Read(otherRelay)
	var id [16]byte
	_, _ = rand.Read(id[:])
	now := time.Unix(3000, 0)
	ticket := Ticket{ID: id, IssuerKeyID: KeyID(pub), RelayKeyHash: RelayKeyHash(relay), IssuedAt: 3000, RedeemBefore: 3005, SessionTTL: 30}
	raw, _ := Sign(ticket, priv)
	wrong, _ := NewVerifier([]ed25519.PublicKey{pub}, otherRelay, "", now)
	if _, err := wrong.Verify(raw, now); err != ErrRelay {
		t.Fatalf("relay pin=%v", err)
	}
	v, _ := NewVerifier([]ed25519.PublicKey{pub}, relay, "", now)
	if _, err := v.Verify(raw, now.Add(6*time.Second)); err != ErrExpired {
		t.Fatalf("expiry=%v", err)
	}
}
