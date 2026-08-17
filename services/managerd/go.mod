module ardor.local/managerd

go 1.25.0

require (
	ardor.local/cloudprotocol v0.0.0
	github.com/coder/websocket v1.8.15
	github.com/grandcat/zeroconf v1.0.0
	golang.org/x/crypto v0.55.0
	golang.org/x/sys v0.47.0
)

require (
	github.com/cenkalti/backoff v2.2.1+incompatible // indirect
	github.com/miekg/dns v1.1.27 // indirect
	golang.org/x/net v0.57.0 // indirect
)

replace ardor.local/cloudprotocol => ../../protocol/cloud
