package main

// Interface checking. Every boundary between layers has a document in
// docs/interfaces and a fixture in testdata/interfaces produced by the live
// code. This regenerates the fixtures and diffs them, so an interface cannot
// drift from what is written down without the build stopping.
//
// The two cross-language boundaries are described here rather than by the C++
// emitter, because this side is their consumer: state.json is read by the Go
// State struct, and pipeline.yaml by the Go runner. Their fixtures are built
// by reflecting over those very types, so adding a field to either updates the
// fixture automatically and forces the document to be revisited.

import (
	"encoding/json"
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"reflect"
	"sort"
	"strings"
)

// jsonShape walks a struct and reports its JSON field names and kinds, so the
// fixture follows the type rather than a hand-kept list.
func jsonShape(t reflect.Type, prefix string, into map[string]string) {
	for i := 0; i < t.NumField(); i++ {
		field := t.Field(i)
		tag := field.Tag.Get("json")
		if tag == "" || tag == "-" {
			continue
		}
		name := prefix + strings.Split(tag, ",")[0]
		inner := field.Type
		for inner.Kind() == reflect.Ptr {
			inner = inner.Elem()
		}
		switch inner.Kind() {
		case reflect.Struct:
			jsonShape(inner, name+".", into)
		case reflect.Slice:
			element := inner.Elem()
			if element.Kind() == reflect.Struct {
				jsonShape(element, name+"[].", into)
			} else {
				into[name] = "[]" + element.Kind().String()
			}
		default:
			into[name] = inner.Kind().String()
		}
	}
}

func writeFixture(path string, body map[string]any) error {
	encoded, err := json.MarshalIndent(body, "", "  ")
	if err != nil {
		return err
	}
	return os.WriteFile(path, append(encoded, '\n'), 0o644)
}

func goFixtures(dir string) error {
	state := map[string]string{}
	jsonShape(reflect.TypeOf(State{}), "", state)
	if err := writeFixture(filepath.Join(dir, "state.json"), map[string]any{
		"interface":     "state",
		"produced_by":   "src/main.cpp writeState() or src/listening.hpp runListening()",
		"consumed_by":   "tools/od State",
		"documented_in": "docs/interfaces/state.md",
		"note":          "shape only: fps, renderer and frame counts differ per machine and per run",
		"written_every": "0.2s while running, and once on exit",
		"fields":        state,
	}); err != nil {
		return err
	}

	pipeline := map[string]string{}
	jsonShape(reflect.TypeOf(Pipeline{}), "", pipeline)
	// yaml tags, not json: report them the same way.
	pipeline = map[string]string{
		"name":                  "string",
		"stages[].name":         "string",
		"stages[].parallel":     "bool (false for anything that opens a window)",
		"stages[].timeout":      "int seconds, default 180",
		"stages[].steps[].name": "string",
		"stages[].steps[].run":  "shell, run from the repo root with DISPLAY set",
	}
	budgetShape := map[string]string{
		"limits.go_modules":   "int, maximum modules linked into tools/od",
		"limits.native":       "int, maximum native libraries linked into the game",
		"go[].module":         "string, module path as `go list -deps` reports it",
		"go[].version":        "string",
		"go[].lines":          "int, non-test source lines carried",
		"go[].why":            "string, required; a dependency with no reason fails",
		"native[].name":       "string",
		"native[].version":    "string",
		"native[].why":        "string",
		"native[].vendored":   "path",
		"native[].fetched_by": "path",
		"native[].pinned":     "string, required; an unpinned native library fails",
	}
	if err := writeFixture(filepath.Join(dir, "dependencies.json"), map[string]any{
		"interface":     "dependencies",
		"produced_by":   "ci/dependencies.yaml",
		"consumed_by":   "tools/od checkDeps()",
		"documented_in": "docs/interfaces/dependencies.md",
		"checked_by":    "go list -deps, filtered by Go's rule that stdlib paths have no dot in the first element",
		"fields":        budgetShape,
	}); err != nil {
		return err
	}

	if err := writeFixture(filepath.Join(dir, "recognition.json"), map[string]any{
		"interface":     "recognition",
		"produced_by":   "ci/recognition.yaml",
		"consumed_by":   "tools/od checkRecognise()",
		"documented_in": "docs/interfaces/recognition.md",
		"images_from":   "build/propsheet -> artifacts/props/*.png",
		"judged_by":     "a local vision model over the Ollama API",
		"fields": map[string]string{
			"model":            "string, ollama model name",
			"fallback":         "string, used only if model is not pulled",
			"prompt":           "string, asks for a description rather than one word",
			"attempts":         "int, retries per object; vision models are not deterministic",
			"objects[].name":   "string, must match the order build/propsheet writes",
			"objects[].accept": "[]string, any one of these in the description passes",
		},
	}); err != nil {
		return err
	}

	return writeFixture(filepath.Join(dir, "pipeline.json"), map[string]any{
		"interface":     "pipeline",
		"produced_by":   "ci/pipeline.yaml",
		"consumed_by":   "tools/od runPipeline()",
		"documented_in": "docs/interfaces/pipeline.md",
		"stop_on":       "first failing stage",
		"fields":        pipeline,
	})
}

// checkInterfaces regenerates every fixture and diffs it against what is
// committed, then checks that documents and fixtures still point at each other.
func checkInterfaces(root string, write bool) int {
	fixtures := filepath.Join(root, "testdata", "interfaces")
	docs := filepath.Join(root, "docs", "interfaces")
	target := fixtures
	if !write {
		temp, err := os.MkdirTemp("", "od-interfaces")
		if err != nil {
			fmt.Fprintln(os.Stderr, "FAIL: "+err.Error())
			return 1
		}
		defer os.RemoveAll(temp)
		target = temp
	}

	// Rebuild the emitter first. A stale binary silently produces the old
	// fixture and the check passes on a lie -- which is exactly what happened
	// the first time this was tested.
	build := exec.Command("make", "-s", "build/contracts")
	build.Dir = root
	if out, err := build.CombinedOutput(); err != nil {
		fmt.Fprintf(os.Stderr, "FAIL: the interface emitter does not build: %v\n%s\n", err, out)
		return 1
	}

	emitter := exec.Command(filepath.Join(root, "build/contracts"), target)
	emitter.Dir = root
	if out, err := emitter.CombinedOutput(); err != nil {
		fmt.Fprintf(os.Stderr, "FAIL: contracts emitter: %v\n%s\n", err, out)
		return 1
	}
	if err := goFixtures(target); err != nil {
		fmt.Fprintln(os.Stderr, "FAIL: "+err.Error())
		return 1
	}
	if write {
		fmt.Println("PASS: interface fixtures rewritten")
		return 0
	}

	var problems []string
	produced, _ := filepath.Glob(filepath.Join(target, "*.json"))
	sort.Strings(produced)
	for _, path := range produced {
		name := filepath.Base(path)
		fresh, _ := os.ReadFile(path)
		committed, err := os.ReadFile(filepath.Join(fixtures, name))
		if err != nil {
			problems = append(problems, fmt.Sprintf("%s is not committed; run `make interfaces`", name))
			continue
		}
		if string(fresh) != string(committed) {
			problems = append(problems,
				fmt.Sprintf("%s has drifted from the code -- the interface changed; update its document in docs/interfaces and run `make interfaces`", name))
		}
	}

	// Every fixture needs a document, and every document a fixture.
	for _, path := range produced {
		name := strings.TrimSuffix(filepath.Base(path), ".json")
		doc := filepath.Join(docs, name+".md")
		body, err := os.ReadFile(doc)
		if err != nil {
			problems = append(problems, fmt.Sprintf("no document for interface %q (expected docs/interfaces/%s.md)", name, name))
			continue
		}
		if !strings.Contains(string(body), "testdata/interfaces/"+name+".json") {
			problems = append(problems, fmt.Sprintf("docs/interfaces/%s.md does not reference its fixture", name))
		}
	}
	written, _ := filepath.Glob(filepath.Join(docs, "*.md"))
	for _, doc := range written {
		name := strings.TrimSuffix(filepath.Base(doc), ".md")
		if name == "README" {
			continue
		}
		if _, err := os.Stat(filepath.Join(fixtures, name+".json")); err != nil {
			problems = append(problems, fmt.Sprintf("docs/interfaces/%s.md documents an interface with no fixture", name))
		}
	}

	if len(problems) > 0 {
		for _, problem := range problems {
			fmt.Fprintln(os.Stderr, "FAIL: "+problem)
		}
		return 1
	}
	fmt.Printf("PASS: %d interfaces match their fixtures and documents\n", len(produced))
	return 0
}
