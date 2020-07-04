#ifndef LIBVED_PLUS_H
#define LIBVED_PLUS_H

DeclareSelf (This);
DeclareClass (I);

extern Class (This) *__THIS__;
extern Self (This)  *__SELF__;
extern Class (I)    *__I__;

/* avoid __SELF__ ? */
//#define This(__f__, ...) (*(Self (This) *) (__THIS__->self)).__f__ (__THIS__, ## __VA_ARGS__)
#define This(__f__, ...) (*__SELF__).__f__ (__THIS__, ## __VA_ARGS__)
#define E(__f__, ...) This(e.__f__, ## __VA_ARGS__)
#define In (*__I__).self

#if HAS_RUNTIME_INTERPRETER
#define L (*__L__).self
#define L_CUR_STATE  __L__->states[__L__->cur_state]
#endif

#define __E     __THIS__->__E__->self
#define Ed      __THIS__->__E__->ed->self
#define Cstring __THIS__->__E__->ed->Cstring.self
#define Ustring __THIS__->__E__->ed->Ustring.self
#define Vstring __THIS__->__E__->ed->Vstring.self
#define String  __THIS__->__E__->ed->String.self
#define Rline   __THIS__->__E__->ed->Rline.self
#define Error   __THIS__->__E__->ed->Error.self
#define Vsys    __THIS__->__E__->ed->Vsys.self
#define Term    __THIS__->__E__->ed->Term.self
#define Cursor  __THIS__->__E__->ed->Cursor.self
#define Video   __THIS__->__E__->ed->Video.self
#define Screen  __THIS__->__E__->ed->Screen.self
#define Input   __THIS__->__E__->ed->Input.self
#define File    __THIS__->__E__->ed->File.self
#define Path    __THIS__->__E__->ed->Path.self
#define Buf     __THIS__->__E__->ed->Buf.self
#define Win     __THIS__->__E__->ed->Win.self
#define Msg     __THIS__->__E__->ed->Msg.self
#define Dir     __THIS__->__E__->ed->Dir.self
#define Re      __THIS__->__E__->ed->Re.self

#if HAS_SHELL_COMMANDS
#define Proc ((Self (This) *) __THIS__->self)->proc
#endif

mutable public void __alloc_error_handler__ (int, size_t, char *,
                                                 const char *, int);
private void sigwinch_handler (int sig);

DeclareType (argparse);
DeclareType (argparse_option);

typedef int argparse_callback (argparse_t *, const argparse_option_t *);
int argparse_help_cb (argparse_t *, const argparse_option_t *);
int argparse_init (argparse_t *, argparse_option_t *, const char *const *, int);
int argparse_parse (argparse_t *, int, const char **);

enum argparse_flag {
  ARGPARSE_STOP_AT_NON_OPTION = 1,
  SHORT_OPT_HAS_NO_DASH,
};

enum argparse_option_type {
  ARGPARSE_OPT_END,
  ARGPARSE_OPT_GROUP,
  ARGPARSE_OPT_BOOLEAN,
  ARGPARSE_OPT_BIT,
  ARGPARSE_OPT_INTEGER,
  ARGPARSE_OPT_FLOAT,
  ARGPARSE_OPT_STRING,
};

enum argparse_option_flags {
  OPT_NONEG = 1,
};

#define OPT_END()        { ARGPARSE_OPT_END, 0, NULL, NULL, 0, NULL, 0, 0 }
#define OPT_BOOLEAN(...) { ARGPARSE_OPT_BOOLEAN, __VA_ARGS__ }
#define OPT_BIT(...)     { ARGPARSE_OPT_BIT, __VA_ARGS__ }
#define OPT_INTEGER(...) { ARGPARSE_OPT_INTEGER, __VA_ARGS__ }
#define OPT_FLOAT(...)   { ARGPARSE_OPT_FLOAT, __VA_ARGS__ }
#define OPT_STRING(...)  { ARGPARSE_OPT_STRING, __VA_ARGS__ }
#define OPT_GROUP(h)     { ARGPARSE_OPT_GROUP, 0, NULL, NULL, h, NULL, 0, 0 }
#define OPT_HELP()       OPT_BOOLEAN('h', "help", NULL,                 \
                                     "show this help message and exit", \
                                     argparse_help_cb, 0, OPT_NONEG)

NewType (argparse_option,
  enum argparse_option_type type;
  const char short_name;
  const char *long_name;
  void *value;
  const char *help;
  argparse_callback *callback;
  intptr_t data;
  int flags;
);

NewType (argparse,
  const argparse_option_t *options;
  const char *const *usages;
  int flags;
  const char *description;
  const char *epilog;
  int argc;
  const char **argv;
  const char **out;
  int cpidx;
  const char *optvalue;
);

#if HAS_REGEXP
  private int ext_re_compile (regexp_t *);
  private int ext_re_exec (regexp_t *, char *, size_t);
  private string_t *ext_re_parse_substitute (regexp_t *, char *, char *);
#endif

#if HAS_SHELL_COMMANDS
NewProp (proc,
  pid_t  pid;

  char
    *stdin_buf,
    **argv;

   int
     argc,
     sys_errno,
     is_bg,
     read_stdout,
     read_stderr,
     dup_stdin,
     stdout_fds[2],
     stderr_fds[2],
     stdin_fds[2],
     status,
     reset_term,
     prompt_atend;

   size_t stdin_buf_size;

   term_t *term;
   buf_t *buf;
   ed_t *ed;
   PopenRead_cb read;
);

NewType (proc,
  Prop (proc) *prop;
);

NewSelf (proc,
  proc_t *(*new) (void);

  void
    (*free) (proc_t *),
    (*free_argv) (proc_t *);

  char **(*parse) (proc_t *, char *);
  int
    (*exec) (proc_t *, char *),
    (*read) (proc_t *);

  pid_t (*wait) (proc_t *);
);

NewClass (proc,
  Self (proc) self;
);

#define PIPE_READ_END  0
#define PIPE_WRITE_END 1
  public Class (proc) __init_proc__ (void);
  public void __deinit_proc__ (Class (proc) **);

  private int ext_ed_sh_popen (ed_t *, buf_t *, char *, int, int, PopenRead_cb);
#endif /* HAS_SHELL_COMMANDS */

#if HAS_USER_EXTENSIONS
  private void __init_usr__ (ed_t *);
  private void __deinit_usr__ (void);
#endif

#if HAS_LOCAL_EXTENSIONS
  private void __init_local__ (ed_t *);
  private void __deinit_local__ (void);
#endif

#if HAS_RUNTIME_INTERPRETER
#include <stdbool.h>
#include <lai.h>
#include <led.h>

typedef l_t Thislself_t;
typedef l_table_t Thisltableself_t;
typedef l_table_get_t Thisltablegetself_t;
typedef l_module_t Thislmodulegetself_t;
typedef lang_t L_T;
#endif /* HAS_RUNTIME_INTERPRETER */

#if HAS_TCC
#include <libtcc.h>

#define TCC_CONFIG_TCC_DIR    1
#define TCC_ADD_INC_PATH      2
#define TCC_ADD_SYS_INC_PATH  3
#define TCC_ADD_LPATH         4
#define TCC_ADD_LIB           5
#define TCC_SET_OUTPUT_PATH   6
#define TCC_COMPILE_FILE      7

typedef void (*TCCErrorFunc) (void *, const char *);

NewType (tcc,
  TCCState *handler;
  int retval;
  int state;
  int id;
);

NewSubSelf (tcc, set,
  int
    (*path) (tcc_t *, char *, int),
    (*output_type) (tcc_t *, int);

  void (*options) (tcc_t *, char *);
  void (*error_handler) (tcc_t *, void *, TCCErrorFunc);
);

NewProp (tcc,
  int id;
);

NewSelf (tcc,
  Prop (tcc) prop;
  SubSelf (tcc, set) set;

  tcc_t *(*new) (void);
    void (*free) (tcc_t **);

  int
     (*compile_string) (tcc_t *, char *),
     (*compile_file) (tcc_t *, char *),
     (*run) (tcc_t *, int, char **),
     (*relocate) (tcc_t *, void *);
);

NewClass (tcc,
  Self (tcc) self;
);

public tcc_T __init_tcc__ (void);
public void __deinit_tcc__ (tcc_T **this);

#endif /* HAS_TCC */

private void __init_ext__ (ed_t *);
private void __deinit_ext__ (void);

/* ************************************************ */

typedef intptr_t ival_t;
typedef ival_t (*Cfunc) (ival_t, ival_t, ival_t, ival_t);
typedef ival_t (*Opfunc) (ival_t, ival_t);

enum {
  I_OK = 0,
  I_ERR_NOMEM = -1,
  I_ERR_SYNTAX = -2,
  I_ERR_UNKNOWN_SYM = -3,
  I_ERR_BADARGS = -4,
  I_ERR_TOOMANYARGS = -5,
  I_ERR_OK_ELSE = 1, // special internal condition
};

#define MAX_BUILTIN_PARAMS 4

// symbols can take the following forms:
#define INT      0x0  // integer
#define STRING   0x1  // string
#define OPERATOR 0x2  // operator; precedence in high 8 bits
#define ARG      0x3  // argument; value is offset on stack
#define BUILTIN  'B'  // builtin: number of operands in high 8 bits
#define USRFUNC  'f'  // user defined a procedure; number of operands in high 8 bits
#define TOK_BINOP 'o'

#define STRING_TYPE_FUNC_ARGUMENT 1 << 8

#define OUT_OF_FUNCTION_SCOPE     1 << 0
#define FUNCTION_SCOPE            1 << 1
#define FUNCTION_ARGUMENT_SCOPE   1 << 2

#define BINOP(x) (((x) << 8) + TOK_BINOP)
#define CFUNC(x) (((x) << 8) + BUILTIN)

#define I_TOK_SYMBOL     'A'
#define I_TOK_NUMBER     'N'
#define I_TOK_HEX_NUMBER 'X'
#define I_TOK_STRING     'S'
#define I_TOK_IF         'i'
#define I_TOK_IFNOT      'I'
#define I_TOK_ELSE       'e'
#define I_TOK_WHILE      'w'
#define I_TOK_PRINT      'p'
#define I_TOK_PRINTLN    'P'
#define I_TOK_VAR        'v'
#define I_TOK_VARDEF     'V'
#define I_TOK_BUILTIN    'B'
#define I_TOK_BINOP      'o'
#define I_TOK_FUNCDEF    'F'
#define I_TOK_SYNTAX_ERR 'Z'
#define I_TOK_RETURN     'r'
#define I_TOK_CHAR       'C'

typedef struct {
  unsigned len_;
  const char *ptr_;
} String_t;

typedef struct symbol {
  String_t name;
  int type;      // symbol type
  ival_t value;  // symbol value, or string ptr
} Sym;

typedef struct ufunc {
  String_t body;
  int nargs;
  String_t argName[MAX_BUILTIN_PARAMS];
} UserFunc;

typedef void (*PrintByte_cb) (FILE *, int);
typedef void (*PrintBytes_cb) (FILE *, const char *);
typedef void (*PrintFmtBytes_cb) (FILE *, const char *, ...);

NewType (i_options,
  char  *name;
  int    name_gen;
  size_t mem_size;
  size_t max_script_size;
  FILE  *err_fp;
  FILE  *out_fp;
  PrintByte_cb print_byte;
  PrintBytes_cb print_bytes;
  PrintFmtBytes_cb print_fmt_bytes;
);

#define I_INIT Type (i_options)
#define I_INIT_QUAL(...) (I_INIT) {      \
  .mem_size = 4096,                      \
  .print_byte = NULL,                    \
  .print_bytes = NULL,                   \
  .print_fmt_bytes = NULL,               \
  .err_fp = stderr,                      \
  .out_fp = stdout,                      \
  .name = NULL,                          \
  .name_gen = 97,                        \
  .max_script_size = 1 << 16,            \
  __VA_ARGS__}

DeclareType (i);

NewType (istring,
  char *ibuf;
  istring_t *next;
);

NewType (Istrings,
  Type (istring) *head;
);

NewType (i,
  char name[32];
  size_t
    mem_size,
    max_script_size;

  char *arena;
  int
    linenum,
    scope;

  FILE
    *err_fp,
    *out_fp;

  Type (Istrings) *strings;

  Sym *symptr;
  ival_t *valptr;

  String_t parseptr;  // acts as instruction pointer

  char ns[MAXLEN_NAME];
  ival_t fArgs[MAX_BUILTIN_PARAMS];
  ival_t fResult;

  // variables for parsing
  int curToken;  // what kind of token is current
  int tokenArgs; // number of arguments for this token
  String_t token;  // the actual string representing the token
  ival_t tokenVal;  // for symbolic tokens, the symbol's value
  Sym *tokenSym;

  PrintByte_cb print_byte;
  PrintBytes_cb print_bytes;
  PrintFmtBytes_cb print_fmt_bytes;

  Type (i) *next;
);

NewProp (I,
  int name_gen;
  Type (i) *head;
  int num_instances;
  int current_idx;
);

NewSubSelf (i, get,
  Type (i) *(*current) (Class (I) *);
  int (*current_idx) (Class (I) *);
);

NewSubSelf (i, set,
  Type (i) *(*current) (Class (I) *, int);
);

NewSelf (i,
  SubSelf (i, get) get;
  SubSelf (i, set) set;

  void (*free) (i_t **);
  i_t *(*new) (void);

  void (*remove_instance) (Class (I) *, Type (i) *);
  Type (i) *(*append_instance) (Class (I) *, Type (i) *);

  int
    (*init) (Class (I) *, i_t *, I_INIT),
    (*def)  (i_t *, const char *, int, ival_t),
    (*eval_file) (i_t *, const char *fname),
    (*eval_string) (i_t *, const char *, int, int);
);

NewClass (I,
  Self (i) self;
  Prop (I) *prop;
);

public char *i_pop_string (ival_t);
public Class (I) *__init_i__ (void);
public void __deinit_i__ (Class (I) **);

NewSubSelf (Thisparse, arg,
  int (*init) (Class (This) *, argparse_t *, argparse_option_t *, const char *const *, int);
  int (*run) (Class (This) *, argparse_t *, int, const char **);
);

NewSubSelf (Thise, set,
  void (*at_exit_cb) (Class (This) *, EAtExit_cb);

  ed_t
    *(*next) (Class (This) *),
    *(*current) (Class (This) *, int);
);

NewSubSelf (Thise, get,
  ed_t
    *(*next) (Class (This) *, ed_t *),
    *(*head) (Class (This) *),
    *(*current) (Class (This) *);

  int
    (*current_idx) (Class (This) *),
    (*prev_idx) (Class (This) *),
    (*num) (Class (This) *),
    (*state) (Class (This) *);

  string_t *(*env) (Class (This) *, char *);
);

NewSubSelf (This, e,
  SubSelf (Thise, set) set;
  SubSelf (Thise, get) get;

  ed_t
    *(*new)  (Class (This) *, ED_INIT_OPTS),
    *(*init) (Class (This) *, EdAtInit_cb);

  int
     (*main) (Class (This) *, buf_t *);
);

NewSubSelf (This, parse,
  SubSelf (Thisparse, arg) arg;
  string_t *(*command) (Class (This) *, char *);
);

NewSubSelf (This, i,
  i_t *(*init_instance) (Class (This) *, Class (I) *);
  int (*load_file) (Class (This) *, char *);
);

NewSelf (This,
  SubSelf (This, e) e;
  SubSelf (This, i) i;
  SubSelf (This, parse) parse;
#if HAS_SHELL_COMMANDS
  Self (proc) proc;
#endif

#if HAS_RUNTIME_INTERPRETER
  SubSelf (This, l) l;
#endif
);

NewProp (This,
  char *name;
  Class (I) *__I__;

#if HAS_RUNTIME_INTERPRETER
  Class (L) *__L__;
#endif
);

public Class (This) *__init_this__ (void);
public void __deinit_this__ (Class (This) **);
#endif
