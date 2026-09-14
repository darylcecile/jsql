import { expect, test } from "bun:test";
import { query } from "./run";

test("loads JSON values for filtering and aggregation", () => {
  const input = JSON.stringify([
    { name: "Ada", score: 30, active: true },
    { name: "Lin", score: 20, active: true },
    { name: "Sam", score: 90, active: false },
  ]);
  expect(query(input, "SELECT sum(score) AS total FROM data WHERE active = 1"))
    .toEqual([{ total: 50 }]);
});

test("collects columns across rows and quotes JSON keys", () => {
  const input = '[{"id":1,"display name":"Ada"},{"id":2,"a\\"b":7}]';
  expect(query(input, 'SELECT * FROM data ORDER BY id')).toEqual([
    { id: 1, "display name": "Ada", 'a"b': null },
    { id: 2, "display name": null, 'a"b': 7 },
  ]);
});

test("loads a single object with nested JSON and distinct scalar types", () => {
  const input = '{"n":0.12345678901234567,"s":"42","profile":{"city":"London"},"tags":["sql"],"note":null,"text":"quotes\\\" and\\n🙂"}';
  expect(query(input, "SELECT * FROM data")).toEqual([
    { n: 0.12345678901234567, s: "42", profile: '{"city":"London"}', tags: '["sql"]', note: null, text: 'quotes" and\n🙂' },
  ]);
  expect(query(input, "SELECT CAST(s AS BLOB) AS bytes FROM data")).toEqual([
    { bytes: { "0": 52, "1": 50 } },
  ]);
});

test("exposes scalar rows through value, including an empty array", () => {
  expect(query('[3,1,2]', "SELECT value FROM data ORDER BY value")).toEqual([
    { value: 1 }, { value: 2 }, { value: 3 },
  ]);
  expect(query('[]', "SELECT * FROM data")).toEqual([]);
});
