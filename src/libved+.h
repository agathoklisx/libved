#ifndef LIBVED_PLUS_H
#define LIBVED_PLUS_H

DeclareSelf (This);

extern Class (This) *__THIS__;
extern Self (This)  *__SELF__;

/* avoid __SELF__ ? */
//#define This(__f__, ...) (*(Self (This) *) (__THIS__->self)).__f__ (__THIS__, ## __VA_ARGS__)
#define This(__f__, ...) (*__SELF__).__f__ (__THIS__, ## __VA_ARGS__)
#define E(__f__, ...) This(e.__f__, ## __VA_ARGS__)

#if HAS_PROGRAMMING_LANGUAGE
#define L (*__L__).self
#define L_CUR_STATE  __L__->states[__L__->cur_state]
#endif

#define __E     __THIS__->__E__->self
#define Ed      __THIS__->__E__->__ED__->self
#define Win     __THIS__->__E__->__ED__->Win.self
#define Buf     __THIS__->__E__->__ED__->Buf.self
#define I       __THIS__->__E__->__ED__->I.self
#define Re      __THIS__->__E__->__ED__->Re.self
#define Msg     __THIS__->__E__->__ED__->Msg.self
#define Dir     __THIS__->__E__->__ED__->Dir.self
#define File    __THIS__->__E__->__ED__->File.self
#define Path    __THIS__->__E__->__ED__->Path.self
#define Vsys    __THIS__->__E__->__ED__->Vsys.self
#define Term    __THIS__->__E__->__ED__->Term.self
#define Imap    __THIS__->__E__->__ED__->Imap.self
#define Input   __THIS__->__E__->__ED__->Input.self
#define Error   __THIS__->__E__->__ED__->Error.self
#define Rline   __THIS__->__E__->__ED__->Rline.self
#define Video   __THIS__->__E__->__ED__->Video.self
#define Cursor  __THIS__->__E__->__ED__->Cursor.self
#define Screen  __THIS__->__E__->__ED__->Screen.self
#define String  __THIS__->__E__->__ED__->String.self
#define Cstring __THIS__->__E__->__ED__->Cstring.self
#define Ustring __THIS__->__E__->__ED__->Ustring.self
#define Vstring __THIS__->__E__->__ED__->Vstring.self

#define Argparse ((Self (This) *) __THIS__->self)->argparse
#define Spell    ((Self (This) *) __THIS__->self)->spell
#define Proc     ((Self (This) *) __THIS__->self)->proc

mutable public void __alloc_error_handler__ (int, size_t, char *,
                                                 const char *, int);
private void sigwinch_handler (int sig);

DeclareType (argparse);
DeclareType (argparse_option);

typedef int argparse_callback (argparse_t *, const argparse_option_t *);
int argparse_help_cb (argparse_t *, const argparse_option_t *);

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

#if HAS_USER_EXTENSIONS
  private void __init_usr__ (ed_t *);
  private void __deinit_usr__ (void);
#endif

#if HAS_LOCAL_EXTENSIONS
  private void __init_local__ (ed_t *);
  private void __deinit_local__ (void);
#endif

#if HAS_PROGRAMMING_LANGUAGE
#include <stdbool.h>
#include <lai.h>
#include <led.h>

typedef l_t Thislself_t;
typedef l_table_t Thisltableself_t;
typedef l_table_get_t Thisltablegetself_t;
typedef l_module_t Thislmodulegetself_t;
typedef lang_t L_T;
#endif /* HAS_PROGRAMMING_LANGUAGE */

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

NewSubSelf (Thise, set,
  void
    (*at_init_cb) (Class (This) *, EdAtInit_cb),
    (*at_exit_cb) (Class (This) *, EAtExit_cb);

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

  Class (I) *(*iclass) (Class (This) *);
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

NewSelf (argparse,
  int
    (*init) (argparse_t *, argparse_option_t *, const char *const *, int),
    (*exec) (argparse_t *, int, const char **);
);

NewClass (argparse,
  Self (argparse) self;
);

NewSelf (This,
  SubSelf (This, e) e;

  Self (argparse) argparse;
  Self (spell) spell;
  Self (proc) proc;

  string_t *(*parse_command) (Class (This) *, char *);

#if HAS_PROGRAMMING_LANGUAGE
  SubSelf (This, l) l;
#endif
);

NewProp (This,
  char *name;

  Class (spell) spell;

#if HAS_PROGRAMMING_LANGUAGE
  Class (L) *__L__;
#endif
);

public Class (This) *__init_this__ (void);
public void __deinit_this__ (Class (This) **);
#endif
