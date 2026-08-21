// Package timesync bootstraps wall-clock time on hardware without an RTC.
package timesync

import (
	"context"
	"encoding/binary"
	"fmt"
	"log"
	"net"
	"time"

	"golang.org/x/sys/unix"
)

const (
	ntpEpochOffset = 2_208_988_800
	minValidYear   = 2024
)

var defaultServers = []string{
	"0.pool.ntp.org:123",
	"1.pool.ntp.org:123",
	"time.cloudflare.com:123",
}

type client struct {
	servers  []string
	dial     func(context.Context, string, string) (net.Conn, error)
	setClock func(time.Time) error
}

func newClient() client {
	return client{
		servers:  defaultServers,
		dial:     (&net.Dialer{Timeout: 5 * time.Second}).DialContext,
		setClock: setClock,
	}
}

// Run keeps trying while the network is coming up, then refreshes daily.
// Failure is non-fatal: the manager remains available on an offline pedal.
func Run(ctx context.Context, logger *log.Logger) {
	client := newClient()
	delay := 5 * time.Second
	for {
		if err := client.sync(ctx); err == nil {
			logger.Printf("ardor clock synchronized via NTP")
			delay = 24 * time.Hour
		} else if ctx.Err() == nil {
			logger.Printf("ardor clock sync unavailable: %v; retrying", err)
			delay = min(delay*2, 5*time.Minute)
		}
		select {
		case <-ctx.Done():
			return
		case <-time.After(delay):
		}
	}
}

func (client client) sync(ctx context.Context) error {
	var lastErr error
	for _, server := range client.servers {
		clock, err := client.request(ctx, server)
		if err != nil {
			lastErr = err
			continue
		}
		return client.setClock(clock)
	}
	if lastErr == nil {
		lastErr = fmt.Errorf("no NTP servers configured")
	}
	return lastErr
}

func (client client) request(ctx context.Context, server string) (time.Time, error) {
	connection, err := client.dial(ctx, "udp", server)
	if err != nil {
		return time.Time{}, err
	}
	defer connection.Close()
	if deadline, ok := ctx.Deadline(); ok {
		_ = connection.SetDeadline(deadline)
	} else {
		_ = connection.SetDeadline(time.Now().Add(5 * time.Second))
	}
	request := make([]byte, 48)
	request[0] = 0x23 // leap=0, version=4, client mode=3
	if _, err := connection.Write(request); err != nil {
		return time.Time{}, err
	}
	response := make([]byte, 48)
	if _, err := connection.Read(response); err != nil {
		return time.Time{}, err
	}
	seconds := binary.BigEndian.Uint32(response[40:44])
	fraction := binary.BigEndian.Uint32(response[44:48])
	if seconds < ntpEpochOffset {
		return time.Time{}, fmt.Errorf("NTP response predates the Unix epoch")
	}
	clock := time.Unix(int64(seconds-ntpEpochOffset), int64(fraction)*int64(time.Second)>>32).UTC()
	if clock.Year() < minValidYear {
		return time.Time{}, fmt.Errorf("NTP response has implausible year %d", clock.Year())
	}
	return clock, nil
}

func setClock(clock time.Time) error {
	return unix.ClockSettime(unix.CLOCK_REALTIME, &unix.Timespec{Sec: clock.Unix(), Nsec: int64(clock.Nanosecond())})
}
