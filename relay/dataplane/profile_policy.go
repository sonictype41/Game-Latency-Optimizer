package main

import (
	"encoding/json"
	"errors"
	"fmt"
	glocore "glo.local/glo/relay/core/glo_core"
	"net"
	"os"
)

type profilePolicyFileFormat struct {
	Schema   int `json:"schema"`
	Revision int `json:"revision"`
	Routes   []struct {
		CIDR    string `json:"cidr"`
		PortMin uint16 `json:"port_min"`
		PortMax uint16 `json:"port_max"`
	} `json:"routes"`
}

func readProfileRules(path string) ([]glocore.TargetRule, error) {
	f, err := os.Open(path)
	if err != nil {
		return nil, err
	}
	defer f.Close()
	info, err := f.Stat()
	if err != nil {
		return nil, err
	}
	if info.Size() > 131072 || info.Size() == 0 {
		return nil, errors.New("oversized/empty relay profile policy")
	}
	decoder := json.NewDecoder(f)
	decoder.DisallowUnknownFields()
	var doc profilePolicyFileFormat
	if err := decoder.Decode(&doc); err != nil {
		return nil, err
	}
	if doc.Schema != 1 || doc.Revision < 1 || len(doc.Routes) < 1 || len(doc.Routes) > 512 {
		return nil, errors.New("invalid relay profile policy header")
	}
	seen := map[string]bool{}
	out := make([]glocore.TargetRule, 0, len(doc.Routes))
	for _, r := range doc.Routes {
		ip, n, e := net.ParseCIDR(r.CIDR)
		if e != nil || ip.To4() == nil || n == nil {
			return nil, fmt.Errorf("invalid CIDR: %s", r.CIDR)
		}
		ones, bits := n.Mask.Size()
		if ones < 16 || ones > 32 || bits != 32 || !glocore.PublicIPv4(ip) || !glocore.PublicIPv4(lastIPv4(n)) || !ip.Equal(n.IP) {
			return nil, errors.New("relay profile requires aligned public /16-/32 CIDRs")
		}
		key := fmt.Sprintf("%s:%d-%d", r.CIDR, r.PortMin, r.PortMax)
		if seen[key] || r.PortMin < 1 || r.PortMax < r.PortMin {
			return nil, errors.New("duplicate route or invalid UDP port policy")
		}
		seen[key] = true
		out = append(out, glocore.TargetRule{Network: n, MinPort: r.PortMin, MaxPort: r.PortMax})
	}
	return out, nil
}

// Last IPv4 address must also be globally routable; reject prefixes spanning restricted space.
func lastIPv4(n *net.IPNet) net.IP {
	ip := n.IP.To4()
	if ip == nil {
		return nil
	}
	last := append(net.IP(nil), ip...)
	for i := range last {
		last[i] |= ^n.Mask[i]
	}
	return last
}
