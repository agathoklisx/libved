/* Upstream repository at:
 * https://github.com/zserge/jsmn
 * see at jsmn directory for details/examples and LICENSE or
 * git clone https://github.com/zserge/jsmn.git
 * Many Thanks for the excellent code.
 *
 * Also look the LICENSE at this directory, as I've used from the jsmn-example
 * project, the state machine to parse the results from jsmn and modeled the
 * json structure based on this.
 */

#define JSMN_STATIC
#include "jsmn/jsmn.h"

#define JSON_NUM_TOKENS 256
#define JSON_ERR_MSG_LEN 256
#define JSON_DEF_DATA_SIZE 256

DeclareType (json);

typedef int (*JsonParse_cb) (json_t *);
typedef int (*JsonGetData_cb) (json_t *);

NewType (json,
  char
     error_msg[JSON_ERR_MSG_LEN];

  string_t *data;

  int
    retval,
    num_tokens;

  jsmntok_t   *tokens;
  jsmn_parser *jsmn;

  JsonParse_cb parse;
  JsonGetData_cb get_data;

  void *obj;
);

NewSelf (json,
    void  (*free) (json_t **);
  json_t *(*new) (int, JsonParse_cb, JsonGetData_cb);
     int  (*parse) (json_t *);
);

NewClass (json,
  Self (json) self;
);

private void json_free (json_t **thisp) {
  json_t *this = *thisp;
  if (NULL is this) return;
  String.free (this->data);

  ifnot (NULL is this->tokens) {
    free (this->tokens);
    this->tokens = NULL;
  }

  ifnot (NULL is this->jsmn) {
    free (this->jsmn);
    this->jsmn = NULL;
  }

  free (this);
  thisp = NULL;
}

private json_t *json_new (int num_tokens, JsonParse_cb parse_cb,
                                     JsonGetData_cb get_data_cb) {
  json_t *this = AllocType (json);
  this->num_tokens = (0 > num_tokens ? JSON_NUM_TOKENS : num_tokens);
  this->tokens = Alloc (sizeof (jsmntok_t) * this->num_tokens);
  this->jsmn = Alloc (sizeof (jsmn_parser));
  this->parse = parse_cb;
  this->get_data = get_data_cb;
  this->retval = NOTOK;
  this->data = String.new (JSON_DEF_DATA_SIZE);
  jsmn_init (this->jsmn);
  return this;
}

private int json_parse (json_t *this) {
  if (NOTOK is this->get_data (this)) return NOTOK;

  this->retval = jsmn_parse (this->jsmn, this->data->bytes, this->data->num_bytes,
 	 this->tokens, this->num_tokens);

  while (this->retval is JSMN_ERROR_NOMEM) {
    this->num_tokens = (this->num_tokens * 2) + 1;
    this->tokens = Realloc (this->tokens, sizeof (jsmntok_t) * this->num_tokens);
    this->retval = jsmn_parse (this->jsmn, this->data->bytes, this->data->num_bytes,
        this->tokens, this->num_tokens);
  }

  if (this->retval < 0) {
    if (this->retval is JSMN_ERROR_INVAL)
      Cstring.cp (this->error_msg, JSON_ERR_MSG_LEN, "jsmn_parse: invalid JSON string", JSON_ERR_MSG_LEN -1);
    else if (this->retval is JSMN_ERROR_PART)
      Cstring.cp (this->error_msg, JSON_ERR_MSG_LEN, "jsmn_parse: truncated JSON string", JSON_ERR_MSG_LEN - 1);
  }

  ifnot (NULL is this->parse) return this->parse (this);
  return this->retval;
}

public json_T __init_json__ (void) {
  return ClassInit (json,
    .self = SelfInit (json,
      .free = json_free,
      .new = json_new,
      .parse = json_parse
    )
  );
}

public void __deinit_json__ (json_T *this) {
  (void) this;
}
