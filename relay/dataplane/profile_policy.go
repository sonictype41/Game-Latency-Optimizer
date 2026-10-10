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
		if ones != 32 || bits != 32 || !glocore.PublicIPv4(ip) || ip.String() != n.IP.String() {
			return nil, errors.New("relay profile must contain only public /32 routes")
		}
		if seen[r.CIDR] || r.PortMin < 1 || r.PortMax < r.PortMin {
			return nil, errors.New("duplicate route or invalid UDP port policy")
		}
		seen[r.CIDR] = true
		out = append(out, glocore.TargetRule{Network: n, MinPort: r.PortMin, MaxPort: r.PortMax})
	}
	return out, nil
}
