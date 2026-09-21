package main

// Runs the YAML pipeline: the one command that proves everything still works.

import (
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"strings"
	"sync"
	"time"

	"gopkg.in/yaml.v3"
)

type Step struct {
	Name string `yaml:"name"`
	Run  string `yaml:"run"`
}

type Stage struct {
	Name     string `yaml:"name"`
	Parallel bool   `yaml:"parallel"`
	Timeout  int    `yaml:"timeout"`
	Steps    []Step `yaml:"steps"`
}

type Pipeline struct {
	Name   string  `yaml:"name"`
	Stages []Stage `yaml:"stages"`
}

type result struct {
	step   Step
	err    error
	output string
	took   time.Duration
}

func runStep(root string, step Step, timeout time.Duration) result {
	start := time.Now()
	cmd := exec.Command("sh", "-c", step.Run)
	cmd.Dir = root
	cmd.Env = append(os.Environ(), "DISPLAY="+display())
	done := make(chan struct{})
	var out []byte
	var err error
	go func() { out, err = cmd.CombinedOutput(); close(done) }()
	select {
	case <-done:
	case <-time.After(timeout):
		if cmd.Process != nil {
			_ = cmd.Process.Kill()
		}
		<-done
		err = fmt.Errorf("timed out after %s", timeout)
	}
	return result{step: step, err: err, output: string(out), took: time.Since(start)}
}

func display() string {
	if d := os.Getenv("DISPLAY"); d != "" {
		return d
	}
	return ":0"
}

// lastLine is what a passing check should be judged by: every check prints a
// single PASS line, and dumping the whole log for a pass buries it.
func lastLine(text string) string {
	lines := strings.Split(strings.TrimRight(text, "\n"), "\n")
	for i := len(lines) - 1; i >= 0; i-- {
		if strings.TrimSpace(lines[i]) != "" {
			return strings.TrimSpace(lines[i])
		}
	}
	return ""
}

func runPipeline(root, path string) int {
	raw, err := os.ReadFile(path)
	if err != nil {
		fmt.Fprintf(os.Stderr, "cannot read %s: %v\n", path, err)
		return 1
	}
	var pipeline Pipeline
	if err := yaml.Unmarshal(raw, &pipeline); err != nil {
		fmt.Fprintf(os.Stderr, "cannot parse %s: %v\n", path, err)
		return 1
	}

	overall := time.Now()
	failures := 0
	for _, stage := range pipeline.Stages {
		timeout := time.Duration(stage.Timeout) * time.Second
		if timeout == 0 {
			timeout = 180 * time.Second
		}
		mode := "serial"
		if stage.Parallel {
			mode = "parallel"
		}
		fmt.Printf("\n\033[1m== %s\033[0m (%d steps, %s)\n", stage.Name, len(stage.Steps), mode)
		start := time.Now()

		results := make([]result, len(stage.Steps))
		if stage.Parallel {
			var wait sync.WaitGroup
			for i, step := range stage.Steps {
				wait.Add(1)
				go func(i int, step Step) {
					defer wait.Done()
					results[i] = runStep(root, step, timeout)
				}(i, step)
			}
			wait.Wait()
		} else {
			for i, step := range stage.Steps {
				results[i] = runStep(root, step, timeout)
			}
		}

		stageFailed := false
		for _, r := range results {
			if r.err != nil {
				stageFailed = true
				failures++
				fmt.Printf("  \033[31mFAIL\033[0m %-14s %6.1fs  %v\n", r.step.Name, r.took.Seconds(), r.err)
				for _, line := range strings.Split(strings.TrimRight(r.output, "\n"), "\n") {
					fmt.Printf("       | %s\n", line)
				}
			} else {
				note := lastLine(r.output)
				if len(note) > 78 {
					note = note[:78] + "..."
				}
				fmt.Printf("  \033[32mok\033[0m   %-14s %6.1fs  %s\n", r.step.Name, r.took.Seconds(), note)
			}
		}
		fmt.Printf("  -- %s in %.1fs\n", stage.Name, time.Since(start).Seconds())
		if stageFailed {
			fmt.Printf("\n\033[31m%s FAILED\033[0m after %.1fs (stopped at stage %q)\n",
				pipeline.Name, time.Since(overall).Seconds(), stage.Name)
			return 1
		}
	}
	fmt.Printf("\n\033[32m%s PASSED\033[0m in %.1fs\n", pipeline.Name, time.Since(overall).Seconds())
	return 0
}

func repoRoot() string {
	exe, err := os.Executable()
	if err == nil {
		// tools/od/od -> repo root
		if root := filepath.Dir(filepath.Dir(filepath.Dir(exe))); root != "" {
			if _, err := os.Stat(filepath.Join(root, "Makefile")); err == nil {
				return root
			}
		}
	}
	wd, _ := os.Getwd()
	for dir := wd; dir != "/" && dir != ""; dir = filepath.Dir(dir) {
		if _, err := os.Stat(filepath.Join(dir, "Makefile")); err == nil {
			return dir
		}
	}
	return wd
}
