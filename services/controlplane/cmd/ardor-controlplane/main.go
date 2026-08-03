package main

import (
	"context"
	"errors"
	"log"
	"net/http"
	"os"
	"os/signal"
	"path/filepath"
	"strings"
	"syscall"
	"time"

	controlserver "ardor.local/controlplane/internal/server"
	"ardor.local/controlplane/internal/store"
)

func main() {
	bind := environment("ARDOR_CONTROL_BIND", "127.0.0.1:8090")
	databasePath := environment("ARDOR_CONTROL_DB", "data/controlplane.sqlite")
	publicOrigin := environment("ARDOR_PUBLIC_ORIGIN", "http://127.0.0.1:8090")
	insecureDevelopment := os.Getenv("ARDOR_INSECURE_HTTP") == "on"
	if !insecureDevelopment && !strings.HasPrefix(publicOrigin, "https://") {
		log.Fatal("ARDOR_PUBLIC_ORIGIN must use HTTPS unless ARDOR_INSECURE_HTTP=on")
	}
	if err := os.MkdirAll(filepath.Dir(databasePath), 0o700); err != nil {
		log.Fatal(err)
	}
	repository, err := store.OpenSQLite(databasePath)
	if err != nil {
		log.Fatal(err)
	}
	defer repository.Close()
	if err := repository.Migrate(context.Background()); err != nil {
		log.Fatal(err)
	}
	controlPlane, err := controlserver.New(controlserver.Config{PublicOrigin: publicOrigin, SecureCookies: !insecureDevelopment}, repository)
	if err != nil {
		log.Fatal(err)
	}
	defer controlPlane.Close()

	ctx, stop := signal.NotifyContext(context.Background(), syscall.SIGINT, syscall.SIGTERM)
	defer stop()
	httpServer := &http.Server{Addr: bind, Handler: controlPlane.Handler(), ReadHeaderTimeout: 10 * time.Second}
	go func() {
		<-ctx.Done()
		shutdownContext, cancel := context.WithTimeout(context.Background(), 10*time.Second)
		defer cancel()
		_ = httpServer.Shutdown(shutdownContext)
	}()
	log.Printf("ardor-controlplane listening on %s origin=%s database=%s", bind, publicOrigin, databasePath)
	if err := httpServer.ListenAndServe(); err != nil && !errors.Is(err, http.ErrServerClosed) {
		log.Fatal(err)
	}
}

func environment(key, fallback string) string {
	if value := os.Getenv(key); value != "" {
		return value
	}
	return fallback
}
