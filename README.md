# jsql

Query JSON from your shell using SQL. A TypeScript CLI compiled with
[scriptc](https://scriptc.dev), with a small C bridge to embedded SQLite.

```sh
echo '[{"name":"Ada","age":36},{"name":"Lin","age":25}]' \
  | jsql 'SELECT name FROM data WHERE age >= 30'
# [{"name":"Ada"}]
```

## Install with Binzo

With [Binzo](https://github.com/darylcecile/binzo) installed, download the latest
release for your platform:

```sh
binzo install darylcecile/jsql
jsql --help
```

## Build and run

On Linux or macOS, building requires [Node.js](https://nodejs.org) 24+,
[Bun](https://bun.sh) 1.3.10+, Clang, and `unzip`. Then, in this repo:

```sh
bun install --frozen-lockfile
bun run build
./dist/jsql 'SELECT * FROM data' < input.json
```

The executable is `dist/jsql`, about 1.1 MiB on macOS ARM64. It includes SQLite and
runs without Bun, Node.js, SQLite, or npm packages installed.
To put it on your PATH:

```sh
mkdir -p ~/.local/bin
install -m 755 dist/jsql ~/.local/bin/jsql
export PATH="$HOME/.local/bin:$PATH"
```

## Use

Pass one quoted SQL query. Input comes from stdin; output is a compact JSON array
followed by a newline. Errors go to stderr with exit code 1. Use `jsql --help` for usage.

```sh
# Filter, sort, and limit
jsql 'SELECT name, age FROM data WHERE age >= 18 ORDER BY age DESC LIMIT 10' < people.json

# Aggregate
cat sales.json | jsql 'SELECT region, sum(amount) AS total FROM data GROUP BY region'

# Read nested properties with SQLite JSON functions
jsql "SELECT name, json_extract(address, '$.city') AS city FROM data" < people.json

# Pipe results into another query
cat people.json | jsql 'SELECT * FROM data WHERE age >= 18' | jsql 'SELECT count(*) AS adults FROM data'
```

### How JSON maps to SQL

- A JSON array becomes rows in a table named `data`. A single object becomes one row.
- Object keys become columns; missing fields and JSON `null` become SQL `NULL`.
  Quote names with spaces or SQL keywords: `SELECT "display name" FROM data`.
- Numbers and strings keep their types. Booleans become `1` / `0`.
- Nested objects and arrays become JSON text; use SQLite's JSON functions to query them.
  Selecting them directly returns strings in the output.
- Scalar rows (like `[1,2,3]`) use a `value` column. Empty input arrays return no rows;
  inputs with no object fields also use `value`.

Input must be one JSON document. The input and its binary JSON representation are
held in memory; output is written in chunks. Sorting and grouping can use additional
memory. Numbers use SQLite's signed 64-bit integers or double-precision floats.
SQL syntax and functions follow [SQLite](https://www.sqlite.org/lang.html) 3.51.0.

## Performance

Queries read a JSONB-backed `data` view directly. A key-only scan discovers columns,
and output is buffered in 64 KiB chunks.

Measured on an Apple M4 Max, macOS ARM64, scriptc 0.0.36 (median of seven runs after two
warmups). These binary timings include startup, piped input, and collecting output:

| Rows | Input | Filter + limit | Group + sum | Full output |
| ---: | ---: | ---: | ---: | ---: |
| 100,000 | 7.22 MiB | 23 ms | 58 ms | 80 ms |
| 1,000,000 | 74.07 MiB | 193 ms | 821 ms | 794 ms |

Run `bun run bench` for startup, filtering, aggregation, sorting, and full-output
benchmarks at 1,000 and 100,000 rows. See [CONTRIBUTING.md](CONTRIBUTING.md) for local development.
