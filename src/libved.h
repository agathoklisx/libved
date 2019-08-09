#ifndef LIBVED_H
#define LIBVED_H

#define OUTPUT_FD STDOUT_FILENO

#define MAX_FRAMES 3
#define RLINE_HISTORY_NUM_ENTRIES 20
#define UNDO_NUM_ENTRIES 20
#define DEFAULT_SHIFTWIDTH 2
#define DEFAULT_PROMPT_CHAR ':'
#define TMPDIR "/tmp"

/* this is not configurable as the code will not count the change in the calculations */
#define TABLENGTH 1

#define TAB_ON_INSERT_MODE_INDENTS 0
#define CARRIAGE_RETURN_ON_NORMAL_IS_LIKE_INSERT_MODE 1
#define SPACE_ON_NORMAL_IS_LIKE_INSERT_MODE 1
#define SMALL_E_ON_NORMAL_GOES_INSERT_MODE 1
#define BACKSPACE_ON_FIRST_IDX_REMOVE_TRAILING_SPACES 1
#define BACKSPACE_ON_NORMAL_IS_LIKE_INSERT_MODE 1
#define READ_FROM_SHELL 1

#ifndef PATH_MAX
#define PATH_MAX 4096  /* bytes in a path name */
#endif

#ifndef NAME_MAX
#define NAME_MAX 255  /* bytes in a file name */
#endif

#ifndef MAXLINE
#define MAXLINE 4096
#endif

#ifndef MAXWORD
#define MAXWORD 64
#endif

#define IS_UTF8(c)      (((c) & 0xC0) == 0x80)
#define PATH_SEP       ':'
#define DIR_SEP        '/'
#define IS_DIR_SEP(c)  (c == DIR_SEP)
#define IS_PATH_ABS(p)  IS_DIR_SEP (p[0])
#define IS_DIGIT(c)     ('0' <= (c) && (c) <= '9')
#define IS_CNTRL(c)     (c < 0x20 || c == 0x7f)
#define IS_SPACE(c)     ((c) == ' ' || (c) == '\t' || (c) == '\r' || (c) == '\n')
#define IS_ALPHA(c)     (((c) >= 'a' && (c) <= 'z') || ((c) >= 'A' && (c) <= 'Z'))
#define IS_ALNUM(c)     (IS_ALPHA(c) || IS_DIGIT(c))
#define IS_HEX_DIGIT(c) (IS_DIGIT(c) || ((c) >= 'a' && (c) <= 'f') || ((c) >= 'A' && (c) <= 'F')))

#define RLINE_HISTORY  0
#define SEARCH_HISTORY 1

#define STRCHOP_NOTOK NOTOK
#define STRCHOP_OK OK
#define STRCHOP_RETURN (OK + 1)

#define ED_INIT_ERROR   (1 << 0)

#define ED_SUSPENDED    (1 << 0)

enum {
  FTYPE_DEFAULT,
  FTYPE_C,
  FTYPE_END
 };

#define BACKSPACE_KEY   010
#define ESCAPE_KEY      033
#define ARROW_DOWN_KEY  0402
#define ARROW_UP_KEY    0403
#define ARROW_LEFT_KEY  0404
#define ARROW_RIGHT_KEY 0405
#define HOME_KEY        0406
#define FN_KEY(x)       (x + 0410)
#define DELETE_KEY      0512
#define INSERT_KEY      0513
#define PAGE_DOWN_KEY   0522
#define PAGE_UP_KEY     0523
#define END_KEY         0550
#define CTRL_(X)        (X - '@')
//#define ALT(x)          (0551 + (x - 'a'))

#define COLOR_RED         31
#define COLOR_GREEN       32
#define COLOR_YELLOW      33
#define COLOR_BLUE        34
#define COLOR_MAGENTA     35
#define COLOR_CYAN        36
#define COLOR_WHITE       37
#define COLOR_FG_NORMAL   39
#define COLOR_BG_NORMAL   49

#define COLOR_TOPLINE     COLOR_YELLOW
#define COLOR_STATUSLINE  COLOR_BLUE
#define COLOR_SUCCESS     COLOR_GREEN
#define COLOR_FAILURE     COLOR_RED
#define COLOR_NORMAL      COLOR_FG_NORMAL
#define COLOR_PROMPT      COLOR_YELLOW
#define COLOR_BOX         COLOR_YELLOW
#define COLOR_MENU_BG     COLOR_RED
#define COLOR_MENU_SEL    COLOR_GREEN
#define COLOR_MENU_HEADER COLOR_CYAN
#define COLOR_DIVIDER     COLOR_MAGENTA

#define HL_VISUAL       COLOR_CYAN
#define HL_IDENTIFIER   COLOR_BLUE
#define HL_NUMBER       COLOR_MAGENTA
#define HL_KEYWORD      COLOR_MAGENTA
#define HL_OPERATOR     COLOR_MAGENTA
#define HL_STR_DELIM    COLOR_YELLOW
#define HL_COMMENT      COLOR_YELLOW
#define HL_TRAILING_WS  COLOR_RED
#define HL_TAB          COLOR_CYAN
#define HL_ERROR        COLOR_RED

#define RE_IGNORE_CASE (1 << 0)
#define RE_ENCLOSE_PAT_IN_PAREN (1 << 1)
#define RE_PATTERN_IS_STRING_LITERAL (1 << 2)

#define RE_MAX_NUM_CAPTURES 9

/* These corespond to SLRE failure codes */
#define RE_NO_MATCH                          -1
#define RE_UNEXPECTED_QUANTIFIER_ERROR       -2
#define RE_UNBALANCED_BRACKETS_ERROR         -3
#define RE_INTERNAL_ERROR                    -4
#define RE_INVALID_CHARACTER_SET_ERROR       -5
#define RE_INVALID_METACHARACTER_ERROR       -6
#define RE_CAPS_ARRAY_TOO_SMALL_ERROR        -7
#define RE_TOO_MANY_BRANCHES_ERROR           -8
#define RE_TOO_MANY_BRACKETS_ERROR           -9
#define RE_SUBSTITUTION_STRING_PARSING_ERROR -10

#define RL_ERROR                             -20
#define RL_NO_COMMAND                        -21
#define RL_ARG_AWAITING_STRING_OPTION_ERROR  -22
#define RL_ARGUMENT_MISSING_ERROR            -23
#define RL_UNTERMINATED_QUOTED_STRING_ERROR  -24
#define RL_UNRECOGNIZED_OPTION               -25

#define RL_ARG_FILENAME (1 << 0)
#define RL_ARG_RANGE (1 << 1)
#define RL_ARG_GLOBAL (1 << 2)
#define RL_ARG_PATTERN (1 << 3)
#define RL_ARG_SUB (1 << 4)
#define RL_ARG_INTERACTIVE (1 << 5)
#define RL_ARG_APPEND (1 << 6)
#define RL_ARG_BUFNAME (1 << 7)
#define RL_ARG_VERBOSE (1 << 8)
#define RL_ARG_ANYTYPE (1 << 9)

#define INDEX_ERROR                          -1000
#define NULL_PTR_ERROR                       -1001
#define INTEGEROVERFLOW_ERROR                -1002

#define NO_GLOBAL 0
#define GLOBAL    1

#define NO_INTERACTIVE 0
#define INTERACTIVE    1

#define NL "\n"

typedef signed int utf8;
typedef unsigned int uint;
typedef unsigned char uchar;

#define public __attribute__((visibility ("default")))
#define private __attribute__((visibility ("hidden")))
#define mutable __attribute__((__weak__))
#define __fallthrough__  __attribute__ ((fallthrough))
//#define __unused__  __attribute__ ((unused))

#define bytelen strlen
#define is    ==
#define isnot !=
#define and   &&
#define or    ||
#define ifnot(expr) if (0 == (expr))
#define loop(num_) for (int $i = 0; $i < (num_); $i++)
#define forever for (;;)
#define OK     0
#define NOTOK -1

typedef void (*AllocErrorHandlerF) (int, size_t, char *, const char *, int);

AllocErrorHandlerF AllocErrorHandler;

#define __REALLOC__ realloc
#define __CALLOC__  calloc

/* reallocarray:
 * $OpenBSD: reallocarray.c,v 1.1 2014/05/08 21:43:49 deraadt Exp $
 * Copyright (c) 2008 Otto Moerbeek <otto@drijf.net>
 */

#define MUL_NO_OVERFLOW ((size_t) 1 << (sizeof (size_t) * 4))
#define MEM_IS_INT_OVERFLOW(nmemb, ssize)                             \
 (((nmemb) >= MUL_NO_OVERFLOW || (ssize) >= MUL_NO_OVERFLOW) &&       \
  (nmemb) > 0 && SIZE_MAX / (nmemb) < (ssize))

#define Alloc(size) ({                                                \
  void *ptr__ = NULL;                                                 \
  if (MEM_IS_INT_OVERFLOW (1, (size))) {                              \
    errno = INTEGEROVERFLOW_ERROR;                                    \
    AllocErrorHandler (errno, (size),  __FILE__, __func__, __LINE__); \
  } else {                                                            \
    if (NULL == (ptr__ = __CALLOC__ (1, (size))))                     \
      AllocErrorHandler (errno, (size), __FILE__, __func__, __LINE__);\
    }                                                                 \
  ptr__;                                                              \
  })

#define Realloc(ptr, size) ({                                         \
  void *ptr__ = NULL;                                                 \
  if (MEM_IS_INT_OVERFLOW (1, (size))) {                              \
    errno = INTEGEROVERFLOW_ERROR;                                    \
    AllocErrorHandler (errno, (size),  __FILE__, __func__, __LINE__); \
  } else {                                                            \
    if (NULL == (ptr__ = __REALLOC__ ((ptr), (size))))                \
      AllocErrorHandler (errno, (size), __FILE__, __func__, __LINE__);\
    }                                                                 \
  ptr__;                                                              \
  })

#define ___Me__  Me
#define __this__ this
#define __prop__ prop
#define __self__ self
#define __root__ root
#define __parent__ parent
#define __current__ current
#define Type(__type__) __type__ ## _t
#define DeclareType(__t__) typedef struct Type(__t__) Type(__t__)
#define AllocType(__t__) Alloc (sizeof (Type(__t__)))
#define NewType(__t__, ...) DeclareType(__t__); struct Type(__t__) {__VA_ARGS__}
#define Prop(__prop__) __prop__ ## prop_t
#define DeclareProp(__p__) typedef struct Prop(__p__) Prop(__p__)
#define AllocProp(__p__) Alloc (sizeof (Prop(__p__)))
#define NewProp(__p__, ...) DeclareProp(__p__); struct Prop(__p__) {__VA_ARGS__}
#define $myprop __this__->__prop__
#define $my(__p__) __this__->__prop__->__p__
#define $self(__f__) $myprop->___Me__->self.__f__
#define self(__f__, ...) $self(__f__)(__this__, ##__VA_ARGS__)
#define My(__C__) $my(__C__)->self
//#define $from(__v__) __v__->__prop__
#define $from(__v__, __p__) __v__->__prop__->__p__
#define $mycur(__v__) __this__->__current__->__v__
#define $myparents(__p__) __this__->__prop__->__parent__->__prop__->__p__
#define $myroots(__p__) __this__->__prop__->__root__->__prop__->__p__
#define Self(__name__) __name__ ## self_t
#define SubSelf(__super__, __name__) __super__ ## __name__ ## self_t
#define DeclareSelf(__name__) typedef struct Self (__name__) Self (__name__ )
#define NewSelf(__name__, ...)  DeclareSelf (__name__); \
  struct __name__ ## self_t {__VA_ARGS__}
#define NewSubSelf(__super__, __name__, ...) DeclareSelf (__super__ ## __name__); \
  struct __super__ ## __name__ ## self_t {__VA_ARGS__}
#define SelfInit(__name__, ...) (Self (__name__)) {__VA_ARGS__}
#define SubSelfInit(__super__, __name__, ...) SelfInit (__super__ ## __name__, __VA_ARGS__)
#define Class(__Type__) __Type__ ## _T
#define DeclareClass(__T__) typedef struct Class(__T__) Class(__T__)
#define NewClass(__T__, ...) DeclareClass(__T__); struct Class(__T__) {__VA_ARGS__}
#define ClassInit(__T__, ...) (Class (__T__)) {__VA_ARGS__}

DeclareType (term);
DeclareType (string);
DeclareType (vstring);
DeclareType (vstr);
DeclareType (vchar);
DeclareType (line);
DeclareType (regexp);
DeclareType (vrow);
DeclareType (video);
DeclareType (dirlist);
DeclareType (arg);
DeclareType (rline);
DeclareType (act);
DeclareType (action);
DeclareType (reg);
DeclareType (rg);
DeclareType (undo);
DeclareType (hist);
DeclareType (h_rlineitem);
DeclareType (histitem);
DeclareType (ftype);
DeclareType (menu);
DeclareType (sch);
DeclareType (row);
DeclareType (dim);
DeclareType (syn);
DeclareType (fp);

DeclareType (buf);
DeclareProp (buf);

DeclareType (win);
DeclareProp (win);

DeclareType (ed);
DeclareProp (ed);
DeclareClass (ed);

DeclareSelf (input);
DeclareClass (video);
DeclareClass (string);

/* this might make things harder for the reader, because hides details, but if
 * something is gonna change in future, if it's not just a signle change it is
 * certainly (easier) searchable */

typedef utf8 (*InputGetch_cb) (term_t *);
typedef int  (*Rline_cb) (buf_t **, rline_t *, utf8);
typedef int  (*StrChop_cb) (vstr_t *, char *, void *);
typedef int  (*RlineAtBeg_cb) (rline_t **);
typedef int  (*RlineAtEnd_cb) (rline_t **);
typedef int  (*RlineTabCompletion_cb) (rline_t *);
typedef int  (*PopenRead_cb) (buf_t *, fp_t *);

NewType (string,
  size_t  num_bytes;
  size_t  mem_size;
    char *bytes;
);

NewType (vstring,
   string_t *data;
  vstring_t *next;
  vstring_t *prev;
);

NewType (vstr,
  vstring_t *head;
  vstring_t *tail;
  vstring_t *current;
        int  cur_idx;
        int  num_items;
);

NewType (dirlist,
  vstr_t *list;
  char dir[PATH_MAX];
  void (*free) (dirlist_t *);
);

NewType (fp,
  FILE   *fp;
  size_t  num_bytes;

  int
    fd,
    error;
);

NewType (capture,
  const char *ptr;
  int len;
);

NewType (regexp,
  string_t *pat;
  capture_t **cap;
  char *buf;
  size_t buflen;
  int retval;
  int flags;
  int num_caps;
  int total_caps;
  int match_idx;
  int match_len;
  char *match_ptr;
  string_t *match;
  char errmsg[256];
);

NewSubSelf (video, draw,
  void
     (*row_at) (video_t *, int),
     (*all) (video_t *);

  int
    (*bytes) (video_t *, char *, size_t);
);

NewSelf (video,
  SubSelf (video, draw) Draw;
  video_t
    *(*new) (int, int, int, int, int);

  void
     (*free) (video_t *),
     (*flush) (video_t *, string_t *),
     (*set_with) (video_t *, int, char *);
);

NewClass (video,
  Self (video) self;
);

NewSubSelf (term, screen,
  void
    (*restore) (term_t *),
    (*save)  (term_t *),
    (*clear) (term_t *),
    (*clear_eol) (term_t *);
);

NewClass (screen,
  SubSelf (term, screen) self;
);

NewSubSelf (term, cursor,
  void
    (*set_pos) (term_t *, int, int),
    (*hide) (term_t *),
    (*show) (term_t *),
    (*restore) (term_t *);

  int (*get_pos) (term_t *, int *, int *);
);

NewClass (cursor,
  SubSelf (term, cursor) self;
);

NewSubSelf (term, input,
  utf8 (*get) (term_t *);
);

NewSubSelf (term, get,
  int *(*dim) (term_t *, int *),
    (*lines) (term_t *),
    (*columns) (term_t *);
);

NewClass (input,
  SubSelf (term, input) self;
);

NewSelf (term,
  SubSelf (term, screen) Screen;
  SubSelf (term, cursor) Cursor;
  SubSelf (term, input) Input;
  SubSelf (term, get) get;

  term_t *(*new) (void);

  void
    (*free) (term_t *),
    (*restore) (term_t *),
    (*set_name) (term_t *),
    (*init_size) (term_t *, int *, int *);

  int
    (*set) (term_t *),
    (*reset) (term_t *),
    (*set_mode) (term_t *, char);
);

NewClass (term,
  Self (term) self;
);

NewSelf (cstring,
  char
     *(*itoa) (int, char *, int),
     *(*dup) (const char *, size_t);
  int
    (*eq) (const char *, const char *),
    (*cmp_n) (const char *, const char *, size_t);

  vstr_t *(*chop) (char *, char, vstr_t *, StrChop_cb, void *);
);

NewClass (cstring,
  Self (cstring) self;
);

NewSelf (string,
  void
     (*free) (string_t *),
     (*clear) (string_t *),
     (*clear_at) (string_t *, int);

  string_t
    *(*new) (void),
    *(*new_with) (const char *),
    *(*new_with_fmt) (const char *, ...),
    *(*append_byte) (string_t *, char),
    *(*prepend_byte) (string_t *, char),
    *(*append)    (string_t *, const char *),
    *(*prepend)   (string_t *, const char *);
  string_t
    *(*append_fmt) (string_t *, const char *, ...),
    *(*prepend_fmt) (string_t *, const char *, ...),
    *(*insert_at) (string_t *, const char *, int),
    *(*replace_with) (string_t *, char *),
    *(*replace_with_fmt) (string_t *, const char *, ...),
    *(*replace_numbytes_at_with) (string_t *, int, int, const char *);

    int (*delete_numbytes_at) (string_t *, int, int);
);

NewClass (string,
  Self (string) self;
);

NewSelf (vstring,
  void (*free) (vstr_t *);
  vstr_t *(*new) (void);
  string_t *(*join) (vstr_t *, char *sep);
);

NewClass (vstring,
  Self (vstring) self;
);

NewSubSelf (rline, get,
  string_t
     *(*line) (rline_t *),
     *(*command) (rline_t *),
     *(*anytype_arg) (rline_t *, char *);

  vstr_t   *(*arg_fnames) (rline_t *, int);
);

NewSubSelf (rline, arg,
  int (*exists) (rline_t *, char *);
);

NewSelf (rline,
  SubSelf (rline, get) get;
  SubSelf (rline, arg) arg;
);

NewClass (rline,
  Self (rline) self;
);

NewSelf (re,
  regexp_t *(*new) (char *, int, int, int (*) (regexp_t *));
      void  (*free) (regexp_t *);
      void  (*free_captures) (regexp_t *);
      void  (*reset_captures) (regexp_t *);
      void  (*allocate_captures) (regexp_t *, int);
      void  (*free_pat) (regexp_t *);

       int  (*exec) (regexp_t *, char *, size_t);
       int  (*compile) (regexp_t *);
  string_t *(*parse_substitute) (regexp_t *, char *, char *);
  string_t *(*get_match) (regexp_t *, int);
);

NewClass (re,
  Self (re) self;
);

NewSelf (msg,
  void
    (*send) (ed_t *, int, char *),
    (*send_fmt) (ed_t *, int, char *, ...),
    (*error) (ed_t *, char *, ...);
  char *(*fmt) (ed_t *, int, ...);
);

NewClass (msg,
  Self (msg) self;
);

NewSelf (error,
  char *(*string) (ed_t *, int);
);

NewClass (error,
  Self (error) self;
);

NewSelf (file,
  int
    (*exists) (const char *),
    (*is_executable) (const char *),
    (*is_reg) (const char *),
    (*is_elf) (const char *);

  vstr_t *(*readlines) (char *, vstr_t *, StrChop_cb, void *);
);

NewClass (file,
  Self (file) self;
);

NewSelf (dir,
  dirlist_t *(*list) (char *, int);
);

NewClass (dir,
  Self (dir) self;
);

NewSubSelf (buf, get,
  char *(*fname) (buf_t *);
  size_t (*num_lines) (buf_t *);
);

NewSubSelf (bufset, as,
  void
    (*unamed) (buf_t *),
    (*non_existant) (buf_t *);
);

NewSubSelf (buf, set,
  int  (*fname) (buf_t *, char *);
  void
    (*video_first_row) (buf_t *, int),
    (*mode) (buf_t *, char *);

  SubSelf (bufset, as) as;

  ftype_t *(*ftype) (buf_t *, int);
);

NewSubSelf (buf, to,
  void (*video) (buf_t *);
);

NewSubSelf (buf, cur,
  int
    (*set) (buf_t *, int);

  row_t
    *(*pop) (buf_t *),
    *(*delete) (buf_t *),
    *(*prepend) (buf_t *, row_t *),
    *(*append) (buf_t *, row_t *),
    *(*append_with) (buf_t *, char *),
    *(*prepend_with) (buf_t *, char *);
);

NewSubSelf (buf, free,
  void
     (*row) (buf_t *, row_t *),
     (*rows) (buf_t *);
);

NewSubSelf (buf, row,
  row_t *(*new_with) (buf_t *, const char *);
);

NewSubSelf (buf, read,
  ssize_t  (*fname) (buf_t *);
  int (*from_fp) (buf_t *, fp_t *);
);

NewSelf (buf,
  SubSelf (buf, cur) cur;
  SubSelf (buf, set) set;
  SubSelf (buf, get) get;
  SubSelf (buf, to) to;
  SubSelf (buf, free) free;
  SubSelf (buf, row) row;
  SubSelf (buf, read) read;

  void
    (*draw) (buf_t *),
    (*flush) (buf_t *),
    (*draw_cur_row) (buf_t *),
    (*clear) (buf_t *);

  int
    (*write) (buf_t *, int),
    (*substitute) (buf_t *, char *, char *, int, int, int, int);

  row_t *(*append_with) (buf_t *, char *);
);

NewClass (buf,
  Self (buf) self;
);

NewSubSelf (win, adjust,
  void (*buf_dim) (win_t *);
);

NewSubSelf (win, set,
  buf_t *(*current_buf) (win_t*, int);
   void  (*video_dividers) (win_t *);
);

NewSubSelf (win, get,
  buf_t
    *(*current_buf) (win_t *),
    *(*buf_by_idx) (win_t *, int),
    *(*buf_by_name) (win_t *, const char *, int *);

  int
    (*num_buf) (win_t *),
    (*current_buf_idx) (win_t *);
);

NewSubSelf (win, pop,
  int (*current_buf) (win_t *);
);

NewSelf (win,
  SubSelf (win, set) set;
  SubSelf (win, get) get;
  SubSelf (win, pop) pop;
  SubSelf (win, adjust) adjust;

  void (*draw) (win_t *);

  buf_t *(*buf_new) (win_t *, char *, int, int);

  int
    (*append_buf)    (win_t *, buf_t *),
    (*prepend_buf)   (win_t *, buf_t *),
    (*insert_buf_at) (win_t *, buf_t *, int);
);

NewClass (win,
  Self (win) self;
);

NewSubSelf (ed, get,
  buf_t
    *(*bufname) (ed_t *, char *),
    *(*current_buf) (ed_t *),
    *(*scratch_buf) (ed_t *);

  int
    (*num_rline_commands) (ed_t *),
    (*num_win) (ed_t *, int),
    (*current_win_idx) (ed_t *),
    (*state) (ed_t *);

  win_t
    *(*current_win) (ed_t *),
    *(*win_head) (ed_t *),
    *(*win_next) (ed_t *, win_t *),
    *(*win_by_idx) (ed_t *, int),
    *(*win_by_name) (ed_t *, char *, int *);

  term_t *(*term) (ed_t *);

  ed_t *(*next) (ed_t *);
  ed_t *(*prev) (ed_t *);
);

NewSubSelf (ed, set,
   void
     (*screen_size) (ed_t *),
     (*topline) (buf_t *),
     (*rline_cb) (ed_t *, Rline_cb),
     (*cw_mode_actions) (ed_t *, utf8 *, int, char *, int (*) (buf_t *, string_t *, utf8)),
     (*lw_mode_actions) (ed_t *, utf8 *, int, char *, int (*) (buf_t *, vstr_t *, utf8)),
     (*word_actions_cb) (ed_t *, int (*) (buf_t **, char *, utf8)),
     (*word_actions)    (ed_t *, utf8 *, int, char *, int (*) (buf_t **, char *, utf8));

  win_t *(*current_win) (ed_t *, int);
  dim_t *(*dim) (ed_t *, int, int, int, int);
);

NewSubSelf (ed, append,
   int (*win) (ed_t *, win_t *);
  void
    (*message) (ed_t *, char *),
    (*command_arg) (ed_t *, char *, char *),
    (*rline_commands) (ed_t *, char **, int, int[], int[]);

);

NewSubSelf (ed, readjust,
  void (*win_size) (ed_t *, win_t *);
);

NewSubSelf (ed, exec,
  int (*cmd) (buf_t **, utf8, int *, int);
);

NewSubSelf (ed, buf,
  int (*change) (ed_t  *, buf_t **, char *, char *);
  buf_t *(*get) (ed_t  *, char *, char *);
);

NewSubSelf (ed, win,
  win_t *(*new) (ed_t *, char *, int);
  win_t *(*new_special) (ed_t *, char *, int);
  int (*change) (ed_t  *, buf_t **, int, char *, int);
);

NewSubSelf (ed, sh,
  int (*popen) (ed_t *, buf_t *, char *, int, int, PopenRead_cb);
);

NewSubSelf (ed, vsys,
  string_t *(*which) (char *, char *);
);

NewSubSelf (ed, venv,
  string_t *(*get) (ed_t *, char *);
);

NewSubSelf (ed, history,
  void
     (*add) (ed_t *, vstr_t *, int),
     (*read) (ed_t *, char *),
     (*write) (ed_t *, char *);
);

NewSubSelf (ed, draw,
  void (*current_win) (ed_t *);
);

NewSelf (ed,
  ed_t *(*new) (Class (ed) *, int);

  void
    (*free) (ed_t *),
    (*free_reg) (ed_t *, rg_t *),
    (*suspend) (ed_t *),
    (*resume) (ed_t *);

  SubSelf (ed, set) set;
  SubSelf (ed, get) get;
  SubSelf (ed, append) append;
  SubSelf (ed, exec) exec;
  SubSelf (ed, readjust) readjust;
  SubSelf (ed, buf) buf;
  SubSelf (ed, win) win;
  SubSelf (ed, sh) sh;
  SubSelf (ed, vsys) vsys;
  SubSelf (ed, venv) venv;
  SubSelf (ed, history) history;
  SubSelf (ed, draw) draw;

  int
    (*scratch) (ed_t *, buf_t **, int),
    (*quit) (ed_t *, int),
    (*loop) (ed_t *, buf_t *),
    (*main) (ed_t *, buf_t *);

);

NewClass (ed,
  char name[8];
   int error_state;
   int state;

  Prop (ed) *prop;
  Self (ed)  self;
  Class (buf) Buf;
  Class (win) Win;
  Class (term) Term;
  Class (video) Video;
  Class (cstring) Cstring;
  Class (string) String;
  Class (re) Re;
  Class (input) Input;
  Class (screen) Screen;
  Class (cursor) Cursor;
  Class (msg) Msg;
  Class (error) Error;
  Class (file) File;
  Class (dir) Dir;
  Class (rline) Rline;
  Class (vstring) Vstring;

  ed_t *head;
  ed_t *tail;
  ed_t *current;
   int  cur_idx;
   int  num_items;
);

public ed_T *__init_ved__ (void);
public void __deinit_ved__ (ed_T *);

public mutable size_t tostderr (char *);
public mutable size_t tostdout (char *);
public void toscratch (ed_t *, char *, int);

#endif /* LIBVED_H */
