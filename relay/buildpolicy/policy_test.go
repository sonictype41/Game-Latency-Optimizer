package buildpolicy

import (
	"flag"
	"io"
	"testing"
)

func pointer(n uint64) *uint64 { return &n }
func fixture(t *testing.T, enabled bool) (Policy, *flag.FlagSet) {
	t.Helper()
	fs := flag.NewFlagSet("test", flag.ContinueOnError)
	fs.SetOutput(io.Discard)
	fs.Uint("limit", 10, "")
	fs.Bool("debug", false, "")
	return Policy{Version: "0.13.0", Edition: "generic", Args: map[string]Rule{
		"limit": {Enabled: enabled, Default: "10", Min: pointer(1), Max: pointer(100)},
		"debug": {Enabled: enabled, Default: "false"},
	}}, fs
}
func TestValidation(t *testing.T) {
	for _, tc := range []struct {
		args []string
		ok   bool
	}{
		{[]string{}, true}, {[]string{"--limit", "1"}, true}, {[]string{"--limit=100"}, true},
		{[]string{"--limit=0"}, false}, {[]string{"--limit=101"}, false}, {[]string{"--limit=-1"}, false},
		{[]string{"--limit=2x"}, false}, {[]string{"--limit=18446744073709551616"}, false},
		{[]string{"--limit"}, false}, {[]string{"--debug", "false"}, false}, {[]string{"--wat"}, false},
		{[]string{"--limit=10", "--limit=11"}, false}, {[]string{"positional", "--limit=0"}, false},
	} {
		p, fs := fixture(t, true)
		err := p.Apply(fs)
		if err == nil {
			err = Precheck(fs, tc.args)
		}
		if err == nil {
			err = fs.Parse(tc.args)
		}
		if err == nil {
			err = p.Validate(fs)
		}
		if (err == nil) != tc.ok {
			t.Fatalf("%v: %v", tc.args, err)
		}
	}
}
func TestLockedEvenDefault(t *testing.T) {
	p, fs := fixture(t, false)
	if e := p.Apply(fs); e != nil {
		t.Fatal(e)
	}
	fs.Parse([]string{"--limit=10"})
	if p.Validate(fs) == nil {
		t.Fatal("locked flag accepted")
	}
}
func TestMissingRule(t *testing.T) {
	p, fs := fixture(t, true)
	delete(p.Args, "debug")
	if p.Apply(fs) == nil {
		t.Fatal("missing policy accepted")
	}
}
func TestZeroExplicit(t *testing.T) {
	p, fs := fixture(t, true)
	r := p.Args["limit"]
	r.AllowZero = true
	p.Args["limit"] = r
	if e := p.Apply(fs); e != nil {
		t.Fatal(e)
	}
	fs.Parse([]string{"--limit=0"})
	if e := p.Validate(fs); e != nil {
		t.Fatal(e)
	}
}
func TestInvalidDefault(t *testing.T) {
	p, fs := fixture(t, true)
	r := p.Args["limit"]
	r.Default = "101"
	p.Args["limit"] = r
	if p.Apply(fs) == nil {
		t.Fatal("bad default accepted")
	}
}
