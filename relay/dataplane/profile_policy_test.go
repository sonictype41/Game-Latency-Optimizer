package main

import (
	glocore "glo.local/glo/relay/core/glo_core"
	"net"
	"os"
	"path/filepath"
	"testing"
)

func TestPublishedProfilePolicy004(t *testing.T) {
	p := filepath.Join(t.TempDir(), "profile.json")
	os.WriteFile(p, []byte(`{"schema":1,"revision":1,"routes":[{"cidr":"128.116.97.33/32","port_min":49152,"port_max":65535}]}`), 0600)
	rules, e := readProfileRules(p)
	if e != nil || len(rules) != 1 {
		t.Fatal(e)
	}
	policy := glocore.TargetPolicy{Rules: rules}
	if !policy.Allows(net.ParseIP("128.116.97.33"), 53388, 50000) || policy.Allows(net.ParseIP("128.116.97.33"), 53, 50000) {
		t.Fatal("bad destination port control")
	}
}

func TestMultipleGamesShareHostWithDifferentUDPPorts(t *testing.T) {
	p := filepath.Join(t.TempDir(), "profiles.json")
	content := `{"schema":1,"revision":2,"routes":[{"cidr":"43.229.65.1/32","port_min":7000,"port_max":8000},{"cidr":"43.229.65.1/32","port_min":8180,"port_max":8181}]}`
	if err := os.WriteFile(p, []byte(content), 0600); err != nil {
		t.Fatal(err)
	}
	rules, err := readProfileRules(p)
	if err != nil {
		t.Fatal(err)
	}
	policy := glocore.TargetPolicy{Rules: rules}
	if !policy.Allows(net.ParseIP("43.229.65.1"), 7231, 61582) || !policy.Allows(net.ParseIP("43.229.65.1"), 8181, 61582) || policy.Allows(net.ParseIP("43.229.65.1"), 8100, 61582) {
		t.Fatal("port gap incorrectly allowed or valid range rejected")
	}
}
