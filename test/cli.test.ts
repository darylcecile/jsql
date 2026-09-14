import { expect, test } from "bun:test";
import { run } from "./run";

test("queries piped stdin and writes newline-terminated JSON", () => {
  const output = run('[{"name":"Ada","age":36},{"name":"Lin","age":25}]',
    "SELECT name FROM data WHERE age >= 30 ORDER BY name");
  expect(output).toBe('[{"name":"Ada"}]\n');
});

test("prints help without reading a JSON document", () => {
  expect(run("", "--help")).toContain("Usage: jsql");
});
