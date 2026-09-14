#include <sqlite3.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

static void column(sqlite3_str *view, const char *key, int *count) {
  if ((*count)++) sqlite3_str_appendall(view, ",");
  sqlite3_str_appendf(view, "CASE WHEN row.type='object' THEN json_extract(row.value,'$.'||json_quote(%Q)) ELSE ", key);
  sqlite3_str_appendall(view, strcmp(key, "value") == 0
    ? "CASE WHEN row.type='array' THEN json(row.value) ELSE row.value END" : "NULL");
  sqlite3_str_appendf(view, " END AS \"%w\"", key);
}

typedef struct { char *raw; size_t length; char *name; } Key;
typedef struct {
  Key *keys;
  int size, capacity, columns;
  sqlite3_stmt *decode;
  sqlite3_str *view;
} Schema;

static int add_key(Schema *schema, const uint8_t *raw, size_t length) {
  for (int i = 0; i < schema->size; i++) {
    Key *key = &schema->keys[i];
    if (key->length == length && memcmp(key->raw, raw, length) == 0) return 1;
  }
  // Decode each distinct spelling once, including escaped/Unicode property names.
  sqlite3_bind_text64(schema->decode, 1, (const char *)raw, length, SQLITE_STATIC, SQLITE_UTF8);
  if (sqlite3_step(schema->decode) != SQLITE_ROW) return 0;
  char *name = sqlite3_mprintf("%s", sqlite3_column_text(schema->decode, 0));
  sqlite3_reset(schema->decode);
  if (!name) return 0;
  int duplicate = 0;
  for (int i = 0; i < schema->size; i++) if (strcmp(schema->keys[i].name, name) == 0) duplicate = 1;
  if (!duplicate) column(schema->view, name, &schema->columns);
  if (schema->size == schema->capacity) {
    int capacity = schema->capacity ? schema->capacity * 2 : 16;
    Key *keys = sqlite3_realloc64(schema->keys, capacity * sizeof(Key));
    if (!keys) { sqlite3_free(name); return 0; }
    schema->keys = keys;
    schema->capacity = capacity;
  }
  char *copy = sqlite3_mprintf("%.*s", (int)length, raw);
  if (!copy) { sqlite3_free(name); return 0; }
  schema->keys[schema->size++] = (Key){copy, length, name};
  return 1;
}

static int space(uint8_t c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }

// SQLite has already validated the JSON. This scan discovers only top-level row
// keys, avoiding millions of virtual-table steps for repeated object schemas.
static int scan_schema(sqlite3 *db, sqlite3_str *view, const uint8_t *input, size_t length) {
  Schema schema = {.view = view};
  int result = -1;
  if (sqlite3_prepare_v2(db, "SELECT json_extract(?,'$')", -1, &schema.decode, NULL) != SQLITE_OK) goto done;
  size_t first = 0;
  while (first < length && space(input[first])) first++;
  int array = first < length && input[first] == '[';
  int object = first < length && input[first] == '{';
  int row_depth = array ? 2 : 1, depth = 0;
  if (!array && !object) {
    if (!add_key(&schema, (const uint8_t *)"\"value\"", 7)) goto done;
  } else for (size_t i = first; i < length; i++) {
    uint8_t c = input[i];
    if (c == '"') {
      size_t start = i++;
      while (i < length && input[i] != '"') { if (input[i] == '\\') i++; i++; }
      size_t next = i + 1;
      while (next < length && space(input[next])) next++;
      if (object && depth == row_depth && next < length && input[next] == ':') {
        if (!add_key(&schema, input + start, i - start + 1)) goto done;
      } else if (array && depth == 1) {
        if (!add_key(&schema, (const uint8_t *)"\"value\"", 7)) goto done;
      }
    } else if (c == '{' || c == '[') {
      if (array && depth == 1) {
        object = c == '{';
        if (!object && !add_key(&schema, (const uint8_t *)"\"value\"", 7)) goto done;
      }
      depth++;
    } else if (c == '}' || c == ']') depth--;
    else if (array && depth == 1 && c != ',' && !space(c)) {
      if (!add_key(&schema, (const uint8_t *)"\"value\"", 7)) goto done;
      while (i + 1 < length && input[i + 1] != ',' && input[i + 1] != ']') i++;
    }
  }
  result = schema.columns;
done:
  for (int i = 0; i < schema.size; i++) { sqlite3_free(schema.keys[i].raw); sqlite3_free(schema.keys[i].name); }
  sqlite3_free(schema.keys);
  sqlite3_finalize(schema.decode);
  return result;
}

typedef struct { char bytes[65536]; size_t used; } Output;

static void flush(Output *out) {
  fwrite(out->bytes, 1, out->used, stdout);
  out->used = 0;
}

static void bytes(Output *out, const void *data, size_t length) {
  if (length > sizeof(out->bytes) - out->used) flush(out);
  if (length >= sizeof(out->bytes)) fwrite(data, 1, length, stdout);
  else { memcpy(out->bytes + out->used, data, length); out->used += length; }
}

static void character(Output *out, char c) {
  if (out->used == sizeof(out->bytes)) flush(out);
  out->bytes[out->used++] = c;
}

static void integer(Output *out, int64_t value) {
  char buffer[32];
  int position = sizeof(buffer);
  uint64_t n = value < 0 ? (uint64_t)(-(value + 1)) + 1 : (uint64_t)value;
  do { buffer[--position] = '0' + n % 10; n /= 10; } while (n);
  if (value < 0) buffer[--position] = '-';
  bytes(out, buffer + position, sizeof(buffer) - position);
}

// Batch ordinary UTF-8 bytes and escape JSON's special bytes.
static void json_string(Output *out, const unsigned char *s, int length) {
  character(out, '"');
  int start = 0;
  for (int i = 0; i < length; i++) {
    unsigned char c = s[i];
    if (c >= 32 && c != '"' && c != '\\') continue;
    bytes(out, s + start, i - start);
    if (c == '"' || c == '\\') { character(out, '\\'); character(out, c); }
    else { char escape[7]; snprintf(escape, sizeof(escape), "\\u%04x", c); bytes(out, escape, 6); }
    start = i + 1;
  }
  bytes(out, s + start, length - start);
  character(out, '"');
}

typedef struct { const void *data; sqlite3_uint64 size; } Document;

static void input_document(sqlite3_context *context, int argc, sqlite3_value **argv) {
  (void)argc; (void)argv;
  const Document *doc = sqlite3_user_data(context);
  sqlite3_result_blob64(context, doc->data, doc->size, SQLITE_STATIC);
}

int32_t jsql_query(const uint8_t *input, size_t input_len,
                   const uint8_t *sql, size_t sql_len) {
  sqlite3 *db = NULL;
  sqlite3_stmt *stmt = NULL;
  sqlite3_stmt *document = NULL;
  Document doc = {0};
  sqlite3_str *view = NULL;
  char *schema = NULL;
  const char *error = NULL;
  int status = 1;
  if (sqlite3_open(":memory:", &db) != SQLITE_OK) goto done;
  sqlite3_exec(db, "PRAGMA temp_store=MEMORY", NULL, NULL, NULL);

  // Validate strict JSON before the key scanner, then retain a binary JSON buffer.
  size_t first = 0;
  while (first < input_len && space(input[first])) first++;
  const char *parse = first < input_len && input[first] == '['
    ? "SELECT jsonb(?1) WHERE json_valid(?1)"
    : "SELECT jsonb_array(jsonb(?1)) WHERE json_valid(?1)";
  if (sqlite3_prepare_v2(db, parse, -1, &document, NULL) != SQLITE_OK) goto done;
  sqlite3_bind_text64(document, 1, (const char *)input, input_len, SQLITE_STATIC, SQLITE_UTF8);
  int rc = sqlite3_step(document);
  if (rc != SQLITE_ROW) {
    if (rc == SQLITE_DONE) error = "input must be one JSON document";
    goto done;
  }
  doc.data = sqlite3_column_blob(document, 0);
  doc.size = sqlite3_column_bytes(document, 0);
  // The document statement owns this buffer until every reader is finalized.
  if (sqlite3_create_function_v2(db, "_input", 0, SQLITE_UTF8 | SQLITE_DETERMINISTIC,
      &doc, input_document, NULL, NULL, NULL) != SQLITE_OK) goto done;

  view = sqlite3_str_new(db);
  sqlite3_str_appendall(view, "CREATE VIEW data AS SELECT ");
  int count = scan_schema(db, view, input, input_len);
  if (count < 0) goto done;
  if (!count) sqlite3_str_appendall(view, "NULL AS value");
  sqlite3_str_appendall(view, " FROM jsonb_each(_input()) AS row");
  schema = sqlite3_str_finish(view); view = NULL;
  if (sqlite3_exec(db, schema, NULL, NULL, NULL) != SQLITE_OK) goto done;

  if (sqlite3_prepare_v3(db, (const char *)sql, (int)sql_len, 0, &stmt, NULL) != SQLITE_OK) goto done;
  Output out = {0};
  character(&out, '[');
  int row_count = 0;
  int columns = sqlite3_column_count(stmt);
  while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
    if (row_count++) character(&out, ',');
    character(&out, '{');
    for (int i = 0; i < columns; i++) {
      if (i) character(&out, ',');
      const char *name = sqlite3_column_name(stmt, i);
      json_string(&out, (const unsigned char *)name, (int)strlen(name));
      character(&out, ':');
      int type = sqlite3_column_type(stmt, i);
      if (type == SQLITE_NULL) bytes(&out, "null", 4);
      else if (type == SQLITE_INTEGER) integer(&out, sqlite3_column_int64(stmt, i));
      else if (type == SQLITE_FLOAT) {
        double value = sqlite3_column_double(stmt, i);
        char number[32];
        int length = isfinite(value) ? snprintf(number, sizeof(number), "%.17g", value) : snprintf(number, sizeof(number), "null");
        bytes(&out, number, length);
      } else if (type == SQLITE_BLOB) {
        const unsigned char *data = sqlite3_column_blob(stmt, i);
        int length = sqlite3_column_bytes(stmt, i);
        character(&out, '{');
        for (int j = 0; j < length; j++) {
          if (j) character(&out, ',');
          character(&out, '"'); integer(&out, j); bytes(&out, "\":", 2);
          integer(&out, data[j]);
        }
        character(&out, '}');
      } else json_string(&out, sqlite3_column_text(stmt, i), sqlite3_column_bytes(stmt, i));
    }
    character(&out, '}');
  }
  if (rc != SQLITE_DONE) goto done;
  bytes(&out, "]\n", 2);
  flush(&out);
  status = 0;
done:
  if (status) fprintf(stderr, "jsql: %s\n", error ? error : db ? sqlite3_errmsg(db) : "cannot open SQLite");
  if (view) sqlite3_free(sqlite3_str_finish(view));
  sqlite3_free(schema);
  sqlite3_finalize(stmt);
  sqlite3_finalize(document);
  sqlite3_close(db);
  return status;
}
