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
	app, err := Launch(root, "listening", "--listen", "--dev", "--monitor", sink+".monitor")
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
	tone := func(hz float64) *ListeningState {
		stop := play(hz, 5)
		waitFor(3*time.Second, func(l *ListeningState) bool { return l.Connected && l.RMS > .05 })
		// The bands are smoothed and follow the sound rather than arriving with
		// it, so reading them the instant the level rises still reports the tone
		// before this one.
		time.Sleep(800 * time.Millisecond)
		heard := app.State().Listening
		stop()
		return heard
	}

	opening := app.State()
	if opening.Mode != "listening" || opening.Listening == nil {
		app.Fail("listening mode did not publish a sand table")
		return app.Report("sand table")
	}
	// The grain the tray opens on is a preference and moves; that it opened on
	// a real grid with the ball in charge is the thing worth asserting.
	if tray := opening.Listening; tray.Surface != "rake" || tray.FieldSize < 64 || tray.GrainPx <= 0 || !tray.GPURelief {
		app.Fail("the tray did not open on the raking ball (surface %q, field %d at %.2f px, gpu %v)",
			tray.Surface, tray.FieldSize, tray.GrainPx, tray.GPURelief)
	}

	low := tone(70)
	if low == nil || !low.Connected || low.RMS < .01 || low.Bass <= low.Air*3 {
		app.Fail("low tone did not reach the table")
		return app.Report("sand table")
	}
	if low.Distance <= 0 || low.Strokes == 0 || low.Speed <= 0 {
		app.Fail("the ball did not carve while music played")
	}
	high := tone(7000)
	if high == nil || high.Air <= high.Bass*4 {
		app.Fail("bright tone did not reach the table")
	}
	quiet := waitFor(4*time.Second, func(l *ListeningState) bool { return l.RMS <= .0001 })
	if quiet == nil || quiet.RMS > .0001 || quiet.Speed != 0 {
		app.Fail("the ball did not stop when the output went silent")
	}

	// Hand the tray to the plate, levelled for it.
	app.Key("p")
	plate := app.State().Listening
	if plate == nil || plate.Surface != "plate" {
		app.Fail("P did not hand the tray to the plate")
		return app.Report("sand table")
	}
	bed := plate.Bed
	if bed <= 0 || math.Abs(plate.SandMass-bed) > bed*.02 || plate.SandSpread > bed*.2 {
		app.Fail("the plate did not start from a level bed (bed %.4f, sand %.4f, spread %.4f)",
			bed, plate.SandMass, plate.SandSpread)
	}

	// The figure builds at a measured and steady rate: about a third of the bed
	// in spread every four seconds. Eight is comfortably clear of the floor
	// below without waiting for a finished picture -- counted from when the
	// plate actually starts shaking rather than from when pacat was asked to.
	stop := play(300, 14)
	waitFor(4*time.Second, func(l *ListeningState) bool { return l.Agitation > .3 })
	time.Sleep(8 * time.Second)
	shaken := app.State().Listening
	stop()
	if shaken == nil || shaken.Agitation <= .3 {
		app.Fail("a steady tone did not shake the plate")
		return app.Report("sand table")
	}
	if math.Abs(shaken.ToneHz-300) > 36 || shaken.Clarity < .6 {
		app.Fail("the plate did not hear the tone it was given (%.1f Hz, clarity %.2f)", shaken.ToneHz, shaken.Clarity)
	}
	if shaken.Lobes < 1 || shaken.Rings < 4 {
		app.Fail("the tone chose no plate mode (%d lobes, %.1f rings)", shaken.Lobes, shaken.Rings)
	}
	// The figure itself: sand off the shaking ground and onto the still lines.
	if shaken.SandSpread < bed*.35 {
		app.Fail("the sand never left the flat (spread %.4f against a %.4f bed)", shaken.SandSpread, bed)
	}
	// And it is the same sand throughout. This is the whole claim of the plate.
	if math.Abs(shaken.SandMass-bed) > bed*.02 {
		app.Fail("the plate did not conserve the sand (%.5f against a %.5f bed)", shaken.SandMass, bed)
	}

	stop = play(1500, 8)
	higher := waitFor(6*time.Second, func(l *ListeningState) bool { return l.Rings > shaken.Rings*1.5 })
	stop()
	if higher == nil || higher.Rings < shaken.Rings*1.5 {
		app.Fail("a higher note did not break the plate into more rings (%.1f then %.1f)", shaken.Rings, higher.Rings)
	}

	// Capture stops reporting a second after the sound does, and the shaking
	// settles out over about three more, so silence needs a real wait.
	settled := waitFor(8*time.Second, func(l *ListeningState) bool { return l.Agitation == 0 })
	if settled == nil || settled.Agitation != 0 {
		app.Fail("the plate kept shaking after the output went silent")
	}
	// Silence is a held figure, not a fade: the sand remembers.
	if settled == nil || settled.SandSpread < higher.SandSpread*.9 {
		app.Fail("the figure faded when the music stopped")
	}

	// The grain slider. The tray opens on the coarse end, so finer is the way
	// it can move: a finer grain is a smaller cell, and the tray is rebuilt and
	// levelled rather than the picture being sharpened.
	app.Drag(242, app.Height-78, -205, 0)
	time.Sleep(700 * time.Millisecond)
	finer := app.State().Listening
	if finer == nil || finer.FieldSize <= settled.FieldSize || finer.GrainPx >= settled.GrainPx {
		app.Fail("the grain slider did not change the grain (%d cells at %.2f px, was %d at %.2f)",
			finer.FieldSize, finer.GrainPx, settled.FieldSize, settled.GrainPx)
	} else if math.Abs(finer.SandMass-bed) > bed*.02 || finer.SandSpread > bed*.2 {
		app.Fail("a rebuilt tray did not come back level (sand %.3f, spread %.3f, bed %.3f)",
			finer.SandMass, finer.SandSpread, bed)
	}

	app.Key("c")
	cleared := app.State().Listening
	if cleared == nil || cleared.Clears != 1 || cleared.SandSpread > bed*.2 || math.Abs(cleared.SandMass-bed) > bed*.02 {
		app.Fail("clearing did not level the tray")
	}
	app.Key("Escape")
	return app.Report("desktop monitor capture, real spectral reactions, silence, a carving ball, and a plate that sorts conserved sand onto its nodal lines")
}
