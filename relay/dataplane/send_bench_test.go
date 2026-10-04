package main

import (
	"runtime"
	"sync"
	"testing"
)

func BenchmarkSendGuardWithGlobalMutex(b *testing.B) {
	var mu sync.Mutex
	b.RunParallel(func(pb *testing.PB) {
		var local uint64
		for pb.Next() {
			mu.Lock()
			local++
			mu.Unlock()
		}
		runtime.KeepAlive(local)
	})
}

func BenchmarkSendGuardWithoutGlobalMutex(b *testing.B) {
	b.RunParallel(func(pb *testing.PB) {
		var local uint64
		for pb.Next() {
			local++
		}
		runtime.KeepAlive(local)
	})
}
