// Package the checked-in source and local game assets: go run tools/release/main.go
package main

import (
	"archive/tar"
	"compress/gzip"
	"crypto/sha256"
	"encoding/json"
	"fmt"
	"io"
	"os"
	"os/exec"
	"path/filepath"
	"runtime"
	"sort"
	"strings"
	"time"
)

func must(err error) {
	if err != nil {
		panic(err)
	}
}
func read(path string) []byte { b, err := os.ReadFile(path); must(err); return b }
func command(args ...string) []byte {
	b, err := exec.Command(args[0], args[1:]...).CombinedOutput()
	if err != nil {
		panic(fmt.Sprintf("%v: %s: %v", args, b, err))
	}
	return b
}

func pack(path, prefix string, files []string) string {
	f, err := os.OpenFile(path, os.O_CREATE|os.O_EXCL|os.O_WRONLY, 0644)
	must(err)
	hash := sha256.New()
	gz := gzip.NewWriter(io.MultiWriter(f, hash))
	tw := tar.NewWriter(gz)
	sort.Strings(files)
	seen := map[string]bool{}
	for _, name := range files {
		if seen[name] {
			continue
		}
		seen[name] = true
		info, err := os.Lstat(name)
		must(err)
		if !info.Mode().IsRegular() {
			panic("Not a regular file: " + name)
		}
		header, err := tar.FileInfoHeader(info, "")
		must(err)
		header.Name = prefix + "/" + filepath.ToSlash(name)
		header.Uid, header.Gid, header.Uname, header.Gname = 0, 0, "", ""
		header.ModTime = time.Unix(0, 0)
		header.AccessTime = time.Time{}
		header.ChangeTime = time.Time{}
		must(tw.WriteHeader(header))
		input, err := os.Open(name)
		must(err)
		_, err = io.Copy(tw, input)
		must(err)
		must(input.Close())
	}
	must(tw.Close())
	must(gz.Close())
	must(f.Close())
	return fmt.Sprintf("%x  %s\n", hash.Sum(nil), filepath.Base(path))
}

func main() {
	if runtime.GOOS != "linux" || runtime.GOARCH != "amd64" {
		panic("Build the Linux x86-64 release on Linux amd64")
	}
	if len(command("git", "status", "--porcelain")) != 0 {
		panic("Commit the release source before packaging")
	}
	version := strings.TrimSpace(string(read("VERSION")))
	if !strings.HasPrefix(string(command("build/orbital-drift", "--help")), "Orbital Drift "+version+"\n") {
		panic("Rebuild the binary for VERSION")
	}
	var manifest []struct {
		File   string `json:"file"`
		SHA256 string `json:"sha256"`
	}
	must(json.Unmarshal(read("assets/manifest.json"), &manifest))
	if len(manifest) != 7 {
		panic("Expected seven stems")
	}
	assets := []string{"assets/font.ttf", "build/deps/raylib-5.5/LICENSE"}
	// Preserve the notices in raylib's embedded dependencies verbatim, including
	// those carried inside source headers rather than standalone license files.
	must(filepath.WalkDir("build/deps/raylib-5.5/src/external", func(path string, entry os.DirEntry, err error) error {
		if err != nil {
			return err
		}
		if !entry.IsDir() {
			assets = append(assets, path)
		}
		return nil
	}))
	for _, entry := range manifest {
		if filepath.Base(entry.File) != entry.File {
			panic("Invalid asset path")
		}
		name := "assets/audio/" + entry.File
		if fmt.Sprintf("%x", sha256.Sum256(read(name))) != entry.SHA256 {
			panic("Stem checksum mismatch: " + name)
		}
		assets = append(assets, name)
	}
	tracked := strings.Split(strings.TrimSuffix(string(command("git", "ls-files", "-z")), "\x00"), "\x00")
	binary := append([]string{"build/orbital-drift", "README.md", "VERSION"}, assets...)
	for _, name := range tracked {
		if strings.HasPrefix(name, "assets/") || strings.HasPrefix(name, "campaigns/") {
			binary = append(binary, name)
		}
	}
	source := append(append([]string{}, tracked...), assets...)
	source = append(source, "build/deps/raylib-5.5.tar.gz")
	dir := "build/releases/v" + version
	must(os.MkdirAll(dir, 0755))
	base := "orbital-drift-" + version
	checksums := pack(dir+"/"+base+"-linux-x86_64.tar.gz", base+"-linux-x86_64", binary)
	checksums += pack(dir+"/"+base+"-source.tar.gz", base+"-source", source)
	must(os.WriteFile(dir+"/SHA256SUMS.txt", []byte(checksums), 0644))
	fmt.Print(checksums)
}
