package discovery

import (
	"context"
	"errors"
	"fmt"
	"log"
	"net"
	"strings"

	"github.com/grandcat/zeroconf"
)

const Service = "_ardor-manager._tcp"

func Hostname(deviceID string) (string, error) {
	compact := strings.ToLower(strings.ReplaceAll(deviceID, "-", ""))
	if len(compact) < 8 {
		return "", errors.New("device ID is too short for discovery hostname")
	}
	for _, character := range compact[:8] {
		if (character < 'a' || character > 'f') && (character < '0' || character > '9') {
			return "", errors.New("device ID is not hexadecimal")
		}
	}
	return "ardor-" + compact[:8], nil
}

func Run(ctx context.Context, deviceID string, port int, logger *log.Logger) error {
	hostname, err := Hostname(deviceID)
	if err != nil {
		return err
	}
	addresses, interfaces, err := localAddresses()
	if err != nil {
		return err
	}
	if len(addresses) == 0 {
		return errors.New("no multicast-capable local address is available")
	}
	server, err := zeroconf.RegisterProxy(
		"Ardor Pedal "+strings.TrimPrefix(hostname, "ardor-"), Service, "local.", port,
		hostname+".local.", addresses,
		[]string{"path=/", "version=1", "auth=local-session"}, interfaces,
	)
	if err != nil {
		return fmt.Errorf("register mDNS service: %w", err)
	}
	defer server.Shutdown()
	if logger != nil {
		logger.Printf("ardor local discovery available at http://%s.local:%d", hostname, port)
	}
	<-ctx.Done()
	return nil
}

func localAddresses() ([]string, []net.Interface, error) {
	all, err := net.Interfaces()
	if err != nil {
		return nil, nil, err
	}
	addresses := []string{}
	interfaces := []net.Interface{}
	for _, networkInterface := range all {
		if networkInterface.Flags&net.FlagUp == 0 || networkInterface.Flags&net.FlagLoopback != 0 || networkInterface.Flags&net.FlagMulticast == 0 {
			continue
		}
		interfaceAddresses, err := networkInterface.Addrs()
		if err != nil {
			continue
		}
		added := false
		for _, address := range interfaceAddresses {
			ip, _, err := net.ParseCIDR(address.String())
			if err != nil || ip.IsLoopback() || ip.IsUnspecified() || !ip.IsGlobalUnicast() {
				continue
			}
			addresses = append(addresses, ip.String())
			added = true
		}
		if added {
			interfaces = append(interfaces, networkInterface)
		}
	}
	return addresses, interfaces, nil
}
