#ifndef JSON_H
#define JSON_H

#include "jsmn/jsmn.h"

DeclareClass (json);

public Class (json) JsonClass;
#define  Json JsonClass.self

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

public Class (json) __init_json__ (void);
public void __deinit_json__ (Class (json) *);
#endif
