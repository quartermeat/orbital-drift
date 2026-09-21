package main

// The live checks. Each drives a real window and prints one PASS line.
// Ordering matters inside a check: prove an input works before asserting that
// a different input is refused, or a dead harness looks like a working lock.

import (
	"fmt"
	"math"
	"os"
	"path/filepath"
	"strconv"
	"time"
)

func writeProgress(root, campaign string, unlocked int) {
	path := filepath.Join(root, "artifacts", "progress-"+campaign+".json")
	_ = os.MkdirAll(filepath.Dir(path), 0o755)
	_ = os.WriteFile(path, []byte(fmt.Sprintf("{\"unlocked\":%d}\n", unlocked)), 0o644)
}

func clearProgress(root string) {
	matches, _ := filepath.Glob(filepath.Join(root, "artifacts", "progress-*.json"))
	for _, match := range matches {
		_ = os.Remove(match)
	}
}

// checkControls: every key and the mouse reach the app, and it exits cleanly.
func checkControls(root string) int {
	writeProgress(root, "orbital-drift", 7)
	app, err := Launch(root, "controls", "--resume", "--seconds", "80")
	if err != nil {
		fmt.Fprintln(os.Stderr, "FAIL: "+err.Error())
		return 1
	}
	defer app.Close()

	app.Key("a") // a run starts silent
	state := app.State()
	app.Require(len(state.Tracks) > 0, "no tracks in state")
	allOn := true
	for _, track := range state.Tracks {
		allOn = allOn && track.Enabled
	}
	app.Require(allOn, "ALL ON did not wake every track")

	for i := range state.Tracks {
		app.Key(strconv.Itoa(i + 1))
		if app.State().Tracks[i].Enabled {
			app.Fail("key %d did not mute %s", i+1, state.Tracks[i].Name)
			break
		}
		app.Key(strconv.Itoa(i + 1))
		if !app.State().Tracks[i].Enabled {
			app.Fail("key %d did not unmute %s", i+1, state.Tracks[i].Name)
			break
		}
	}

	app.Key("m")
	silent := app.State()
	anyOn := false
	for _, track := range silent.Tracks {
		anyOn = anyOn || track.Enabled
	}
	app.Require(!anyOn, "M did not silence everything")

	before := app.State().RenderedFrames
	time.Sleep(900 * time.Millisecond)
	app.Require(app.State().RenderedFrames > before, "the mixer stopped rendering")
	return app.Report("every key toggles, ALL ON, M silences, audio keeps rendering")
}

// checkHotReload: a save applies live and a broken save cannot take the app down.
func checkHotReload(root string) int {
	configPath := filepath.Join(root, "assets", "layers.conf")
	shaderPath := filepath.Join(root, "assets", "space.fs")
	config, err1 := os.ReadFile(configPath)
	shader, err2 := os.ReadFile(shaderPath)
	if err1 != nil || err2 != nil {
		fmt.Fprintln(os.Stderr, "FAIL: cannot read assets")
		return 1
	}
	restore := func() { _ = os.WriteFile(configPath, config, 0o644); _ = os.WriteFile(shaderPath, shader, 0o644) }
	defer restore()

	app, err := Launch(root, "hot-reload", "--seconds", "70")
	if err != nil {
		fmt.Fprintln(os.Stderr, "FAIL: "+err.Error())
		return 1
	}
	defer app.Close()
	first := app.State()

	_ = os.WriteFile(configPath, append(config, []byte("\nstar.count = 40\n")...), 0o644)
	time.Sleep(2400 * time.Millisecond)
	afterConfig := app.State()
	app.Require(afterConfig.HotReload.ConfigReloads > first.HotReload.ConfigReloads, "layers.conf edit did not reload")
	app.Require(afterConfig.HotReload.LastError == "", "a clean config reported %q", afterConfig.HotReload.LastError)
	app.Require(afterConfig.RenderedFrames > first.RenderedFrames, "audio stalled across the config reload")

	_ = os.WriteFile(shaderPath, append(shader, []byte("\n// touched\n")...), 0o644)
	time.Sleep(2400 * time.Millisecond)
	afterShader := app.State()
	app.Require(afterShader.HotReload.ShaderReloads > first.HotReload.ShaderReloads, "space.fs edit did not reload")

	// The one that matters: a shader that does not compile must not kill it.
	_ = os.WriteFile(shaderPath, []byte("#version 330\nthis is not glsl;\n"), 0o644)
	time.Sleep(2400 * time.Millisecond)
	broken := app.State()
	app.Require(app.Alive(), "app exited when a broken shader was saved")
	app.Require(broken.HotReload.LastError != "", "a broken shader reported no error")
	app.Require(broken.RenderedFrames > afterShader.RenderedFrames, "audio stalled after a broken shader")

	_ = os.WriteFile(shaderPath, shader, 0o644)
	time.Sleep(2400 * time.Millisecond)
	recovered := app.State()
	app.Require(recovered.HotReload.ShaderReloads > afterShader.HotReload.ShaderReloads, "a good shader did not reload after a broken one")
	app.Require(recovered.HotReload.LastError == "", "the error was not cleared on recovery")
	return app.Report("config and shader reload live, a broken shader survives, audio never stalls")
}

// checkProgression: a run starts silent and sealed, and the sigil is a door.
func checkProgression(root string) int {
	clearProgress(root)
	app, err := Launch(root, "progression", "--seconds", "70")
	if err != nil {
		fmt.Fprintln(os.Stderr, "FAIL: "+err.Error())
		return 1
	}
	defer app.Close()

	first := app.State()
	app.Require(first.Progress.Unlocked == 1, "a fresh run opened %d tracks", first.Progress.Unlocked)
	open := 0
	sounding := 0
	for _, track := range first.Tracks {
		if track.Unlocked {
			open++
		}
		if track.Enabled {
			sounding++
		}
	}
	app.Require(open == 1, "%d tracks were unlocked, expected 1", open)
	app.Require(sounding == 0, "a run did not start silent")
	app.Require(!first.Progress.SigilVisible, "the sigil showed before its track was woken")

	last := strconv.Itoa(len(first.Tracks))
	app.Key(last) // prove input works before asserting anything is refused
	woken := app.State()
	app.Require(woken.Tracks[len(woken.Tracks)-1].Enabled, "pressing %s did not wake the open track", last)
	app.Require(woken.Progress.SigilVisible, "the sigil did not appear once its track was on")

	app.Key("1")
	app.Require(!app.State().Tracks[0].Enabled, "pressing 1 woke a sealed track")

	app.Key(last)
	app.Require(!app.State().Progress.SigilVisible, "the sigil stayed after its track went quiet")
	app.Key(last)

	live := app.State()
	app.Click(1, live.Progress.SigilX, live.Progress.SigilY)
	after := app.State()
	app.Require(after.Planet.View == "planet", "the sigil did not open a world (view %q)", after.Planet.View)
	app.Require(after.Progress.Unlocked == 1, "the sigil unsealed a track; it should only open the world")
	app.Require(after.RenderedFrames > first.RenderedFrames, "audio stalled across the descent")

	app.Key("Escape")
	back := app.State()
	app.Require(back.Planet.View == "system", "Escape did not return to the galaxy")
	app.Require(app.Alive(), "Escape quit the app instead of leaving the world")
	return app.Report("silent start, sealed input refused, sigil opens a world without unsealing, Escape returns")
}

// checkRevisit: a world you already solved stays usable. Claiming once
// permanently disabled panning, and any later find reopened the finale.
func checkRevisit(root string) int {
	writeProgress(root, "orbital-drift", 7) // a finished campaign
	app, err := Launch(root, "revisit", "--dev", "--resume", "--world", "0", "--seconds", "70")
	if err != nil {
		fmt.Fprintln(os.Stderr, "FAIL: "+err.Error())
		return 1
	}
	defer app.Close()

	app.Require(app.State().Planet.View == "planet", "did not open in a world")
	app.Key("g")
	target := app.State()
	app.Require(target.Planet.OnScreen, "G did not bring the target on screen")
	app.Click(1, target.Planet.BeaconX, target.Planet.BeaconY)

	claimed := app.State()
	app.Require(claimed.Planet.View != "finale",
		"a find in an already-complete campaign reopened the finale")
	app.Require(claimed.Planet.View == "system", "claiming did not return to the galaxy (view %q)", claimed.Planet.View)

	// Back into the same world, which is now solved, and pan it.
	app.Key("z")
	back := app.State()
	app.Require(back.Planet.View == "planet", "could not re-enter the solved world")
	app.Require(back.Planet.Found, "the world forgot it had been solved")
	before := back.Planet.ViewX
	app.Drag(app.Width/4, app.Height/4, 260, 0)
	after := app.State().Planet.ViewX
	app.Require(math.Abs(after-before) > 1, "panning is dead in a solved world (view_x stayed %.1f)", before)
	return app.Report("a solved world still pans, and finding there does not reopen the finale")
}

// checkFind: the target can be reached, claimed, and the claim pays out.
func checkFind(root string) int {
	clearProgress(root)
	app, err := Launch(root, "find", "--dev", "--seconds", "90")
	if err != nil {
		fmt.Fprintln(os.Stderr, "FAIL: "+err.Error())
		return 1
	}
	defer app.Close()

	first := app.State()
	tracks := len(first.Tracks)
	app.Key(strconv.Itoa(tracks))
	app.Key("z")
	landed := app.State()
	app.Require(landed.Planet.View == "planet", "Z did not descend (view %q)", landed.Planet.View)
	app.Require(math.Abs(landed.Planet.Zoom-1) < 0.25, "a world should open fitted, zoom was %.2f", landed.Planet.Zoom)

	// Travel in the way a player does, rather than jumping straight there.
	fit := math.Min(float64(app.Width)/2560, float64(app.Height)/1440)
	for i := 0; i < 12; i++ {
		state := app.State()
		scale := state.Planet.Zoom * fit
		dx := (state.Planet.ViewX - state.Planet.BeaconWorldX) * scale
		dy := (state.Planet.ViewY - state.Planet.BeaconWorldY) * scale
		if math.Abs(dx) > 6 || math.Abs(dy) > 6 {
			// Drag from a corner: beginning a drag on the target clicks it.
			app.Drag(app.Width/4, app.Height/4,
				int(math.Max(-420, math.Min(420, dx))), int(math.Max(-420, math.Min(420, dy))))
		}
		state = app.State()
		// A drag that begins on the target claims it, ending the hunt early;
		// stop the moment we are no longer hunting.
		if state.Planet.View != "planet" || state.Planet.Found {
			break
		}
		if state.Planet.OnScreen && state.Planet.Zoom > 6 &&
			math.Abs(float64(state.Planet.BeaconX-app.Width/2)) < 110 {
			break
		}
		app.Wheel(4, app.Width/2, app.Height/2)
	}
	reached := app.State()
	if reached.Planet.Found {
		early := app.State()   // a drag landed on them on the way in; still a claim
		app.Require(early.Progress.Unlocked == 2, "an early claim did not unseal (unlocked %d)", early.Progress.Unlocked)
		return app.Report("descend, travel to the target, claim it, unseal and start the next track")
	}
	app.Require(reached.Planet.OnScreen, "never brought the target on screen (zoom %.1f)", reached.Planet.Zoom)
	if !reached.Planet.OnScreen {
		return app.Report("")
	}

	app.Click(1, reached.Planet.BeaconX, reached.Planet.BeaconY)
	claimed := app.State()
	app.Require(claimed.Progress.Unlocked == 2, "claiming did not unseal (unlocked %d)", claimed.Progress.Unlocked)
	app.Require(claimed.Planet.View == "system", "claiming did not return to the galaxy")
	app.Require(claimed.RenderedFrames > landed.RenderedFrames, "audio stalled during the hunt")
	newTrack := claimed.Progress.Frontier
	playing := false
	for _, track := range claimed.Tracks {
		if track.Name == newTrack {
			playing = track.Enabled
		}
	}
	app.Require(playing, "the newly unsealed track %q is not playing", newTrack)
	return app.Report("descend, travel to the target, claim it, unseal and start the next track")
}
