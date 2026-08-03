module ardor.local/managerd

go 1.22

require (
	ardor.local/cloudprotocol v0.0.0
	github.com/coder/websocket v1.8.13
)

replace ardor.local/cloudprotocol => ../../protocol/cloud
