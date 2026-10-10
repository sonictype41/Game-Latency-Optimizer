package main

import (
	"crypto/ed25519"
	"encoding/hex"
	"encoding/json"
	"os"
	"path/filepath"
	"strings"
	"testing"
	"time"

	sessionkey "glo.local/glo/session_key"
)

func TestOfflineIssueAndValidate(t *testing.T) {
	d := t.TempDir()
	priv := filepath.Join(d, "issuer.key")
	pub := filepath.Join(d, "issuer.pub")
	config := filepath.Join(d, "session.json")
	if err := run([]string{"gen-issuer", "-key", priv, "-public", pub}); err != nil {
		t.Fatal(err)
	}
	if err := run([]string{"gen-issuer", "-key", priv, "-public", pub}); err == nil {
		t.Fatal("issuer overwrite allowed")
	}
	issuerPub, err := os.ReadFile(pub)
	if err != nil {
		t.Fatal(err)
	}
	trusted, err := hex.DecodeString(strings.TrimSpace(string(issuerPub)))
	if err != nil {
		t.Fatal(err)
	}
	relayPublic := make([]byte, 32)
	for i := range relayPublic {
		relayPublic[i] = byte(i + 1)
	}
	relayHex := hex.EncodeToString(relayPublic)
	if err := run([]string{"issue", "-issuer-key", priv, "-relay", "192.0.2.10:43170", "-relay-pub", relayHex, "-out", config}); err != nil {
		t.Fatal(err)
	}
	if err := run([]string{"issue", "-issuer-key", priv, "-relay", "192.0.2.10:43170", "-relay-pub", relayHex, "-out", config}); err == nil {
		t.Fatal("config overwrite allowed")
	}
	raw, err := os.ReadFile(config)
	if err != nil {
		t.Fatal(err)
	}
	var c struct {
		Schema         int    `json:"schema"`
		RelayPublicKey string `json:"relay_public_key"`
		Grant          string `json:"grant"`
		ProfileID      string `json:"profile_id"`
		GameplayIPv4   string `json:"gameplay_ipv4"`
		PortMin        int    `json:"port_min"`
		PortMax        int    `json:"port_max"`
	}
	if err := json.Unmarshal(raw, &c); err != nil {
		t.Fatal(err)
	}
	if c.Schema != 2 || c.RelayPublicKey != relayHex || c.ProfileID != "roblox-sg" || c.GameplayIPv4 == "" || c.PortMin != 49152 || c.PortMax != 65535 {
		t.Fatal("incorrect portable config")
	}
	ticket, err := hex.DecodeString(c.Grant)
	if err != nil {
		t.Fatal(err)
	}
	if len(ticket) != sessionkey.TicketSize {
		t.Fatalf("incorrect grant bytes: %d", len(ticket))
	}
	verify, err := sessionkey.NewVerifier([]ed25519.PublicKey{ed25519.PublicKey(trusted)}, relayPublic, "", time.Now())
	if err != nil {
		t.Fatal(err)
	}
	if _, err := verify.Verify(ticket, time.Now()); err != nil {
		t.Fatal("signed grant did not verify:", err)
	}
	if info, err := os.Stat(config); err != nil || info.Mode().Perm() != 0600 {
		t.Fatalf("config must be 0600: %v, %v", info, err)
	}
}
