package main

// Cheap headless checks: every campaign config still loads and runs, and a PNG
// of every world can still be produced.

import (
	"flag"
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"regexp"
	"time"
	"strings"
)

// checkCampaigns boots each campaign briefly. Parsing is unit-tested; this
// catches the rest -- missing stems, a bad stem folder, a count the mixer
// refuses -- which only shows up when the app actually starts.
func checkCampaigns(root string) int {
	configs, _ := filepath.Glob(filepath.Join(root, "campaigns", "*.conf"))
	if len(configs) == 0 {
		fmt.Fprintln(os.Stderr, "FAIL: no campaigns found")
		return 1
	}
	line := regexp.MustCompile(`^\[campaign\] (.+)$`)
	failed := false
	var summaries []string
	for _, config := range configs {
		cmd := exec.Command(filepath.Join(root, "build/orbital-drift"),
			"--windowed", "--campaign", config,
			"--state", filepath.Join(root, "artifacts", "campaign-probe.json"),
			"--seconds", "3")
		cmd.Dir = root
		cmd.Env = append(os.Environ(), "DISPLAY="+display())
		out, err := cmd.CombinedOutput()
		name := filepath.Base(config)
		if err != nil {
			failed = true
			fmt.Fprintf(os.Stderr, "FAIL: %s did not run: %v\n", name, err)
			continue
		}
		summary := ""
		for _, text := range strings.Split(string(out), "\n") {
			if match := line.FindStringSubmatch(strings.TrimSpace(text)); match != nil {
				summary = match[1]
				break
			}
		}
		if summary == "" {
			failed = true
			fmt.Fprintf(os.Stderr, "FAIL: %s reported no campaign line\n", name)
			continue
		}
		summaries = append(summaries, summary)
	}
	if failed {
		return 1
	}
	fmt.Printf("PASS: %d campaigns load and run (%s)\n", len(summaries), strings.Join(summaries, "; "))
	return 0
}

// previewWorlds saves a PNG per world. One short run each, using the app's own
// capture: ImageMagick's import returns the same stale frame under a
// compositor, which once produced seven byte-identical "different" worlds.
func previewWorlds(root string) int {
	out := filepath.Join(root, "artifacts", "worlds")
	_ = os.MkdirAll(out, 0o755)
	old, _ := filepath.Glob(filepath.Join(out, "*.png"))
	for _, path := range old {
		_ = os.Remove(path)
	}
	cmd := exec.Command(filepath.Join(root, "build/orbital-drift"), "--help")
	_ = cmd.Run()
	made := 0
	for index := 0; index < 16; index++ {
		target := filepath.Join(out, fmt.Sprintf("world-%02d.png", index+1))
		run := exec.Command(filepath.Join(root, "build/orbital-drift"),
			"--windowed", "--world", fmt.Sprint(index),
			"--capture", target, "--capture-after", "2.5", "--seconds", "4")
		run.Dir = root
		run.Env = append(os.Environ(), "DISPLAY="+display())
		if err := run.Run(); err != nil {
			break
		}
		if _, err := os.Stat(target); err != nil {
			break
		}
		made++
	}
	if made == 0 {
		fmt.Fprintln(os.Stderr, "FAIL: no world images produced")
		return 1
	}
	fmt.Printf("PASS: %d world backgrounds saved to %s\n", made, out)
	return 0
}

// shot captures one world at a chosen zoom. Driving a window from a shell
// script means fighting backgrounding and focus; the App driver already knows
// how, so screenshots live here too.
func shot(root string, args []string) int {
	set := flag.NewFlagSet("shot", flag.ExitOnError)
	world := set.Int("world", 0, "which world to open")
	clicks := set.Int("zoom", 0, "wheel clicks to zoom in")
	out := set.String("out", "artifacts/shot.png", "where to write the PNG")
	dev := set.Bool("dev", false, "dev overlay")
	_ = set.Parse(args)

	target := *out
	if !filepath.IsAbs(target) {
		target = filepath.Join(root, target)
	}
	_ = os.MkdirAll(filepath.Dir(target), 0o755)
	_ = os.Remove(target)

	launch := []string{"--world", fmt.Sprint(*world), "--capture", target,
		"--capture-after", "60", "--seconds", "90"}
	if *dev {
		launch = append(launch, "--dev")
	}
	app, err := Launch(root, "shot", launch...)
	if err != nil {
		fmt.Fprintln(os.Stderr, "FAIL: "+err.Error())
		return 1
	}
	defer app.Close()
	if *clicks > 0 {
		app.Wheel(*clicks, app.Width/2, app.Height/2)
	}
	time.Sleep(900 * time.Millisecond)
	state := app.State()
	// The app captures on its own clock; ask for it now that the view is set.
	app.Key("F2")
	time.Sleep(1200 * time.Millisecond)
	if _, err := os.Stat(target); err != nil {
		fmt.Fprintf(os.Stderr, "FAIL: no image written to %s\n", target)
		return 1
	}
	fmt.Printf("PASS: %s at zoom %.1f, %d fps\n", target, state.Planet.Zoom, state.FPS)
	return 0
}
