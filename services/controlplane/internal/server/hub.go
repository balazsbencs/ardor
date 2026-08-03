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
	connection *websocket.Conn
	writes     sync.Mutex
}

func (socket *deviceSocket) write(ctx context.Context, envelope cloudprotocol.Envelope) error {
	socket.writes.Lock()
	defer socket.writes.Unlock()
	writeContext, cancel := context.WithTimeout(ctx, 10*time.Second)
	defer cancel()
	return writeEnvelope(writeContext, socket.connection, envelope)
}

type deviceHub struct {
	mu          sync.RWMutex
	connections map[string]*deviceSocket
}

func newDeviceHub() *deviceHub {
	return &deviceHub{connections: map[string]*deviceSocket{}}
}

func (hub *deviceHub) attach(deviceID string, connection *websocket.Conn) *deviceSocket {
	socket := &deviceSocket{connection: connection}
	hub.mu.Lock()
	old := hub.connections[deviceID]
	hub.connections[deviceID] = socket
	hub.mu.Unlock()
	if old != nil {
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

func (hub *deviceHub) online(deviceID string) bool {
	hub.mu.RLock()
	defer hub.mu.RUnlock()
	return hub.connections[deviceID] != nil
}

func (hub *deviceHub) closeAll() {
	hub.mu.Lock()
	connections := hub.connections
	hub.connections = map[string]*deviceSocket{}
	hub.mu.Unlock()
	for _, socket := range connections {
		socket.connection.CloseNow()
	}
}
