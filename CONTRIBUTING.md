# Contributing

Use Linux or macOS with Node.js 24+, Bun 1.3.10+, Clang, and `unzip`.
On macOS, Clang comes with Xcode Command Line Tools (`xcode-select --install`).
On Ubuntu, install `clang build-essential unzip` with apt.

From your clone:

```sh
bun install --frozen-lockfile
bun run build
echo '[{"name":"Ada"}]' | ./dist/jsql 'SELECT name FROM data'
```

The build downloads checksum-pinned SQLite source into `dist/` and links it into
the executable. scriptc's optional postinstall only warms a compiler cache; the
build works with that script blocked.

`src/cli.ts` handles arguments and stdin. `src/bridge.c` exposes JSON as SQLite rows
and writes results. `src/ffi.json` connects the TypeScript declaration to the native
function. `build.ts` compiles both into `dist/jsql` for the current machine.

## Check changes

```sh
bun run check
bun run test   # builds the binary, then tests it
```

Keep changes and tests focused on our JSON/CLI behavior. Describe the change and
how you checked it in the pull request. CI builds and tests on Linux and macOS;
build on the target OS and architecture when distributing a binary.

## Benchmarks

```sh
bun run bench
bun run bench 1000000
```

The benchmark reports seven-run medians after two warmups. Each run starts a fresh
binary and includes piped input, SQL execution, and collecting stdout. Inputs and
expected results are generated outside the timer, and every output is checked.
Peak memory is measured in the child process. Results go to `dist/benchmark.json`.
Compare on the same machine; these are warm-cache measurements.

## Releases

In GitHub, open **Actions → Release → Run workflow**, choose the source branch,
and enter a tag such as `v0.1.0`. After all builds and tests pass, the workflow
creates or moves that tag to the selected commit and creates or updates its release.
It attaches Linux and macOS binaries for x64 and ARM64 as `.tar.gz` archives, plus
`SHA256SUMS`. Re-running a tag replaces matching assets and preserves existing release notes.
