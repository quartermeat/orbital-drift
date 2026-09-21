package main

// od: the project's tool. `od ci` is the one command to run; the rest exist so
// the pipeline has something to call.

import (
	"fmt"
	"os"
	"path/filepath"
)

func usage() {
	fmt.Println(`od - Orbital Drift tooling

  od ci [pipeline.yaml]   run the whole pipeline (this is the one to run)
  od check <name>         one live check: controls | hot-reload | progression | find | revisit
  od interfaces [--write] check every layer interface against its fixture and doc
  od deps                 check the dependency budget in ci/dependencies.yaml
  od campaigns            parse every campaigns/*.conf and report
  od worlds               save a PNG of every world background
  od shot -world N -zoom K [-dev] -out f.png   capture one world at a zoom`)
}

func main() {
	root := repoRoot()
	if len(os.Args) < 2 {
		usage()
		os.Exit(2)
	}
	switch os.Args[1] {
	case "ci":
		path := filepath.Join(root, "ci", "pipeline.yaml")
		if len(os.Args) > 2 {
			path = os.Args[2]
		}
		os.Exit(runPipeline(root, path))
	case "check":
		if len(os.Args) < 3 {
			usage()
			os.Exit(2)
		}
		switch os.Args[2] {
		case "controls":
			os.Exit(checkControls(root))
		case "hot-reload":
			os.Exit(checkHotReload(root))
		case "progression":
			os.Exit(checkProgression(root))
		case "find":
			os.Exit(checkFind(root))
		case "revisit":
			os.Exit(checkRevisit(root))
		default:
			fmt.Fprintf(os.Stderr, "unknown check %q\n", os.Args[2])
			os.Exit(2)
		}
	case "interfaces":
		os.Exit(checkInterfaces(root, len(os.Args) > 2 && os.Args[2] == "--write"))
	case "deps":
		os.Exit(checkDeps(root))
	case "campaigns":
		os.Exit(checkCampaigns(root))
	case "shot":
		os.Exit(shot(root, os.Args[2:]))
	case "worlds":
		os.Exit(previewWorlds(root))
	default:
		usage()
		os.Exit(2)
	}
}
