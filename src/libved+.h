#ifndef LIBVED_PLUS_H
#define LIBVED_PLUS_H

extern Class (this) *__This__;

#define THIS    __This__
#define This    (*(Self (this) *) __This__->self)

#define THIS_E  THIS->__E__
#define E       THIS_E->self

#define Ed      __This__->__E__->__Ed__->self
#define Win     __This__->__E__->__Ed__->__Win__.self
#define Buf     __This__->__E__->__Ed__->__Buf__.self
#define I       __This__->__E__->__Ed__->__I__.self
#define Fd      __This__->__E__->__Ed__->__Fd__.self
#define Re      __This__->__E__->__Ed__->__Re__.self
#define Msg     __This__->__E__->__Ed__->__Msg__.self
#define Dir     __This__->__E__->__Ed__->__Dir__.self
#define File    __This__->__E__->__Ed__->__File__.self
#define Path    __This__->__E__->__Ed__->__Path__.self
#define Vsys    __This__->__E__->__Ed__->__Vsys__.self
#define Term    __This__->__E__->__Ed__->__Term__.self
#define Smap    __This__->__E__->__Ed__->__Smap__.self
#define Imap    __This__->__E__->__Ed__->__Imap__.self
#define Input   __This__->__E__->__Ed__->__Input__.self
#define Error   __This__->__E__->__Ed__->__Error__.self
#define Rline   __This__->__E__->__Ed__->__Rline__.self
#define Video   __This__->__E__->__Ed__->__Video__.self
#define Cursor  __This__->__E__->__Ed__->__Cursor__.self
#define Screen  __This__->__E__->__Ed__->__Screen__.self
#define String  __This__->__E__->__Ed__->__String__.self
#define Cstring __This__->__E__->__Ed__->__Cstring__.self
#define Ustring __This__->__E__->__Ed__->__Ustring__.self
#define Vstring __This__->__E__->__Ed__->__Vstring__.self

#define Math     ((Self (this) *) __This__->self)->math
#define Proc     ((Self (this) *) __This__->self)->proc
#define Spell    ((Self (this) *) __This__->self)->spell
#define Argparse ((Self (this) *) __This__->self)->argparse
#define Sys      ((Self (this) *) __This__->self)->sys

#define __SYS__  ((Prop (this) *) __This__->prop)->sys

#ifdef HAS_TCC
#define Tcc      ((Self (this) *) __This__->self)->tcc
#endif

#ifdef HAS_PROGRAMMING_LANGUAGE
#define __L__ ((Prop (this) *) __This__->prop)->__L
#define L (*__L__).self
#define L_CUR_STATE __L__->states[__L__->cur_state]

#include <stdbool.h>
#include <lai.h>
#include <led.h>

typedef l_table_t ltableself_t;
typedef l_table_get_t ltablegetself_t;
typedef l_module_t lmodulegetself_t;
typedef l_t lself_t;
typedef lang_t l_T;
#endif /* HAS_PROGRAMMING_LANGUAGE */

mutable public void __alloc_error_handler__ (int, size_t, char *,
                                                 const char *, int);
public void sigwinch_handler (int sig);

DeclareType (argparse);
DeclareType (argparse_option);

typedef int argparse_callback (argparse_t *, const argparse_option_t *);
public int argparse_help_cb (argparse_t *, const argparse_option_t *);

enum argparse_flag {
  ARGPARSE_STOP_AT_NON_OPTION = (1 << 0),
  SHORT_OPT_HAS_NO_DASH = (1 << 1),
  ARGPARSE_DONOT_EXIT_ON_UNKNOWN = (1 << 2)
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

typedef Imap_t spelldic_t;
NewType (spell,
  char
    word[MAXLEN_WORD];

  int
    retval;

  utf8 c;

  size_t
    num_dic_words,
    min_word_len,
    word_len;

  string_t
    *tmp,
    *dic_file;

  spelldic_t
    *dic,
    *ign_words;

  Vstring_t
    *words,
    *guesses,
    *messages;
);

NewSelf (spell,
  spell_t *(*new) (void);
  void
    (*free) (spell_t *, int),
    (*clear) (spell_t *, int),
    (*add_word_to_dictionary) (spell_t *, char *);

  int
    (*init_dictionary) (spell_t *, string_t *, int, int),
    (*correct) (spell_t *);
);

NewClass (spell,
  Self (spell) self;
  spelldic_t *current_dic;
  string_t *dic_file;
  int num_entries;
);

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
     prompt_atend,
     retval;

   size_t stdin_buf_size;

   term_t *term;
   buf_t *buf;
   ed_t *ed;
   PopenRead_cb read;

   void *object;
);

NewType (proc,
  Prop (proc) *prop;
);

NewSelf (proc,
  proc_t *(*new) (void);

  void
    (*free) (proc_t *),
    (*free_argv) (proc_t *),
    (*set_stdin) (proc_t *, char *, size_t);

  char **(*parse) (proc_t *, char *);
  int
    (*exec) (proc_t *, char *),
    (*read) (proc_t *);

  pid_t (*wait) (proc_t *);
);

NewClass (proc,
  Self (proc) self;
);

/* Math Expr */

DeclareType (math);
DeclareSelf (math);

typedef void (*MathFree_cb) (math_t *);
typedef int  (*MathEval_cb) (math_t *);
typedef int  (*MathInterp_cb) (math_t *);
typedef int  (*MathCompile_cb) (math_t *);
typedef char *(*MathStrError_cb) (math_t *, int);

NewType (math,
  int retval;
  int error;

  string_t
    *lang,
    *data,
    *error_string;

  union {
    double double_v;
       int int_v;
  } val;

  void *i_obj;
  void *ff_obj;

  MathEval_cb eval;
  MathInterp_cb interp;
  MathCompile_cb compile;
  MathFree_cb free;
  MathStrError_cb strerror;
);

NewSelf (math,
  math_t
    *(*new) (char *, MathCompile_cb, MathEval_cb, MathInterp_cb, MathFree_cb);
  void
    (*free) (math_t **);

  char
    *(*strerror) (math_t *, int);

  int
    (*interp) (math_t *),
    (*eval) (math_t *),
    (*compile) (math_t *);
);

NewClass (math,
  Self (math) self;
);

#ifdef HAS_TCC
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
#endif /* HAS_TCC */

/* ************************************************ */

NewSelf (argparse,
  int
    (*init) (argparse_t *, argparse_option_t *, const char *const *, int),
    (*exec) (argparse_t *, int, const char **);
);

NewClass (argparse,
  Self (argparse) self;
);

DeclareClass (sys);

NewType (sysenv,
  string_t
    *sysname,
    *man_exec,
    *battery_dir;
);

NewProp (sys,
  string_t *shared_str;
  sysenv_t *env;
);

NewSubSelf (sys, get,
  string_t *(*env) (Class (sys) *, char *);
);

NewSelf (sys,
  SubSelf (sys, get) get;
  int
    (*mkdir) (char *, mode_t, int, int),
    (*man) (buf_t **, char *, int),
    (*stat) (buf_t **, char *),
    (*battery_info) (char *, int);

);

NewClass (sys,
  Prop (sys) *prop;
  Self (sys)  self;
);

NewSelf (this,
  Self (argparse) argparse;
  Self (spell) spell;
  Self (proc) proc;
  Self (math) math;
  Self (sys) sys;

#ifdef HAS_TCC
  Self (tcc) tcc;
#endif

  string_t *(*parse_command) (Class (this) *, char *);
);

NewProp (this,
  char *name;

  Class (spell) spell;
  Class (sys) *sys;

#ifdef HAS_TCC
  Class (tcc) tcc;
#endif

#ifdef HAS_PROGRAMMING_LANGUAGE
  Class (l) *__L;
#endif
);

#ifdef HAS_USER_EXTENSIONS
  private void __init_usr__ (ed_t *);
  private void __deinit_usr__ (void);
#endif

#ifdef HAS_LOCAL_EXTENSIONS
  private void __init_local__ (ed_t *);
  private void __deinit_local__ (void);
#endif

public void __init_ext__ (ed_t *);
public void __deinit_ext__ (void);

public Class (this) *__init_this__ (void);
public void __deinit_this__ (Class (this) **);
#endif
