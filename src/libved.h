#ifndef LIBVED_H
#define LIBVED_H

#define OUTPUT_FD STDOUT_FILENO

/* this is not configurable as the code will not count the change in the calculations */
#define TABLENGTH 1
/* there is a significant price to pay in almost any calculation and conditional jumps
 * are expensive, and for something that has no practice other in Makefiles (in any case, the
 * code highlight tabs). But by default, it is not possible to add a new one (which indents),
 * however it is possible to change that per filetype basis, by setting the relative option;
   in that case a tab is treated like the rest in its range, that is one cell width.
 */

#define MAX_FRAMES 3
#define RLINE_HISTORY_NUM_ENTRIES 20
#define UNDO_NUM_ENTRIES 20
#define DEFAULT_SHIFTWIDTH 2
#define DEFAULT_PROMPT_CHAR ':'
#define CARRIAGE_RETURN_ON_NORMAL_IS_LIKE_INSERT_MODE 1
#define SPACE_ON_NORMAL_IS_LIKE_INSERT_MODE 1
#define SMALL_E_ON_NORMAL_GOES_INSERT_MODE 1

#define ED_INIT_ERROR   (1 << 0)

#define ED_SUSPENDED    (1 << 0)

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
#define COLOR_MENU_BG     COLOR_YELLOW
#define COLOR_MENU_SEL    COLOR_RED
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

#define INDEX_ERROR     -2
#define NULL_PTR_ERROR  -3
#define INTEGEROVERFLOW_ERROR -4

typedef signed int utf8;
typedef unsigned int uint;
typedef unsigned char uchar;

#define public __attribute__((visibility ("default")))
#define private __attribute__((visibility ("hidden")))
#define mutable __attribute__((__weak__))
#define __fallthrough__  __attribute__ ((fallthrough))
#define __unused__  __attribute__ ((unused))

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
#define $from(__v__) __v__->__prop__
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
DeclareType (vstritem);
DeclareType (vstr);
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

NewClass(screen,
  SubSelf (term, screen) self;
);

NewSubSelf (term, cursor,
  void
    (*set_pos) (term_t *, int, int),
    (*hide) (term_t *),
    (*restore) (term_t *);

  int (*get_pos) (term_t *, int *, int *);
);

NewClass(cursor,
  SubSelf (term, cursor) self;
);

NewSubSelf (term, input,
  utf8 (*get) (term_t *);
);

NewClass(input,
  SubSelf (term, input) self;
);

NewSelf (term,
  SubSelf (term, screen) Screen;
  SubSelf (term, cursor) Cursor;
  SubSelf (term, input) Input;

  term_t *(*new) (void);

  void
     (*free) (term_t *),
     (*restore) (term_t *),
     (*get_size) (term_t *, int *, int *);

  int (*raw) (term_t *);
  int (*sane) (term_t *);
);

NewClass (term,
  Self (term) self;
);

NewSelf (string,
  void
     (*free) (string_t *),
     (*clear) (string_t *),
     (*clear_at) (string_t *, int),
     (*append_byte) (string_t *, char);

  string_t
    *(*new) (void),
    *(*new_with) (const char *),
    *(*new_with_fmt) (const char *, ...);

   int
     (*replace_with) (string_t *, char *),
     (*replace_with_fmt) (string_t *, const char *, ...),
     (*insert_at) (string_t *, const char *, int),
     (*append)    (string_t *, const char *),
     (*prepend)   (string_t *, const char *),
     (*append_fmt) (string_t *, const char *, ...),
     (*prepend_fmt) (string_t *, const char *, ...),
     (*delete_numbytes_at) (string_t *, int, int),
     (*replace_numbytes_at_with) (string_t *, int, int, const char *);
);

NewClass (string,
  Self (string) self;
);

NewSubSelf (buf, get,
  char *(*fname) (buf_t *);
);

NewSubSelf (buf, set,
  int  (*fname) (buf_t *, char *);
  void (*video_first_row) (buf_t *, int);
  ftype_t *(*ftype) (buf_t *);
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
     (*me) (buf_t *),
     (*row) (buf_t *, row_t *);
);

NewSubSelf (buf, row,
  row_t *(*new_with) (buf_t *, const char *);
);

NewSubSelf (buf, read,
  ssize_t  (*fname) (buf_t *);
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
   (*draw_cur_row) (buf_t *);
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
    *(*current_buf) (win_t*),
    *(*buf_by_idx) (win_t *, int),
    *(*buf_by_fname) (win_t *, const char *, int *);

  int (*num_buf) (win_t *);
);

NewSelf (win,
  SubSelf (win, set) set;
  SubSelf (win, get) get;
  SubSelf (win, adjust) adjust;

  void (*draw) (win_t *);

  buf_t *(*buf_new) (win_t *, char *, int);

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
    *(*current_buf) (ed_t *);

  int
    (*current_win_idx) (ed_t *),
    (*state) (ed_t *);

  win_t
    *(*current_win) (ed_t *),
    *(*win_head) (ed_t *),
    *(*win_next) (ed_t *, win_t *);

  ed_t *(*next) (ed_t *);
  ed_t *(*prev) (ed_t *);
);

NewSubSelf (ed, set,
   void  (*screen_size) (ed_t *);
   void  (*topline) (buf_t *);
  win_t *(*current_win) (ed_t *, int);
  dim_t *(*dim) (ed_t *, int, int, int, int);
);

NewSubSelf (ed, append,
  int (*win) (ed_t *, win_t *);
);

NewSubSelf (ed, readjust,
  void (*win_size) (ed_t *, win_t *);
);

NewSubSelf (ed, exec,
  int (*cmd) (buf_t **, utf8, int *, int);
);

NewSubSelf (ed, win,
  win_t *(*new) (ed_t *, int);
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
  SubSelf (ed, win) win;

  int
    (*loop)  (ed_t *, buf_t *),
    (*main)  (ed_t *, buf_t *);
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
  Class (string) String;
  Class (input) Input;
  Class (screen) Screen;
  Class (cursor) Cursor;

  ed_t *head;
  ed_t *tail;
  ed_t *current;
   int  cur_idx;
   int  num_items;
);

public ed_T *__init_ved__ (void);
public void __deinit_ved__ (ed_T *);

#endif /* LIBVED_H */
