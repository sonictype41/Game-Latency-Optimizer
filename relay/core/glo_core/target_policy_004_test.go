package glocore

import (
	"net"
	"testing"
)

func TestProfileDestinationPortPolicy004(t *testing.T) {
	_, n, _ := net.ParseCIDR("128.116.97.33/32")
	p := TargetPolicy{Allowed: []*net.IPNet{n}, TargetPortMin: 49152, TargetPortMax: 65535}
	if !p.Allows(net.ParseIP("128.116.97.33"), 53388, 50000) {
		t.Fatal("profile datagram rejected")
	}
	for _, x := range []struct {
		ip   string
		port uint16
	}{{"128.116.97.34", 53388}, {"128.116.97.33", 53}, {"10.0.0.1", 53388}} {
		if p.Allows(net.ParseIP(x.ip), x.port, 50000) {
			t.Fatal("unauthorized destination accepted", x)
		}
	}
}
