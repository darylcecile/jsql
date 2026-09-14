import { mkdirSync } from "node:fs";
import { resolve } from "node:path";

const root = import.meta.dir;
const out = resolve(root, "dist");
mkdirSync(out, { recursive: true });

function command(args: string[]) {
  const child = Bun.spawnSync(args, { cwd: root, stdin: "inherit", stdout: "inherit", stderr: "inherit" });
  if (child.exitCode !== 0) throw new Error(`Failed: ${args.join(" ")}`);
}

const archive = resolve(root, "dist/sqlite-amalgamation-3510000.zip");
if (!(await Bun.file(archive).exists())) {
  const response = await fetch("https://sqlite.org/2025/sqlite-amalgamation-3510000.zip");
  if (!response.ok) throw new Error(`SQLite download: HTTP ${response.status}`);
  await Bun.write(archive, await response.arrayBuffer());
}
const digest = new Bun.CryptoHasher("sha256").update(await Bun.file(archive).arrayBuffer()).digest("hex");
if (digest !== "1caf7116f2910600d04473ad69d37ec538fa62fa36adccd37b5e0e43647c98be") throw new Error("SQLite source checksum mismatch");
command(["unzip", "-oq", archive, "-d", out]);

const cc = process.env.CC ?? "clang";
const sqlite = resolve(out, "sqlite-amalgamation-3510000");
command([cc, "-O2", "-DSQLITE_THREADSAFE=0", "-DSQLITE_DEFAULT_MEMSTATUS=0", "-DSQLITE_OMIT_LOAD_EXTENSION", "-DSQLITE_DQS=0", "-c", resolve(sqlite, "sqlite3.c"), "-o", resolve(out, "sqlite3.o")]);
command([cc, "-O2", "-Wall", "-Wextra", `-I${sqlite}`, "-c", "src/bridge.c", "-o", resolve(out, "bridge.o")]);

const compiler = resolve(import.meta.dir, "node_modules/scriptc/dist/bootstrap.js");
command(["node", compiler, "build", "src/cli.ts", "--ffi", "src/ffi.json", "--no-keep-c", "-o", "dist/jsql"]);
