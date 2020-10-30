#ifndef LIBVED_H
#define LIBVED_H

#define MYNAME "veda"
#define ED_INSTANCES 252

#define MIN_LINES 8
#define MIN_COLS  2

#define MAX_FRAMES 3

#define DEFAULT_SHIFTWIDTH 0
#define DEFAULT_PROMPT_CHAR ':'
#define DEFAULT_ON_EMPTY_LINE_STRING "~"

#define DEFAULT_CLOCK CLOCK_REALTIME

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

#ifndef AUTOCHDIR
#define AUTOCHDIR 1
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

#ifndef C_DEFAULT_SHIFTWIDTH
#define C_DEFAULT_SHIFTWIDTH 2
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

#ifndef BACKSPACE_ON_NORMAL_GOES_UP
#define BACKSPACE_ON_NORMAL_GOES_UP 1
#else
#if (BACKSPACE_ON_NORMAL_GOES_UP == 1)
#undef  BACKSPACE_ON_FIRST_IDX_REMOVE_TRAILING_SPACES
#define BACKSPACE_ON_FIRST_IDX_REMOVE_TRAILING_SPACES 0
#endif
#endif

#ifndef BACKSPACE_ON_INSERT_GOES_UP_AND_JOIN
#define BACKSPACE_ON_INSERT_GOES_UP_AND_JOIN 1
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
#define MAXLEN_NAME        16
#define MAXLEN_MODE        16
#define MAXLEN_FTYPE_NAME  16
#define MAXLEN_WORD_ACTION 512
#define MAX_SCREEN_ROWS    256
#define MAXLEN_COM         512
#define DIRWALK_MAX_DEPTH 1024

#define MAXLEN_ED_NAME MAXLEN_NAME
#define OUTPUT_FD STDOUT_FILENO

#define Notword ".,?/+*-=~%<>[](){}\\'\";"
#define Notword_len 22
#define Notfname "|][\""
#define Notfname_len 4

#define IS_UTF8(c)      (((c) & 0xC0) == 0x80)
#define PATH_SEP        ':'
#define DIR_SEP         '/'
#define DIR_SEP_STR     "/"
#define IS_DIR_SEP(c)   (c == DIR_SEP)
#define IS_DIR_ABS(p)   IS_DIR_SEP (p[0])
#define IS_DIGIT(c)     ('0' <= (c) && (c) <= '9')
#define IS_CNTRL(c)     ((c < 0x20 && c >= 0) || c == 0x7f)
#define IS_SPACE(c)     ((c) == ' ' || (c) == '\t' || (c) == '\r' || (c) == '\n')
#define IS_ALPHA(c)     (((c) >= 'a' && (c) <= 'z') || ((c) >= 'A' && (c) <= 'Z'))
#define IS_ALNUM(c)     (IS_ALPHA(c) || IS_DIGIT(c))
#define IS_HEX_DIGIT(c_) (IS_DIGIT(c_) || (c_ >= 'a' && c_ <= 'f') || (c_ >= 'A' && c_ <= 'F')))

#define IsAlsoAHex(c)    (((c) >= 'a' && (c) <= 'f') || ((c) >= 'A' && (c) <= 'F'))
#define IsAlsoANumber(c) ((c) == '.' || (c) == 'x' || IsAlsoAHex (c))

#define ARRLEN(arr) (sizeof(arr) / sizeof((arr)[0]))

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

#define DONOR_REOPEN 0
#define REOPEN       1

#define X_PRIMARY     0
#define X_CLIPBOARD   1

#define DONOT_REDIRECT 0
#define REDIRECT       1

#define NULL_CALLBACK_FN  NULL

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

#define NO_NL  0
#define ADD_NL 1

#define NO  0
#define YES 1

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

#define FILE_IS_REGULAR     (1 << 0)
#define FILE_IS_RDONLY      (1 << 1)
#define FILE_EXISTS         (1 << 2)
#define FILE_IS_READABLE    (1 << 3)
#define FILE_IS_WRITABLE    (1 << 4)
#define BUF_IS_MODIFIED     (1 << 5)
#define BUF_IS_VISIBLE      (1 << 6)
#define BUF_IS_RDONLY       (1 << 7)
#define BUF_IS_PAGER        (1 << 8)
#define BUF_IS_SPECIAL      (1 << 9)
#define BUF_FORCE_REOPEN    (1 << 10)
#define PTR_IS_AT_EOL       (1 << 12)
#define BUF_LW_RESELECT     (1 << 13)

#define ED_SUSPENDED        (1 << 0)
#define ED_EXIT             (1 << 1)
#define ED_EXIT_ALL         (1 << 2)
#define ED_EXIT_ALL_FORCE   (1 << 3)
#define ED_PAUSE            (1 << 4)
#define ED_NEW              (1 << 5)
#define ED_NEXT             (1 << 6)
#define ED_PREV             (1 << 7)
#define ED_PREV_FOCUSED     (1 << 8)

#define E_SUSPENDED                (1 << 0)
#define E_EXIT                     (1 << 1)
#define E_DONOT_RESTORE_TERM_STATE (1 << 2)
#define E_DONOT_CHANGE_FOCUS       (1 << 3)
#define E_PAUSE                    (1 << 4)

#define IDX_OUT_OF_BOUNDS_ERROR_STATE  (1 << 0)

#define LAST_INSTANCE_ERROR_STATE      (1 << 0)
#define ARG_IDX_IS_CUR_IDX_ERROR_STATE (1 << 1)

#define FTYPE_DEFAULT 0

#define UNNAMED         "[No Name]"
#define BACKUP_SUFFIX   "~"

#define FIRST_FRAME  0
#define SECOND_FRAME 1
#define THIRD_FRAME  2

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

#ifndef CTRL
#define CTRL(X) (X & 037)
#endif

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

#define COLOR_SU          COLOR_RED
#define COLOR_BOX         COLOR_YELLOW
#define COLOR_MSG         COLOR_YELLOW
#define COLOR_ERROR       COLOR_RED
#define COLOR_PROMPT      COLOR_YELLOW
#define COLOR_NORMAL      COLOR_FG_NORMAL
#define COLOR_MENU_BG     COLOR_RED
#define COLOR_TOPLINE     COLOR_YELLOW
#define COLOR_DIVIDER     COLOR_MAGENTA
#define COLOR_WARNING     COLOR_MAGENTA
#define COLOR_SUCCESS     COLOR_GREEN
#define COLOR_TOPLINE     COLOR_YELLOW
#define COLOR_WARNING     COLOR_MAGENTA
#define COLOR_MENU_SEL    COLOR_GREEN
#define COLOR_STATUSLINE  COLOR_BLUE
#define COLOR_MENU_HEADER COLOR_CYAN

#define HL_NORMAL         COLOR_NORMAL
#define HL_VISUAL         COLOR_CYAN
#define HL_IDENTIFIER     COLOR_BLUE
#define HL_KEYWORD        COLOR_MAGENTA
#define HL_OPERATOR       COLOR_MAGENTA
#define HL_FUNCTION       COLOR_MAGENTA
#define HL_VARIABLE       COLOR_BLUE
#define HL_TYPE           COLOR_BLUE
#define HL_DEFINITION     COLOR_BLUE
#define HL_COMMENT        COLOR_YELLOW
#define HL_NUMBER         COLOR_MAGENTA
#define HL_STRING_DELIM   COLOR_GREEN
#define HL_STRING         COLOR_YELLOW
#define HL_TRAILING_WS    COLOR_RED
#define HL_TAB            COLOR_CYAN
#define HL_ERROR          COLOR_RED
#define HL_QUOTE          COLOR_YELLOW
#define HL_QUOTE_1        COLOR_BLUE
#define HL_QUOTE_2        COLOR_CYAN

#define COLOR_CHARS  "IKCONSDFVTMEQ><"
// I: identifier, K: keyword, C: comment, O: operator, N: number, S: string
// D:_delimiter F: function   V: variable, T: type,  M: macro,
// E: error, Q: quote, >: qoute1, <: quote_2  

#define TERM_LAST_RIGHT_CORNER      "\033[999C\033[999B"
#define TERM_LAST_RIGHT_CORNER_LEN  12
#define TERM_FIRST_LEFT_CORNER      "\033[H"
#define TERM_FIRST_LEFT_CORNER_LEN  3
#define TERM_NEXT_BOL              "\033E"
#define TERM_NEXT_BOL_LEN           2
#define TERM_BOL                    "\033[E"
#define TERM_BOL_LEN                3
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
#define TERM_CURSOR_SAVE            "\0337"
#define TERM_CURSOR_SAVE_LEN        2
#define TERM_CURSOR_RESTORE         "\0338"
#define TERM_CURSOR_RESTORE_LEN     2
#define TERM_LINE_CLR_EOL           "\033[2K"
#define TERM_LINE_CLR_EOL_LEN       4
#define TERM_BOLD                   "\033[1m"
#define TERM_BOLD_LEN               4
#define TERM_BELL                   "\033[7"
#define TERM_BELL_LEN               3
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
#define SEND_ESC_SEQ(fd, seq) fd_write ((fd), seq, seq ## _LEN)
#define TERM_SEND_ESC_SEQ(seq) fd_write ($my(out_fd), seq, seq ## _LEN)

#define TERM_DONOT_SAVE_SCREEN    (1 << 0)
#define TERM_DONOT_CLEAR_SCREEN   (1 << 1)
#define TERM_DONOT_RESTORE_SCREEN (1 << 2)

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

#define RL_OK (1 << 0)
#define RL_CONTINUE (1 << 1)
#define RL_BREAK (1 << 2)
#define RL_PROCESS_CHAR (1 << 3)
#define RL_INSERT_CHAR (1 << 4)
#define RL_CLEAR (1 << 5)
#define RL_WRITE (1 << 6)
#define RL_IS_VISIBLE (1 << 7)
#define RL_CURSOR_HIDE (1 << 8)
#define RL_CLEAR_FREE_LINE (1 << 9)
#define RL_POST_PROCESS (1 << 10)
#define RL_SET_POS (1 << 11)
#define RL_EXEC (1 << 12)
#define RL_FIRST_CHAR_COMPLETION (1 << 13)

#define RL_OPT_HAS_TAB_COMPLETION (1 << 0)
#define RL_OPT_HAS_HISTORY_COMPLETION (1 << 1)
#define RL_OPT_RETURN_AFTER_TAB_COMPLETION (1 << 2)

#define INDEX_ERROR            -1000
#define NULL_PTR_ERROR         -1001
#define INTEGEROVERFLOW_ERROR  -1002

enum {
  NO_CALLBACK_FUNCTION = -4,
  RLINE_NO_COMMAND = -3,
  ERROR = -2,
  NOTHING_TODO = -1,
  DONE = 0,
  IGNORE_BLOCK,
  EXIT_THIS,
  EXIT_ALL,
  EXIT_ALL_FORCE,
  WIN_EXIT,
  BUF_QUIT,
  NEWCHAR,
};

typedef signed int utf8;
typedef unsigned int uint;
typedef unsigned char uchar;
typedef size_t uidx_t;

#include <stddef.h>
typedef ptrdiff_t idx_t;
typedef ptrdiff_t msize_t;

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
#define ifnot(__expr__) if (0 == (__expr__))
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

/* from man printf(3) Linux Programmer's Manual */
#define VA_ARGS_FMT_SIZE(fmt_)                                        \
({                                                                    \
  int size = 0;                                                       \
  va_list ap; va_start(ap, fmt_);                                     \
  size = vsnprintf (NULL, size, fmt_, ap);                            \
  va_end(ap);                                                         \
  size;                                                               \
})
/*
 * gcc used to complain on -Werror=alloc-size-larger-than= or -fsanitize=undefined,
 *  with:
 *  argument 1 range [18446744071562067968, 18446744073709551615]
 *  exceeds maximum object size 9223372036854775807
 *  in a call to built-in allocation function '__builtin_alloca_with_align'
 */
//#define VA_ARGS_FMT_SIZE (MAXLEN_LINE * 2)

#define VA_ARGS_GET_FMT_STR(buf_, size_, fmt_)                        \
({                                                                    \
  va_list ap; va_start(ap, fmt_);                                     \
  vsnprintf (buf_, size_ + 1, fmt_, ap);                              \
  va_end(ap);                                                         \
  buf_;                                                               \
})

#define STR_FMT(fmt_, ...)                                            \
({                                                                    \
  char buf_[MAXLEN_LINE];                                             \
  snprintf (buf_, MAXLEN_LINE, fmt_, __VA_ARGS__);                    \
  buf_;                                                               \
})

#define __Me__  Me
#define __this__ this
#define __thisp__ thisp
#define __prop__ prop
#define __self__ self
#define __root__ root
#define __parent__ parent
#define __current__ current
#define Type(__type__) __type__ ## _t
#define DeclareType(__t__) typedef struct Type(__t__) Type(__t__)
#define AllocType(__t__) Alloc (sizeof (Type(__t__)))
#define NewType(__t__, ...) DeclareType(__t__); struct Type(__t__) {__VA_ARGS__}
#define Prop(__p__) __p__ ## prop_t
#define DeclareProp(__p__) typedef struct Prop(__p__) Prop(__p__)
#define AllocProp(__p__) Alloc (sizeof (Prop(__p__)))
#define NewProp(__p__, ...) DeclareProp(__p__); struct Prop(__p__) {__VA_ARGS__}
#define $myprop __this__->__prop__
#define $my(__p__) __this__->__prop__->__p__
#define $self(__f__) $myprop->__Me__->self.__f__
#define $selfp(__f__) (*__thisp__)->prop->__Me__->self.__f__
#define self(__f__, ...) $self(__f__)(__this__, ##__VA_ARGS__)
#define selfp(__f__, ...) $selfp(__f__)(__thisp__, ##__VA_ARGS__)
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
#define DeclareClass(__C__) typedef struct Class(__C__) Class(__C__)
#define NewClass(__C__, ...) DeclareClass(__C__); struct Class(__C__) {__VA_ARGS__}
#define ClassInit(__C__, ...) (Class (__C__)) {__VA_ARGS__}
#define AllocClass(__C__) ({                           \
  Class(__C__) *__c__ = Alloc (sizeof (Class(__C__))); \
  __c__->__prop__ = AllocProp(__C__);                  \
  __c__;                                               \
})
#define AllocSelf(__S__) ({                            \
  Self (__S__) *__s__ = Alloc (sizeof (Self(__S__)));  \
  __s__;                                               \
})

DeclareType (fp);
DeclareType (reg);
DeclareType (Reg);
DeclareType (arg);
DeclareType (row);
DeclareType (dim);
DeclareType (syn);
DeclareType (undo);
DeclareType (term);
DeclareType (hist);
DeclareType (menu);
DeclareType (mark);
DeclareType (video);
DeclareType (rline);
DeclareType (ftype);
DeclareType (regexp);
DeclareType (search);
DeclareType (Search);
DeclareType (action);
DeclareType (Action);
DeclareType (string);
DeclareType (dirlist);
DeclareType (dirwalk);
DeclareType (vstring);
DeclareType (Vstring);
DeclareType (ustring);
DeclareType (Ustring);
DeclareType (histitem);
DeclareType (h_rlineitem);

DeclareType (buf);
DeclareProp (buf);
DeclareType (bufiter);

DeclareType (win);
DeclareProp (win);

DeclareType (ed);
DeclareProp (ed);
DeclareClass (ed);

DeclareSelf (input);
DeclareClass (video);
DeclareClass (string);

/* interpeter */
DeclareType (i);
DeclareSelf (i);
DeclareProp (i);
DeclareClass (i);

DeclareProp (E);
DeclareSelf (E);
DeclareClass (E);

/* this might make things harder for the reader, because hides details, but if
 * something is gonna change in future, if it's not just a signle change it is
 * certainly (easier) searchable */

typedef utf8 (*InputGetch_cb) (term_t *);
typedef int  (*Rline_cb) (buf_t **, rline_t *, utf8);
typedef int  (*StrChop_cb) (Vstring_t *, char *, void *);
typedef int  (*FileReadLines_cb) (Vstring_t *, char *, size_t, int, void *);
typedef int  (*RlineAtBeg_cb) (rline_t **);
typedef int  (*RlineAtEnd_cb) (rline_t **);
typedef int  (*RlineTabCompletion_cb) (rline_t *);
typedef int  (*PopenRead_cb) (buf_t *, FILE *stream, fp_t *);
typedef int  (*MenuProcessList_cb) (menu_t *);
typedef int  (*VisualLwMode_cb) (buf_t **, int, int, Vstring_t *, utf8, char *);
typedef int  (*VisualCwMode_cb) (buf_t **, int, int, string_t *, utf8, char *);
typedef int  (*WordActions_cb)  (buf_t **, int, int, bufiter_t *, char *, utf8, char *);
typedef int  (*LineMode_cb)     (buf_t **, utf8, char *, char *, size_t);
typedef int  (*FileActions_cb)  (buf_t **, utf8, char *);
typedef int  (*BufNormalBeg_cb) (buf_t **, utf8, int, int);
typedef int  (*BufNormalEnd_cb) (buf_t **, utf8, int, int);
typedef int  (*BufNormalOng_cb) (buf_t **, int);
typedef int  (*ReCompile_cb) (regexp_t *);
typedef int  (*DirProcessDir_cb) (dirwalk_t *, char *, struct stat *);
typedef int  (*DirProcessFile_cb) (dirwalk_t *, char *, struct stat *);
typedef int  (*DirStatFile_cb) (const char *, struct stat *);
typedef char *(*FtypeOpenFnameUnderCursor_cb) (char *, size_t, size_t);
typedef dim_t **(*WinDimCalc_cb) (win_t *, int, int, int, int);
typedef string_t *(*FtypeAutoIndent_cb) (buf_t *, row_t *);
typedef int (*Balanced_cb) (buf_t **, int, int);
typedef int (*ExprRegister_cb) (ed_t *, buf_t *, int);
typedef void (*EAtExit_cb) (void);
typedef void (*EdAtExit_cb) (ed_t *);
typedef void (*EdAtInit_cb) (ed_t *);

/* in between */
typedef void (*Record_cb) (ed_t *, char *);
typedef int  (*IRecord_cb) (ed_t *, Vstring_t *);
typedef char *(*InitRecord_cb) (ed_t *);

/* interpeter */
typedef void (*IPrintByte_cb) (FILE *, int);
typedef void (*IPrintBytes_cb) (FILE *, const char *);
typedef void (*IPrintFmtBytes_cb) (FILE *, const char *, ...);
typedef int  (*ISyntaxError_cb) (i_t *, const char *);

typedef intptr_t ival_t;
typedef ival_t (*Cfunc) (i_t *, ival_t, ival_t, ival_t, ival_t, ival_t, ival_t, ival_t, ival_t, ival_t);
typedef ival_t (*Opfunc) (ival_t, ival_t);

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

NewType (Vstring,
  vstring_t *head;
  vstring_t *tail;
  vstring_t *current;
        int  cur_idx;
        int  num_items;
);

NewType (imap,
  char *key;
  int   val;
  imap_t *next;
);

NewType (Imap,
  imap_t **slots;
  size_t
    num_slots,
    num_keys;
);

NewType (smap,
  char *key;
  string_t *val;
  smap_t *next;
);

NewType (Smap,
  smap_t **slots;
  size_t
    num_slots,
    num_keys;
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

  char *(*parse) (buf_t *, char *, int, int, row_t *);
  ftype_t *(*init) (buf_t *);

  int state;

  size_t
     multiline_comment_continuation_len,
    *keywords_len,
    *keywords_colors;

  char *balanced_pairs;
);

NewType (ftype,
  char
    name[MAXLEN_FTYPE_NAME];

  string_t *on_emptyline;

  int
    shiftwidth,
    tabwidth,
    autochdir,
    tab_indents,
    clear_blanklines,
    cr_on_normal_is_like_insert_mode,
    backspace_on_normal_is_like_insert_mode,
    backspace_on_normal_goes_up,
    backspace_on_insert_goes_up_and_join,
    backspace_on_first_idx_remove_trailing_spaces,
    space_on_normal_is_like_insert_mode,
    small_e_on_normal_goes_insert_mode,
    read_from_shell;

  FtypeAutoIndent_cb autoindent;
  FtypeOpenFnameUnderCursor_cb on_open_fname_under_cursor;
  Balanced_cb balanced;
);

#define DIRLIST_DONOT_CHECK_DIRECTORY (1 << 0)

NewType (dirlist,
  Vstring_t *list;
  char dir[PATH_MAX];
  void (*free) (dirlist_t *);
);

NewType (dirwalk,
  string_t *dir;

  Vstring_t *files;

  int
    orig_depth,
    depth,
    status;

  void *object;

  DirProcessDir_cb process_dir;
  DirProcessFile_cb process_file;
  DirStatFile_cb stat_file;
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

NewType (ustring,
  utf8 code;
  char buf[5];
  int
    len,
    width;

  ustring_t
    *next,
    *prev;
);

NewType (Ustring,
  ustring_t *head;
  ustring_t *tail;
  ustring_t *current;
      int  cur_idx;
      int  num_items;
      int  len;
);

NewType (balanced,
  char bytes[512];
  int  linenr[512];
  int  last_idx;
  int  has_opening_string;
);

NewType (bufinfo,
  char
    *fname,
    *cwd,
    *parents_name;

  int
    at_frame,
    cur_idx,
    is_writable;

  size_t
    num_bytes,
    num_lines;
);

NewType (wininfo,
  char
    *name,
    *parents_name,
    *cur_buf,
    **buf_names;

  int
    num_frames,
    cur_idx;

  size_t
    num_items;
);

NewType (edinfo,
  char
    *name,
    *cur_win,
    **win_names,
    **special_win_names;

  int
    cur_idx;

  size_t
    num_special_win,
    num_items;
);

/* QUALIFIERS (quite ala S_Lang (in C they have to be declared though)) */
NewType (buf_init_opts,
   win_t *win;

   char
     *fname,
     *backup_suffix;

   int
     flags,
     ftype,
     at_frame,
     at_linenr,
     at_column,
     backupfile;

   long autosave;
);

NewType (ed_init_opts,
  int num_win;
  int term_flags;
  EdAtInit_cb init_cb;
);

#define QUAL(__qual, ...) __qual##_QUAL (__VA_ARGS__)

#define ED_INIT_OPTS  Type (ed_init_opts)
#define ED_INIT_QUAL(...) (ED_INIT_OPTS) {       \
  .num_win = 1,                                  \
  .term_flags = 0,                               \
  .init_cb = NULL,                               \
  __VA_ARGS__}

#define BUF_INIT_OPTS Type (buf_init_opts)
#define BUF_INIT_QUAL(...) (BUF_INIT_OPTS) {     \
  .at_frame = 0, .at_linenr = 1, .at_column = 1, \
  .ftype = FTYPE_DEFAULT,                        \
  .autosave = 0,                                 \
  .backupfile = 0,                               \
  .backup_suffix = BACKUP_SUFFIX,                \
  .flags = 0, .fname = UNNAMED, __VA_ARGS__}

#define FTYPE_QUAL(...) (ftype_t) {              \
  .name = "",                                    \
  .on_emptyline = NULL,                          \
  .shiftwidth = DEFAULT_SHIFTWIDTH,              \
  .tabwidth = TABWIDTH,                          \
  .autochdir = AUTOCHDIR,                        \
  .tab_indents = TAB_ON_INSERT_MODE_INDENTS,     \
  .clear_blanklines = CLEAR_BLANKLINES,          \
  .cr_on_normal_is_like_insert_mode = CARRIAGE_RETURN_ON_NORMAL_IS_LIKE_INSERT_MODE,  \
  .backspace_on_normal_is_like_insert_mode = BACKSPACE_ON_NORMAL_IS_LIKE_INSERT_MODE,  \
  .backspace_on_normal_goes_up = BACKSPACE_ON_NORMAL_GOES_UP, \
  .backspace_on_insert_goes_up_and_join = BACKSPACE_ON_INSERT_GOES_UP_AND_JOIN, \
  .backspace_on_first_idx_remove_trailing_spaces = BACKSPACE_ON_FIRST_IDX_REMOVE_TRAILING_SPACES,  \
  .space_on_normal_is_like_insert_mode = SPACE_ON_NORMAL_IS_LIKE_INSERT_MODE,  \
  .small_e_on_normal_goes_insert_mode = SMALL_E_ON_NORMAL_GOES_INSERT_MODE,  \
  .read_from_shell = 1,                          \
  .autoindent = NULL,                            \
  .on_open_fname_under_cursor = NULL,            \
  .balanced = NULL,                              \
  __VA_ARGS__ }

/* interpeter */
NewType (i_options,
  char  *name;
  int    name_gen;
  size_t mem_size;
  size_t max_script_size;
  FILE  *err_fp;
  FILE  *out_fp;
  IPrintByte_cb print_byte;
  IPrintBytes_cb print_bytes;
  IPrintFmtBytes_cb print_fmt_bytes;
  ISyntaxError_cb syntax_error;
);

#define I_INIT Type (i_options)
#define I_INIT_QUAL(...) (I_INIT) {      \
  .mem_size = 4096,                      \
  .print_byte = NULL,                    \
  .print_bytes = NULL,                   \
  .print_fmt_bytes = NULL,               \
  .syntax_error = i_syntax_error_to_ed,  \
  .err_fp = stderr,                      \
  .out_fp = stdout,                      \
  .name = NULL,                          \
  .name_gen = 97,                        \
  .max_script_size = 1 << 16,            \
  __VA_ARGS__}

NewSubSelf (video, set,
  void (*row_with) (video_t *, int, char *);
);

NewSubSelf (video, draw,
  void
    (*row_at) (video_t *, int),
    (*all) (video_t *);

  int
    (*bytes) (video_t *, char *, size_t);
);

NewSelf (video,
  SubSelf (video, set) set;
  SubSelf (video, draw) draw;

  video_t
    *(*new) (int, int, int, int, int);

  void
    (*free) (video_t *),
    (*flush) (video_t *, string_t *);
);

NewClass (video,
  Self (video) self;
);

NewSubSelf (term, screen,
  void
    (*bell) (term_t *),
    (*save)  (term_t *),
    (*clear) (term_t *),
    (*restore) (term_t *),
    (*clear_eol) (term_t *),
    (*set_color) (term_t *, int);
);

NewClass (screen,
  SubSelf (term, screen) self;
);

NewSubSelf (term, cursor,
  void
    (*hide) (term_t *),
    (*show) (term_t *),
    (*save) (term_t *),
    (*restore) (term_t *),
    (*set_pos) (term_t *, int, int);

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
  SubSelf (term, screen) __Screen__;
  SubSelf (term, cursor) __Cursor__;
  SubSelf (term, input) __Input__;
  SubSelf (term, get) get;

  term_t *(*new) (void);

  void
    (*free) (term_t **),
    (*restore) (term_t *),
    (*set_name) (term_t *),
    (*init_size) (term_t *, int *, int *),
    (*set_state_bit) (term_t *, int),
    (*unset_state_bit) (term_t *, int);

  int
    (*set) (term_t *),
    (*reset) (term_t *),
    (*set_mode) (term_t *, char);
);

NewClass (term,
  Self (term) self;
);

NewSelf (fd,
  int
    (*read) (int, char *, size_t),
    (*write) (int, char *, size_t);
);

NewClass (fd,
  Self (fd) self;
);

NewSubSelf (ustring, get,
  utf8 (*code_at) (char *, size_t, int, int *);
);

NewSelf (ustring,
  SubSelf (ustring, get) get;

  Ustring_t *(*new) (void);
  void (*free) (Ustring_t *);

  ustring_t *(*encode) (Ustring_t *, char *, size_t, int, int, int);

  char *(*character) (utf8, char *, int *);

  int
    (*width) (char *, int),
    (*charlen) (uchar),
    (*is_lower) (utf8),
    (*is_upper) (utf8),
    (*swap_case) (char *, char *, size_t len),
    (*change_case) (char *, char *, size_t len, int);

  size_t (*validate) (unsigned char *, size_t, char **, int *);

  utf8
    (*to_lower) (utf8),
    (*to_upper) (utf8);
);

NewClass (ustring,
  Self (ustring) self;
);

NewSubSelf (cstring, trim,
  char *(*end) (char *, char);
);

NewSubSelf (cstring, byte,
  size_t (*mv) (char *, size_t, size_t, size_t, size_t);
  char
    *(*null_in_str) (const char *),
    *(*in_str) (const char *, int),
    *(*in_str_r) (const char *, int);
);

NewSelf (cstring,
  SubSelf (cstring, trim) trim;
  SubSelf (cstring, byte) byte;

  char
    *(*bytes_in_str) (const char *, const char *),
    *(*substr) (char *, size_t, char *, size_t, size_t),
    *(*extract_word_at) (char *, size_t, char *, size_t, char *, size_t, int, int *, int *),
    *(*itoa) (int, char *, int),
    *(*dup) (const char *, size_t);

  int
    (*eq) (const char *, const char *),
    (*eq_n) (const char *, const char *, size_t),
    (*cmp_n) (const char *, const char *, size_t);

  size_t
    (*cat) (char *, size_t, const char *),
    (*cp) (char *, size_t, const char *, size_t),
    (*cp_fmt) (char *, size_t, char *, ...);

  Vstring_t *(*chop) (char *, char, Vstring_t *, StrChop_cb, void *);
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
    *(*trim_end) (string_t *, char c),
    *(*replace_numbytes_at_with) (string_t *, int, int, const char *);

  int (*delete_numbytes_at) (string_t *, int, int);
);

NewClass (string,
  Self (string) self;
);

NewSubSelf (vstring, current,
  void
    (*append_with) (Vstring_t *, char *),
    (*append_with_len) (Vstring_t *, char *, size_t);
);

NewSubSelf (vstring, get,
  size_t (*size) (Vstring_t *);
);

NewSubSelf (vstring, add,
  Vstring_t *(*sort_and_uniq) (Vstring_t *, char *bytes);
);

NewSubSelf (vstring, to,
  char *(*cstring) (Vstring_t *, int);
);

NewSelf (vstring,
  SubSelf (vstring, current) current;
  SubSelf (vstring, add) add;
  SubSelf (vstring, to) to;
  SubSelf (vstring, get) get;

  Vstring_t
    *(*new) (void),
    *(*dup) (Vstring_t *);

  void
    (*free) (Vstring_t *),
    (*clear) (Vstring_t *),
    (*append) (Vstring_t *, vstring_t *),
    (*append_uniq) (Vstring_t *, char *),
    (*append_with) (Vstring_t *, char *),
    (*append_with_len) (Vstring_t *, char *, size_t),
    (*append_with_fmt) (Vstring_t *, char *, ...);

  char **(*shallow_copy) (Vstring_t *, char **);

  vstring_t *(*pop_at) (Vstring_t *, int);
  string_t *(*join) (Vstring_t *, char *sep);
);

NewClass (vstring,
  Self (vstring) self;
);

NewSelf (imap,
  void
    (*free) (Imap_t *),
    (*clear) (Imap_t *);

  Imap_t *(*new) (int);

  int
    (*get) (Imap_t *, char *),
    (*key_exists) (Imap_t *, char *);

  uint
    (*set) (Imap_t *, char *, int),
    (*set_with_keylen) (Imap_t *, char *);
);

NewClass (imap,
  Self (imap) self;
);

NewSelf (smap,
  void
    (*free) (Smap_t *),
    (*clear) (Smap_t *);

  Smap_t *(*new) (int);

  int
    (*key_exists) (Smap_t *, char *);

  string_t
    *(*get) (Smap_t *, char *);

  uint
    (*set) (Smap_t *, char *, string_t *);
);

NewClass (smap,
  Self (smap) self;
);

NewSubSelf (rline, set,
  void
    (*line) (rline_t *, char *, size_t),
    (*opts) (rline_t *, int),
    (*state) (rline_t *, int),
    (*opts_bit) (rline_t *, int),
    (*state_bit) (rline_t *, int),
    (*visibility) (rline_t *, int),
    (*prompt_char) (rline_t *, char),
    (*user_object) (rline_t *, void *);
);

NewSubSelf (rline, get,
  string_t
     *(*line) (rline_t *),
     *(*command) (rline_t *),
     *(*anytype_arg) (rline_t *, char *);

  Vstring_t *(*anytype_args) (rline_t *, char *);

  arg_t *(*arg) (rline_t *, int);

  int
    (*opts) (rline_t *),
    (*state) (rline_t *),
    (*buf_range) (rline_t *, buf_t *, int *);

  void *(*user_object) (rline_t *);

  Vstring_t *(*arg_fnames) (rline_t *, int);
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

  rline_t
     *(*edit) (rline_t *),
     *(*parse) (rline_t *, buf_t *);

  void
    (*free) (rline_t *),
    (*clear) (rline_t *),
    (*write_and_break) (rline_t *);

  int
    (*exec) (rline_t *, buf_t **);

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
    (*write_fmt) (ed_t *, char *, ...),
    (*set) (ed_t *, int, int, char *, size_t),
    (*set_fmt) (ed_t *, int, int, char *, ...),
    (*line) (ed_t *, int, char *, ...),
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
    (*is_reg) (const char *),
    (*is_elf) (const char *),
    (*is_rwx) (const char *),
    (*exists) (const char *),
    (*is_sock) (const char *),
    (*is_readable) (const char *),
    (*is_writable) (const char *),
    (*is_executable) (const char *);

  ssize_t
    (*write) (char *, char *, ssize_t),
    (*append) (char *, char *, ssize_t);

  Vstring_t *(*readlines) (char *, Vstring_t *, FileReadLines_cb, void *);
);

NewClass (file,
  Self (file) self;
);

NewSelf (path,
  char
    *(*real) (const char *, char *),
    *(*basename) (char *),
    *(*basename_sans_extname) (char *),
    *(*extname) (char *),
    *(*dirname) (char *);

  int (*is_absolute) (char *);
);

NewClass (path,
  Self (path) self;
);

NewSubSelf (dir, walk,
  dirwalk_t *(*new) (DirProcessDir_cb, DirProcessFile_cb);
  void (*free) (dirwalk_t **);
  int (*run) (dirwalk_t *, char *);
);

NewSelf (dir,
  SubSelf (dir, walk) walk;
  dirlist_t *(*list) (char *, int);
  char *(*current) (void);
  int (*is_directory) (char *);
);

NewClass (dir,
  Self (dir) self;
);

NewSubSelf (vsys, stat,
  char *(*mode_to_string) (char *, mode_t);
);

NewSubSelf (vsys, get,
  long (*clock_sec) (clockid_t);
);

NewSelf (vsys,
  SubSelf (vsys, stat) stat;
  SubSelf (vsys, get) get;
  string_t *(*which) (char *, char *);
);

NewClass (vsys,
  Self (vsys) self;
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
    *(*at) (buf_t *, int);

  string_t
     *(*current_bytes) (buf_t *),
     *(*bytes_at) (buf_t *, int);

  int
    (*col_idx) (buf_t *, row_t *);
);

NewSubSelf (bufget, prop,
  int (*tabwidth) (buf_t *);
);

NewSubSelf (bufget, info,
  bufinfo_t *(*as_type) (buf_t *);
);

NewSubSelf (buf, get,
  SubSelf (bufget, row) row;
  SubSelf (bufget, prop) prop;
  SubSelf (bufget, info) info;

  char
    *(*contents) (buf_t *, int),
    *(*basename) (buf_t *),
    *(*fname) (buf_t *),
    *(*ftype_name) (buf_t *),
    *(*info_string) (buf_t *),
    *(*current_word) (buf_t *, char *, char *, int, int *, int *);

  size_t
    (*size) (buf_t *),
    (*num_lines) (buf_t *);

  string_t *(*shared_str) (buf_t *);

  row_t *(*line_at) (buf_t *, int);

  int
    (*flags) (buf_t *),
    (*current_col_idx) (buf_t *),
    (*current_row_idx) (buf_t *),
    (*current_video_row) (buf_t *),
    (*current_video_col) (buf_t *);

  win_t *(*my_parent) (buf_t *);
  ed_t  *(*my_root) (buf_t *);
);

NewSubSelf (bufset, as,
  void
    (*pager) (buf_t *),
    (*unnamed) (buf_t *),
    (*non_existant) (buf_t *);
);

NewSubSelf (bufset, row,
  int (*idx) (buf_t *, int, int, int);
);

NewSubSelf (bufset, normal,
  void
    (*at_beg_cb) (buf_t *, BufNormalBeg_cb),
    (*at_end_cb) (buf_t *, BufNormalEnd_cb);
);

NewSubSelf (buf, set,
  SubSelf (bufset, as) as;
  SubSelf (bufset, row) row;
  SubSelf (bufset, normal) normal;

  int (*fname) (buf_t *, char *);

  void
    (*backup) (buf_t *, int, char *),
    (*autosave) (buf_t *, long),
    (*modified) (buf_t *),
    (*video_first_row) (buf_t *, int),
    (*mode) (buf_t *, char *),
    (*show_statusline) (buf_t *, int),
    (*on_emptyline) (buf_t *, char *),
    (*ftype) (buf_t *, int);
);

NewSubSelf (buf, syn,
  ftype_t *(*init) (buf_t *);
     char *(*parser) (buf_t *, char *, int, int, row_t *);
);

NewSubSelf (buf, ftype,
  void (*free) (buf_t *);

  ftype_t
     *(*init) (buf_t *, int, FtypeAutoIndent_cb),
     *(*set)  (buf_t *, int, ftype_t);
);

NewSubSelf (buf, to,
  void (*video) (buf_t *);
);

NewSubSelf (buf, isit,
  int (*special_type) (buf_t *);
);

NewSubSelf (buf, current,
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
     (*line) (buf_t *),
     (*info) (buf_t *, bufinfo_t **),
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
  int (*from_fp) (buf_t *, FILE *, fp_t *);
);

NewSubSelf (buf, Action,
  Action_t *(*new) (buf_t *);
  void
    (*free) (buf_t *, Action_t *),
    (*set_with) (buf_t *, Action_t *, int, int, char *, size_t),
    (*set_with_current) (buf_t *, Action_t *, int);
);

NewSubSelf (buf, action,
  action_t
    *(*new) (buf_t *),
    *(*new_with) (buf_t *, int, int, char *, size_t);

  void
    (*free) (buf_t *, action_t *);
);

NewSubSelf (buf, undo,
  void
    (*free) (buf_t *),
    (*init) (buf_t *),
    (*push) (buf_t *, Action_t *),
    (*clear) (buf_t *);

  int
    (*exec) (buf_t *, utf8),
    (*insert) (buf_t *, Action_t *, action_t *),
    (*delete_line) (buf_t *, Action_t *, action_t *),
    (*replace_line) (buf_t *, Action_t *, action_t *);

  Action_t
    *(*pop) (buf_t *this);
);

NewSubSelf (buf, redo,
  void
    (*push) (buf_t *, Action_t *);

  Action_t
    *(*pop) (buf_t *this);
);

NewSubSelf (bufnormal, visual,
  int
    (*bw) (buf_t *),
    (*lw) (buf_t **),
    (*cw) (buf_t **);
);

NewSubSelf (bufnormal, handle,
  int
    (*g) (buf_t **, int),
    (*G) (buf_t *, int),
    (*d) (buf_t *, int, int),
    (*c) (buf_t **, int, int),
    (*W) (buf_t **),
    (*F) (buf_t **),
    (*L) (buf_t **),
    (*comma) (buf_t **),
    (*ctrl_w) (buf_t **);
);

NewSubSelf (buf, normal,
  SubSelf (bufnormal, handle) handle;
  SubSelf (bufnormal, visual) visual;

  int
    (*up) (buf_t *, int, int, int),
    (*bol) (buf_t *, int),
    (*eol) (buf_t *, int),
    (*bof) (buf_t *, int),
    (*eof) (buf_t *, int),
    (*put) (buf_t *, int, utf8),
    (*join) (buf_t *, int),
    (*yank) (buf_t *, int, int),
    (*Yank) (buf_t *, int, int),
    (*down) (buf_t *, int, int, int),
    (*left) (buf_t *, int, int),
    (*right) (buf_t *, int, int),
    (*delete) (buf_t *, int, int, int),
    (*noblnk) (buf_t *),
    (*page_up) (buf_t *, int, int),
    (*end_word) (buf_t *, int, int, int),
    (*page_down) (buf_t *, int, int),
    (*delete_eol) (buf_t *, int, int),
    (*change_case) (buf_t *),
    (*goto_linenr) (buf_t *, int, int),
    (*replace_character) (buf_t *),
    (*replace_character_with) (buf_t *, utf8);
);

NewSubSelf (buf, insert,
  int
    (*mode) (buf_t **, utf8, char *),
    (*string) (buf_t *, char *, size_t, int),
    (*new_line) (buf_t **, utf8);
);

NewSubSelf (buf, delete,
  int
    (*word) (buf_t *, int),
    (*line) (buf_t *, int, int);
);

NewSubSelf (buf, jump,
  void (*push) (buf_t *, mark_t *);
  int (*to) (buf_t *, int);
);

NewSubSelf (buf, jumps,
  void
     (*free) (buf_t *),
     (*init) (buf_t *);
);

NewSubSelf (buf, mark,
  int
    (*set) (buf_t *, int),
    (*jump) (buf_t *),
    (*restore) (buf_t *, mark_t *);
);

NewSubSelf (buf, adjust,
  void
    (*view) (buf_t *),
    (*marks) (buf_t *, int, int, int);

  int (*col) (buf_t *, int, int);
);

NewSelf (buf,
  SubSelf (buf, current) current;
  SubSelf (buf, to) to;
  SubSelf (buf, set) set;
  SubSelf (buf, get) get;
  SubSelf (buf, syn) syn;
  SubSelf (buf, row) row;
  SubSelf (buf, isit) isit;
  SubSelf (buf, free) free;
  SubSelf (buf, undo) undo;
  SubSelf (buf, redo) redo;
  SubSelf (buf, read) read;
  SubSelf (buf, iter) iter;
  SubSelf (buf, mark) mark;
  SubSelf (buf, jump) jump;
  SubSelf (buf, jumps) jumps;
  SubSelf (buf, ftype) ftype;
  SubSelf (buf, Action) Action;
  SubSelf (buf, action) action;
  SubSelf (buf, normal) normal;
  SubSelf (buf, delete) delete;
  SubSelf (buf, insert) insert;
  SubSelf (buf, adjust) adjust;

  void
    (*draw) (buf_t *),
    (*flush) (buf_t *),
    (*clear) (buf_t *),
    (*draw_current_row) (buf_t *);

  char
    *(*info) (buf_t *);

  int
    (*rline) (buf_t **, rline_t *),
    (*write) (buf_t *, int),
    (*search) (buf_t *, char, char *, utf8),
    (*indent) (buf_t *, int, utf8),
    (*substitute) (buf_t *, char *, char *, int, int, int, int),
    (*backupfile) (buf_t *);

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
    (*previous_idx) (win_t *,  int),
    (*dim) (win_t *, dim_t *, int, int, int, int),
    (*num_frames) (win_t *, int),
    (*min_rows) (win_t *, int),
    (*has_dividers) (win_t *, int),
    (*video_dividers) (win_t *);

  buf_t *(*current_buf) (win_t*, int, int);
);

NewSubSelf (winget, info,
  wininfo_t *(*as_type) (win_t *);
);

NewSubSelf (win, get,
  SubSelf (winget, info) info;

  buf_t
    *(*current_buf) (win_t *),
    *(*buf_head) (win_t *),
    *(*buf_by_idx) (win_t *, int),
    *(*buf_next) (win_t *, buf_t *),
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

NewSubSelf (win, isit,
  int (*special_type) (win_t *);
);

NewSelf (win,
  SubSelf (win, set) set;
  SubSelf (win, get) get;
  SubSelf (win, isit) isit;
  SubSelf (win, pop) pop;
  SubSelf (win, adjust) adjust;
  SubSelf (win, buf) buf;
  SubSelf (win, frame) frame;

  void
    (*free_info) (win_t *, wininfo_t **),
    (*draw) (win_t *);

  int
    (*edit_fname)    (win_t *, buf_t **, char *, int, int, int, int),
    (*append_buf)    (win_t *, buf_t *),
    (*prepend_buf)   (win_t *, buf_t *),
    (*insert_buf_at) (win_t *, buf_t *, int);

  dim_t
     **(*dim_calc) (win_t *);
);

NewClass (win,
  Self (win) self;
);

NewSubSelf (edget, info,
  edinfo_t *(*as_type) (ed_t *);
);

NewSubSelf (ed, get,
  ed_t *(*self) (ed_t *);

  SubSelf (edget, info) info;

  buf_t
    *(*bufname) (ed_t *, char *),
    *(*current_buf) (ed_t *),
    *(*scratch_buf) (ed_t *);

  int
    (*num_rline_commands) (ed_t *),
    (*num_win) (ed_t *, int),
    (*current_win_idx) (ed_t *),
    (*num_special_win) (ed_t *),
    (*state) (ed_t *);

  win_t
    *(*current_win) (ed_t *),
    *(*win_head) (ed_t *),
    *(*win_next) (ed_t *, win_t *),
    *(*win_by_idx) (ed_t *, int),
    *(*win_by_name) (ed_t *, char *, int *);

  term_t *(*term) (ed_t *);

  string_t *(*topline) (ed_t *);

  video_t *(*video) (ed_t *);

  void *(*callback_fun) (ed_t *, char *);
);

NewSubSelf (ed, unset,
  void
    (*state_bit) (ed_t *, int);
);

NewSubSelf (ed, test,
  int
    (*state_bit) (ed_t *, int);
);

NewSubSelf (ed, set,
  void
    (*state) (ed_t *, int),
    (*state_bit) (ed_t *, int),
    (*screen_size) (ed_t *),
    (*exit_quick) (ed_t *, int),
    (*topline) (ed_t *, buf_t *),
    (*rline_cb) (ed_t *, Rline_cb),
    (*lang_map) (ed_t *, int[][26]),
    (*record_cb) (ed_t *, Record_cb),
    (*at_exit_cb) (ed_t *, EdAtExit_cb),
    (*i_record_cb) (ed_t *, IRecord_cb),
    (*expr_reg_cb) (ed_t *, ExprRegister_cb),
    (*init_record_cb) (ed_t *, InitRecord_cb),
    (*on_normal_g_cb)  (ed_t *, BufNormalOng_cb),
    (*word_actions) (ed_t *, utf8 *, int, char *, WordActions_cb),
    (*cw_mode_actions) (ed_t *, utf8 *, int, char *, VisualCwMode_cb),
    (*lw_mode_actions) (ed_t *, utf8 *, int, char *, VisualLwMode_cb),
    (*line_mode_actions) (ed_t *, utf8 *, int, char *, LineMode_cb),
    (*file_mode_actions) (ed_t *, utf8 *, int, char *, FileActions_cb);

  win_t *(*current_win) (ed_t *, int);
  dim_t *(*dim) (ed_t *, dim_t *, int, int, int, int);
);

NewSubSelf (ed, syn,
  void (*append) (ed_t *, syn_t);
  int  (*get_ftype_idx) (ed_t *, char *);
);

NewSubSelf (ed, reg,
  Reg_t
     *(*set) (ed_t *, int, int, char *, int),
     *(*setidx) (ed_t *, int, int, char *, int);
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

NewSubSelf (ed, rline,
  rline_t
    *(*new) (ed_t *),
    *(*new_with) (ed_t *, char *);
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
    (*add) (ed_t *, Vstring_t *, int),
    (*read) (ed_t *),
    (*write) (ed_t *);
);

NewSubSelf (ed, draw,
  void (*current_win) (ed_t *);
);

NewSelf (ed,
  SubSelf (ed, sh) sh;
  SubSelf (ed, buf) buf;
  SubSelf (ed, win) win;
  SubSelf (ed, set) set;
  SubSelf (ed, get) get;
  SubSelf (ed, syn) syn;
  SubSelf (ed, reg) reg;
  SubSelf (ed, test) test;
  SubSelf (ed, menu) menu;
  SubSelf (ed, draw) draw;
  SubSelf (ed, unset) unset;
  SubSelf (ed, rline) rline;
  SubSelf (ed, append) append;
  SubSelf (ed, history) history;
  SubSelf (ed, readjust) readjust;

  void
    (*free) (ed_t *),
    (*free_info) (ed_t *, edinfo_t **),
    (*record) (ed_t *, char *, ...),
    (*suspend) (ed_t *),
    (*resume) (ed_t *),
    (*deinit_commands) (ed_t *);

  int
    (*check_sanity) (ed_t *),
    (*quit) (ed_t *, int, int),
    (*delete) (ed_t *, ed_T *, int, int),
    (*scratch) (ed_t *, buf_t **, int),
    (*messages) (ed_t *, buf_t **, int);

  utf8 (*question) (ed_t *, char *, utf8 *, int);

  dim_t
     **(*dim_calc) (ed_t *, int, int, int, int),
     **(*dims_init) (ed_t *, int);
);

/* interpeter */
NewSubSelf (i, get,
  i_t *(*current) (Class (i) *);
  int (*current_idx) (Class (i) *);
);

NewSubSelf (i, set,
  i_t *(*current) (Class (i) *, int);
);

NewSelf (i,
  SubSelf (i, get) get;
  SubSelf (i, set) set;

  void
    (*free) (i_t **),
    (*remove_instance) (Class (i) *, Type (i) *);

  i_t
    *(*new) (void),
    *(*init_instance) (Class (i) *),
    *(*append_instance) (Class (i) *, Type (i) *);

  int
    (*def) (i_t *, const char *, int, ival_t),
    (*init) (Class (i) *, i_t *, I_INIT),
    (*eval_file) (i_t *, const char *),
    (*load_file) (Class (i) *, char *),
    (*eval_string) (i_t *, const char *, int, int);
);

NewClass (i,
  Self (i) self;
  Prop (i) *prop;
);

NewClass (ed,
  Prop (ed) *prop;
  Self (ed)  self;
  Class (buf) __Buf__;
  Class (win) __Win__;
  Class (i) __I__;

  Class (re) __Re__;
  Class (fd) __Fd__;
  Class (msg) __Msg__;
  Class (dir) __Dir__;
  Class (vsys) __Vsys__;
  Class (term) __Term__;
  Class (smap) __Smap__;
  Class (imap) __Imap__;
  Class (path) __Path__;
  Class (file) __File__;
  Class (rline) __Rline__;
  Class (video) __Video__;
  Class (input) __Input__;
  Class (error) __Error__;
  Class (screen) __Screen__;
  Class (cursor) __Cursor__;
  Class (string) __String__;
  Class (cstring) __Cstring__;
  Class (vstring) __Vstring__;
  Class (ustring) __Ustring__;
);

NewSubSelf (E, unset,
  void
    (*state_bit) (E_T *, int);
);

NewSubSelf (E, test,
  int
    (*state_bit) (E_T *, int);
);

NewSubSelf (E, set,
  void
    (*state) (E_T *, int),
    (*state_bit) (E_T *, int),
    (*save_image) (E_T *, int),
    (*image_name) (E_T *, char *),
    (*image_file) (E_T *, char *),
    (*at_exit_cb) (E_T *, EAtExit_cb),
    (*at_init_cb) (E_T *, EdAtInit_cb),
    (*persistent_layout) (E_T *, int);

  ed_t
    *(*next) (E_T *),
    *(*prev) (E_T *),
    *(*current) (E_T *, int);
);

NewSubSelf (E, get,
  ed_t
    *(*head) (E_T *),
    *(*current) (E_T *),
    *(*next) (E_T *, ed_t *);

  int
    (*num) (E_T *),
    (*idx) (E_T *, ed_t *),
    (*state) (E_T *),
    (*prev_idx) (E_T *),
    (*error_state) (E_T *),
    (*current_idx) (E_T *);

  term_t *(*term) (E_T *);

  string_t *(*env) (E_T *, char *);

  Class (i) *(*iclass) (E_T *);
);

NewSelf (E,
  SubSelf (E, get) get;
  SubSelf (E, set) set;
  SubSelf (E, test) test;
  SubSelf (E, unset) unset;

  ed_t
    *(*init) (E_T *, EdAtInit_cb),
    *(*new) (E_T *, ED_INIT_OPTS);

  int
    (*save_image) (E_T *, char *),
    (*main) (E_T *, buf_t *),
    (*exit_all) (E_T *),
    (*delete) (E_T *, int, int);

  string_t *(*create_image) (E_T *);
);

NewClass (this,
  void *self;
  void *prop;
  Class (E) *__E__;
);

NewClass (E,
  Self (E) self;
  Prop (E) *prop;
  Class (ed) *__Ed__;
  Class (this) *__This__;
);

public Class (E) *__init_ed__ (char *);
public void __deinit_ed__ (Class (E) **);

public mutable size_t tostderr (char *);
public mutable size_t tostdout (char *);

#endif /* LIBVED_H */
