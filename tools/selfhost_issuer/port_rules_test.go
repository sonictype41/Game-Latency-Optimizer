package main

import "testing"

func TestNormalizeUDPRules(t *testing.T) {
 got,err:=normalizeUDPRules("7000-8000,8181,7500-8100,9000-9001,9002")
 if err!=nil||got!="7000-8100,8181,9000-9002"{t.Fatalf("unexpected rules: %q %v",got,err)}
 for _,v:=range []string{"", "0", "65536", "1,", "1,,2", "3-2", "abc", "1-2-3"}{
  if _,err:=normalizeUDPRules(v);err==nil{t.Errorf("accepted invalid %q",v)}
 }
}
