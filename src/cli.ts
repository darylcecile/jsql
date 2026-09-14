import { readFileSync } from "node:fs";

declare function runSql(input: string, sql: string): number;

const usage = `Usage: jsql 'SELECT ... FROM data' < input.json

Query a JSON array or a single JSON value using SQLite SQL.
Object keys become columns in the table "data"; scalar rows use "value".
Writes a JSON array to stdout.

Example:
  echo '[{"name":"Ada","age":36}]' | jsql 'SELECT name FROM data WHERE age > 30'
`;

function main() {
  const args = process.argv.slice(2);
  if (args.length === 1 && (args[0] === "--help" || args[0] === "-h")) {
    process.stdout.write(usage);
    return;
  }
  if (args.length !== 1 || !args[0]) throw new Error(usage.trim());
  if (process.stdin.isTTY) throw new Error("Pipe JSON into jsql, or use < input.json.");

  process.exit(runSql(readFileSync(0, "utf8"), args[0]));
}

try {
  main();
} catch (error) {
  console.error(`jsql: ${error instanceof Error ? error.message : String(error)}`);
  process.exit(1);
}
