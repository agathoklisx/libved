#ifndef LIBVED_H
#define LIBVED_H

#define MAX_FRAMES 3
#define DEFAULT_SHIFTWIDTH 2
#define DEFAULT_PROMPT_CHAR ':'
#define DEFAULT_ON_EMPTY_LINE_CHAR '~'

#ifndef RLINE_HISTORY_NUM_ENTRIES
#define RLINE_HISTORY_NUM_ENTRIES 20
#endif

#define RLINE_LAST_COMPONENT_NUM_ENTRIES 10

#ifndef UNDO_NUM_ENTRIES
#define UNDO_NUM_ENTRIES 40
#endif

#ifndef TABWIDTH
#define TABWIDTH 8
#endif

#ifndef CLEAR_BLANKLINES
#define CLEAR_BLANKLINES 1
#endif

#ifndef TAB_ON_INSERT_MODE_INDENTS
#define TAB_ON_INSERT_MODE_INDENTS 0
#endif

#ifndef C_TAB_ON_INSERT_MODE_INDENTS
#define C_TAB_ON_INSERT_MODE_INDENTS 1
#endif

#ifndef CARRIAGE_RETURN_ON_NORMAL_IS_LIKE_INSERT_MODE
#define CARRIAGE_RETURN_ON_NORMAL_IS_LIKE_INSERT_MODE 1
#endif

#ifndef SPACE_ON_NORMAL_IS_LIKE_INSERT_MODE
#define SPACE_ON_NORMAL_IS_LIKE_INSERT_MODE 1
#endif

#ifndef SMALL_E_ON_NORMAL_GOES_INSERT_MODE
#define SMALL_E_ON_NORMAL_GOES_INSERT_MODE 1
#endif

#ifndef BACKSPACE_ON_FIRST_IDX_REMOVE_TRAILING_SPACES
#define BACKSPACE_ON_FIRST_IDX_REMOVE_TRAILING_SPACES 1
#endif

#ifndef BACKSPACE_ON_NORMAL_IS_LIKE_INSERT_MODE
#define BACKSPACE_ON_NORMAL_IS_LIKE_INSERT_MODE 1
#endif

#ifndef READ_FROM_SHELL
#define READ_FROM_SHELL 1
#endif

#ifndef NUM_SYNTAXES
#define NUM_SYNTAXES 32
#endif

#ifndef MAX_BACKTRACK_LINES_FOR_ML_COMMENTS
#define MAX_BACKTRACK_LINES_FOR_ML_COMMENTS 24
#endif

#ifndef PATH_MAX
#define PATH_MAX 4096  /* bytes in a path name */
#endif

#ifndef NAME_MAX
#define NAME_MAX 255  /* bytes in a file name */
#endif

#define MAXLEN_PATH        PATH_MAX
#define MAXLEN_LINE        4096
#define MAXLEN_WORD        256
#define MAXLEN_ERR_MSG     256
#define MAXLEN_PAT         PATH_MAX
#define MAXLEN_MODE        16
#define MAXLEN_FTYPE_NAME  16
#define MAXLEN_WORD_ACTION 512
#define MAX_SCREEN_ROWS    256
#define MAXLEN_COM         32

#define OUTPUT_FD STDOUT_FILENO

#define Notword ".,?/+*-=~%<>[](){}\\'\";"
#define Notword_len 22
#define Notfname "|][\""
#define Notfname_len 4

#define IS_UTF8(c)      (((c) & 0xC0) == 0x80)
#define PATH_SEP        ':'
#define DIR_SEP         '/'
#define IS_DIR_SEP(c)   (c == DIR_SEP)
#define IS_DIR_ABS(p)   IS_DIR_SEP (p[0])
#define IS_DIGIT(c)     ('0' <= (c) && (c) <= '9')
#define IS_CNTRL(c)     ((c < 0x20 and c >= 0) || c == 0x7f)
#define IS_SPACE(c)     ((c) == ' ' || (c) == '\t' || (c) == '\r' || (c) == '\n')
#define IS_ALPHA(c)     (((c) >= 'a' && (c) <= 'z') || ((c) >= 'A' && (c) <= 'Z'))
#define IS_ALNUM(c)     (IS_ALPHA(c) || IS_DIGIT(c))
#define IS_HEX_DIGIT(c) (IS_DIGIT(c) || ((c) >= 'a' && (c) <= 'f') || ((c) >= 'A' && (c) <= 'F')))

#define IsAlsoAHex(c)    (((c) >= 'a' and (c) <= 'f') or ((c) >= 'A' and (c) <= 'F'))
#define IsAlsoANumber(c) ((c) is '.' or (c) is 'x' or IsAlsoAHex (c))

#define NO_GLOBAL 0
#define GLOBAL    1

#define NO_FORCE 0
#define FORCE    1

#define NO_INTERACTIVE 0
#define INTERACTIVE    1

#define NOT_AT_EOF     0
#define AT_EOF         1

#define NO_COUNT_SPECIAL 0
#define COUNT_SPECIAL    1

#define DONOT_OPEN_FILE_IFNOT_EXISTS 0
#define OPEN_FILE_IFNOT_EXISTS       1

#define DONOT_REOPEN_FILE_IF_LOADED 0
#define REOPEN_FILE_IF_LOADED       1

#define SHARED_ALLOCATION 0
#define NEW_ALLOCATION    1

#define DONOT_ABORT_ON_ESCAPE 0
#define ABORT_ON_ESCAPE       1

#define VERBOSE_OFF 0
#define VERBOSE_ON  1

#define DONOT_DRAW 0
#define DRAW       1

#define DONOT_APPEND 0
#define APPEND       1

#define DONOT_CLEAR  0
#define CLEAR        1

#define X_PRIMARY     0
#define X_CLIPBOARD   1

#define DEFAULT_ORDER  0
#define REVERSE_ORDER -1
#define NORMAL_ORDER DEFAULT_ORDER

#define LEFT_DIRECTION   0
#define RIGHT_DIRECTION -1

#define DONOT_ADJUST_COL 0
#define ADJUST_COL 1

#define AT_CURRENT_FRAME -1

#define NO_CB_FN NULL

#define NO_COMMAND 0
#define NO_OPTION 0

#define RLINE_HISTORY  0
#define SEARCH_HISTORY 1

#define STRCHOP_NOTOK NOTOK
#define STRCHOP_OK OK
#define STRCHOP_RETURN (OK + 1)

#define LINEWISE 1
#define CHARWISE 2

#define TO_LOWER 0
#define TO_UPPER 1

#define DELETE_LINE  1
#define REPLACE_LINE 2
#define INSERT_LINE  3

#define HL_STRINGS_NO 0
#define HL_STRINGS 1

#define HL_NUMBERS_NO 0
#define HL_NUMBERS 1

#define NOT       (0 << 0)
#define UNSET     NOT

#define MSG_SET_RESET       (1 << 0)
#define MSG_SET_APPEND      (1 << 1)
#define MSG_SET_OPEN        (1 << 2)
#define MSG_SET_CLOSE       (1 << 3)
#define MSG_SET_DRAW        (1 << 4)
#define MSG_SET_COLOR       (1 << 5)
#define MSG_SET_TO_MSG_BUF  (1 << 6)
#define MSG_SET_TO_MSG_LINE (1 << 7)

#define ED_INIT_ERROR   (1 << 0)

#define ED_SUSPENDED    (1 << 0)

#define FTYPE_DEFAULT 0

#define UNAMED          "[No Name]"

#define FIRST_FRAME 0
#define SECOND_FRAME 1
#define THIRD_FRAME 2

#define BACKSPACE_KEY   010
#define ESCAPE_KEY      033
#define LAST_ARG_KEY    037
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
#define COLOR_NORMAL      COLOR_FG_NORMAL
#define COLOR_ERROR       COLOR_RED
#define COLOR_WARNING     COLOR_MAGENTA
#define COLOR_PROMPT      COLOR_YELLOW
#define COLOR_BOX         COLOR_YELLOW
#define COLOR_MENU_BG     COLOR_RED
#define COLOR_MENU_SEL    COLOR_GREEN
#define COLOR_MENU_HEADER COLOR_CYAN
#define COLOR_DIVIDER     COLOR_MAGENTA

#define HL_NORMAL       COLOR_NORMAL
#define HL_VISUAL       COLOR_CYAN
#define HL_IDENTIFIER   COLOR_BLUE
#define HL_KEYWORD      COLOR_MAGENTA
#define HL_OPERATOR     COLOR_MAGENTA
#define HL_FUNCTION     COLOR_MAGENTA
#define HL_VARIABLE     COLOR_BLUE
#define HL_TYPE         COLOR_BLUE
#define HL_DEFINITION   COLOR_BLUE
#define HL_COMMENT      COLOR_YELLOW
#define HL_NUMBER       COLOR_MAGENTA
#define HL_STRING_DELIM COLOR_GREEN
#define HL_STRING       COLOR_YELLOW
#define HL_TRAILING_WS  COLOR_RED
#define HL_TAB          COLOR_CYAN
#define HL_ERROR        COLOR_RED
#define HL_QUOTE        COLOR_YELLOW
#define HL_QUOTE_1      COLOR_BLUE
#define HL_QUOTE_2      COLOR_CYAN

#define COLOR_CHARS  "IKCONSDFVTMEQ><"
// I: identifier, K: keyword, C: comment, O: operator, N: number, S: string
// D:_delimiter F: function   V: variable, T: type,  M: macro,
// E: error, Q: quote, >: qoute1, <: quote_2  

#define TERM_LAST_RIGHT_CORNER      "\033[999C\033[999B"
#define TERM_LAST_RIGHT_CORNER_LEN  12
#define TERM_FIRST_LEFT_CORNER      "\033[H"
#define TERM_FIRST_LEFT_CORNER_LEN  3
#define TERM_BOL                    "\033[E"
#define TERM_GET_PTR_POS            "\033[6n"
#define TERM_GET_PTR_POS_LEN        4
#define TERM_SCREEN_SAVE            "\033[?47h"
#define TERM_SCREEN_SAVE_LEN        6
#define TERM_SCREEN_RESTORE        "\033[?47l"
#define TERM_SCREEN_RESTORE_LEN     6
#define TERM_SCREEN_CLEAR           "\033[2J"
#define TERM_SCREEN_CLEAR_LEN       4
#define TERM_CURSOR_HIDE            "\033[?25l"
#define TERM_CURSOR_HIDE_LEN        6
#define TERM_CURSOR_SHOW            "\033[?25h"
#define TERM_CURSOR_SHOW_LEN        6
#define TERM_CURSOR_RESTORE         "\033[?25h"
#define TERM_CURSOR_RESTORE_LEN     6
#define TERM_LINE_CLR_EOL           "\033[2K"
#define TERM_LINE_CLR_EOL_LEN       4
#define TERM_BOLD                   "\033[1m"
#define TERM_BOLD_LEN               4
#define TERM_ITALIC                 "\033[3m"
#define TERM_ITALIC_LEN             4
#define TERM_UNDERLINE              "\033[4m"
#define TERM_UNDERLINE_LEN          4
#define TERM_INVERTED               "\033[7m"
#define TERM_INVERTED_LEN           4
#define TERM_REVERSE_SCREEN         "\033[?5h"
#define TERM_REVERSE_SCREEN_LEN     5
#define TERM_SCREEN_NORMAL          "\033[?5l"
#define TERM_SCREEN_NORMAL_LEN      5
#define TERM_SCROLL_REGION_FMT      "\033[%d;%dr"
#define TERM_COLOR_RESET            "\033[m"
#define TERM_COLOR_RESET_LEN        3
#define TERM_GOTO_PTR_POS_FMT       "\033[%d;%dH"
#define TERM_SET_COLOR_FMT          "\033[%dm"
#define TERM_SET_COLOR_FMT_LEN      5
#define TERM_MAKE_COLOR(clr) \
({char b__[8];snprintf (b__, 8, TERM_SET_COLOR_FMT, (clr));b__;})

#define NO_OFFSET  0

/* #define RESET_ERRNO errno = 0   mostly as an indicator and reminder */

#define ZERO_FLAGS 0

#define RE_IGNORE_CASE               (1 << 0)
#define RE_ENCLOSE_PAT_IN_PAREN      (1 << 1)
#define RE_PATTERN_IS_STRING_LITERAL (1 << 2)

#define RE_MAX_NUM_CAPTURES 9

/* These correspond to SLRE failure codes */
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

#define RL_ARG_FILENAME    (1 << 0)
#define RL_ARG_RANGE       (1 << 1)
#define RL_ARG_GLOBAL      (1 << 2)
#define RL_ARG_PATTERN     (1 << 3)
#define RL_ARG_SUB         (1 << 4)
#define RL_ARG_INTERACTIVE (1 << 5)
#define RL_ARG_APPEND      (1 << 6)
#define RL_ARG_BUFNAME     (1 << 7)
#define RL_ARG_VERBOSE     (1 << 8)
#define RL_ARG_ANYTYPE     (1 << 9)
#define RL_ARG_RECURSIVE   (1 << 10)

#define INDEX_ERROR            -1000
#define NULL_PTR_ERROR         -1001
#define INTEGEROVERFLOW_ERROR  -1002

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

DeclareType (bufiter);
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

typedef vchar_t u8char_t;
typedef line_t u8_t;

/* this might make things harder for the reader, because hides details, but if
 * something is gonna change in future, if it's not just a signle change it is
 * certainly (easier) searchable */

typedef utf8 (*InputGetch_cb) (term_t *);
typedef int  (*Rline_cb) (buf_t **, rline_t *, utf8);
typedef int  (*StrChop_cb) (vstr_t *, char *, void *);
typedef int  (*FileReadLines_cb) (vstr_t *, char *, size_t, int, void *);
typedef int  (*RlineAtBeg_cb) (rline_t **);
typedef int  (*RlineAtEnd_cb) (rline_t **);
typedef int  (*RlineTabCompletion_cb) (rline_t *);
typedef int  (*PopenRead_cb) (buf_t *, fp_t *);
typedef int  (*MenuProcessList_cb) (menu_t *);
typedef int  (*VisualLwMode_cb) (buf_t **, int, int, vstr_t *, utf8, char *);
typedef int  (*VisualCwMode_cb) (buf_t **, int, int, string_t *, utf8, char *);
typedef int  (*WordActions_cb) (buf_t **, int, int, bufiter_t *, char *, utf8, char *);
typedef int  (*BufNormalBeg_cb) (buf_t **, utf8, int *, int);
typedef int  (*BufNormalEnd_cb) (buf_t **, utf8, int *, int);
typedef int  (*BufNormalOng_cb) (buf_t **, int);
typedef int  (*ReCompile_cb) (regexp_t *);
typedef string_t *(Indent_cb) (buf_t *, row_t *);
typedef dim_t **(*WinDimCalc_cb) (win_t *, int, int, int, int);

#define NULL_REF NULL

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

/* do not change order */
NewType (syn,
  char
    *filetype,
    **filenames,
    **extensions,
    **shebangs,
    **keywords,
    *operators,
    *singleline_comment,
    *multiline_comment_start,
    *multiline_comment_end,
    *multiline_comment_continuation;

  int
    hl_strings,
    hl_numbers;

  char  *(*parse) (buf_t *, char *, int, int, row_t *);
  ftype_t *(*init) (buf_t *);
  int state;
  size_t *keywords_len;
  size_t *keywords_colors;
);

NewType (ftype,
  char
    name[MAXLEN_FTYPE_NAME],
    on_emptyline[2];

  int
    shiftwidth,
    tabwidth,
    autochdir,
    tab_indents,
    clear_blanklines,
    cr_on_normal_is_like_insert_mode,
    backspace_on_normal_is_like_insert_mode,
    backspace_on_first_idx_remove_trailing_spaces,
    space_on_normal_is_like_insert_mode,
    small_e_on_normal_goes_insert_mode,
    read_from_shell;

  string_t *(*autoindent) (buf_t *, row_t *);
      char *(*on_open_fname_under_cursor) (char *, size_t, size_t);
);

NewType (dirlist,
  vstr_t *list;
  char dir[PATH_MAX];
  void (*free) (dirlist_t *);
);

NewType (tmpfname,
  int fd;
  string_t *fname;
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

#define RE_MAXLEN_ERR_MSG MAXLEN_ERR_MSG

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
  char errmsg[RE_MAXLEN_ERR_MSG];
);

NewType (bufiter,
  int
    idx,
    col_idx;

  size_t num_lines;
  row_t *row;
  string_t *line;
);

NewType (buf_init_opts,
   win_t *win;

   char *fname;
   int
     flags,
     ftype,
     at_frame,
     at_linenr,
     at_column;
);

/* QUALIFIERS (quite ala S_Lang (in C they have to be declared though)) */
#define QUAL(__qual, ...) __qual##_QUAL (__VA_ARGS__)

#define BUF_INIT_OPTS Type (buf_init_opts)
#define BUF_INIT_QUAL(...) (BUF_INIT_OPTS) {                             \
  .at_frame = 0, .at_linenr = 1, .at_column = 1, .ftype = FTYPE_DEFAULT, \
  .flags = 0, .fname = UNAMED, __VA_ARGS__}

NewType (vchar,
  utf8 code;
  char buf[5];
  int
    len,
    width;

  vchar_t
    *next,
    *prev;
);

NewType (line,
  vchar_t *head;
  vchar_t *tail;
  vchar_t *current;
      int  cur_idx;
      int  num_items;
      int  len;
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
  int
    *(*dim) (term_t *, int *),
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

NewSubSelf (ustring, get,
  utf8 (*code_at) (char *, size_t, int, int *);
);

NewSelf (ustring,
  SubSelf (ustring, get) get;

  char *(*character) (utf8, char *, int *);

  int
    (*change_case) (char *, char *, size_t len, int),
    (*charlen) (uchar),
    (*is_lower) (utf8),
    (*is_upper) (utf8);

  utf8
    (*to_lower) (utf8),
    (*to_upper) (utf8);

  void (*free) (u8_t *);
  u8_t *(*new) (void);
  u8char_t  *(*encode) (u8_t *, char *, size_t, int, int, int);
);

NewClass (ustring,
  Self (ustring) self;
);

NewSubSelf (cstring, trim,
  char *(*end) (char *, char);
);

NewSubSelf (cstring, byte,
  size_t (*mv) (char *, size_t, size_t, size_t, size_t);
);

NewSelf (cstring,
  SubSelf (cstring, trim) trim;
  SubSelf (cstring, byte) byte;

  char
    *(*substr) (char *, size_t, char *, size_t, size_t),
    *(*extract_word_at) (char *, size_t, char *, size_t, char *, size_t, int, int *, int *),
    *(*itoa) (int, char *, int),
    *(*dup) (const char *, size_t),
    *(*byte_in_str) (const char *, int);

  int
    (*eq) (const char *, const char *),
    (*eq_n) (const char *, const char *, size_t),
    (*cmp_n) (const char *, const char *, size_t);

  size_t
    (*cp) (char *, size_t, const char *, size_t),
    (*cp_fmt) (char *, size_t, char *, ...);

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
    *(*new) (size_t),
    *(*reallocate) (string_t *, size_t),
    *(*new_with) (const char *),
    *(*new_with_len) (const char *, size_t),
    *(*new_with_fmt) (const char *, ...),
    *(*append_byte) (string_t *, char),
    *(*prepend_byte) (string_t *, char),
    *(*append) (string_t *, const char *),
    *(*append_fmt) (string_t *, const char *, ...),
    *(*append_with_len) (string_t *, const char *, size_t),
    *(*prepend) (string_t *, const char *),
    *(*prepend_fmt) (string_t *, const char *, ...),
    *(*insert_at) (string_t *, const char *, int),
    *(*insert_at_with_len) (string_t *, const char *, int, size_t),
    *(*replace_with) (string_t *, char *),
    *(*replace_with_len) (string_t *, const char *, size_t),
    *(*replace_with_fmt) (string_t *, const char *, ...),
    *(*replace_numbytes_at_with) (string_t *, int, int, const char *);

  int (*delete_numbytes_at) (string_t *, int, int);
);

NewClass (string,
  Self (string) self;
);

NewSubSelf (vstring, cur,
  void
    (*append_with) (vstr_t *, char *);
);

NewSubSelf (vstring, add,
  vstr_t *(*sort_and_uniq) (vstr_t *, char *bytes);
);

NewSelf (vstring,
  SubSelf (vstring, cur) cur;
  SubSelf (vstring, add) add;

  void
    (*free) (vstr_t *),
    (*clear) (vstr_t *),
    (*append_with_fmt) (vstr_t *, char *, ...);

  vstr_t *(*new) (void);
  string_t *(*join) (vstr_t *, char *sep);
);

NewClass (vstring,
  Self (vstring) self;
);

NewSubSelf (rline, set,
  void (*line) (rline_t *, char *, size_t);
);

NewSubSelf (rline, get,
  string_t
     *(*line) (rline_t *),
     *(*command) (rline_t *),
     *(*anytype_arg) (rline_t *, char *);

  arg_t *(*arg) (rline_t *, int);

  int (*range) (rline_t *, buf_t *, int *);

  vstr_t *(*arg_fnames) (rline_t *, int);
);

NewSubSelf (rline, arg,
  int (*exists) (rline_t *, char *);
);

NewSubSelf (rline, history,
  void (*push) (rline_t *);
);

NewSelf (rline,
  SubSelf (rline, get) get;
  SubSelf (rline, set) set;
  SubSelf (rline, arg) arg;
  SubSelf (rline, history) history;

  rline_t *(*new) (ed_t *);
  void (*free) (rline_t *);
  int (*exec) (buf_t **, rline_t *);
  rline_t *(*parse) (buf_t *, rline_t *);
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
    (*write) (ed_t *, char *),
    (*set) (ed_t *, int, int, char *, size_t),
    (*set_fmt) (ed_t *, int, int, char *, ...),
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

NewSubSelf (file, tmpfname,
  tmpfname_t *(*new) (char *, char *);
  void (*free) (tmpfname_t *);
);

NewSelf (file,
  SubSelf (file, tmpfname) tmpfname;

  int
    (*exists) (const char *),
    (*is_readable) (const char *),
    (*is_executable) (const char *),
    (*is_reg) (const char *),
    (*is_elf) (const char *);

  ssize_t
    (*write) (char *, char *, ssize_t),
    (*append) (char *, char *, ssize_t);

  vstr_t *(*readlines) (char *, vstr_t *, FileReadLines_cb, void *);
);

NewClass (file,
  Self (file) self;
);

NewSelf (path,
  char
    *(*basename) (char *),
    *(*extname) (char *),
    *(*dirname) (char *);

  int (*is_absolute) (char *);
);

NewClass (path,
  Self (path) self;
);

NewSelf (dir,
  dirlist_t *(*list) (char *, int);
  char *(*current) (void);
);

NewClass (dir,
  Self (dir) self;
);

NewSubSelf (buf, iter,
  void (*free) (buf_t *, bufiter_t *);

  bufiter_t
    *(*new)  (buf_t *, int),
    *(*next) (buf_t *, bufiter_t *);
);

NewSubSelf (bufget, row,
  row_t
    *(*current) (buf_t *),
    *(*at) (buf_t *, int idx);

  string_t *(*current_bytes) (buf_t *);

  int
    (*current_col_idx) (buf_t *),
    (*col_idx) (buf_t *, row_t *);
);

NewSubSelf (bufget, prop,
  int (*tabwidth) (buf_t *);
);

NewSubSelf (buf, get,
  SubSelf (bufget, row) row;
  SubSelf (bufget, prop) prop;

  char *(*fname) (buf_t *);
  size_t (*num_lines) (buf_t *);
  row_t *(*line_at) (buf_t *, int);

  int
    (*current_video_row) (buf_t *),
    (*current_video_col) (buf_t *);
);

NewSubSelf (bufset, as,
  void
    (*unamed) (buf_t *),
    (*non_existant) (buf_t *);
);

NewSubSelf (bufset, row,
  int (*idx) (buf_t *, int, int, int);
);

NewSubSelf (bufset, normal,
  void (*at_beg_cb) (buf_t *, BufNormalBeg_cb);
);

enum {
  NO_CALLBACK_FUNCTION = -4,
  RLINE_NO_COMMAND = -3,
  ERROR = -2,
  NOTHING_TODO = -1,
  DONE = 0,
  NEWCHAR,
  EXIT,
  WIN_EXIT,
  BUF_EXIT,
  BUF_QUIT,
};

NewSubSelf (vsys, stat,
  char *(*mode_to_string) (char *, mode_t);
);

NewSelf (vsys,
  SubSelf (vsys, stat) stat;
  string_t *(*which) (char *, char *);
);

NewClass (vsys,
  Self (vsys) self;
);

NewSelf (venv,
  string_t *(*get) (ed_t *, char *);
);

NewClass (venv,
  Self (venv) self;
);

NewSubSelf (buf, set,
  SubSelf (bufset, as) as;
  SubSelf (bufset, row) row;
  SubSelf (bufset, normal) normal;

  int (*fname) (buf_t *, char *);

  void
    (*modified) (buf_t *),
    (*video_first_row) (buf_t *, int),
    (*mode) (buf_t *, char *),
    (*show_statusline) (buf_t *, int),
    (*ftype) (buf_t *, int);
);

NewSubSelf (buf, syn,
  ftype_t *(*init) (buf_t *);
     char *(*parser) (buf_t *, char *, int, int, row_t *);
);

NewSubSelf (buf, ftype,
  ftype_t *(*init) (buf_t *, int, Indent_cb);
  string_t *(*autoindent) (buf_t *, row_t *);
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
    *(*append_with_len) (buf_t *, char *, size_t),
    *(*prepend_with) (buf_t *, char *),
    *(*replace_with) (buf_t *, char *);
);

NewSubSelf (buf, free,
  void
     (*row) (buf_t *, row_t *),
     (*rows) (buf_t *);
);

NewSubSelf (buf, row,
  row_t
    *(*new_with) (buf_t *, const char *),
    *(*new_with_len) (buf_t *, const char *, size_t);
);

NewSubSelf (buf, read,
  ssize_t  (*fname) (buf_t *);
  int (*from_fp) (buf_t *, fp_t *);
);

NewSubSelf (buf, action,
  action_t *(*new) (buf_t *);
  void
    (*free) (buf_t *, action_t *),
    (*set_with) (buf_t *, action_t *, int, int, char *, size_t),
    (*set_current) (buf_t *, action_t *, int),
    (*push) (buf_t *this, action_t *);
);

NewSubSelf (buf, normal,
  int
    (*up) (buf_t *, int, int, int),
    (*down) (buf_t *, int, int, int),
    (*bof) (buf_t *, int),
    (*eof) (buf_t *, int),
    (*page_down) (buf_t *, int),
    (*goto_linenr) (buf_t *, int, int);
);

NewSelf (buf,
  SubSelf (buf, cur) cur;
  SubSelf (buf, set) set;
  SubSelf (buf, get) get;
  SubSelf (buf, syn) syn;
  SubSelf (buf, ftype) ftype;
  SubSelf (buf, to) to;
  SubSelf (buf, free) free;
  SubSelf (buf, row) row;
  SubSelf (buf, read) read;
  SubSelf (buf, iter) iter;
  SubSelf (buf, action) action;
  SubSelf (buf, normal) normal;

  void
    (*draw) (buf_t *),
    (*flush) (buf_t *),
    (*draw_cur_row) (buf_t *),
    (*clear) (buf_t *);

  int
    (*write) (buf_t *, int),
    (*substitute) (buf_t *, char *, char *, int, int, int, int);

  ssize_t (*init_fname) (buf_t *, char *);

  row_t *(*append_with) (buf_t *, char *);
  string_t *(*input_box) (buf_t *, int, int, int, char *);
);

NewClass (buf,
  Self (buf) self;
);

NewSubSelf (win, adjust,
  void (*buf_dim) (win_t *);
);

NewSubSelf (win, set,
  void
    (*dim) (win_t *, dim_t *, int, int, int, int),
    (*num_frames) (win_t *, int),
    (*min_rows) (win_t *, int),
    (*has_dividers) (win_t *, int),
    (*video_dividers) (win_t *);

  buf_t *(*current_buf) (win_t*, int, int);
);

NewSubSelf (win, get,
  buf_t
    *(*current_buf) (win_t *),
    *(*buf_by_idx) (win_t *, int),
    *(*buf_by_name) (win_t *, const char *, int *);

  int
    (*num_cols) (win_t *),
    (*num_rows) (win_t *),
    (*num_buf) (win_t *),
    (*current_buf_idx) (win_t *);
);

NewSubSelf (win, pop,
  int (*current_buf) (win_t *);
);

NewSubSelf (win, buf,
  buf_t
    *(*init) (win_t *, int, int),
    *(*new) (win_t *, BUF_INIT_OPTS);
);

NewSubSelf (win, frame,
  buf_t
    *(*change) (win_t *, int, int);
);

NewSelf (win,
  SubSelf (win, set) set;
  SubSelf (win, get) get;
  SubSelf (win, pop) pop;
  SubSelf (win, adjust) adjust;
  SubSelf (win, buf) buf;
  SubSelf (win, frame) frame;

  void (*draw) (win_t *);

  int
    (*append_buf)    (win_t *, buf_t *),
    (*prepend_buf)   (win_t *, buf_t *),
    (*insert_buf_at) (win_t *, buf_t *, int);

  dim_t
     **(*dim_calc) (win_t *);
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
     (*on_normal_g_cb)  (ed_t *, BufNormalOng_cb),
     (*cw_mode_actions) (ed_t *, utf8 *, int, char *, VisualCwMode_cb),
     (*lw_mode_actions) (ed_t *, utf8 *, int, char *, VisualLwMode_cb),
     (*word_actions)    (ed_t *, utf8 *, int, char *, WordActions_cb),
     (*lang_map) (ed_t *, int[][26]);

  win_t *(*current_win) (ed_t *, int);
  dim_t *(*dim) (ed_t *, dim_t *, int, int, int, int);
);

NewSubSelf (ed, syn,
  void (*append) (ed_t *, syn_t);
  int  (*get_ftype_idx) (ed_t *, char *);
);

NewSubSelf (ed, reg,
  rg_t
     *(*set) (ed_t *, int, int, char *, int);
);

NewSubSelf (ed, append,
  int (*win) (ed_t *, win_t *);

  void
    (*message_fmt) (ed_t *, char *, ...),
    (*message) (ed_t *, char *),
    (*toscratch_fmt) (ed_t *, int, char *, ...),
    (*toscratch) (ed_t *, int, char *),
    (*command_arg) (ed_t *, char *, char *, size_t),
    (*rline_commands) (ed_t *, char **, int, int[], int[]),
    (*rline_command) (ed_t *, char *, int, int);
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
  win_t
    *(*new) (ed_t *, char *, int),
    *(*init) (ed_t *, char *, WinDimCalc_cb),
    *(*new_special) (ed_t *, char *, int);
  int (*change) (ed_t *, buf_t **, int, char *, int, int);
);

NewSubSelf (ed, menu,
   void (*free) (ed_t *, menu_t *);
   menu_t *(*new) (ed_t *, buf_t *, MenuProcessList_cb);
   char *(*create) (ed_t *, menu_t *);
);

NewSubSelf (ed, sh,
  int (*popen) (ed_t *, buf_t *, char *, int, int, PopenRead_cb);
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
  ed_t
    *(*init) (Class (ed) *),
    *(*new) (Class (ed) *, int);

  SubSelf (ed, set) set;
  SubSelf (ed, get) get;
  SubSelf (ed, syn) syn;
  SubSelf (ed, reg) reg;
  SubSelf (ed, append) append;
  SubSelf (ed, exec) exec;
  SubSelf (ed, readjust) readjust;
  SubSelf (ed, buf) buf;
  SubSelf (ed, win) win;
  SubSelf (ed, menu) menu;
  SubSelf (ed, sh) sh;
  SubSelf (ed, history) history;
  SubSelf (ed, draw) draw;

  void
    (*free) (ed_t *),
    (*free_reg) (ed_t *, rg_t *),
    (*suspend) (ed_t *),
    (*resume) (ed_t *);

  int
    (*scratch) (ed_t *, buf_t **, int),
    (*messages) (ed_t *, buf_t **, int),
    (*quit) (ed_t *, int),
    (*loop) (ed_t *, buf_t *),
    (*main) (ed_t *, buf_t *);

  utf8 (*question) (ed_t *, char *, utf8 *, int);

  dim_t
     **(*dim_calc) (ed_t *, int, int, int, int),
     **(*dims_init) (ed_t *, int);
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
  Class (vstring) Vstring;
  Class (ustring) Ustring;
  Class (string) String;
  Class (re) Re;
  Class (input) Input;
  Class (screen) Screen;
  Class (cursor) Cursor;
  Class (msg) Msg;
  Class (error) Error;
  Class (file) File;
  Class (path) Path;
  Class (dir) Dir;
  Class (rline) Rline;
  Class (vsys) Vsys;
  Class (venv) Venv;

  ed_t *head;
  ed_t *tail;
  ed_t *current;
   int  cur_idx;
   int  num_items;
);

public ed_T *__init_ed__ (void);
public void __deinit_ed__ (ed_T *);

public mutable size_t tostderr (char *);
public mutable size_t tostdout (char *);

#define CSTRING_OUT_OF_RANGE -1

 public mutable int byte_mv (char *str, size_t len, size_t from_idx, size_t to_idx,
     size_t nelem);
#endif /* LIBVED_H */
