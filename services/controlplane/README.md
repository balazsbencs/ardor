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

The hosted manager can relay preset list/read operations to a claimed online
pedal. Save/apply requests additionally require a UUID `Idempotency-Key`, are
recorded durably with their response, and only run when the pedal explicitly
enables remote mutations.

Asset list, upload, rename, and delete use the same authenticated outbound
device connection. Uploads are streamed in bounded chunks and published only
after the pedal verifies their size, SHA-256, extension, and basic file format.

To enable hosted TONE3000 selection, configure its publishable OAuth client ID:

```sh
TONE3000_CLIENT_ID=your_publishable_client_id
```

Register `${ARDOR_PUBLIC_ORIGIN}/v1/integrations/tone3000/callback` as the fixed
callback. `TONE3000_BASE_URL` is optional and defaults to the official service.
OAuth access remains in server memory for the short selection/install flow and
is never returned to the browser or pedal.
