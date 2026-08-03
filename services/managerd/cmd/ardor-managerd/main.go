package main

import (
	"context"
	"errors"
	"log"
	"net/http"
	"os/signal"
	"syscall"
	"time"

	"ardor.local/managerd/internal/cloudagent"
	"ardor.local/managerd/internal/config"
	"ardor.local/managerd/internal/deviceclaim"
	"ardor.local/managerd/internal/deviceidentity"
	"ardor.local/managerd/internal/discovery"
	"ardor.local/managerd/internal/server"
	"ardor.local/managerd/internal/webui"
)

func main() {
	cfg, err := config.LoadFromEnv()
	if err != nil {
		log.Fatal(err)
	}
	ctx, stop := signal.NotifyContext(context.Background(), syscall.SIGINT, syscall.SIGTERM)
	defer stop()
	identity, err := deviceidentity.LoadOrCreate(cfg.DataRoot)
	if err != nil {
		log.Fatalf("load device identity: %v", err)
	}
	if cfg.DiscoveryEnabled {
		go func() {
			if err := discovery.Run(ctx, identity.DeviceID, cfg.Port, log.Default()); err != nil && ctx.Err() == nil {
				log.Printf("ardor local discovery unavailable: %v", err)
			}
		}()
	}
	if cfg.CloudEnabled {
		claimGate, err := deviceclaim.NewFileGate(cfg.DataRoot)
		if err != nil {
			log.Fatalf("configure physical claim gate: %v", err)
		}
		go claimGate.Run(ctx)
		agent, err := cloudagent.New(cloudagent.Config{
			BaseURL: cfg.CloudURL, ClaimGate: claimGate, DataRoot: cfg.DataRoot,
			RemoteMutationsEnabled: cfg.CloudRemoteMutationsEnabled,
		}, identity)
		if err != nil {
			log.Fatalf("configure cloud agent: %v", err)
		}
		go agent.Run(ctx)
		log.Printf("ardor cloud connection enabled (remote mutations=%t)", cfg.CloudRemoteMutationsEnabled)
	}
	addr := server.ListenAddress(cfg)
	log.Printf("ardor-managerd listening on %s dataRoot=%s auth=%t", addr, cfg.DataRoot, cfg.AuthEnabled)
	handler, err := server.Build(ctx, cfg, webui.Files())
	if err != nil {
		log.Fatal(err)
	}
	httpServer := &http.Server{Addr: addr, Handler: handler, ReadHeaderTimeout: 10 * time.Second}
	go func() {
		<-ctx.Done()
		shutdownContext, cancel := context.WithTimeout(context.Background(), 5*time.Second)
		defer cancel()
		_ = httpServer.Shutdown(shutdownContext)
	}()
	if err := httpServer.ListenAndServe(); err != nil && !errors.Is(err, http.ErrServerClosed) {
		log.Fatal(err)
	}
}
