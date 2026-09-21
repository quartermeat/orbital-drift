package main

// Dependency budget. Everything third-party that links must be listed in
// ci/dependencies.yaml with a reason, and the counts must stay under their
// limits. Creep is easy to do by accident and hard to undo later, so it is
// made visible rather than trusted.

import (
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"strings"

	"gopkg.in/yaml.v3"
)

type GoDep struct {
	Module  string `yaml:"module"`
	Version string `yaml:"version"`
	Lines   int    `yaml:"lines"`
	Why     string `yaml:"why"`
}

type NativeDep struct {
	Name     string `yaml:"name"`
	Version  string `yaml:"version"`
	Why      string `yaml:"why"`
	Vendored string `yaml:"vendored"`
	Pinned   string `yaml:"pinned"`
}

type Budget struct {
	Limits struct {
		GoModules int `yaml:"go_modules"`
		Native    int `yaml:"native"`
	} `yaml:"limits"`
	Go     []GoDep     `yaml:"go"`
	Native []NativeDep `yaml:"native"`
}

func loadBudget(root string) (Budget, error) {
	var budget Budget
	raw, err := os.ReadFile(filepath.Join(root, "ci", "dependencies.yaml"))
	if err != nil {
		return budget, err
	}
	return budget, yaml.Unmarshal(raw, &budget)
}

// thirdParty reports the non-stdlib packages actually linked into the tool.
// Go's own rule: a stdlib import path has no dot in its first element.
func thirdParty(root string) ([]string, error) {
	cmd := exec.Command("go", "list", "-deps", ".")
	cmd.Dir = filepath.Join(root, "tools", "od")
	out, err := cmd.Output()
	if err != nil {
		return nil, err
	}
	seen := map[string]bool{}
	var modules []string
	for _, line := range strings.Split(strings.TrimSpace(string(out)), "\n") {
		first := strings.SplitN(line, "/", 2)[0]
		if !strings.Contains(first, ".") || strings.HasPrefix(line, "orbitaldrift/") {
			continue
		}
		// Collapse a package back to the module root we would list.
		parts := strings.Split(line, "/")
		module := parts[0]
		if len(parts) > 1 {
			module = strings.Join(parts[:min(3, len(parts))], "/")
		}
		if module == "gopkg.in" && len(parts) > 1 {
			module = strings.Join(parts[:2], "/")
		}
		if !seen[module] {
			seen[module] = true
			modules = append(modules, module)
		}
	}
	return modules, nil
}

func min(a, b int) int {
	if a < b {
		return a
	}
	return b
}

func checkDeps(root string) int {
	budget, err := loadBudget(root)
	if err != nil {
		fmt.Fprintln(os.Stderr, "FAIL: "+err.Error())
		return 1
	}
	linked, err := thirdParty(root)
	if err != nil {
		fmt.Fprintln(os.Stderr, "FAIL: "+err.Error())
		return 1
	}

	allowed := map[string]bool{}
	for _, dep := range budget.Go {
		if dep.Why == "" {
			fmt.Fprintf(os.Stderr, "FAIL: %s is listed with no reason\n", dep.Module)
			return 1
		}
		allowed[dep.Module] = true
	}

	var problems []string
	for _, module := range linked {
		if !allowed[module] {
			problems = append(problems,
				fmt.Sprintf("%s links but is not in ci/dependencies.yaml -- add it with a reason, or drop it", module))
		}
	}
	for module := range allowed {
		found := false
		for _, have := range linked {
			found = found || have == module
		}
		if !found {
			problems = append(problems,
				fmt.Sprintf("%s is listed but no longer links -- remove it from ci/dependencies.yaml", module))
		}
	}
	if len(budget.Go) > budget.Limits.GoModules {
		problems = append(problems, fmt.Sprintf("%d Go modules, budget is %d", len(budget.Go), budget.Limits.GoModules))
	}
	if len(budget.Native) > budget.Limits.Native {
		problems = append(problems, fmt.Sprintf("%d native libraries, budget is %d", len(budget.Native), budget.Limits.Native))
	}
	for _, dep := range budget.Native {
		if dep.Pinned == "" {
			problems = append(problems, fmt.Sprintf("%s is not pinned to a checksum", dep.Name))
		}
	}

	if len(problems) > 0 {
		for _, problem := range problems {
			fmt.Fprintln(os.Stderr, "FAIL: "+problem)
		}
		return 1
	}
	total := 0
	for _, dep := range budget.Go {
		total += dep.Lines
	}
	fmt.Printf("PASS: %d Go module and %d native library, %d third-party lines linked\n",
		len(budget.Go), len(budget.Native), total)
	return 0
}
