package timesync

import (
	"context"
	"encoding/binary"
	"net"
	"testing"
	"time"
)

func TestSyncSetsClockFromNTPResponse(t *testing.T) {
	server, err := net.ListenPacket("udp", "127.0.0.1:0")
	if err != nil {
		t.Fatal(err)
	}
	defer server.Close()
	want := time.Date(2026, time.August, 21, 17, 0, 1, 500_000_000, time.UTC)
	go func() {
		request := make([]byte, 48)
		_, address, err := server.ReadFrom(request)
		if err != nil {
			return
		}
		response := make([]byte, 48)
		binary.BigEndian.PutUint32(response[40:44], uint32(want.Unix()+ntpEpochOffset))
		binary.BigEndian.PutUint32(response[44:48], uint32(uint64(want.Nanosecond())<<32/uint64(time.Second)))
		_, _ = server.WriteTo(response, address)
	}()
	var got time.Time
	client := newClient()
	client.servers = []string{server.LocalAddr().String()}
	client.setClock = func(clock time.Time) error { got = clock; return nil }
	if err := client.sync(context.Background()); err != nil {
		t.Fatal(err)
	}
	if !got.Equal(want) {
		t.Fatalf("clock=%s want %s", got, want)
	}
}
