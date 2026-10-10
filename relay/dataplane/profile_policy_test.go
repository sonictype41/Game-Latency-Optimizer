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
