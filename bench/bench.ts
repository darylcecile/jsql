import { deepStrictEqual } from "node:assert";
import { cpus, release } from "node:os";
import { resolve } from "node:path";

const binary = resolve(import.meta.dir, "../dist/jsql");
const sizes = process.argv.length > 2 ? process.argv.slice(2).map(Number) : [1_000, 100_000];
const samples = 7;
const environment = {
  platform: process.platform, release: release(), arch: process.arch,
  cpu: cpus()[0]?.model, scriptc: "0.0.36", sqlite: "3.51.0", samples,
  binaryBytes: Bun.file(binary).size,
};
console.log(environment);
console.log("Median after 2 warmups; fresh processes, piped stdin/stdout, JSON output checked outside the timer.");

const results: Record<string, string | number>[] = [];
for (const size of [0, ...sizes]) {
  if (!Number.isSafeInteger(size) || size < 0) throw new Error("Row counts must be nonnegative integers");
  const rows = Array.from({ length: size }, (_, id) => ({
    id, name: `user-${id}`, team: `team-${id % 20}`, score: id % 1000, active: id % 2 === 0,
  }));
  const bytes = new TextEncoder().encode(JSON.stringify(rows));
  const groups = new Map<string, { team: string; count: number; total: number }>();
  for (const row of rows) {
    const group = groups.get(row.team) ?? { team: row.team, count: 0, total: 0 };
    group.count++;
    group.total += row.score;
    groups.set(row.team, group);
  }
  const cases = [
    { name: "startup", sql: "SELECT * FROM data LIMIT 0", expected: [] },
    { name: "filter", sql: "SELECT name, score FROM data WHERE score >= 990 LIMIT 100",
      expected: rows.filter((row) => row.score >= 990).slice(0, 100).map(({ name, score }) => ({ name, score })) },
    { name: "aggregate", sql: "SELECT team, count(*) AS count, sum(score) AS total FROM data GROUP BY team ORDER BY team",
      expected: [...groups.values()].sort((a, b) => a.team < b.team ? -1 : a.team > b.team ? 1 : 0) },
    { name: "sort", sql: "SELECT name, score FROM data ORDER BY score DESC, name ASC LIMIT 100",
      expected: [...rows].sort((a, b) => b.score - a.score || (a.name < b.name ? -1 : a.name > b.name ? 1 : 0))
        .slice(0, 100).map(({ name, score }) => ({ name, score })) },
    { name: "export", sql: "SELECT * FROM data",
      expected: rows.map((row) => ({ ...row, active: Number(row.active) })) },
  ];

  for (const { name, sql, expected } of cases) {
    if ((size === 0) !== (name === "startup")) continue;
    const times: number[] = [], memory: number[] = [];
    for (let i = 0; i < samples + 2; i++) {
      Bun.gc(true);
      const start = performance.now();
      const child = Bun.spawnSync([binary, sql], {
        stdin: bytes, stdout: "pipe", stderr: "pipe", maxBuffer: 1024 ** 3, timeout: 120_000,
      });
      const ms = performance.now() - start;
      if (child.exitCode !== 0) throw new Error(child.stderr.toString());
      deepStrictEqual(JSON.parse(child.stdout.toString()), expected, `${size}/${name}`);
      if (i >= 2) { times.push(ms); memory.push(child.resourceUsage.maxRSS); }
    }
    times.sort((a, b) => a - b);
    memory.sort((a, b) => a - b);
    const ms = times[Math.floor(samples / 2)]!;
    results.push({
      rows: size, query: name, "input MiB": +(bytes.length / 1024 ** 2).toFixed(2),
      "median ms": +ms.toFixed(2), "MiB/s": +(bytes.length / 1024 ** 2 / (ms / 1000)).toFixed(1),
      "peak MiB": +(memory[Math.floor(samples / 2)]! / 1024 ** 2).toFixed(1),
    });
  }
}
console.table(results);
await Bun.write(resolve(import.meta.dir, "../dist/benchmark.json"), JSON.stringify({ environment, results }, null, 2) + "\n");
