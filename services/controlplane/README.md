# Ardor hosted control plane

This service provides hosted accounts, recovery, device presence, and the
physically confirmed device-claim protocol. It intentionally uses SQLite for
the first single-instance deployment. WAL mode, foreign keys, a busy timeout,
and one ordered writer connection are configured at open time.

## Local development

```sh
ARDOR_INSECURE_HTTP=on \
ARDOR_PUBLIC_ORIGIN=http://127.0.0.1:8090 \
go run ./cmd/ardor-controlplane
```

The default database is `data/controlplane.sqlite`. Override it with
`ARDOR_CONTROL_DB`; change the listener with `ARDOR_CONTROL_BIND`.

Production requires an HTTPS `ARDOR_PUBLIC_ORIGIN`. A reverse proxy should
serve the manager's `dist-hosted` bundle and route `/v1`, `/healthz`, and the
device WebSocket to this service on the same origin. Back up the SQLite database
with a SQLite-aware online backup or a brief service stop; do not copy only the
main file while WAL writes are active.

Remote preset mutations are not enabled in this phase.
