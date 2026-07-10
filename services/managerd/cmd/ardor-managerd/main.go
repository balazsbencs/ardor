package main

import (
	"log"
	"net/http"

	"ardor.local/managerd/internal/config"
	"ardor.local/managerd/internal/server"
)

func main() {
	cfg, err := config.LoadFromEnv()
	if err != nil {
		log.Fatal(err)
	}
	addr := server.ListenAddress(cfg)
	log.Printf("ardor-managerd listening on %s dataRoot=%s auth=%t", addr, cfg.DataRoot, cfg.AuthEnabled)
	if err := http.ListenAndServe(addr, server.New(cfg)); err != nil {
		log.Fatal(err)
	}
}
