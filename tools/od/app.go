package main

// Drives a real Orbital Drift window. Every timing here was arrived at the
// hard way and is the reason these checks are one shared driver rather than
// five copies:
//
//   - XTEST's default tap is ~12 ms and can begin and end between two of the
//     60 Hz renderer's input polls, so presses are held apart from releases.
//   - Window X/Y is frame-relative on a reparented window, so the pointer is
//     positioned with `mousemove --window`.
//   - Two windows named "Orbital Drift" fight over focus and input goes to the
//     wrong one, so a run kills strays first and checks never run in parallel.

import (
	"encoding/json"
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"strconv"
	"strings"
	"syscall"
	"time"
)

const (
	keyHold   = 140 * time.Millisecond
	clickHold = 100 * time.Millisecond
	settle    = 700 * time.Millisecond
)

type State struct {
	Mode           string          `json:"mode,omitempty"`
	Listening      *ListeningState `json:"listening,omitempty"`
	FPS            int             `json:"fps"`
	Playing        bool            `json:"playing"`
	RenderedFrames uint64          `json:"rendered_frames"`
	OutputRMS      float64         `json:"output_rms"`
	Tracks         []struct {
		Name     string  `json:"name"`
		Enabled  bool    `json:"enabled"`
		Unlocked bool    `json:"unlocked"`
		Level    float64 `json:"level"`
	} `json:"tracks"`
	HotReload struct {
		ConfigReloads int    `json:"config_reloads"`
		ShaderReloads int    `json:"shader_reloads"`
		LastError     string `json:"last_error"`
	} `json:"hot_reload"`
	Progress struct {
		Unlocked     int    `json:"unlocked"`
		Frontier     string `json:"frontier"`
		NextLocked   string `json:"next_locked"`
		SigilVisible bool   `json:"sigil_visible"`
		Complete     bool   `json:"complete"`
		SigilX       int    `json:"sigil_x"`
		SigilY       int    `json:"sigil_y"`
	} `json:"progress"`
	Planet struct {
		View         string  `json:"view"`
		Track        string  `json:"track"`
		OnScreen     bool    `json:"beacon_on_screen"`
		Found        bool    `json:"beacon_found"`
		BeaconX      int     `json:"beacon_x"`
		BeaconY      int     `json:"beacon_y"`
		Zoom         float64 `json:"zoom"`
		ViewX        float64 `json:"view_x"`
		ViewY        float64 `json:"view_y"`
		BeaconWorldX float64 `json:"beacon_world_x"`
		BeaconWorldY float64 `json:"beacon_world_y"`
	} `json:"planet"`
}

type ListeningState struct {
	Connected  bool    `json:"connected"`
	Source     string  `json:"source"`
	Error      string  `json:"error"`
	SampleRate int     `json:"sample_rate"`
	Frames     uint64  `json:"frames"`
	RMS        float64 `json:"rms"`
	Bass       float64 `json:"bass"`
	Body       float64 `json:"body"`
	Air        float64 `json:"air"`
	Pulse      float64 `json:"pulse"`
	Onsets     uint64  `json:"onsets"`
	Tone       float64 `json:"tone"`
	ToneHz     float64 `json:"tone_hz"`
	Clarity    float64 `json:"clarity"`

	// The tray, and which mechanism currently has it.
	Surface    string  `json:"surface"`
	FieldSize  int     `json:"field_size"`
	GPURelief  bool    `json:"gpu_relief"`
	SandMass   float64 `json:"sand_mass"`
	SandSpread float64 `json:"sand_spread"`
	Bed        float64 `json:"bed"`
	Paused     bool    `json:"paused"`
	Clears     int     `json:"clears"`
	Strokes    uint64  `json:"strokes"`
	Sweeps     uint64  `json:"sweeps"`
	GrainPx    float64 `json:"grain_px"`

	// The raking ball.
	BallX       float64 `json:"ball_x"`
	BallY       float64 `json:"ball_y"`
	BallScreenX int     `json:"ball_screen_x"`
	BallScreenY int     `json:"ball_screen_y"`
	Distance    float64 `json:"distance"`
	Speed       float64 `json:"speed"`
	Drive       float64 `json:"drive"`

	// The shaking plate.
	Rings        float64 `json:"rings"`
	Lobes        int     `json:"lobes"`
	Spin         float64 `json:"spin"`
	Harmonic     float64 `json:"harmonic"`
	Agitation    float64 `json:"agitation"`
	Tuning       float64 `json:"tuning"`
	Reconfigures int     `json:"reconfigures"`
}

type App struct {
	root      string
	cmd       *exec.Cmd
	statePath string
	logPath   string
	window    string
	lostFocus bool
	Width     int
	Height    int
	failures  []string
}

func xdo(args ...string) error { return exec.Command("xdotool", args...).Run() }

func xdoOut(args ...string) (string, error) {
	out, err := exec.Command("xdotool", args...).Output()
	return strings.TrimSpace(string(out)), err
}

// killStrays removes leftover windows before a run: a second window with the
// same name silently steals every keystroke.
func killStrays() {
	_ = exec.Command("pkill", "-f", "build/orbital-drift").Run()
	// A dying window can still hold focus for a moment, and whoever holds it
	// receives the next keystroke.
	time.Sleep(1100 * time.Millisecond)
}

func Launch(root, name string, args ...string) (*App, error) {
	killStrays()
	artifacts := filepath.Join(root, "artifacts")
	_ = os.MkdirAll(artifacts, 0o755)
	app := &App{
		root:      root,
		statePath: filepath.Join(artifacts, name+".json"),
		logPath:   filepath.Join(artifacts, name+".log"),
	}
	_ = os.Remove(app.statePath)
	logFile, err := os.Create(app.logPath)
	if err != nil {
		return nil, err
	}
	full := append([]string{"--windowed", "--state", app.statePath}, args...)
	app.cmd = exec.Command(filepath.Join(root, "build/orbital-drift"), full...)
	app.cmd.Dir = root
	app.cmd.Stdout, app.cmd.Stderr = logFile, logFile
	app.cmd.Env = append(os.Environ(), "DISPLAY="+display())
	if err := app.cmd.Start(); err != nil {
		return nil, err
	}
	for i := 0; i < 60; i++ {
		out, _ := xdoOut("search", "--onlyvisible", "--pid", strconv.Itoa(app.cmd.Process.Pid), "--name", "^Orbital Drift$")
		if lines := strings.Fields(out); len(lines) > 0 {
			app.window = lines[0]
			break
		}
		time.Sleep(200 * time.Millisecond)
	}
	if app.window == "" {
		app.Close()
		return nil, fmt.Errorf("window never appeared (see %s)", app.logPath)
	}
	app.focus()
	time.Sleep(600 * time.Millisecond) // settle focus before the first input
	geometry, _ := xdoOut("getwindowgeometry", "--shell", app.window)
	for _, line := range strings.Split(geometry, "\n") {
		parts := strings.SplitN(strings.TrimSpace(line), "=", 2)
		if len(parts) != 2 {
			continue
		}
		value, _ := strconv.Atoi(parts[1])
		switch parts[0] {
		case "WIDTH":
			app.Width = value
		case "HEIGHT":
			app.Height = value
		}
	}
	// Park the pointer somewhere harmless. xdotool --clearmodifiers can restore
	// a held button as a synthesized click, and a pointer left sitting over an
	// orbit node turns the next keystroke into a descent.
	_ = xdo("mousemove", "--window", app.window, "6", "6")
	time.Sleep(1900 * time.Millisecond) // first frames, audio device, first bake
	return app, nil
}

func (a *App) Close() {
	if a.cmd != nil && a.cmd.Process != nil {
		_ = a.cmd.Process.Kill()
		_, _ = a.cmd.Process.Wait()
	}
}

// Alive asks the kernel, not Go: signal 0 tests for existence without
// delivering anything. Passing a nil os.Signal reports every live process as
// dead, which looks exactly like a crash that never happened.
func (a *App) Alive() bool {
	if a.cmd == nil || a.cmd.Process == nil {
		return false
	}
	return a.cmd.Process.Signal(syscall.Signal(0)) == nil
}

// focus insists rather than asks. windowactivate can return before the window
// manager has moved focus, and input then goes wherever focus actually is --
// which is how a live check fails for reasons that have nothing to do with the
// product. Verified, not assumed.
func (a *App) focus() bool {
	if a.lostFocus {
		return false
	}
	for attempt := 0; attempt < 12; attempt++ {
		_ = xdo("windowraise", a.window)
		_ = xdo("windowactivate", "--sync", a.window, "windowfocus", "--sync", a.window)
		if held, err := xdoOut("getwindowfocus"); err == nil && held == a.window {
			return true
		}
		time.Sleep(250 * time.Millisecond)
	}
	// Someone is using the desktop. That is not a product failure, and pressing
	// on would produce assertions about input that never arrived.
	name, _ := xdoOut("getwindowfocus", "getwindowname")
	a.lostFocus = true
	a.Fail("ENVIRONMENT: could not take focus, %q is holding it -- live checks need an idle desktop", name)
	return false
}

func (a *App) Key(name string) {
	if !a.focus() {
		return
	}
	_ = xdo("keydown", "--clearmodifiers", name)
	time.Sleep(keyHold)
	_ = xdo("keyup", name)
	time.Sleep(settle)
}

func (a *App) Move(x, y int) {
	if !a.focus() {
		return
	}
	_ = xdo("mousemove", "--window", a.window, strconv.Itoa(x), strconv.Itoa(y))
	time.Sleep(200 * time.Millisecond)
}

func (a *App) Click(button int, x, y int) {
	a.Move(x, y)
	_ = xdo("mousedown", strconv.Itoa(button))
	time.Sleep(clickHold)
	_ = xdo("mouseup", strconv.Itoa(button))
	time.Sleep(500 * time.Millisecond)
}

func (a *App) Drag(fromX, fromY, dx, dy int) {
	a.Move(fromX, fromY)
	_ = xdo("mousedown", "1")
	for i := 1; i <= 6; i++ { // across frames, or the delta is never seen
		_ = xdo("mousemove", "--window", a.window,
			strconv.Itoa(fromX+dx*i/6), strconv.Itoa(fromY+dy*i/6))
		time.Sleep(45 * time.Millisecond)
	}
	_ = xdo("mouseup", "1")
	time.Sleep(250 * time.Millisecond)
}

func (a *App) Wheel(clicks, x, y int) {
	a.Move(x, y)
	button := "4"
	if clicks < 0 {
		button, clicks = "5", -clicks
	}
	for i := 0; i < clicks; i++ {
		_ = xdo("click", button)
		time.Sleep(70 * time.Millisecond)
	}
	time.Sleep(250 * time.Millisecond)
}

func (a *App) State() State {
	deadline := time.Now().Add(8 * time.Second)
	for time.Now().Before(deadline) {
		raw, err := os.ReadFile(a.statePath)
		if err == nil {
			var state State
			if json.Unmarshal(raw, &state) == nil {
				return state
			}
		}
		time.Sleep(90 * time.Millisecond)
	}
	a.Fail("app never wrote state to " + a.statePath)
	return State{}
}

func (a *App) Fail(format string, args ...any) {
	a.failures = append(a.failures, fmt.Sprintf(format, args...))
}

func (a *App) Require(ok bool, format string, args ...any) {
	if !ok {
		a.Fail(format, args...)
	}
}

// Report prints the outcome the pipeline reads: one PASS line, or the reasons.
func (a *App) Report(pass string) int {
	if a.lostFocus && len(a.failures) > 1 {
		a.failures = a.failures[:1] // the rest are consequences, not findings
	}
	if len(a.failures) == 0 {
		fmt.Println("PASS: " + pass)
		return 0
	}
	for _, failure := range a.failures {
		fmt.Fprintln(os.Stderr, "FAIL: "+failure)
	}
	fmt.Fprintf(os.Stderr, "log: %s\n", a.logPath)
	return 1
}
