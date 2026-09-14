import { resolve } from "node:path";

export function run(input: string, sql: string): string {
  const binary = process.env.JSQL_BIN ?? resolve(import.meta.dir, "../dist/jsql");
  const child = Bun.spawnSync([binary, sql], {
    stdin: new TextEncoder().encode(input), stdout: "pipe", stderr: "pipe",
  });
  if (child.exitCode !== 0) throw new Error(child.stderr.toString());
  return child.stdout.toString();
}

export function query(input: string, sql: string): unknown {
  return JSON.parse(run(input, sql));
}
