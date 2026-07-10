package config

import (
	"errors"
	"os"
	"strconv"
)

type Config struct {
	DataRoot    string
	Bind        string
	Port        int
	AuthEnabled bool
	Token       string
}

func LoadFromEnv() (Config, error) {
	cfg := Config{
		DataRoot:    env("ARDOR_DATA_ROOT", "/opt/ardor-pedal"),
		Bind:        env("ARDOR_API_BIND", "0.0.0.0"),
		AuthEnabled: env("ARDOR_API_AUTH", "on") != "off",
		Token:       os.Getenv("ARDOR_API_TOKEN"),
	}
	port, err := strconv.Atoi(env("ARDOR_API_PORT", "8080"))
	if err != nil || port < 1 || port > 65535 {
		if err == nil {
			err = errors.New("port must be between 1 and 65535")
		}
		return Config{}, err
	}
	cfg.Port = port
	if cfg.AuthEnabled && cfg.Token == "" {
		return Config{}, errors.New("ARDOR_API_TOKEN is required when auth is enabled")
	}
	return cfg, nil
}

func env(key string, fallback string) string {
	if value := os.Getenv(key); value != "" {
		return value
	}
	return fallback
}
