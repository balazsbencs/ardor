package server

import (
	"context"
	"errors"
	"sync"
	"time"

	"ardor.local/cloudprotocol"
	"github.com/coder/websocket"
)

var errDeviceOffline = errors.New("device is offline")

type deviceSocket struct {
	connection             *websocket.Conn
	writes                 sync.Mutex
	pendingMu              sync.Mutex
	pending                map[string]chan cloudprotocol.Envelope
	closed                 chan struct{}
	closeOnce              sync.Once
	remoteMutationsEnabled bool
}

func (socket *deviceSocket) write(ctx context.Context, envelope cloudprotocol.Envelope) error {
	socket.writes.Lock()
	defer socket.writes.Unlock()
	writeContext, cancel := context.WithTimeout(ctx, 10*time.Second)
	defer cancel()
	return writeEnvelope(writeContext, socket.connection, envelope)
}

func (socket *deviceSocket) request(ctx context.Context, envelope cloudprotocol.Envelope, mutation bool) (cloudprotocol.Envelope, error) {
	if mutation && !socket.remoteMutationsEnabled {
		return cloudprotocol.Envelope{}, errors.New("remote mutations are disabled on the device")
	}
	response := make(chan cloudprotocol.Envelope, 1)
	socket.pendingMu.Lock()
	if _, exists := socket.pending[envelope.MessageID]; exists {
		socket.pendingMu.Unlock()
		return cloudprotocol.Envelope{}, errors.New("duplicate device request id")
	}
	socket.pending[envelope.MessageID] = response
	socket.pendingMu.Unlock()
	defer func() {
		socket.pendingMu.Lock()
		delete(socket.pending, envelope.MessageID)
		socket.pendingMu.Unlock()
	}()
	if err := socket.write(ctx, envelope); err != nil {
		return cloudprotocol.Envelope{}, err
	}
	select {
	case reply := <-response:
		return reply, nil
	case <-socket.closed:
		return cloudprotocol.Envelope{}, errDeviceOffline
	case <-ctx.Done():
		return cloudprotocol.Envelope{}, ctx.Err()
	}
}

func (socket *deviceSocket) deliver(envelope cloudprotocol.Envelope) bool {
	if envelope.CorrelationID == "" {
		return false
	}
	socket.pendingMu.Lock()
	response := socket.pending[envelope.CorrelationID]
	socket.pendingMu.Unlock()
	if response == nil {
		return false
	}
	select {
	case response <- envelope:
		return true
	default:
		return false
	}
}

func (socket *deviceSocket) shutdown() {
	socket.closeOnce.Do(func() { close(socket.closed) })
}

type deviceHub struct {
	mu          sync.RWMutex
	connections map[string]*deviceSocket
}

func newDeviceHub() *deviceHub {
	return &deviceHub{connections: map[string]*deviceSocket{}}
}

func (hub *deviceHub) attach(deviceID string, connection *websocket.Conn, remoteMutationsEnabled bool) *deviceSocket {
	socket := &deviceSocket{
		connection: connection, pending: map[string]chan cloudprotocol.Envelope{},
		closed: make(chan struct{}), remoteMutationsEnabled: remoteMutationsEnabled,
	}
	hub.mu.Lock()
	old := hub.connections[deviceID]
	hub.connections[deviceID] = socket
	hub.mu.Unlock()
	if old != nil {
		old.shutdown()
		_ = old.connection.Close(websocket.StatusPolicyViolation, "superseded by newer device connection")
	}
	return socket
}

func (hub *deviceHub) detach(deviceID string, socket *deviceSocket) {
	hub.mu.Lock()
	defer hub.mu.Unlock()
	if hub.connections[deviceID] == socket {
		delete(hub.connections, deviceID)
	}
	socket.shutdown()
}

func (hub *deviceHub) request(ctx context.Context, deviceID string, envelope cloudprotocol.Envelope, mutation bool) (cloudprotocol.Envelope, error) {
	hub.mu.RLock()
	socket := hub.connections[deviceID]
	hub.mu.RUnlock()
	if socket == nil {
		return cloudprotocol.Envelope{}, errDeviceOffline
	}
	return socket.request(ctx, envelope, mutation)
}

func (hub *deviceHub) send(ctx context.Context, deviceID string, envelope cloudprotocol.Envelope) error {
	hub.mu.RLock()
	socket := hub.connections[deviceID]
	hub.mu.RUnlock()
	if socket == nil {
		return errDeviceOffline
	}
	return socket.write(ctx, envelope)
}

func (hub *deviceHub) status(deviceID string) (online bool, remoteMutationsEnabled bool) {
	hub.mu.RLock()
	defer hub.mu.RUnlock()
	socket := hub.connections[deviceID]
	if socket == nil {
		return false, false
	}
	return true, socket.remoteMutationsEnabled
}

func (hub *deviceHub) closeAll() {
	hub.mu.Lock()
	connections := hub.connections
	hub.connections = map[string]*deviceSocket{}
	hub.mu.Unlock()
	for _, socket := range connections {
		socket.shutdown()
		socket.connection.CloseNow()
	}
}
