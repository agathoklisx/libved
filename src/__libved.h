#define MAXWORDLEN  256
#define MAXCOMLEN   32
#define MAXERRLEN   256
#define MAXPATLEN PATH_MAX
#define MAX_SCREEN_ROWS  256
#define MAX_COUNT_DIGITS 8

#define VED_WIN_NORMAL_TYPE  0
#define VED_WIN_SPECIAL_TYPE 1

#define VED_MSG_WIN     "message"
#define VED_MSG_BUF     "[messages]"
#define VED_SEARCH_WIN  "search"
#define VED_SEARCH_BUF  "[search]"
#define VED_DIFF_WIN    "diff"
#define VED_DIFF_BUF    "[diff]"
#define UNAMED          "[No Name]"
#define NORMAL_MODE     "normal"
#define INSERT_MODE     "insert"
#define VISUAL_MODE_LW  "visual lw"
#define VISUAL_MODE_CW  "visual cw"
#define VISUAL_MODE_BW  "visual bw"

#define CASE_A ",[]()+-:;}{<>_"
#define CASE_B ".][)(-+;:{}><-"

#define LINEWISE 1
#define CHARWISE 2
#define DELETE_LINE  1
#define REPLACE_LINE 2
#define INSERT_LINE  3

#define MENU_INIT (1 << 0)
#define MENU_QUIT (1 << 1)
#define MENU_INSERT (1 << 2)
#define MENU_REINIT_LIST (1 << 3)
#define MENU_FINALIZE (1 << 4)
#define MENU_DONE (1 << 5)
#define MENU_LIST_IS_ALLOCATED (1 << 6)
#define MENU_REDO (1 << 7)

#define RL_OPT_HAS_TAB_COMPLETION (1 << 0)
#define RL_OPT_HAS_HISTORY_COMPLETION (1 << 1)

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

#define RL_TOK_COMMAND (1 << 0)
#define RL_TOK_ARG (1 << 1)
#define RL_TOK_ARG_SHORT (1 << 2)
#define RL_TOK_ARG_LONG (1 << 3)
#define RL_TOK_ARG_OPTION (1 << 4)
#define RL_TOK_ARG_FILENAME (1 << 5)

#define RL_VED_ARG_FILENAME (1 << 0)
#define RL_VED_ARG_RANGE (1 << 1)
#define RL_VED_ARG_GLOBAL (1 << 2)
#define RL_VED_ARG_PATTERN (1 << 3)
#define RL_VED_ARG_SUB (1 << 4)
#define RL_VED_ARG_INTERACTIVE (1 << 5)
#define RL_VED_ARG_APPEND (1 << 6)
#define RL_VED_ARG_BUFNAME (1 << 7)
#define RL_VED_ARG_ANYTYPE (1 << 8)

#define ACCEPT_TAB_WHEN_INSERT (1 << 0)

#define FILE_IS_REGULAR  (1 << 0)
#define FILE_IS_RDONLY   (1 << 1)
#define FILE_EXISTS      (1 << 2)
#define FILE_IS_READABLE (1 << 3)
#define FILE_IS_WRITABLE (1 << 4)
#define BUF_IS_MODIFIED  (1 << 5)
#define BUF_IS_VISIBLE   (1 << 6)
#define BUF_IS_RDONLY    (1 << 7)
#define BUF_IS_PAGER     (1 << 8)
#define BUF_IS_SPECIAL   (1 << 9)
#define BUF_FORCE_REOPEN (1 << 10)

/* buf is already open 
  instances {...} */

#define VUNDO_RESET (1 << 0)

#define X_PRIMARY     0
#define X_CLIPBOARD   1

#define DEFAULT_ORDER  0
#define REVERSE_ORDER -1

#define AT_CURRENT_FRAME -1

#define VERBOSE_OFF 0
#define VERBOSE_ON 1

#define NO_COMMAND 0
#define NO_OPTION 0

#define NO_APPEND 0
#define APPEND 1

#define NO_FORCE 0
#define FORCE 1

#define NO_COUNT_SPECIAL 0
#define COUNT_SPECIAL 1

#define DONOT_OPEN_FILE_IFNOT_EXISTS 0
#define OPEN_FILE_IFNOT_EXISTS 1

#define DONOT_REOPEN_FILE_IF_LOADED 0
#define REOPEN_FILE_IF_LOADED 1

#define SHARED_ALLOCATION 0
#define NEW_ALLOCATION 1

#define DONOT_ABORT_ON_ESCAPE 0
#define ABORT_ON_ESCAPE 1

enum {
  ERROR = -2,
  NOTHING_TODO = -1,
  DONE = 0,
  NEWCHAR,
  EXIT,
  WIN_EXIT,
  BUF_EXIT,
  BUF_QUIT,
};

enum {
  VED_COM_BUF_CHANGE_NEXT = 0,
  VED_COM_BUF_CHANGE_NEXT_ALIAS,
  VED_COM_BUF_CHANGE_PREV_FOCUSED,
  VED_COM_BUF_CHANGE_PREV_FOCUSED_ALIAS,
  VED_COM_BUF_CHANGE_PREV,
  VED_COM_BUF_CHANGE_PREV_ALIAS,
  VED_COM_BUF_DELETE_FORCE,
  VED_COM_BUF_DELETE_FORCE_ALIAS,
  VED_COM_BUF_DELETE,
  VED_COM_BUF_DELETE_ALIAS,
  VED_COM_BUF_CHANGE,
  VED_COM_BUF_CHANGE_ALIAS,
  VED_COM_DIFF_BUF,
  VED_COM_DIFF,
  VED_COM_EDIT_FORCE,
  VED_COM_EDIT_FORCE_ALIAS,
  VED_COM_EDIT,
  VED_COM_EDIT_ALIAS,
  VED_COM_ENEW,
  VED_COM_ETAIL,
  VED_COM_GREP,
  VED_COM_MESSAGES,
  VED_COM_QUIT_FORCE,
  VED_COM_QUIT_FORCE_ALIAS,
  VED_COM_QUIT,
  VED_COM_QUIT_ALIAS,
  VED_COM_READ,
  VED_COM_READ_ALIAS,
  VED_COM_READ_SHELL,
  VED_COM_REDRAW,
  VED_COM_SEARCHES,
  VED_COM_SHELL,
  VED_COM_SPLIT,
  VED_COM_SUBSTITUTE,
  VED_COM_SUBSTITUTE_WHOLE_FILE_AS_RANGE,
  VED_COM_SUBSTITUTE_ALIAS,
  VED_COM_TEST_KEY,
  VED_COM_WIN_CHANGE_NEXT,
  VED_COM_WIN_CHANGE_NEXT_ALIAS,
  VED_COM_WIN_CHANGE_PREV_FOCUSED,
  VED_COM_WIN_CHANGE_PREV_FOCUSED_ALIAS,
  VED_COM_WIN_CHANGE_PREV,
  VED_COM_WIN_CHANGE_PREV_ALIAS,
  VED_COM_WRITE_FORCE,
  VED_COM_WRITE_FORCE_ALIAS,
  VED_COM_WRITE,
  VED_COM_WRITE_ALIAS,
  VED_COM_WRITE_QUIT_FORCE,
  VED_COM_WRITE_QUIT,
  VED_COM_END,
};

#define MARKS          "`abcdghjklqwertyuiopzxcvbnm1234567890"
#define NUM_MARKS      37
#define MARK_UNAMED    0

/* this produce wrong results when compiled with tcc
  char *r = strchr (REGISTERS, register); r - REGISTERS;
 */

#define REGISTERS      "\"/:%*+=abcdghjklqwertyuiopzxcvbnm1234567890^_\n"
#define NUM_REGISTERS  46

enum {
  REG_UNAMED = 0,
  REG_SEARCH,
  REG_PROMPT,
  REG_FNAME,
  REG_STAR,
  REG_PLUS,
  REG_EXPR,
  REG_CURWORD = NUM_REGISTERS - 3,
  REG_BLACKHOLE,
  REG_RDONLY
};

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

#define SYS_ERRORS "\
1:  EPERM     Operation not permitted,\
2:  ENOENT    No such file or directory,\
3:  ESRCH     No such process,\
4:  EINTR     Interrupted system call,\
5:  EIO       I/O error,\
6:  ENXIO     No such device or address,\
7:  E2BIG     Argument list too long,\
8:  ENOEXEC   Exec format error,\
9:  EBADF     Bad file number,\
10: ECHILD    No child processes,\
11: EAGAIN    Try again,\
12: ENOMEM    Out of memory,\
13: EACCES    Permission denied,\
14: EFAULT    Bad address,\
15: ENOTBLK   Block device required,\
16: EBUSY     Device or resource busy,\
17: EEXIST    File exists,\
18: EXDEV     Cross-device link,\
19: ENODEV    No such device,\
20: ENOTDIR   Not a directory,\
21: EISDIR    Is a directory,\
22: EINVAL    Invalid argument,\
23: ENFILE    File table overflow,\
24: EMFILE    Too many open files,\
25: ENOTTY    Not a typewriter,\
26: ETXTBSY   Text file busy,\
27: EFBIG     File too large,\
28: ENOSPC    No space left on device,\
29: ESPIPE    Illegal seek,\
30: EROFS     Read-only file system,\
31: EMLINK    Too many links,\
32: EPIPE     Broken pipe,\
33: EDOM      Math argument out of domain of func,\
34: ERANGE    Math result not representable,\
35: EDEADLK   Resource deadlock would occur,\
36: ENAMETOOLONG File name too long,\
37: ENOLCK    No record locks available,\
38: ENOSYS    Invalid system call number,\
39: ENOTEMPTY Directory not empty,\
40: ELOOP     Too many symbolic links encountered"

#define ED_ERRORS "\
-3: RE_UNBALANCHED_BRACKETS_ERROR Unbalanced brackets in the pattern,\
-20: RL_ARG_AWAITING_STRING_OPTION_ERROR Awaiting a string after =,\
-21: RL_ARGUMENT_MISSING_ERROR Awaiting argument after dash,\
-22: RL_UNTERMINATED_QUOTED_STRING_ERROR Quoted String is unterminated,\
-23: RL_UNRECOGNIZED_OPTION Unrecognized option,\
-1000: INDEX_ERROR Index is out of range,\
-1001: NULL_PTR_ERROR NULL Pointer,\
-1002: INTEGEROVERFLOW_ERROR Integer overflow"

enum {
  MSG_FILE_EXISTS_AND_NO_FORCE = 1,
  MSG_FILE_EXISTS_BUT_IS_NOT_A_REGULAR_FILE,
  MSG_FILE_EXISTS_BUT_IS_AN_OBJECT_FILE,
  MSG_FILE_EXISTS_AND_IS_A_DIRECTORY,
  MSG_FILE_IS_NOT_READABLE,
  MSG_FILE_IS_LOADED_IN_ANOTHER_BUFFER,
  MSG_FILE_REMOVED_FROM_FILESYSTEM,
  MSG_FILE_HAS_BEEN_MODIFIED,
  MSG_BUF_IS_READ_ONLY,
  MSG_ON_WRITE_BUF_ISNOT_MODIFIED_AND_NO_FORCE,
  MSG_ON_BD_IS_MODIFIED_AND_NO_FORCE,
  MSG_CAN_NOT_WRITE_AN_UNAMED_BUFFER,
  MSG_CAN_NOT_DETERMINATE_CURRENT_DIR
};

#define ED_MSGS_FMT "\
1:file %s: exists, use w! to overwrite.\
2:file %s: exists but is not a regular file.\
3:file %s: exists but is an object (elf) file.\
4:file %s: exists and is a directory.\
5:file %s: is not readable.\
6:file %s: is loaded in another buffer.\
7:[Warning]%s: removed from filesystem since last operation.\
8:[Warning]%s: has been modified since last operation.\
9:buffer is read only.\
10:buffer has not been modified, use w! to force.\
11:buffer has been modified, use bd! to delete it without writing.\
12:can not write an unamed buffer.\
13:can not get current directory!!!."

#define MSG_ERRNO(errno__) \
  My(Msg).error ($my(root), My(Error).string ($my(root), errno__))

#define VED_MSG_ERROR(err__, ...) \
  My(Msg).error ($my(root), My(Msg).fmt ($my(root), err__, ##__VA_ARGS__))

#define MSG(fmt, ...) \
  My(Msg).send_fmt ($my(root), COLOR_NORMAL, fmt, ##__VA_ARGS__)

#define MSG_ERROR(fmt, ...) \
  My(Msg).error ($my(root), fmt, ##__VA_ARGS__)

NewProp (term,
  term_T *Me;
  struct termios orig_mode;
  struct termios raw_mode;

  char
     mode,
    *name;

  int
    in_fd,
    out_fd;

  int orig_curs_row_pos;
  int orig_curs_col_pos;
  int lines;
  int columns;
);

NewType (term,
  Prop (term) *prop;
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

NewType (vchar,
  utf8 code;
  char buf[5];
  int  len;
  int  width;
  vchar_t *next;
  vchar_t *prev;
);

NewType (line,
  vchar_t *head;
  vchar_t *tail;
  vchar_t *current;
      int  cur_idx;
      int  num_items;
      int  len;
);

NewType (dirlist,
  vstr_t *list;
  char dir[PATH_MAX];
  void (*free) (dirlist_t *);
);

NewType (tmpname,
  int fd;
  string_t *fname;
);

NewType (video,
  vstring_t *head;
  vstring_t *tail;
  vstring_t *current;
        int  cur_idx;
        int  num_items;

  string_t *render;

  int  fd;
      int  num_cols;
      int  num_rows;
      int  first_row;
      int  first_col;
      int  last_row;
      int  row_pos;
      int  col_pos;

      int rows[MAX_SCREEN_ROWS];
);

NewType (arg,
  int type;
  int type_val;
  string_t *option;
  string_t *val;
  string_t *data;

  arg_t *next;
  arg_t *prev;
);

NewType (rlcom,
  char *com;
  char **args;
);

NewType (rline,
  char prompt_char;

  int
    rows,
    row_pos,
    prompt_row,
    num_cols,
    num_rows,
    first_row,
    first_col,
    com,
    range[2],
    state,
    opts;

  utf8 c;
  term_t *term;
  ed_t *ed;

  int fd;

  video_t *cur_video;

  vstr_t *line;
   arg_t *head;
   arg_t *tail;
   arg_t *current;
     int  cur_idx;
     int  num_items;

  string_t *render;

  rlcom_t **commands;

  utf8 (*getch) (term_t *);
  int (*at_beg) (rline_t **);
  int (*at_end) (rline_t **);
  int (*tab_completion) (rline_t *);
);

NewType (menu,
  int  fd;
  int
    num_cols,
    num_rows,
    first_row,
    last_row,
    first_col,
    last_col,
    row_pos,
    col_pos,
    min_first_row,
    space_selects,
    return_if_one_item,
    state,
    orig_first_row,
    orig_num_rows;

  utf8 c;
  vstr_t  *list;
    char   pat[MAXPATLEN];
     int   patlen;

  string_t *header;
  video_t *cur_video;
  int (*process_list) (menu_t *);
  int retval;
  buf_t *this;
);

NewType (reg,
  string_t *data;
  reg_t *next;
  reg_t *prev;
);

NewType (rg,
  reg_t *head;
  char reg;
  int  type;
  int  cur_col_idx;
  int  first_col_idx;
  int  col_pos;
);

NewType (mark,
  char mark;
  int  cur_idx;
  int  cur_col_idx;
  int  first_col_idx;
  int  row_pos;
  int  col_pos;
  int  video_first_row_idx;
  row_t *video_first_row;
);

NewType (act,
  act_t *next;
  act_t *prev;

  int  idx;
  int  cur_idx;
  int  cur_col_idx;
  int  first_col_idx;
  int  row_pos;
  int  col_pos;
  int  video_first_row_idx;
  row_t *video_first_row;

  char type;
  char *bytes;
  string_t *__bytes;
  int  num_bytes;
);

NewType (action,
  act_t *head;

  action_t *next;
  action_t *prev;
);

NewType (undo,
  action_t *head;
  action_t *current;
  action_t *tail;
       int  num_items;
       int  cur_idx;
       int  state;
);

NewType (vis,
  int fidx;
  int lidx;
  int orig_idx;
  char  *(*orig_syn_parser) (buf_t *, char *, int, int, row_t *);
);

NewType (sch,
  row_t *row;
  int    idx;
  sch_t *next;
);

NewType (search,
  sch_t *head;

  string_t *pat;
  row_t *row;
  int  idx;
  int  cur_idx;
  int  col;
  int  found;
  int  dir;
  char *prefix;
  char *match;
  char *end;
);

NewType (histitem,
  string_t *data;
  histitem_t *next;
  histitem_t *prev;
);

NewType (h_search,
  histitem_t *head;
  histitem_t *tail;
         int  num_items;
         int  cur_idx;
);

NewType (h_rlineitem,
  h_rlineitem_t *next;
  h_rlineitem_t *prev;
  rline_t *data;
);

NewType (h_rline,
  h_rlineitem_t *head;
  h_rlineitem_t *tail;
  h_rlineitem_t *current;
            int  num_items;
            int  cur_idx;
            int  history_idx;
);

NewType (hist,
  h_search_t *search;
  h_rline_t  *rline;
);

#define MY_PROPERTIES            \
  dim_t *dim;                    \
  video_t *video;                \
    int  num_items

#define MY_CLASSES(__me__)       \
  Class (__me__) *Me;            \
  Class (video) *Video;          \
  Class (term) *Term;            \
  Class (cstring) *Cstring;      \
  Class (string) *String;        \
  Class (re) *Re;                \
  Class (input) *Input;          \
  Class (screen) *Screen;        \
  Class (cursor) *Cursor;        \
  Class (msg) *Msg;              \
  Class (error) *Error

NewType (dim,
  int
    first_row,
    last_row,
    num_rows,
    first_col,
    last_col,
    num_cols;
);

NewType (buf,
  row_t  *head;
  row_t  *tail;
  row_t  *current;
    int   cur_idx;
    int   num_items;

  Prop (buf) *prop;

  void (*free) (buf_t *);
  int (*on_normal_beg) (buf_t **, utf8, int *, int);
  int (*on_normal_end) (buf_t **, utf8, int *, int);

  buf_t  *next;
  buf_t  *prev;
);

NewType (win,
  Prop (win) *prop;

  buf_t *head;
  buf_t *tail;
  buf_t *current;
    int  cur_idx;
    int  prev_idx;
    int  num_items;

  win_t *next;
  win_t *prev;
);

NewType (ed,
  win_t *head;
  win_t *tail;
  win_t *current;
    int  cur_idx;
    int  prev_idx;
    int  num_items;

  int name_gen;

  Prop (ed) *prop;

  ed_t *next;
  ed_t *prev;
);

NewType (row,
  string_t *data;

  int
    first_col_idx,
    cur_col_idx;

   row_t *next;
   row_t *prev;
);

NewType (syn,
  char  *file_type;
  char **file_match;
  char **keywords;
  char  *operators;
  char  *singleline_comment_start;
  char  *multiline_comment_start;
  char  *multiline_comment_end;
  int    hl_strings;
  int    hl_numbers;
  char  *(*parse) (buf_t *, char *, int, int, row_t *);
  ftype_t *(*init) (buf_t *);
  int state;
);

NewType (ftype,
  char name[8];
  char on_emptyline[2];

  int shiftwidth;
  int autochdir;

  int
    tab_indents,
    cr_on_normal_is_like_insert_mode,
    backspace_on_normal_is_like_insert_mode,
    backspace_on_first_idx_remove_trailing_spaces,
    space_on_normal_is_like_insert_mode,
    small_e_on_normal_goes_insert_mode,
    read_from_shell;

  string_t *(*autoindent) (buf_t *, row_t *);
);

NewProp (buf,
  MY_PROPERTIES;
  MY_CLASSES (buf);
  ed_T  *Ed;
  win_T *Win;

  ed_t  *root;
  win_t *parent;
  term_t *term_ptr;
  row_t *video_first_row;
  syn_t *syn;
  ftype_t *ftype;
  undo_t *undo;
  undo_t *redo;
  rg_t *regs;
  mark_t marks[NUM_MARKS];
  hist_t *history;
  vis_t vis[2];
  string_t *last_insert;

  char
    *fname,
    *basename,
    *extname,
    *cwd,
     mode[16];

  int
    fd,
    state,
    at_frame,
    video_first_row_idx,
    is_visible,
    flags,
    cur_lnr,
    cur_col,
    cur_video_row,
    cur_video_col,
    statusline_row;

  int
    *msg_row_ptr,
    *prompt_row_ptr;

  line_t *line;
  size_t num_bytes;
  string_t *statusline;
  string_t *promptline;

  struct stat st;

  int shared_int;
  string_t *shared_str;
);

NewProp (win,
  char *name;
  int flags;
  int type;

  MY_PROPERTIES;
  MY_CLASSES (win);
  buf_T *Buf;
  ed_T  *Ed;

  int
    has_promptline,
    has_msgline,
    has_topline;

  int
    min_rows,
    has_dividers,
    max_frames,
    num_frames,
    cur_frame;

   ed_t  *parent;
   dim_t **frames_dim;
);

NewType (env,
  pid_t pid;
  uid_t uid;
  gid_t gid;

  string_t
     *home_dir,
     *tmp_dir,
     *diff_exec,
     *xclip_exec,
     *path,
     *display;
);

NewProp (ed,
  char name[8];
  int  state;

  MY_PROPERTIES;
  MY_CLASSES (ed);
  buf_T *Buf;
  win_T *Win;

  env_t *env;
  char *saved_cwd;

  int
    has_promptline,
    has_msgline,
    has_topline,
    max_wins,
    max_num_hist_entries,
    max_num_undo_entries;

  int
    msg_row,
    prompt_row,
    msg_send;

  term_t *term;

  string_t *last_insert;
  string_t *msgline;
  string_t *topline;
  string_t *shared_str;
  hist_t *history;
  rg_t regs[NUM_REGISTERS];

  char *lw_mode_actions, *cw_mode_actions, *bw_mode_actions;
  utf8 *lw_mode_chars, *cw_mode_chars;
  int   lw_mode_chars_len, cw_mode_chars_len;
  int (*lw_mode_cb) (buf_t *, vstr_t *, utf8);
  int (*cw_mode_cb) (buf_t *, string_t *, utf8);
);

#undef MY_CLASSES
#undef MY_PROPERTIES

private int ved_quit (ed_t *, int);
private int ved_normal_goto_linenr (buf_t *, int);
private int ved_normal_down (buf_t *, int, int, int);
private int ved_normal_bol (buf_t *);
private int ved_insert (buf_t *, utf8);
private int ved_write_buffer (buf_t *, int);
private int ved_split (buf_t **, char *);
private int ved_enew_fname (buf_t **, char *);
private int ved_edit_fname (buf_t **, char *, int, int, int, int);
private int ved_write_to_fname (buf_t *, char *, int, int, int, int, int);
private int ved_open_fname_under_cursor (buf_t **, int, int, int);
private int ved_buf_change_bufname (buf_t **, char *);
private int ved_buf_change (buf_t **, int);
private int ved_rline (buf_t **, rline_t *);
private rline_t *ved_rline_new (ed_t *, term_t *, utf8 (*getch) (term_t *), int, int, int, video_t *);
private rline_t *rline_new (ed_t *, term_t *, utf8 (*getch) (term_t *), int, int, int, video_t *);
private rline_t *rline_edit (rline_t *);
private void rline_write_and_break (rline_t *);
private void rline_free (rline_t *);
private void rline_clear (rline_t *);
private int  rline_break (rline_t **);
private string_t *input_box (buf_t *, int, int, int);
private utf8 quest (buf_t *, char *, utf8 *, int);
private action_t *vundo_pop (buf_t *);
private void ed_suspend (ed_t *);
private void ed_resume (ed_t *);
private int ed_win_change (ed_t *, buf_t **, int, char *, int);
private int fd_read (int, char *, size_t);

/* this code belongs to? */
static const utf8 offsetsFromUTF8[6] = {
  0x00000000UL, 0x00003080UL, 0x000E2080UL,
  0x03C82080UL, 0xFA082080UL, 0x82082080UL
};
/* the only reference found from the last research,
 * was at the julia programming language sources,
 * (this code and the functions that make use of it,
 * is atleast 4/5 years old, lying (during a non network season))
 */

#define ONE_PAGE ($my(dim->num_rows) - 1)
#define ARRLEN(arr) (sizeof(arr) / sizeof((arr)[0]))
#define isnotutf8(c) IS_UTF8 (c) == 0
#define isnotatty(fd__) (0 == isatty ((fd__)))
#define IsNotDirSep(c) (c != '/')
#define IsSeparator(c)                          \
  ((c) is ' ' or (c) is '\t' or (c) is '\0' or  \
   byte_in_str(",.()+-/=*~%<>[]:;", (c)) isnot NULL)

#define IsAlsoAHex(c) (((c) >= 'a' and (c) <= 'f') or ((c) >= 'A' and (c) <= 'F'))
#define IsAlsoANumber(c) ((c) is '.' or (c) is 'x' or IsAlsoAHex (c))
#define Notword ".,?/+*-=~%<>[](){}\\'\";"
#define Notword_len 22
#define Notfname "|\""
#define Notfname_len 2

#define debug_append(fmt, ...)                            \
({                                                        \
  char *file_ = str_fmt ("/tmp/%s.debug", __func__);      \
  FILE *fp_ = fopen (file_, "a+");                        \
  if (fp_ isnot NULL) {                                   \
    fprintf (fp_, (fmt), ## __VA_ARGS__);                 \
    fclose (fp_);                                         \
  }                                                       \
})

#define str_fmt(fmt, ...)                                 \
({                                                        \
  char buf_[MAXLINE];                                     \
  snprintf (buf_, MAXLINE, fmt, __VA_ARGS__);             \
  buf_;                                                   \
})

#define utf8_code(s_)                                     \
({                                                        \
  int code = 0; int i_ = 0; int sz = 0;                   \
  do {code <<= 6; code += (uchar) s_[i_++]; sz++;}        \
  while (s_[i_] and IS_UTF8(s_[i_]));                     \
                                                          \
  code -= offsetsFromUTF8[sz-1];                          \
  code;                                                   \
})

#define CUR_UTF8_CODE                                     \
({                                                        \
  char *s_ = $mycur(data)->bytes + $mycur(cur_col_idx);   \
  int code = 0; int i_ = 0; int sz = 0;                   \
  do {code <<= 6; code += (uchar) s_[i_++]; sz++;}        \
  while (s_[i_] and IS_UTF8(s_[i_]));                     \
                                                          \
  code -= offsetsFromUTF8[sz-1];                          \
  code;                                                   \
})

#define BUF_GET_NUMBER(intbuf_, idx_)                     \
({                                                        \
  utf8 cc__;                                              \
  while ((idx_) < 8) {                                    \
    cc__ = My(Term).Input.get ($my(term_ptr));            \
                                                          \
    if (IS_DIGIT (cc__))                                  \
      (intbuf_)[(idx_)++] = cc__;                         \
    else                                                  \
      break;                                              \
  }                                                       \
  cc__;                                                   \
})

#define BUF_GET_AS_NUMBER(has_pop_pup, frow, fcol, lcol, prefix)          \
({                                                                        \
  utf8 cc__;                                                              \
  int nr = 0;                                                             \
  string_t *ibuf = NULL, *sbuf = NULL;                                    \
  if (has_pop_pup) {                                                      \
     ibuf = My(String).new_with (""); sbuf = My(String).new_with ("");    \
  }                                                                       \
  while (1) {                                                             \
    if (has_pop_pup) {                                                    \
      My(String).replace_with_fmt (sbuf, "%s %s", prefix, ibuf->bytes);   \
      video_paint_rows_with ($my(video), frow, fcol, lcol, sbuf->bytes);  \
      SEND_ESC_SEQ ($my(video)->fd, TERM_CURSOR_HIDE);                    \
    }                                                                     \
    cc__ = My(Term).Input.get ($my(term_ptr));                            \
                                                                          \
    if (IS_DIGIT (cc__)) {                                                \
      nr = (10 * nr) + (cc__ - '0');                                      \
      if (has_pop_pup) My(String).append_byte (ibuf, cc__);               \
    } else                                                                \
      break;                                                              \
  }                                                                       \
  if (has_pop_pup) {                                                      \
    My(String).free (ibuf); My(String).free (sbuf);                       \
    video_resume_painted_rows ($my(video));                               \
    SEND_ESC_SEQ ($my(video)->fd, TERM_CURSOR_SHOW);                      \
  }                                                                       \
  nr;                                                                     \
})

#define BYTES_TO_RLINE(rl_, bytes, len)                                   \
do {                                                                      \
  char *sp_ = (bytes);                                                    \
  for (int i__ = 0; i__ < (len); i__++) {                                 \
    int clen = char_byte_len ((bytes)[i__]);                              \
    (rl_)->state |= (RL_INSERT_CHAR|RL_BREAK);                            \
    (rl_)->c = utf8_code (sp_);                                           \
    rline_edit ((rl_));                                                   \
    i__ += clen - 1;                                                      \
    sp_ += clen;                                                          \
    }                                                                     \
} while (0)

#define IS_MODE(mode__) str_eq ($my(mode), (mode__))

#define HAS_THIS_LINE_A_TRAILING_NEW_LINE \
({$mycur(data)->bytes[$mycur(data)->num_bytes - 1] is '\n';})

#define RM_TRAILING_NEW_LINE                                        \
  if ($mycur(data)->bytes[$mycur(data)->num_bytes - 1] is '\n' or   \
      $mycur(data)->bytes[$mycur(data)->num_bytes - 1] is 0)        \
     My(String).clear_at ($mycur(data), $mycur(data)->num_bytes - 1)

#define ADD_TRAILING_NEW_LINE                                        \
  if ($mycur(data)->bytes[$mycur(data)->num_bytes - 1] isnot '\n')   \
    My(String).append ($mycur(data), "\n")

#define stack_free(list, type)                                      \
do {                                                                \
  type *item = (list)->head;                                        \
  while (item != NULL) {                                            \
    type *tmp = item->next;                                         \
    free (item);                                                    \
    item = tmp;                                                     \
  }                                                                 \
} while (0)

#define stack_push(list, node)                                      \
({                                                                  \
  if ((list)->head == NULL) {                                       \
    (list)->head = (node);                                          \
    (list)->head->next = NULL;                                      \
  } else {                                                          \
    (node)->next = (list)->head;                                    \
    (list)->head = (node);                                          \
  }                                                                 \
                                                                    \
 list;                                                              \
})

#define stack_pop(list, type)                                       \
({                                                                  \
  type *node = (list)->head;                                        \
  if (node != NULL)                                                 \
    (list)->head = (list)->head->next;                              \
                                                                    \
  node;                                                             \
})

#define list_push(list, node)                                       \
({                                                                  \
  if ((list)->head == NULL) {                                       \
    (list)->head = (node);                                          \
    (list)->head->next = NULL;                                      \
    (list)->head->prev = NULL;                                      \
  } else {                                                          \
    (list)->head->prev = (node);                                    \
    (node)->next = (list)->head;                                    \
    (list)->head = (node);                                          \
  }                                                                 \
                                                                    \
 (list)->num_items++;                                               \
 list;                                                              \
})

#define current_list_prepend(list, node)                            \
({                                                                  \
  if ((list)->current is NULL) {                                    \
    (list)->head = (node);                                          \
    (list)->tail = (node);                                          \
    (list)->cur_idx = 0;                                            \
    (list)->current = (list)->head;                                 \
  } else {                                                          \
    if ((list)->cur_idx == 0) {                                     \
      (list)->head->prev = (node);                                  \
      (node)->next = (list)->head;                                  \
      (list)->head = (node);                                        \
      (list)->current = (list)->head;                               \
    } else {                                                        \
      (list)->current->prev->next = (node);                         \
      (list)->current->prev->next->next = (list)->current;          \
      (list)->current->prev->next->prev = (list)->current->prev;    \
      (list)->current->prev = (list)->current->prev->next;          \
      (list)->current = (list)->current->prev;                      \
    }                                                               \
  }                                                                 \
                                                                    \
  (list)->num_items++;                                              \
  (list)->current;                                                  \
})

#define current_list_append(list, node)                             \
({                                                                  \
  if ((list)->current is NULL) {                                    \
    (list)->head = (node);                                          \
    (list)->tail = (node);                                          \
    (list)->cur_idx = 0;                                            \
    (list)->current = (list)->head;                                 \
  } else {                                                          \
    if ((list)->cur_idx is (list)->num_items - 1) {                 \
      (list)->current->next = (node);                               \
      (node)->prev = (list)->current;                               \
      (list)->current = (node);                                     \
      (node)->next = NULL;                                          \
      (list)->cur_idx++;                                            \
      (list)->tail = (node);                                        \
    } else {                                                        \
      (node)->next = (list)->current->next;                         \
      (list)->current->next = (node);                               \
      (node)->prev = (list)->current;                               \
      (node)->next->prev = (node);                                  \
      (list)->current = (node);                                     \
      (list)->cur_idx++;                                            \
    }                                                               \
  }                                                                 \
                                                                    \
  (list)->num_items++;                                              \
  (list)->current;                                                  \
})

#define list_pop_tail(list, type)                                   \
({                                                                  \
type *node = NULL;                                                  \
do {                                                                \
  if ((list)->tail is NULL) break;                                  \
  node = (list)->tail;                                              \
  (list)->tail->prev->next = NULL;                                  \
  (list)->tail = (list)->tail->prev;                                \
  (list)->num_items--;                                              \
} while (0);                                                        \
  node;                                                             \
})

#define current_list_pop(list, type)                                \
({                                                                  \
type *node = NULL;                                                  \
do {                                                                \
  if ((list)->current is NULL) break;                               \
  node = (list)->current;                                           \
  if (1 is (list)->num_items) {                                     \
    (list)->head = NULL;                                            \
    (list)->tail = NULL;                                            \
    (list)->current = NULL;                                         \
    break;                                                          \
  }                                                                 \
  if (0 is (list)->cur_idx) {                                       \
    (list)->current = (list)->current->next;                        \
    (list)->current->prev = NULL;                                   \
    (list)->head = (list)->current;                                 \
    break;                                                          \
  }                                                                 \
  if ((list)->cur_idx is (list)->num_items - 1) {                   \
    (list)->current = (list)->current->prev;                        \
    (list)->current->next = NULL;                                   \
    (list)->cur_idx--;                                              \
    (list)->tail = (list)->current;                                 \
    break;                                                          \
  }                                                                 \
  (list)->current->next->prev = (list)->current->prev;              \
  (list)->current->prev->next = (list)->current->next;              \
  (list)->current = (list)->current->next;                          \
} while (0);                                                        \
  if (node isnot NULL) (list)->num_items--;                         \
  node;                                                             \
})

#define current_list_set(list, idx)                                 \
({                                                                  \
  int idx_ = idx;                                                   \
  do {                                                              \
    if (0 > idx_) idx_ += (list)->num_items;                        \
    if (idx_ < 0 or idx_ >= (list)->num_items) {                    \
      idx_ = INDEX_ERROR;                                           \
      break;                                                        \
    }                                                               \
    if (idx_ is (list)->cur_idx) break;                             \
    int idx__ = (list)->cur_idx;                                    \
    (list)->cur_idx = idx_;                                         \
    if (idx__ < idx_)                                               \
      while (idx__++ < idx_)                                        \
        (list)->current = (list)->current->next;                    \
    else                                                            \
      while (idx__-- > idx_)                                        \
        (list)->current = (list)->current->prev;                    \
  } while (0);                                                      \
  idx_;                                                             \
})

/* from man printf(3) Linux Programmer's Manual */
/*
#define VA_ARGS_FMT_SIZE                                            \
({                                                                  \
  int size = 0;                                                     \
  va_list ap; va_start(ap, fmt);                                    \
  size = vsnprintf(NULL, size, fmt, ap);                            \
  va_end(ap);                                                       \
  size;                                                             \
})
 *
 * gcc complains on -Werror=alloc-size-larger-than= or -fsanitize=undefined,
 *  with:
 *  argument 1 range [18446744071562067968, 18446744073709551615]
 *  exceeds maximum object size 9223372036854775807
 *  in a call to built-in allocation function '__builtin_alloca_with_align'
 */

#define VA_ARGS_FMT_SIZE (MAXLINE * 2)
