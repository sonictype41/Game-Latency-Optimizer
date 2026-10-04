package main

import (
	"os"
	"path/filepath"
	"testing"
)

func TestLoadCIDRFile(t *testing.T) {
	dir := t.TempDir()
	path := filepath.Join(dir, "allow.txt")
	if err := os.WriteFile(path, []byte("# game targets\n203.0.113.0/24\n198.51.100.7/32 # exact\n\n"), 0600); err != nil {
		t.Fatal(err)
	}
	var got cidrList
	if err := loadCIDRFile(path, &got); err != nil {
		t.Fatal(err)
	}
	if len(got) != 2 || got[0].String() != "203.0.113.0/24" || got[1].String() != "198.51.100.7/32" {
		t.Fatalf("unexpected CIDRs: %v", got)
	}
}

func TestLoadCIDRFileRejectsBadLine(t *testing.T) {
	path := filepath.Join(t.TempDir(), "bad.txt")
	if err := os.WriteFile(path, []byte("not-a-cidr\n"), 0600); err != nil {
		t.Fatal(err)
	}
	var got cidrList
	if err := loadCIDRFile(path, &got); err == nil {
		t.Fatal("invalid CIDR file was accepted")
	}
}

func TestTargetAllowedPrivateStillRequiresCIDR(t *testing.T) {
	var allowed cidrList
	if err := allowed.Set("10.77.0.0/24"); err != nil {
		t.Fatal(err)
	}
	if !targetAllowed([]byte{10, 77, 0, 12}, allowed, true) {
		t.Fatal("explicit private CIDR was rejected with allow-private-targets")
	}
	if targetAllowed([]byte{10, 88, 0, 12}, allowed, true) {
		t.Fatal("private target outside allowlist was accepted")
	}
	if targetAllowed([]byte{203, 0, 113, 7}, allowed, true) {
		t.Fatal("public target outside allowlist was accepted")
	}
	if targetAllowed([]byte{10, 77, 0, 12}, allowed, false) {
		t.Fatal("private target accepted without allow-private-targets")
	}
}
