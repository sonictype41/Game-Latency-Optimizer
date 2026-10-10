package glocore

import "net"

// TargetPolicy is the authoritative userspace destination filter. Optional
// upstream filtering may reject traffic earlier, but relay correctness and
// security never depend on it.
type TargetRule struct {
	Network *net.IPNet
	MinPort uint16
	MaxPort uint16
}
type TargetPolicy struct {
	Rules         []TargetRule
	Allowed       []*net.IPNet
	AllowPrivate  bool
	TargetPortMin uint16
	TargetPortMax uint16
}

func PublicIPv4(ip net.IP) bool {
	v := ip.To4()
	if v == nil {
		return false
	}
	if v[0] == 10 || v[0] == 127 || v[0] == 0 || v[0] >= 224 {
		return false
	}
	if v[0] == 172 && v[1] >= 16 && v[1] <= 31 {
		return false
	}
	if v[0] == 192 && v[1] == 168 {
		return false
	}
	if v[0] == 169 && v[1] == 254 {
		return false
	}
	if v[0] == 100 && v[1] >= 64 && v[1] <= 127 {
		return false
	}
	return true
}

func (p TargetPolicy) Allows(ip net.IP, targetPort, clientPort uint16) bool {
	if ip.To4() == nil || targetPort == 0 || clientPort == 0 {
		return false
	}
	if len(p.Rules) == 0 && p.TargetPortMin != 0 && targetPort < p.TargetPortMin {
		return false
	}
	if len(p.Rules) == 0 && p.TargetPortMax != 0 && targetPort > p.TargetPortMax {
		return false
	}
	if !p.AllowPrivate && !PublicIPv4(ip) {
		return false
	}
	if len(p.Rules) > 0 {
		for _, rule := range p.Rules {
			if rule.Network != nil && rule.Network.Contains(ip) && targetPort >= rule.MinPort && targetPort <= rule.MaxPort {
				return true
			}
		}
		return false
	}
	for _, n := range p.Allowed {
		if n != nil && n.Contains(ip) {
			return true
		}
	}
	return false
}
