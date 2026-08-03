package server

import (
	"net"
	"net/http"
	"strings"
	"sync"
	"time"
)

type attemptState struct {
	failures     int
	blockedUntil time.Time
	lastSeen     time.Time
}

type attemptLimiter struct {
	mu       sync.Mutex
	attempts map[string]attemptState
}

func newAttemptLimiter() *attemptLimiter {
	return &attemptLimiter{attempts: map[string]attemptState{}}
}

func (limiter *attemptLimiter) allow(keys []string, now time.Time) bool {
	limiter.mu.Lock()
	defer limiter.mu.Unlock()
	for _, key := range keys {
		if state := limiter.attempts[key]; state.blockedUntil.After(now) {
			return false
		}
	}
	return true
}

func (limiter *attemptLimiter) failure(keys []string, now time.Time) {
	limiter.mu.Lock()
	defer limiter.mu.Unlock()
	for _, key := range keys {
		state := limiter.attempts[key]
		state.failures++
		state.lastSeen = now
		if state.failures >= 5 {
			delay := time.Second << min(state.failures-5, 8)
			if delay > 5*time.Minute {
				delay = 5 * time.Minute
			}
			state.blockedUntil = now.Add(delay)
		}
		limiter.attempts[key] = state
	}
	limiter.prune(now)
}

func (limiter *attemptLimiter) success(keys []string) {
	limiter.mu.Lock()
	defer limiter.mu.Unlock()
	for _, key := range keys {
		delete(limiter.attempts, key)
	}
}

func (limiter *attemptLimiter) prune(now time.Time) {
	if len(limiter.attempts) < 4096 {
		return
	}
	for key, state := range limiter.attempts {
		if state.lastSeen.Before(now.Add(-time.Hour)) {
			delete(limiter.attempts, key)
		}
	}
}

func rateLimitKeys(request *http.Request, username string) []string {
	host, _, err := net.SplitHostPort(request.RemoteAddr)
	if err != nil {
		host = request.RemoteAddr
	}
	username = strings.ToLower(strings.TrimSpace(username))
	if len(username) > 64 {
		username = username[:64]
	}
	return []string{"source:" + host, "account:" + username}
}

func claimRateLimitKeys(request *http.Request, accountID string) []string {
	host, _, err := net.SplitHostPort(request.RemoteAddr)
	if err != nil {
		host = request.RemoteAddr
	}
	return []string{"claim-source:" + host, "claim-account:" + accountID}
}
