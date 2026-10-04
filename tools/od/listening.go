package main

// End-to-end sand table check on a private, inaudible output sink. The user's
// player, speakers and microphone are never modified by the test.
import (
	"bytes"
	"encoding/binary"
	"fmt"
	"math"
	"os"
	"os/exec"
	"path/filepath"
	"strings"
	"time"
)

func checkListening(root string) int {
	sink := fmt.Sprintf("od_listening_test_%d", os.Getpid())
	out, err := exec.Command("pactl", "load-module", "module-null-sink", "sink_name="+sink).CombinedOutput()
	if err != nil {
		fmt.Printf("FAIL: ENVIRONMENT: cannot create private test output: %s\n", out)
		return 1
	}
	module := strings.TrimSpace(string(out))
	defer exec.Command("pactl", "unload-module", module).Run()
	// A new null sink can inherit restored sink/source volume. Pin only this
	// private test device; pacat's stream volume alone does not control it.
	for _, command := range [][]string{
		{"set-sink-volume", sink, "100%"}, {"set-sink-mute", sink, "0"},
		{"set-source-volume", sink + ".monitor", "100%"}, {"set-source-mute", sink + ".monitor", "0"},
	} {
		if output, err := exec.Command("pactl", command...).CombinedOutput(); err != nil {
			fmt.Printf("FAIL: ENVIRONMENT: cannot set private test volume: %s\n", output)
			return 1
		}
	}
	capture := filepath.Join(root, "artifacts", "sound-table.png")
	_ = os.Remove(capture)
	app, err := Launch(root, "listening", "--listen", "--dev", "--monitor", sink+".monitor", "--capture", capture, "--capture-after", "0.5")
	if err != nil {
		fmt.Println("FAIL:", err)
		return 1
	}
	defer app.Close()

	// play starts a tone on the private sink and hands back the way to stop it,
	// so the table can be watched while it is still being driven.
	play := func(hz float64, seconds int) func() {
		data := make([]byte, 24000*seconds*4)
		for i := 0; i < len(data)/4; i++ {
			v := float32(.25 * math.Sin(2*math.Pi*hz*float64(i)/24000))
			binary.LittleEndian.PutUint32(data[i*4:], math.Float32bits(v))
		}
		// Pin the volume. PulseAudio remembers a volume per application, and a
		// desktop that once turned pacat down to a quarter hands every test tone
		// a 36 dB cut -- which arrives as a tone of the right shape and the wrong
		// size, and reads exactly like a broken analyzer.
		cmd := exec.Command("pacat", "--playback", "--raw", "--device", sink, "--volume=65536",
			"--format=float32le", "--rate=24000", "--channels=1", "--latency-msec=30")
		cmd.Stdin = bytes.NewReader(data)
		if err := cmd.Start(); err != nil {
			app.Fail("test tone failed: %v", err)
			return func() {}
		}
		return func() { _ = cmd.Process.Kill(); _, _ = cmd.Process.Wait() }
	}
	// A fixed sleep here samples whatever pacat happened to have delivered by
	// then, which on a busy machine is silence. Wait for the sound to actually
	// arrive instead, and keep a deadline so a real failure still fails.
	waitFor := func(limit time.Duration, ready func(*ListeningState) bool) *ListeningState {
		deadline := time.Now().Add(limit)
		var last *ListeningState
		for {
			last = app.State().Listening
			if last != nil && ready(last) {
				return last
			}
			if time.Now().After(deadline) {
				return last
			}
			time.Sleep(200 * time.Millisecond)
		}
	}
	tone := func(hz float64, band func(*ListeningState) bool) *ListeningState {
		stop := play(hz, 8)
		// Capture volume can vary with the private monitor. Wait for the measured
		// band itself, not an arbitrary loudness threshold or elapsed time.
		heard := waitFor(6*time.Second, func(l *ListeningState) bool {
			return l.Connected && l.RMS > .001 && band(l)
		})
		stop()
		return heard
	}
	place := func() *ListeningState {
		var result *ListeningState
		for attempt := 0; attempt < 3; attempt++ {
			app.RefreshSize()
			app.Move(app.Width/2, app.Height/2)
			if err := xdo("mousedown", "1"); err != nil {
				app.Fail("cannot press the table: %v", err)
				return nil
			}
			time.Sleep(350 * time.Millisecond)
			_ = xdo("mouseup", "1")
			_ = xdo("mousemove", "--window", app.window, "-20", "-20")
			time.Sleep(350 * time.Millisecond)
			result = app.State().Listening
			if result != nil && result.GrainCount > 0 {
				return result
			}
		}
		return result
	}

	opening := app.State()
	if opening.Mode != "listening" || opening.Listening == nil {
		app.Fail("listening mode did not publish a sand table")
		return app.Report("sand table")
	}
	// The sound table opens empty; grains arrive only through a tray press.
	if tray := opening.Listening; tray.Surface != "plate" || tray.FieldWidth < 64 || tray.FieldHeight < 64 || tray.GrainPx <= 0 || !tray.GPURelief || !tray.StatefulGrains || tray.GrainCount != 0 || tray.SandMass != 0 {
		app.Fail("the tray did not open on the sound table (surface %q, field %d at %.2f px, gpu %v)",
			tray.Surface, tray.FieldWidth, tray.GrainPx, tray.GPURelief)
	}
	if tray := opening.Listening; math.Abs(float64(app.Width)/float64(tray.FieldWidth)-float64(app.Height)/float64(tray.FieldHeight)) > .03 {
		app.Fail("grain cells do not match the window proportions")
	}
	if _, err := os.Stat(capture); err != nil {
		app.Fail("cannot read sound-table capture: %v", err)
	}
	if tray := opening.Listening; tray.Bed != 0 || tray.SandSpread != 0 {
		app.Fail("the plate did not start empty")
	}
	placed := place()
	if placed == nil || placed.GrainCount < 8 || placed.GrainCount > 30 || placed.SandMass <= 0 {
		app.Fail("holding left click did not add timed grains (state %+v)", placed)
		return app.Report("sand table")
	}
	time.Sleep(250 * time.Millisecond)
	if still := app.State().Listening; still == nil || still.GrainCount != placed.GrainCount {
		app.Fail("sand kept appearing after the mouse button was released")
	}

	low := tone(70, func(l *ListeningState) bool { return l.Bass > l.Air*3 })
	if low == nil || !low.Connected || low.RMS < .001 || low.Bass <= low.Air*3 {
		app.Fail("low tone did not reach the table")
		return app.Report("sand table")
	}
	if low.Sweeps == 0 || low.Strokes == 0 || low.Agitation <= 0 {
		app.Fail("the plate did not shake while music played")
	}
	high := tone(7000, func(l *ListeningState) bool { return l.Air > l.Bass*4 })
	if high == nil || high.Air <= high.Bass*4 {
		app.Fail("bright tone did not reach the table")
	}
	quiet := waitFor(8*time.Second, func(l *ListeningState) bool { return l.RMS <= .0001 && l.Agitation == 0 })
	if quiet == nil || quiet.RMS > .0001 || quiet.Agitation != 0 {
		app.Fail("the plate did not stop when the output went silent")
		return app.Report("sand table")
	}

	// The retired surface shortcut must neither switch modes nor erase grains.
	time.Sleep(400 * time.Millisecond)
	quiet = app.State().Listening
	app.Key("p")
	plate := app.State().Listening
	if plate == nil || plate.Surface != "plate" ||
		(plate.FieldWidth == quiet.FieldWidth && plate.FieldHeight == quiet.FieldHeight &&
			(plate.Sweeps != quiet.Sweeps || math.Abs(plate.SandSpread-quiet.SandSpread) > .001)) {
		app.Fail("P changed the sound table or its held figure (before %+v, after %+v)", quiet, plate)
		return app.Report("sand table")
	}
	if plate.FieldWidth != quiet.FieldWidth || plate.FieldHeight != quiet.FieldHeight {
		placed = place()
	}
	// A window-manager resize legitimately clears the tray. If one happens
	// during this tone, place fresh grains in the new tray and try it once more.
	var shaken *ListeningState
	for attempt := 0; attempt < 2; attempt++ {
		stop := play(300, 30)
		waitFor(4*time.Second, func(l *ListeningState) bool { return l.Agitation > .3 })
		time.Sleep(2 * time.Second)
		shaken = waitFor(8*time.Second, func(l *ListeningState) bool { return l.RMS > .01 && l.Agitation > .3 })
		stop()
		if shaken == nil || shaken.FieldWidth == placed.FieldWidth && shaken.FieldHeight == placed.FieldHeight {
			break
		}
		placed = place()
	}
	app.Key("F2") // keep the rectangular figure as a visual QA artifact
	if shaken == nil || shaken.Agitation <= .3 {
		app.Fail("a steady tone did not shake the plate")
		return app.Report("sand table")
	}
	if shaken.FieldWidth != placed.FieldWidth || shaken.FieldHeight != placed.FieldHeight ||
		shaken.GrainCount != placed.GrainCount || shaken.GrainUpdates == 0 || shaken.MovingGrains == 0 {
		app.Fail("not every grain received its own motion update")
	}
	if math.Abs(shaken.ToneHz-300) > 36 || shaken.Clarity < .6 {
		app.Fail("the plate did not hear the tone it was given (%.1f Hz, clarity %.2f)", shaken.ToneHz, shaken.Clarity)
	}
	if shaken.ModeN == 0 && shaken.ModeM == 0 || shaken.ModeFrequency < .5 {
		app.Fail("the tone chose no rectangular plate mode (%d,%d)", shaken.ModeN, shaken.ModeM)
	}
	if math.Abs(shaken.SandMass-placed.SandMass) > .0001 {
		app.Fail("the plate did not conserve the placed sand (%.5f against %.5f)", shaken.SandMass, placed.SandMass)
	}

	stop := play(1500, 8)
	higher := waitFor(6*time.Second, func(l *ListeningState) bool { return l.ModeFrequency > shaken.ModeFrequency*1.5 })
	stop()
	if higher == nil || higher.ModeFrequency < shaken.ModeFrequency*1.5 {
		app.Fail("a higher note did not select a higher rectangular mode")
		return app.Report("sand table")
	}

	// Capture stops reporting a second after the sound does, and the shaking
	// settles out over about three more, so silence needs a real wait.
	settled := waitFor(8*time.Second, func(l *ListeningState) bool { return l.Agitation == 0 })
	if settled == nil || settled.Agitation != 0 {
		app.Fail("the plate kept shaking after the output went silent")
	}
	// Sand can still move during the plate's brief spin-down. Once it has stopped,
	// both every grain state and the resulting figure must remain fixed.
	if settled != nil {
		time.Sleep(500 * time.Millisecond)
		held := app.State().Listening
		if held == nil || held.FieldWidth == settled.FieldWidth && held.FieldHeight == settled.FieldHeight &&
			(held.Sweeps != settled.Sweeps || math.Abs(held.SandSpread-settled.SandSpread) > .001) {
			app.Fail("the figure changed after the plate stopped")
		}
	}

	// The grain slider changes size and clears the tray.
	app.RefreshSize()
	beforeSlider := app.State().Listening
	app.Drag(242, app.Height-78, -205, 0)
	time.Sleep(700 * time.Millisecond)
	finer := app.State().Listening
	if finer != nil && finer.GrainPx >= beforeSlider.GrainPx {
		// Some window managers move the client while snapping it into a tile.
		// Retry with the geometry it actually has after that move.
		app.RefreshSize()
		app.Drag(242, app.Height-78, -205, 0)
		time.Sleep(700 * time.Millisecond)
		finer = app.State().Listening
	}
	if finer == nil || finer.FieldWidth <= beforeSlider.FieldWidth || finer.FieldHeight <= beforeSlider.FieldHeight || finer.GrainPx >= beforeSlider.GrainPx {
		app.Fail("the grain slider did not change the grain")
	} else if finer.GrainCount != 0 || finer.SandMass != 0 {
		app.Fail("a rebuilt tray did not come back empty")
	}

	app.Click(1, app.Width/2, app.Height/2)
	_ = xdo("mousemove", "--window", app.window, "-20", "-20")
	app.Key("c")
	cleared := app.State().Listening
	if cleared == nil || cleared.Clears != 1 || cleared.GrainCount != 0 || cleared.SandMass != 0 {
		app.Fail("clearing did not empty the tray")
	}
	// Fine grains map one-to-one to pixels, even after changing aspect ratio.
	if err := xdo("windowsize", app.window, "901", "701"); err != nil {
		app.Fail("cannot resize sound table: %v", err)
	}
	resized := waitFor(4*time.Second, func(l *ListeningState) bool {
		app.RefreshSize()
		return l.FieldWidth == app.Width && l.FieldHeight == app.Height
	})
	if app.Width != 901 || app.Height != 701 {
		fmt.Printf("INFO: window manager kept the table at %dx%d after a resize request\n", app.Width, app.Height)
	}
	if resized == nil || resized.FieldWidth != app.Width || resized.FieldHeight != app.Height || resized.GrainCount != 0 || resized.SandMass != 0 {
		app.Fail("resizing did not preserve an empty rectangular window")
	}
	app.Key("Escape")
	return app.Report("desktop monitor capture, real spectral reactions, silence, and timed placement of conserved sand")
}
