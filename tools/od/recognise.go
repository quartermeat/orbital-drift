package main

// Asks a local vision model whether each prop reads as the thing it is meant
// to be. This is a test of the *drawing*, not of the model: a silhouette only
// legible to its author is not an object, it is a shape.
//
// Uses Ollama on this machine. If it is not running, or the model is missing,
// that is reported as an environment problem rather than a failing drawing --
// the same distinction the live checks make about window focus.

import (
	"bytes"
	"encoding/base64"
	"encoding/json"
	"fmt"
	"net/http"
	"os"
	"path/filepath"
	"sort"
	"strings"
	"time"

	"gopkg.in/yaml.v3"
)

const ollama = "http://127.0.0.1:11434"

// Config, so the model and the accepted words can change without a rebuild.
type RecognitionObject struct {
	Name   string   `yaml:"name"`
	Accept []string `yaml:"accept"`
}

type Recognition struct {
	Model    string              `yaml:"model"`
	Fallback string              `yaml:"fallback"`
	Prompt   string              `yaml:"prompt"`
	Attempts int                 `yaml:"attempts"`
	Minimum  int                 `yaml:"minimum"`
	Objects  []RecognitionObject `yaml:"objects"`
}

type visionRequest struct {
	Model   string         `json:"model"`
	Prompt  string         `json:"prompt"`
	Images  []string       `json:"images"`
	Stream  bool           `json:"stream"`
	Options map[string]any `json:"options"`
}

type visionReply struct {
	Response string `json:"response"`
}

func describe(path, model, prompt string) (string, error) {
	raw, err := os.ReadFile(path)
	if err != nil {
		return "", err
	}
	body, _ := json.Marshal(visionRequest{
		Model:   model,
		Prompt:  prompt,
		Images:  []string{base64.StdEncoding.EncodeToString(raw)},
		Stream:  false,
		Options: map[string]any{"temperature": 0},
	})
	client := http.Client{Timeout: 180 * time.Second}
	resp, err := client.Post(ollama+"/api/generate", "application/json", bytes.NewReader(body))
	if err != nil {
		return "", err
	}
	defer resp.Body.Close()
	var reply visionReply
	if err := json.NewDecoder(resp.Body).Decode(&reply); err != nil {
		return "", err
	}
	return strings.ToLower(strings.TrimSpace(reply.Response)), nil
}

// pickModel prefers the configured model and falls back to the smaller one, so
// a machine without the big pull still runs the test rather than skipping it.
func pickModel(config Recognition) (string, error) {
	client := http.Client{Timeout: 5 * time.Second}
	resp, err := client.Get(ollama + "/api/tags")
	if err != nil {
		return "", fmt.Errorf("ollama is not answering on %s", ollama)
	}
	defer resp.Body.Close()
	var tags struct {
		Models []struct {
			Name string `json:"name"`
		} `json:"models"`
	}
	if err := json.NewDecoder(resp.Body).Decode(&tags); err != nil {
		return "", err
	}
	has := func(want string) bool {
		for _, model := range tags.Models {
			if strings.HasPrefix(model.Name, strings.SplitN(want, ":", 2)[0]) {
				return true
			}
		}
		return false
	}
	if has(config.Model) {
		return config.Model, nil
	}
	if config.Fallback != "" && has(config.Fallback) {
		return config.Fallback, nil
	}
	return "", fmt.Errorf("neither %q nor %q is pulled (ollama pull %s)", config.Model, config.Fallback, config.Model)
}

func checkRecognise(root string) int {
	raw, err := os.ReadFile(filepath.Join(root, "ci", "recognition.yaml"))
	if err != nil {
		fmt.Fprintln(os.Stderr, "FAIL: "+err.Error())
		return 1
	}
	var config Recognition
	if err := yaml.Unmarshal(raw, &config); err != nil {
		fmt.Fprintln(os.Stderr, "FAIL: "+err.Error())
		return 1
	}
	if config.Attempts < 1 {
		config.Attempts = 1
	}
	model, err := pickModel(config)
	if err != nil {
		fmt.Fprintf(os.Stderr, "FAIL: ENVIRONMENT: %v\n", err)
		return 1
	}

	dir := filepath.Join(root, "artifacts", "props")
	images, _ := filepath.Glob(filepath.Join(dir, "*.png"))
	if len(images) == 0 {
		fmt.Fprintln(os.Stderr, "FAIL: no prop images; run `make props`")
		return 1
	}
	sort.Slice(images, func(a, b int) bool { return propIndex(images[a]) < propIndex(images[b]) })
	if len(images) != len(config.Objects) {
		fmt.Fprintf(os.Stderr, "FAIL: %d images but %d objects in ci/recognition.yaml\n",
			len(images), len(config.Objects))
		return 1
	}

	passed, failed := 0, 0
	for _, image := range images {
		index := propIndex(image)
		if index < 0 || index >= len(config.Objects) {
			continue
		}
		object := config.Objects[index]
		name := strings.TrimSuffix(filepath.Base(image), ".png")
		hit, said := "", ""
		for attempt := 0; attempt < config.Attempts && hit == ""; attempt++ {
			answer, err := describe(image, model, config.Prompt)
			if err != nil {
				fmt.Fprintf(os.Stderr, "FAIL: ENVIRONMENT: %s: %v\n", name, err)
				return 1
			}
			said = answer
			for _, word := range object.Accept {
				if strings.Contains(answer, strings.ToLower(word)) {
					hit = word
					break
				}
			}
		}
		trimmed := said
		if len(trimmed) > 70 {
			trimmed = trimmed[:70] + "..."
		}
		if hit == "" {
			failed++
			fmt.Printf("  MISS %-22s %s\n", name, trimmed)
		} else {
			passed++
			fmt.Printf("  ok   %-22s (%s)\n", name, hit)
		}
	}
	required := config.Minimum
	if required == 0 {
		required = passed + failed // no floor configured means all of them
	}
	if passed < required {
		fmt.Fprintf(os.Stderr, "FAIL: %d of %d objects recognised, %d required; redraw the misses\n",
			passed, passed+failed, required)
		return 1
	}
	if failed > 0 {
		fmt.Printf("PASS: %d of %d objects recognised by %s (floor %d; %d still unread)\n",
			passed, passed+failed, model, required, failed)
		return 0
	}
	fmt.Printf("PASS: all %d objects recognised by %s\n", passed, model)
	return 0
}

func propIndex(path string) int {
	base := filepath.Base(path)
	dash := strings.Index(base, "-")
	if dash < 0 {
		return -1
	}
	index := 0
	for _, c := range base[:dash] {
		if c < '0' || c > '9' {
			return -1
		}
		index = index*10 + int(c-'0')
	}
	return index
}
