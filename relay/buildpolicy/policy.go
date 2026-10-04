// Package buildpolicy implements compile-time embedded relay argument policies.
package buildpolicy

import (
	"encoding/json"
	"errors"
	"flag"
	"fmt"
	"io"
	"strconv"
	"strings"
)

type Rule struct {
	Enabled   bool    `json:"enabled"`
	Default   string  `json:"default"`
	Min       *uint64 `json:"min,omitempty"`
	Max       *uint64 `json:"max,omitempty"`
	AllowZero bool    `json:"allow_zero"`
}
type Policy struct {
	Version string          `json:"version"`
	Edition string          `json:"edition"`
	Args    map[string]Rule `json:"args"`
}

func Load(raw string) (Policy, error) {
	var p Policy
	d := json.NewDecoder(strings.NewReader(raw))
	d.DisallowUnknownFields()
	if err := d.Decode(&p); err != nil {
		return p, err
	}
	if err := d.Decode(new(any)); err != io.EOF {
		return p, errors.New("trailing policy data")
	}
	if p.Version == "" || p.Edition != "generic" {
		return p, errors.New("invalid build identity")
	}
	return p, nil
}
func (p Policy) Apply(fs *flag.FlagSet) error {
	var err error
	fs.VisitAll(func(f *flag.Flag) {
		if err != nil {
			return
		}
		if f.Name == "build-info" || f.Name == "check-config" {
			return
		}
		r, ok := p.Args[f.Name]
		if !ok {
			err = fmt.Errorf("missing build policy for --%s", f.Name)
			return
		}
		f.DefValue = r.Default
		if !r.Enabled {
			f.Usage += " [disabled in this build]"
		}
		// Empty repeatable allow-cidr means no additions. It cannot be Set("").
		if f.Name == "allow-cidr" && r.Default == "" {
			return
		}
		if e := f.Value.Set(r.Default); e != nil {
			err = fmt.Errorf("invalid default --%s: %w", f.Name, e)
		}
	})
	if err != nil {
		return err
	}
	for name := range p.Args {
		if fs.Lookup(name) == nil || name == "build-info" || name == "check-config" {
			return fmt.Errorf("unknown policy argument: %s", name)
		}
	}
	return p.Validate(fs)
}
func (p Policy) Validate(fs *flag.FlagSet) error {
	if fs.NArg() != 0 {
		return errors.New("unexpected positional arguments")
	}
	var err error
	fs.Visit(func(f *flag.Flag) {
		if r, ok := p.Args[f.Name]; ok && !r.Enabled {
			err = fmt.Errorf("argument disabled in this build: --%s", f.Name)
		}
	})
	if err != nil {
		return err
	}
	for name, r := range p.Args {
		if r.Min == nil && r.Max == nil {
			continue
		}
		if r.Min == nil || r.Max == nil || *r.Min > *r.Max {
			return fmt.Errorf("invalid numeric policy --%s", name)
		}
		f := fs.Lookup(name)
		if f == nil {
			return fmt.Errorf("unknown flag --%s", name)
		}
		n, e := strconv.ParseUint(f.Value.String(), 10, 64)
		if e != nil || !(n == 0 && r.AllowZero) && (n < *r.Min || n > *r.Max) {
			return fmt.Errorf("--%s must be %d..%d (allow_zero=%t)", name, *r.Min, *r.Max, r.AllowZero)
		}
	}
	return nil
}

// Precheck prevents Go flag parsing from silently accepting duplicate overrides
// or stopping at the first positional argument. Repeatable CIDRs are intentional.
func Precheck(fs *flag.FlagSet, args []string) error {
	seen := map[string]bool{}
	for i := 0; i < len(args); i++ {
		a := args[i]
		if !strings.HasPrefix(a, "-") || a == "-" || a == "--" {
			return fmt.Errorf("unexpected argument: %s", a)
		}
		name, value, has := strings.Cut(strings.TrimPrefix(strings.TrimPrefix(a, "-"), "-"), "=")
		f := fs.Lookup(name)
		if f == nil {
			if name == "h" || name == "help" {
				continue
			}
			return fmt.Errorf("unknown argument: %s", a)
		}
		if seen[name] && name != "allow-cidr" {
			return fmt.Errorf("duplicate argument: --%s", name)
		}
		seen[name] = true
		b, isBool := f.Value.(interface{ IsBoolFlag() bool })
		if !has && !(isBool && b.IsBoolFlag()) {
			i++
			if i >= len(args) || strings.HasPrefix(args[i], "--") {
				return fmt.Errorf("missing value: --%s", name)
			}
			value = args[i]
		}
		if has && value == "" {
			return fmt.Errorf("empty value: --%s", name)
		}
	}
	return nil
}
