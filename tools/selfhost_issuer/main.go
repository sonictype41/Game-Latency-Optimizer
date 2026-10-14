// selfhost_issuer is a minimal, offline issuer for a single-operator GLO relay.
// It is NOT a public account provider, billing system, or API service.
package main

import (
	"crypto/ed25519"
	"crypto/rand"
	"encoding/hex"
	"encoding/json"
	"errors"
	"flag"
	"fmt"
	"net"
	"os"
	"strconv"
	"strings"
	"time"

	sessionkey "glo.local/glo/session_key"
)

func writeNew(path string, content []byte, mode os.FileMode) error {
	f, err := os.OpenFile(path, os.O_WRONLY|os.O_CREATE|os.O_EXCL, mode)
	if err != nil {
		return err
	}
	_, err = f.Write(content)
	closeErr := f.Close()
	if err != nil {
		return err
	}
	return closeErr
}
func decodeKey(raw string, size int) ([]byte, error) {
	b, err := hex.DecodeString(strings.TrimSpace(raw))
	if err != nil || len(b) != size {
		return nil, fmt.Errorf("expected %d-byte hex key", size)
	}
	return b, nil
}
func run(args []string) error {
	if len(args) < 1 {
		return errors.New("usage: selfhost_issuer gen-issuer | issue; run with -h for each command")
	}
	switch args[0] {
	case "gen-issuer":
		fs := flag.NewFlagSet("gen-issuer", flag.ContinueOnError)
		secret := fs.String("key", "issuer.key", "issuer private key file (keep off relay/client)")
		public := fs.String("public", "issuer.pub", "trusted issuer public-key file for relay")
		if err := fs.Parse(args[1:]); err != nil {
			return err
		}
		if _, err := os.Stat(*public); err == nil {
			return fmt.Errorf("refusing to overwrite %s", *public)
		}
		if _, err := os.Stat(*secret); err == nil {
			return fmt.Errorf("refusing to overwrite %s", *secret)
		}
		pub, priv, err := ed25519.GenerateKey(rand.Reader)
		if err != nil {
			return err
		}
		if err := writeNew(*secret, append([]byte(hex.EncodeToString(priv)), '\n'), 0600); err != nil {
			return err
		}
		if err := writeNew(*public, append([]byte(hex.EncodeToString(pub)), '\n'), 0644); err != nil {
			return err
		}
		fmt.Printf("Issuer public key saved to %s; private issuer key saved to %s (keep secret).\n", *public, *secret)
		return nil
	case "issue":
		fs := flag.NewFlagSet("issue", flag.ContinueOnError)
		issuer := fs.String("issuer-key", "issuer.key", "private issuer key (on trusted issuer host only)")
		relay := fs.String("relay", "", "relay host:UDP-port reachable by the client")
		relayKey := fs.String("relay-pub", "", "relay X25519 public key (64 hex chars)")
		game := fs.String("game", "roblox", "supported game profile")
		minutes := fs.Int("session-minutes", 30, "session lifetime 1..180 minutes")
		redeem := fs.Int("redeem-seconds", 120, "grant redemption window 5..300 seconds")
		out := fs.String("out", "", "new private session config JSON path")
		if err := fs.Parse(args[1:]); err != nil {
			return err
		}
		if *relay == "" || strings.ContainsAny(*relay, " \t\n") || *out == "" {
			return errors.New("-relay and -out are required")
		}
		_, portText, splitErr := net.SplitHostPort(*relay)
		if splitErr != nil {
			return fmt.Errorf("invalid -relay host:port: %w", splitErr)
		}
		port, portErr := strconv.Atoi(portText)
		if portErr != nil || port < 1 || port > 65535 {
			return errors.New("relay UDP port must be 1..65535")
		}
		if *game != "roblox" {
			return errors.New("currently this issuer example supports game=roblox only")
		}
		if *minutes < 1 || *minutes > 180 || *redeem < 5 || *redeem > 300 {
			return errors.New("session-minutes must be 1..180, redeem-seconds 5..300")
		}
		info, err := os.Lstat(*issuer)
		if err != nil {
			return err
		}
		if !info.Mode().IsRegular() || info.Mode().Perm()&0077 != 0 {
			return errors.New("private issuer file must be regular and permission 0600")
		}
		privateText, err := os.ReadFile(*issuer)
		if err != nil {
			return err
		}
		private, err := decodeKey(string(privateText), ed25519.PrivateKeySize)
		if err != nil {
			return err
		}
		relayRaw, err := decodeKey(*relayKey, 32)
		if err != nil {
			return err
		}
		now := uint64(time.Now().Unix())
		var id [16]byte
		if _, err := rand.Read(id[:]); err != nil {
			return err
		}
		pub := ed25519.PrivateKey(private).Public().(ed25519.PublicKey)
		ticket := sessionkey.Ticket{ID: id, IssuerKeyID: sessionkey.KeyID(pub), RelayKeyHash: sessionkey.RelayKeyHash(relayRaw), IssuedAt: now, RedeemBefore: now + uint64(*redeem), SessionTTL: uint32(*minutes * 60)}
		signed, err := sessionkey.Sign(ticket, ed25519.PrivateKey(private))
		if err != nil {
			return err
		}
		config := struct {
			Schema         int    `json:"schema"`
			Relay          string `json:"relay"`
			RelayPublicKey string `json:"relay_public_key"`
			Game           string `json:"game"`
			Grant          string `json:"grant"`
			TimeoutMessage string `json:"timeout_message"`
            ProfileID string `json:"profile_id"`
            ProfileRevision int `json:"profile_revision"`
            GameplayIPv4 string `json:"gameplay_ipv4"`
            PortMin int `json:"port_min"`
            PortMax int `json:"port_max"`
		}{2, *relay, hex.EncodeToString(relayRaw), *game, hex.EncodeToString(signed), "Session expired. Generate a new config.","roblox-sg",1,"128.116.46.33/32,128.116.50.33/32,128.116.54.33/32,128.116.97.33/32",49152,65535}
		data, err := json.MarshalIndent(config, "", "  ")
		if err != nil {
			return err
		}
		if err := writeNew(*out, append(data, '\n'), 0600); err != nil {
			return err
		}
		fmt.Printf("One-time session config saved to %s (treat as secret until redeemed; expires for admission in %d seconds).\n", *out, *redeem)
		return nil
	default:
		return fmt.Errorf("unknown command: %s", args[0])
	}
}
func main() {
	if err := run(os.Args[1:]); err != nil {
		fmt.Fprintln(os.Stderr, "Error:", err)
		os.Exit(1)
	}
}
