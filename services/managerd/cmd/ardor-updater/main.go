package main

import (
	"context"
	"fmt"
	"log"
	"os"
	"os/signal"
	"syscall"

	"ardor.local/managerd/internal/update"
)

func main() {
	if len(os.Args) == 2 && os.Args[1] == "--version" {
		fmt.Printf("ardor-updater %s\n", update.UpdaterVersion)
		return
	}
	if len(os.Args) != 3 || (os.Args[1] != "apply" && os.Args[1] != "recover") {
		log.Fatal("usage: ardor-updater {apply <request.json>|recover <system-root>}")
	}
	ctx, stop := signal.NotifyContext(context.Background(), syscall.SIGINT, syscall.SIGTERM)
	defer stop()
	installer := update.Installer{}
	var err error
	if os.Args[1] == "apply" {
		err = installer.Apply(ctx, os.Args[2])
	} else {
		err = installer.Recover(ctx, os.Args[2])
	}
	if err != nil {
		log.Fatal(err)
	}
}
