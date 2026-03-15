/*
 * json.h  --  Minimal JSON parser/builder for NCD
 *
 * Supports: objects, arrays, strings, integers, booleans, null.
 * All nodes are heap-allocated; call json_free() to release a tree.
 */

#ifndef NCD_JSON_H
#define NCD_JSON_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>

/* ------------------------------------------------------------------ types */

typedef enum {
    JSON_NULL = 0,
    JSON_BOOL,
    JSON_INT,
    JSON_STRING,
    JSON_ARRAY,
    JSON_OBJECT
} JsonType;

typedef struct JsonNode JsonNode;
typedef struct JsonPair JsonPair;

/* linked-list node for object key/value pairs */
struct JsonPair {
    char     *key;
    JsonNode *value;
    JsonPair *next;
};

struct JsonNode {
    JsonType  type;
    JsonNode *next;          /* sibling linkage used by arrays */

    union {
        bool      bval;
        long long ival;
        char     *sval;

        struct {             /* ARRAY */
            JsonNode *head;
            JsonNode *tail;
            int       count;
        } arr;

        struct {             /* OBJECT */
            JsonPair *head;
            JsonPair *tail;
        } obj;
    };
};

/* ---------------------------------------------------------------- parsing */

/* Parse a NUL-terminated JSON text.  Returns NULL on error. */
JsonNode *json_parse(const char *text);

/* Release an entire JSON tree. */
void json_free(JsonNode *node);

/* ---------------------------------------------------------- object access */

/* Return the value for key, or NULL if not found. */
JsonNode   *json_get(const JsonNode *obj, const char *key);

/* ----------------------------------------------------------- array access */

/* Return element at index (0-based), or NULL if out of range. */
JsonNode   *json_at(const JsonNode *arr, int index);

/* Number of elements in an array. */
int         json_count(const JsonNode *arr);

/* ----------------------------------------------------------- value access */

long long   json_int (const JsonNode *node);   /* JSON_INT  */
bool        json_bool(const JsonNode *node);   /* JSON_BOOL */
const char *json_str (const JsonNode *node);   /* JSON_STRING */

/* ---------------------------------------------------- building / mutation */

JsonNode *json_make_null(void);
JsonNode *json_make_bool(bool val);
JsonNode *json_make_int (long long val);
JsonNode *json_make_str (const char *val);   /* copies val */
JsonNode *json_make_arr (void);
JsonNode *json_make_obj (void);

/* Add key->val pair to an object (takes ownership of val). */
void json_obj_set(JsonNode *obj, const char *key, JsonNode *val);

/* Append val to an array (takes ownership of val). */
void json_arr_add(JsonNode *arr, JsonNode *val);

/* ------------------------------------------------------------- serialise  */

/* Returns a malloc'd compact JSON string; caller must free(). */
char *json_stringify(const JsonNode *node);

#ifdef __cplusplus
}
#endif

#endif /* NCD_JSON_H */
