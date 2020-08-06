```C
/*
  This is the groundwork code that serves, first as the underground keystone to support
  the foundation of a system, and secondly as constructor, since it writes this code,
  as foremost is an editor. An editor that develops first itself, from the very early
  days, and supposedly is built on conscience, as it implements a vi(m) like interface,
  though at the beginning coexisted happily with an ed interface.

  It was written (and this isnot almost a joke) somehow at a state in time, a little
  bit after the vt100 introduction and Billy Joy's first "vi", cheating of course a bit
  by knowing the future, from where i stole stable code, ideas, a C11 compiler, but
  basically the knowledge, that a character can be represented with more than one byte
  in a universal character set and with an encoding "yet to be invented" in a restaurant.
  But without a curses library and with really basic unicode unaware libc functions.

  It is written in the form of a library and theoretically, might be useful to some, as an
  independent text visual editor C library that can be embedded in an application, either
  by just "#include" it as a single file unit, or by linking to it as a shared or as a
  static library, without any other prerequisite other than libc; and with a tendency to
  minimize this dependency (whenever the chance) by providing either an own implementation of some
  standard C functions or pulling from the pool of the enormous open source C ecosystem;
  as such it can be used also in primitive environments.

  State:
  The current state is little beyond the bootstrap|prototype level as the basic
  interface is considered quite close to completion, however:

    - it doesn't do any validation of the incoming data (the data it produces
      is rather controllable, but it cannot handle (for instance) data which
      it might be malformed (UTF-8) byte sequences, or escape sequences).

      [update - end of August-2019] While still there is no check for invalid
      UTF-8 byte sequences when initially reading into the buffer structure, now
      it's possible to do it at any point, by using either a command or in visual
      linewise mode (see UTF-8 Validation section).

    - some motions shouldn't behave properly when a character it occupies more than
      one cell width, or behave properly if this character is a tab but (for now)
      a tab it takes one single cell, much like a space. To implement some of this
      functionality an expensive wcwidth() should be included and algorithms should
      adjust.

      [update - first days of September-2019] Both cases should be handled properly
      now, though the adjusted algorithms, complex as they are by nature, were quite
      invasive, and there is no certainity that handle all the conditions that can be
      met in a visual editor, because the width of the character dictates the cursor
      position (but see Tabwidth and Charwidth section for the semantics and details).

    - the code is young and fragile in places and some crude algorithms that had
      been used during this initiation, need revision and functions needs to catch
      more conditions.

      [update - last days of October-2019] While still many of those crude initial
      algorithms haven't been replaced, they happen to work! and probably will stay
      unchanged, though are quite complicated in places.

    - it doesn't try to catch conditions that don't make logical sense and expects
      a common workflow, and there is not really a desire to overflow the universe
      with conditional branches to catch conditions, that never met, and when met,
      there are ways to know where to meet them.

      [update - last days of October-2019] This is a philosophy that stands, but many
      conditions, especially during this last period, should be handled properly now
      (still should be quite more to catch, but most of them quite exotic, though still
       there are many that make sense to catch).

    - the highlighted system is ridiculously simple (note: this never is going to be
      nor ever intented as a general purpose editor; it strives to achieve this level of
      the functionality that can be considered productive but with quite few lines of code;
      as such is full of hand written calculations, which, yes, are compact but fragile)
      (but see CONCENTRATION section for details).

    - there are no tests to guarantee correctness and probably never will (it just follows,
      and as an excuse, the waterfall model).

    - there are no comments to the code, so it is probably hard to understand and hack happily
      (it is a rather idiomatic C, though i believe it makes the complicated code more
      readable, as it is based on intentions; but still hard to understand without comments).
      And usually most of the comments, are not code comments, they are just comments.

    - it is written and tested under a Linux environment, but also the code makes a lot of
      assumptions, as expects a POSIX environment.

    - the code requires at least 6 screen lines and it will result on segmentation
      fault on less (though i could catch the condition on startup or on sigwinch
      (but the constant environment is full screen terminals here, so this isn't an
      issue, that bothers me enough to write the code))

    - bugs that could result in data loss, so it's not smart, to use it with sensitive documents,
      but is a quite functional editor that runs really low in resources in full speed

    - the code is (possibly) a way too compact (though obviously in places there
      are ways to reduce verbosity or|and to reuse existing sources (to be a bit
      more compact):)), and for that good reason sometimes, it does more than it
      was destined to do, as such the only you can do is to pray to got the whole
      things right

    - in the early days i coded some state machines without even knowing that were
      actually state machines ( a lot of functions with states and switch and goto
      statements; very fragile code that seems to work properly though (minus the
      edge cases :))), and it was a couple of days ago, i actually realized that
      really this code was a state machine (but not properly implemented)

    - and a lot of goto's

  It is published of course for recording reasons and a personal one.

  The original purpose was to record the development process so it could be useful as
  a (document) reference for an editor implementation, but the git commits (during this
  initiation) ended as huge incremental commits and there were unusable to describe this
  "step by step" guide. So it was better to reset git and start fresh.
  Today, the git log messages are serving as a general reference document, where they
  describe intentions or semantics (both important, as the first helps the code to develop
  and the latter sets an expectation). Note that the commits were and probably will (if
  there is a will) rather big for the usual conventions. Hopefully the stabilization
  era is close and this is going to change.

  The project was initiated at the end of the autumn of 2018 and at the beginning
  was based at simted (below is the header of this single unit simted.c) and where
  the author implements a simple ed (many thanks).

  *  $name: Jason Wu$
  *  $email: jtywu@uvic.ca
  *  $sid: 0032670$
  *  $logon: jtywu$
  *  Simple Editor written in C using Linked List with undo

  Other code snippets from outer sources, should mention those sources on top of
  those blocks; if not, I'm sorry but this is probably an omission and should be
  fixed.

  The code constantly runs under valgrind, so it is supposed to have no
  memory leaks, which it is simple not true, because the conditions in a
  editor are too many to ever be sure and true. Also diagnostics seems to
  be dependable from compiler's version which valgrind's version should adjust.

  Also uncountable segmentation faults were diagnosed and fixed thanks to gdb.

  Buildind and Testing:
  The library can be compiled as a static or as a shared object, by at least gcc,
  clang and tinycc (likewise for the provided test application except that tcc can
  not provide a static executable). To compile the library and the test application
  using gcc as the default compiler (use the CC variable during compilation to change
  that) issue from the src directory of this distribution (every step implies a zero
  exit code to considered successful (catch that)):
 */

```
```sh

   cd src

   # Ordinary instructions (might bring some confusion):

   # build the shared library
   make shared

   # to build the sample executable that links against the shared object, issue:

   make veda-shared

   # for an easy and quick first impression, you can run the executable and open some
   # of those source files included in this distribution, issue:

   make run_shared

   # otherwise the LD_LIBRARY_PATH should be used to find the installed shared library,
   # like:

   LD_LIBRARY_PATH=sys/lib sys/bin/veda-02_shared

   # to run it under valgrind (works only with shared targets):

   make check_veda_memory_leaks

   # same, but to open another file from the filesystem and redirect valgrind's messages:

   make check_veda_memory_leaks FILE=/tmp/file 2>/tmp/valgrind-report

   # to run the shared executable under gdb:

   make debug_veda_shared

   # or the static one

   make debug_veda_static

   # to preproccess only

   make preproc

   # to clean up and start over

   make clean

   # Alternative method with just one build instruction:

   sh ./convenience.sh

   # to set the SYSDIR variable and use a custom directory:

   LIBVED_SYSDIR=$HOME/libved sh ./convenience.sh

   # or|and to use another C compiler other than gcc:

   CC=clang sh ./convenience.sh

   # Note that because there isn't a separate make install target, if you install
   # the distribution with SU rights, some of the touched files/directories quite
   # possible, might bring some troubles at later time, if you try again to build
   # with user rights. But the worst that can happen, is that during runtine, the
   # application might need to write in, or to read from, one of those installed
   # directories, like a temp directory (see below how to avoid this).

   # Produced hierarhy (after installation) (output at Mon 13 Jul 2020):
   $(SYSDIR)/bin/vedas
   $(SYSDIR)/bin/veda-02_shared
   $(SYSDIR)/include/libved+.h
   $(SYSDIR)/include/libved.h
   $(SYSDIR)/lib/libved.so
   $(SYSDIR)/lib/libved-0.2.so
   $(SYSDIR)/data/spell/Readme
   $(SYSDIR)/data/spell/spell.txt
   $(SYSDIR)/tmp

   # Notes:
   #  - for static targets, replace in the instructions "shared" with "static"
   #
   #  - libved.so is a symbolic link to the corresponded version
   #
   #  - vedas is a shell script wrapper, that calls the veda-02_shared executable,
   #    setting also LD_LIBRARY_PATH
   #
   #  - the data and tmp directories can be set to a custom directory, by using
   #    LIBVED_DATADIR and LIBVED_TMPDIR respectively. This is a prerequisite if
   #    you install the distribution with SU rights, as the application can write
   #    in both of those directories.
   #
   #  - if you build the static target, analogous library files will be produced,
   #    and the executable is installed as veda-02_static. In that case a symbolic
   #    link is created to static installed executable as veda (no LD_LIBRARY_PATH
   #    is needed here).
   #
   #

```
```C
/*
  All the compilation options (options that are passing to make):

  DEBUG=1|0              (en|dis)able debug flags (default 0)

  SYSDIR="dir"           this sets the system directory (default src/sys)
  LIBVED_DATADIR="dir"   this can be used for e.g., history (default $(SYSDIR)/data)
  LIBVED_TMPDIR="dir"    this sets the temp directory (default $(SYSDIR)/tmp)

  The following options can change/control the librart behavior.
  They are being used, to set the defaults during filetype initialization.

  CLEAR_BLANKLINES (1|0) this clear lines with only spaces and when the cursor
                         is on those lines (default 1)
  TAB_ON_INSERT_MODE_INDENTS (1|0) tab in insert mode indents (default 0)
  C_TAB_ON_INSERT_MODE_INDENTS (1|0) (default 1) (special case for C)
  TABWIDTH (width)       this set the default tabwidth (default 8)
  UNDO_NUM_ENTRIES (num) this set the undo entries (default 40)
  RLINE_HISTORY_NUM_ENTRIES (num) this set the readline num history commands (default 20)
  CARRIAGE_RETURN_ON_NORMAL_IS_LIKE_INSERT_MODE (1|0) on normal mode a carriage
                         return can behave, as it was in insert mode (default 1)
  SPACE_ON_NORMAL_IS_LIKE_INSERT_MODE (1|0) likewise (default 1)
  BACKSPACE_ON_NORMAL_IS_LIKE_INSERT_MODE (1|0) likewise (default 1)
  BACKSPACE_ON_FIRST_IDX_REMOVE_TRAILING_SPACES (1|0) when the cursor is on the
                         first column, backspace removes trailing ws (default 1)
  SMALL_E_ON_NORMAL_GOES_INSERT_MODE (1|0) 'e' in normal mode after operation
                         enters insert mode (default 1)

  The next option provides a way to extend the behavior and|or as an API
  documentation, and [wa]is intended for development, but it got so many features
  that it might be usefull, and now after so much time is recommended.

  HAS_USER_EXTENSIONS=1|0 (#in|ex)clude src/usr/usr.c (default 0)

  The above setting also introduce a prerequisite to SYS_NAME definition,
  which can be (and it is) handled and defined by the Makefile.

  HAS_LOCAL_EXTENSIONS=1|0 (#in|ex)clude src/local/local.c (default 0)

  Likewise with HAS_USER_EXTENSIONS, this option provides a way to extend
  the behavior, but this file is not visible in git and should be created
  by the user.
  As the last on the chain this can overide everything. This file should
  provide:

    private void __init_local__ (ed_t *this);
    private void __deinit_local__ (ed_t *this);

  (small emphasis) as the last in the chain, it can overide everything that
  is allowed to overwritten.

  The following options extend the application and any of them enables the
  HAS_USER_EXTENSIONS option.

  HAS_TCC=1|0      (en|dis)able tcc compiler (default 0) (note: requires libtcc)
  HAS_PROGRAMMING_LANGUAGE=1|0 (en|dis)able programming language (default 0)
  HAS_CURL=1|0     (en|dis)able libcurl, used by the PL above (default 0)
  Implemented, but used for now only in local code.
  HAS_JSON=1|0     (en|dis)able json support (defaulr 0)

  The following options extend the compiler flags (intended for -llib) and
  intended to be used mainly by the local namespace, but also to give some
  flexibility to the user.

  USER_EXTENSIONS_FLAGS, LOCAL_EXTENSIONS_FLAGS, VED_APPLICATION_FLAGS

  There two shell scripts located at the src directory, that makes easy to
  enable/disable options.

    - src/default.sh
    - src/convenience.sh

    the default.sh, sets the default options and is pretty minimal and intended
    for testing, while the convenience.sh does the opposite and is intented for
    usage. You can use it to compile the library and the executable simply as:

    sh default.sh
    or
    sh convenience.sh

  Note: It is not guaranteed that these three compilers will produce the same
  results. I didn't see any difference with gcc and tcc, but there was at least
  some color artifacts, with clang on visual mode in the past (but haven't tried
  recenctly). The fact is that i'm developing with gcc and tcc (i need this code
  to be compiled with tcc - tcc compiles this code (almost 12000++ lines) in less
  than a second, while gcc takes 18 and clang 24, in an old chromebook, that runs
  really low in power - because and of the nature of the application (fragile
  algorithms that might need just a single change, or because of a debug message,
  or just because of a tiny compilation error), compilations like these can
  happen (and if it was possible) sometimes 10 times in a couple of minutes, so
  tcc makes this possib[oll]!. Many Thanks guys on and for tcc development.))
     (note 1: since commit d7c2ccd of 8 October-2019, unfortunatelly this not true.
      While tcc can compile the shared library and the test application, without
      errors, the application segfaults, because of the call to stat(). This was
      reported upstream.)

     (note 2: since the introduction of tinyexpr, tcc can not compile the source
      of tinyexpr)

  But see at the very last section of this document (ERRORS - WARNINGS - BUGS),
  for issues/details.

  Platforms:
  The constant development environment is Void-Linux. It is also confirmed that
  builds and run on macOS. It would be desirable to built also on BSD's.

  Command line invocation:
    Usage: veda[s] [options] [filename]

       -h, --help            show this help message and exit

     Options:
       +, --line-nr=<int>    start at line number
       --column=<int>        set pointer at column
       --num-win=<int>       create new [num] windows
       --ftype=<str>         set the file type
       --autosave=<int>      interval time in minutes to autosave buffer
       --backupfile          backup file on initial reading
       --backup-suffix=<str> backup suffix (default: ~)
       --ex-com="command"    run an editor command at the startup (see Utility)
       --load-file=file      evaluate file with libved code (see Scripting)
       --pager               behave like a pager
       --exit                exit quickly (called after --ex-com and|or --load-file)

  Interface and Semantics:
  The is almost a vi[m] like interface and is based on modes, with some of the
  differences explained below to this document:

    - a topline (the first line on screen) that can be disabled, and which by
      default draws:

      current mode - filetype - pid - time

    - the last line on buffer is the statusline and by default draws:

      filename - line number/total lines - current idx - line len - char integer

    - the message line is the last line on screen; any message should be cleared
      after a keypress

    - the prompt row position is one before the message line

    - the command line grows to the top, if it doesn't fit on the line

    - insert/normal/visual/cline modes

  The structure in short:

    - Every buffer belongs to a window.

    - A window can have unlimited buffers.

    - A window can be splited in frames.

    - An Editor instance can have unlimited independed windows.

    - There can be unlimited independent editor instances that can be (de|rea)tached¹.

    and a little more detailed at the Structure Details section.

  Modes:

  These are mostly like vim and which of course lack much of the rich feature set
  of vim. That would require quite the double code i believe (actually much more
  than double).

  Normal mode:
 |
 |   key[s]          |  Semantics                     | count
 | __________________|________________________________|_______
 | CTRL-b, PG_UP     | scroll one page backward       | yes
 | CTRL-f, PG_DOWN   | scroll one page forward        | yes
 | HOME  gg          | home row                       |
 | END,  G           | end row                        |
 | h,l               | left|right cursor              | yes
 | ARROW[LEFT|RIGHT] | likewise                       | yes
 | k,j               | up|down line                   | yes
 | ARROW[UP|DOWN]    | likewise                       | yes
 | $                 | end of line                    |
 | 0                 | beginning of line              |
 | ^                 | first non blank character      |
 | count [gG]        | goes to line                   |
 | gf                | edit filename under the cursor |
 | e                 | end of word (goes insert mode) | yes
 | E                 | end of word                    | yes
 | ~                 | switch case                    |
 | m[mark]           | mark[a-z]                      |
 | `[mark]           | mark[a-z]                      |
 | CTRL-A            | increment (dec|octal|hex|char) | yes
 | CTRL-X            | decrement (likewise)           | yes
 | >, <              | indent [in|out]                | yes
 | [yY]              | yank [char|line]wise           | yes
 | [pP]              | put register                   |
 | d[d]              | delete line[s]                 | yes
 | d[g|HOME]         | delete lines to the beg of file|
 | d[G|END]          | delete lines to the end of file|
 | dw                | delete word                    |
 | cw                | change word                    |
 | ci[char]          | change inner text delimited by [char]|
 | x|DELETE          | delete character               | yes
 | D                 | delete to end of line          |
 | X|BACKSPACE       | delete character to the left   | yes
 | - BACKSPACE and if set and when current idx is 0, deletes trailing spaces|
 | - BACKSPACE and if set is like insert mode         |
 | r                 | replace character              |
 | C                 | delete to end of line (insert) |
 | J                 | join lines                     |
 | i|a|A|o|0         | insert mode                    |
 | u                 | undo                           |
 | CTRL-R            | redo                           |
 | CTRL-L            | redraw current window          |
 | V                 | visual linewise mode           |
 | v                 | visual characterize mode       |
 | CTRL-V            | visual blockwise mode          |
 | /                 | search forward                 |
 | ?                 | search backward                |
 | *                 | search current word forward    |
 | #                 | search current word backward   |
 | n                 | search next                    |
 | N                 | search Next (opposite)         |
 | CTRL-w            |                                |
 |   - CTRL-w        | frame forward                  |
 |   - w|j|ARROW_DOWN| likewise                       |
 |   - k|ARROW_UP    | frame backward                 |
 |   - o             | make current frame the only one|
 |   - s             | split                          |
 |   - n             | new window                     |
 |   - h|ARROW_LEFT  | window to the left             |
 |   - l|ARROW_RIGHT | window to the right            |
 |   - `             | previous focused window        |
 | g                 |                                |
 |   - g             | home row                       |
 |   - v             | visual linewise mode, with the |
 |                 previous selected rows if any      |
 |   - f             | open filename under the cursor |
 |        (on C filetype, can open header <header.h>) |
 | :                 | command line mode              |

 | Normal Mode Extensions or different behavior with vim.|
 | q                 | like :quit  if the buffer type is |
 |                     set to pager                      |
 | g                 |                                   |
 |   - b             | open link under the cursor to the |
 |             browser: requires the elinks text browser |
 |             to be installed, and it uses the -remote  |
 |             elinks option, so elinks should be running|
¹| CTRL-j            | detach editor and gives control to|
 |             the caller, it can be reatached with the  |
 |             exact status                              |
 | CTRL-O|CTRL-I     | jump to the previus|next location |
 |             to the jump list, (this differs from vim, |
 |             as this is like scrolling to the history) |
 |                                                       |
 | W                 | word operations mode (via a selection menu)|                            |
 | (implemented in the library)                          |
 |   - send `word' on XCLIPBOARD                         |
 |   - send `word' on XPRIMARY                           |
 |   - swap case                                         |
 |   - to lower                                          |
 |   - to upper                                          |
 | (extended by the test application)                    |
 |   - interpret `word' as a man page and display it to  |
 |     the scratch buffer (requires the man utility)     |
 |   - spell `word' (check if '`word' is mispelled)      |
 |                                                       |
 | F                 | File operations mode (via a selection menu)|
 |                                                       |
 | ,                 |                                   |
 |   - n             | like :bn (next buffer)            |  see Command mode
 |   - m             | like :bp (previous buffer)        |      -||-
 |   - ,             | like :b` (prev focused buffer)    |      -||-
 |   - .             | like :w` (prev focused window)    |      -||-
 |   - /             | like :winext   (next window)      |      -||-
 |   - ;             | like :ednext   (next editor)      |      -||-
 |   - '             | like :edprev   (prev editor)      |      -||-
 |   - l             | like :edprevfocused (prev focused ed)|   -||-

Insert mode:
 |
 |   key[s]          |  Semantics                     | count
 |___________________|________________________________|_______
 | CTRL-y            | complete based on the prec line|
 | CTRL-e            | complete based on the next line|
 | CTRL-a            | last insert                    |
 | CTRL-x            | completion mode                |
 |   - CTRL-l or l   | complete line                  |
 |   - CTRL-f or f   | complete filename              |
 | CTRL-n            | complete word                  |
 | CTRL-v            | insert character (utf8 code)   |
 | CTRL-k            | insert digraph                 |
 | CTRL-r            | insert register contents (charwise only) |
 | motion normal mode commands with some differences explained bellow|
 | HOME              | goes to the beginning of line  |
 | END               | goes to the end of line        |
 | escape            | aborts                         |

Visual mode:
 |   key[s]          |  Semantics                     | count
 |___________________|________________________________|_______
 | >,<               | indent [in|out]                | yes
 | d                 | delete selected                |
 | y                 | yank selected                  |
 | s                 | search selected [linewise]     |
 | w                 | write selected [linewise]      |
 | i|I               | insert in front [blockwise]    |
 | c                 | change [blockwise]             |
 | both commands above use a readline instance (but without tab|history completion)|
 | x|d               | delete [(block|char)wise]      |
 | +                 | send selected to XA_CLIPBOARD [(char|line)wise|
 | *                 | send selected to XA_PRIMARY   [(char|line)wise|
 | e                 | edit as filename [charwise]    |
 | b                 | check for unbalanced pair of objects [linewise]|
 | v                 | check line[s] for invalid UTF-8 byte sequences [linewise]
 |             note: this requires HAS_USER_EXTENSIONS|
 | S                 | Spell line[s] [(char|line)wise]
 | M                 | evaluate selected as a math expression [(char|line)wise]
 |             note: this requires HAS_USER_EXTENSIONS|HAS_EXPR           |
 | @                 | interpret selected by the builtin interpreter [linewise]|
 | I                 | interpret selected by Dictu PL [linewise]
 |             note: this requires HAS_USER_EXTENSIONS|HAS_PROGRAMMING_LANGUAGE |
 | C                 | compile selected as C code [linewise]
 |             note: this requires HAS_USER_EXTENSIONS|HAS_TCC |
 | TAB               | triggers a completion menu with the correspondent to the
 |                     specific mode above actions    |
 | escape            | aborts                         |
 | HOME|END|PAGE(UP|DOWN)|G|ARROW(RIGHT|LEFT|UP|DOWN) |
 | extend or narrow the selected area (same semantics with the normal mode)

Command line mode:
 |   key[s]          |  Semantics                     |
 |___________________|________________________________|
 | carriage return   | accepts                        |
 | escape            | aborts                         |
 | ARROW[UP|DOWN]    | search item on the history list|
 | ARROW[LEFT|RIGHT] | left|right cursor              |
 | CTRL-a|HOME       | cursor to the beginning        |
 | CTRL-e|END        | cursor to the end              |
 | DELETE|BACKSPACE  | delete next|previous char      |
 | CTRL-r            | insert register contents (charwise only)|
 | CTRL-l            | clear line                     |
 | CTRL-/ |CTRL-_    | insert last component of previous command|
 |   can be repeated for: RLINE_LAST_COMPONENT_NUM_ENTRIES (default: 10)|
 | TAB               | trigger completion[s]          |

Search:
 |   key[s]          |  Semantics                     | count
 |___________________|________________________________|_______
 | CTRL-n            | next                           |
 | CTRL-p            | previous                       |
 | carriage return   | accepts                        |
 | escape            | aborts                         |

  In this implementation while performing a search, the focus do not change
  until user accepts the match. The results and the dialog, are shown at the
  bottom lines (the message line as the last line on screen).
  It searches just once in a line, and it should highlight the captured string
  with a proper message composed as:

    |line_nr byte_index| matched line

  See at Regexp section for details.

  On Normal mode, it is possible to map native language to normal mode commands.
  Here is a sample:

  int lmap[2][26] = {{
    913, 914, 936, 916, 917, 934, 915, 919, 921, 926, 922, 923, 924,
    925, 927, 928, ':', 929, 931, 932, 920, 937, 931, 935, 933, 918},{
    945, 946, 968, 948, 949, 966, 947, 951, 953, 958, 954, 955, 956,
    957, 959, 960, ';', 961, 963, 964, 952, 969, 962, 967, 965, 950
  }};

  Ed.set.lang_map (this, lmap);

  These correspond to 'A'-'Z' and 'a'-'z' respectively.

  File operation mode:
  This is triggered with 'F' in normal mode and for now can:
    - validate current buffer for invalid UTF8 byte sequences
    - write this file
    - compile this file with tcc C compiler
    - spell this file
    - interpret this file with the builtin interpreter
    - interpret this file with Dictu Programming Language

  As an extension and if elinks browser is installed, it can open this file
  in a running elinks instance.

  Command line mode:
  (note) Commands do not get a range as in vi[like], but from the command line
  switch --range=. Generally speaking the experience in the command line should
  feel more like a shell and specifically the zsh completion way.

  Auto completions (triggered with tab):
    - commands
    - arguments
    - filenames

  Command completion is triggered when the cursor is at the first word token.
  (note: and also by default, and when the first char is one of the "`~@" set).

  Arg completion is triggered when the first char word token is an '-' or
  when the current command, gets a bufname as an argument.

  In any other case a filename completion is performed.

  note: that if an argument (like a substitution string) needs a space, it should be
  quoted

  If a command takes a filename or a bufname as an argument, tab completion
  will quote by default the argument (for embedded spaces): this mechanism
  uses the --fname= argument.

  Options are usually long (that means prefixed with two dashes), unless some
  established/unambiguous like (for now):
    -i  for interactive
    -r  for recursive

  Default command line switches:
    --range=...
      valid ranges:
      --range=%              for the whole buffer
      --range=linenr,linenr  counted from 1
      --range=.              for current line
      --range=[linenr|.],$   from linenr to the end
      --range=linenr,. from  linenr to current line
     without --range         assumed current line number

    --global          is like the g flag on vim substitute
    --interactive,-i  is like the c flag on vim substitute
    --append          is like  >> redirection (used when writing to another file)

    --pat=`pat'       pat is a string that describes the pattern.
                     For more details see at Regexp section below to this document.

    --sub=`replacement string' but see at Regexp section for details.

  Commands:
  ! as the last character indicates force, unless is a shell command.

  :s[ubstitute] [--range=] --pat=`pat' --sub=`sub' [-i,--interactive] [--global]
  :w[rite][!] [filename  [--range] [--append]]
  :wq[!]                 (write and quit (if force, do not check for modified buffers))
  :e[!] [filename]       (when e!, reread from current buffer filename)
  :enew [filename]       (new buffer on a new window)
  :etail                 (like :e! and 'G' (reload and go at the end of file))
  :split [filename]      (open filename at a new frame)
  :b[uf]p[rev]           (buffer previous)
  :b[uf]n[ext]           (buffer next)
  :b[uf][`|prevfocused]  (buffer previously focused)
  :b[uf]d[elete][!]      (buffer delete)
  :w[in]p[rev]           (window previous)
  :w[in]n[ext]           (window next)
  :w[in][`|prevfocused]  (window previously focused)
  :ednew|ednext|edprev|edprevfocused
                         (likewise but those are for manipulating editor instances,
                          ednew can use a filename as argument)
  :r[ead] filename       (read filename into current buffer)
  :r! cmd                (read into buffer cmd's standard output)
  :!cmd                  (execute command)
  :diff [--origin]       (shows a unified diff in a diff buffer, see Unified Diff)
  :diffbuf               (change focus to the `diff' window/buffer)
  :vgrep --pat=`pat' [--recursive] fname[s] (search for `pat' to fname[s])
  :redraw                (redraw current window)
  :searches              (change focus to the `search' window/buffer)
  :messages              (change focus to the message window/buffer)
  :testkey               (test keyboard keys)
  :set option            (set option for current buffer and control editor behavior)
                          --ftype=`ftype' set `ftype' as filetype
                          --tabwidth=[int] set tabwidth
                          --shiftwidth=[int] set shiftwidth
                          --backupfile set backup
                          --backup-suffix=[string] set backup suffix (default: ~)
                          --no-backupfile unset the backup option
                          --autosave=[int] set in minutes the interval, (used
                            at the end of insert mode to autosave buffer)
                          --enable-writing this will enable writing (buffer contents)
                          --save-image=[1|0] enable saving editor layout at exit
                          --image-file=`file' save image to `file'
                          --image-name=`name' save image as `name'
                          --persistent-layout=[1|0] [en|dis]able persistent editor layout
  :@balanced_check [--range=] (check for unbalanced pair of objects, without `range'
                          whole file is assumed)
  :@bufbackup             backup file as (dirname(fname)/.basename(fname)`suffix',
                          but it has to be set first (with :set or with --backupfile),
                          if backupfile exists, it raises a question, same if this is
                          true at the initialization
  :@validate_utf8 filename (check filename for invalid UTF-8 byte sequences
  :@save_image [--as=file] (save current layout, that can be used at a next invocation
                            with --load-file=file.i, to restore it,
                            default filename: $SYSDATADIR/images/currentbufname.i)
  :@edit_image           edit the current process image script (if exists)
  :q[!] [--global]       quit (if force[!], do not check for modified buffers),
                              (if --global exit from all running editor instances)

  Application:
  The test application (which simply called veda for: visual editor application),
  can provide the following commands:

  :spell --range=`range' (without range default current line)
  :`mkdir   dir       (create directory)
  :`man     manpage   (display man page on the scratch buffer)
  :`stat    file      (display file status information)
  :`battery           (display battery status to the message line) (only for Linux)
  :@info [--buf,--win,--ed] (with no arguments defaults to --buf) (this prints
                      details to the scratch buffer of the corresponded arguments)

  The `man command requires the man utility, which simply means probably also an
  implementation of a roff system. The col utility is not required, as we filter
  the output by ourselves. It would be best if we could handle the man page lookup,
  through internal code, though it would be perfect if we could also parse roff,
  through a library - from a small research found a parser in js but not in C.
  The command takes --section=section_id (from 1-8) argument, to select a man
  page from the specific section (default section is 2).
  However (like in the case of memmove() for this system that displays bstring(3))
  it's not always succeeds. In this case pointing to the specific man file to the
  file system through tab completion, it should work.

  The `mkdir cannot understand --parents and not --mode= for now. By default the
  permissions are: S_IRWXU|S_IRWXG|S_IROTH|S_IXOTH (this command needs revision).
  (update: this command got a --mode= argument)

  The :`battery command it should work only for Linux.

  note:
  The "`" prefix is associated with shell syntax and is going to be used for internal
  implementations of system commands.

  The "~" prefix is reserved for application specific commands.

  The "@" prefix but is intented to group functions, that either can manage/control
  the current buffer, and for functions that can be executed for that buffer.

  Unified Diff:
  This feature requires (for now) the `diff' utility.

  The :diff command open a dedicated "diff" buffer, with the results (if any) of
  the differences (in a unified format), between the buffer in memory with the one
  that is written on the disk.
  Note that it first clears the previous diff if any.

  The :diff command can take an "--origin" argument. In this case the command will
  display any differences, between the backup file and and the buffer in memory.
  For that to work the "backfile" option should be set (either on invocation or by
  using the :set --backupfile command first with the conjunction with the :@bufbackup
  command. Otherwise a warning message should be displayed.

  The :diffbuf command gives the focus to this "diff" buffer. Note, that this buffer
  can be quickly closed with 'q', line in a pager (likewise for the other special
  buffers, like the message buffer or the scratch buffer).

  Another usage of this feature is when quiting normally with :quit (without forcing) 
  and the buffer has been modified.
  In that case a dialog (below) presents some options:

    "[bufname] has been modified since last change
     continue writing? [yY|nN], [cC]ansel, unified [d]iff?"

    on 'y': write the buffer and continue
    on 'n': continue without writing
    on 'c': cancel operation at this point (some buffers might be closed already)
    on 'd': print to the stdout the unified diff and redo the question (note that
            when printing to the stdout, the previous terminal state is restored;
            any key can bring back the focus)

  UTF-8 Validation:
  There are two ways to check for invalid UTF-8 byte sequences.
  1. using the command :@validate_utf8 filename
  2. in visual linewise mode, by pressing v or through tab completion

  In both cases any error is redirected to the scratch buffer. It doesn't
  and (probably) never is going to do any magic, so the function is mostly
  only informational (at least for now).
  Usually any such invalid byte sequence is visually inspected as it messes
  up the screen.

  The code for this functionality is from the is_utf8 project at:
  https://github.com/JulienPalard/is_utf8
  specifically the is_utf8.c unit and the is_utf8() function
  Many Thanks.

  Copyright (c) 2013 Palard Julien. All rights reserved.
  but see src/lib/utf8/is_utf8.c for details.

  Regexp:
  This library uses a slightly modified version of the slre machine, which is an
  ISO C library that implements a subset of Perl regular expression syntax, see
  and clone at:

  https://github.com/cesanta/slre.git
  Many thanks.

  The substitution string in the ":substitute command", can use '&' to denote the
  full captured matched string.

  For captured substring a \1\2... can be used to mean, `nth' captured substring
  numbering from one.

  It is also possible to force caseless searching, by using (like pcre) (?i) in front
  of the pattern. This option won't work with multibyte characters. Searching for
  multibyte characters it should work properly though.

  To include a white space, the string should be (double) quoted. In that case a
  literal double quote '"', should be escaped. Alternatively a \s can be used to
  include a white space.

  Re Syntax.
    ^       Match beginning of a buffer
    $       Match end of a buffer
    ()      Grouping and substring capturing
    \s      Match whitespace
    \S      Match non-whitespace
    \d      Match decimal digit
    \n      Match new line character
    \r      Match line feed character
    \f      Match form feed character
    \v      Match vertical tab character
    \t      Match horizontal tab character
    \b      Match backspace character
    +       Match one or more times (greedy)
    +?      Match one or more times (non-greedy)
    *       Match zero or more times (greedy)
    *?      Match zero or more times (non-greedy)
    ?       Match zero or once (non-greedy)
    x|y     Match x or y (alternation operator)
    \meta   Match one of the meta character: ^$().[]*+?|\
    \xHH    Match byte with hex value 0xHH, e.g. \x4a
    [...]   Match any character from set. Ranges like [a-z] are supported
    [^...]  Match any character but ones from set

  A pattern can start with (?i) to denote `ignore case`

  Registers and Marks:
  Both are supported but with the minimal features (same with other myriad details
  that needs care).

    Mark set:
    [abcdghjklqwertyuiopzxcvbnm1234567890]
    Special Marks:
    - unnamed mark [`] jumps to the previous position

    Register set:
    [abcdghjklqwertyuiopzxsvbnm1234567890ABCDGHJKLQWERTYUIOPZXSVBNM]

    Special Registers:
    - unnamed ["] register (default)
    - current filename [%] register
    - last search [/] register
    - last command line [:] register
    - registers [+*] send|receive text to|from X clipboard (if xclip is available)
    - blackhole [_] register, which stores nothing
    - expression [=] register (experimental) (runtime code evaluation)
    - CTRL('w') current word
    - shared [`] register (accessed by all the editor instances)

  Note that for uppercase [A-Z], the content is appended to the current content,
  while for the [a-z] set, any previous content is replaced.
  An uppercase register can be cleared, by using in Normal mode the "-" command,
  prefixed with '"' and the register letter, e.g., "Z- for the "Z" register.

  History Completion Semantics (command line and search):
   - the ARROW_UP key starts from the last entry set in history, and scrolls down
     to the past entries

   - the ARROW_DOWN key starts from the first entry, and scrolls up to most recent

  Glob Support:
    (for now)
    - this is limited to just one directory depth
    - it uses only '*'
    - and understands (or should):
      `*'
      `/some/dir/*'
      `*string' or `string*string' or `string*'
        (likewise for directories)

    Note: many commands have a --recursive option

  Menus:
  Many completions (and there are many) are based on menus.
    Semantics and Keys:
    Navigation keys:
    - left and right (for left and right motions)
      the left key should move the focus to the previous item on line, unless the
      focus is on the first item in line, which in that case should focus to the
      previous item (the last one on the previous line, unless is on the first line
      which in that case should jump to the last item (the last item to the last
      line))

    - the right key should jump to the next item, unless the focus is on the last
      item, which in that case should focus to the next item (the first one on the
      next line, unless is on the last line, which in that case should jump to the
      first item (the first item to the first line))

    - page down/up keys for page down|up motions

    - tab key is like the right key

    Decision keys:
    - Enter accepts selection; the function should return the focused item to the
      caller

    - Spacebar can also accept selection if it is enabled by the caller. That is
      because a space can change the initial pattern|seed which calculates the
      displayed results. But using the spacebar speeds a lot of those operations,
      so in most cases is enabled, even in cases like CTRL('n') in insert mode.

    - Escape key aborts the operation

  In all the cases the window state should be restored afterwards.

  The sample Application that provides the main() function, can also read
  from standard input to an unnamed buffer. Thus it can be used as a pager:

     git diff "$@" | vedas --ftype=diff --pager "$@"

  Shell Commands:
  The application can also run shell commands or to read into the current buffer from
  the standard output of a shell command. WARNING: Interactive applications that link
  and use a curses UI, might have unexpected behavior in this implementation.

  Searching Files:
  This is a really quite basic emulation of quickfix vim's windows, written quite
  early at the development, so there has to be some discipline when is being used,
  as there a couple of things that need care.

  The command :vgrep it takes a pattern and at least a filename as argument[s]:

    :vgrep --pat=`pattern' [-r|--recursive] file[s]

  This should open a unique window intended only for searches and re-accessible
  with:

    :searches  (though it might be wise a `:copen' alias (to match vim's expectation))

  This window should open a frame at the bottom, with the results (if any) and it
  will set the pointer to the first item from the sorted and unique in items list.

  A carriage return should open the filename at the specific line number at the
  frame 0.

  A `q' on the results frame (the last one), will quit the window and focus again
  to the previous state (as it acts like a pager).

  This command can search recursively and skips (as a start) any object file.

  Note that because it is a really basic implementation, some unexpected results
  might occur, if there is no usage discipline of this feature (for instance :bd
  can bring some confusion to the layout and the functionality).

  Filetypes:
  Work has been started on the Syntax and Filetypes, with the latter can be play
  a very interesting role, with regards to personalization, but also can be quite
  powerful of a tool, for all kind of things.

  As it concerns the main visible code, there only few filetypes (C, sh, make, lua,
  Dictu, Zig, ...) buf it is true that all but C, are poorly supported.

  As far it conserns the highlighted stuff (which is yes important, because it helps
  visually quite a lot, but performance shouldn't penaltized, so the implementation
  is quite naive and doesn't allow extensibility or smart patterns, and should remain
  at least in this level), one day i've to find the desire to enhance it a but with
  less than today complexity (because the code is complex), but is fast and the rules
  are simple.

  The highlighted system and specifically the multiline comments has some rules,
  stemming from sane practicing to simplify parsing, as the code is searching for
  comments in previous lines, and if current line has no comment tokens of course
  (by default 24), and the relative setting is:
    MAX_BACKTRACK_LINES_FOR_ML_COMMENTS (default 24)

  For instance in C files, backtracking is done with the following way:
  If ("/*" is on index zero, or "/*" has a space before) and there is no "*/",
  then it considered as line with a comment and the search it stops. It even
  stops if " * " is encountered on the beginning of the line (note the spaces
  surrounding "*").
  Note: the relative syntax variable is:
     multiline_comment_continuation as char[]

  So this simply means that if someone do not want to penaltize performance then
  it is wise to use " * " in the beginning of the line to force the code, as soon
  as possible to search on previous lines (as this is expensive).

  The other relative self explained settings:
    - singleline_comment        as char[]
    - multiline_comment_start   likewise
    - multiline_comment_end     likewise

  The code can set filetypes with the following ways and order.

  1. The caller knows the filetype index and set it directly if the index is
  between the bounds of the syntax array (note that the 0 index is the default
  filetype (txt)).

  2. Then is looking to the file extension (.c, .h, ...), and the relative variable:
    - extensions   as *char[]

  3. Then is looking to the file name (Makefile, ...) and the relative variable:
    - filenames    as *char[]

  4. Finally is looking for shebang (first bytes of the file), and the relative variable:
    - shebangs     as *char[] e.g., #!/bin/sh or #!/bin/bash and so on

  The rules and variables:

  1. Keywords as *char[] (e.g., if, else, struct, ....), example:
    char *c_keywords[] = {
        "if I", "for I", "this V", "NULL K", "int T", ...
    The capital letter after the keyword and a space, denotes the type and the
    corresponding color:
        I: identifier, K: keyword, C: comment,  O: operator, N: number, S: string
        D:_delimiter   F: function V: variable, T: type,     M: macro,
        E: error,      Q: quote

  2. Operators as char[] e.g., +|*()[] ...

  It can also highlight strings enclosed with double quotes and the relative variable:
    hl_strings  as int (zero means do not highlight)
    hl_numbers  likewise

  The default parsing function is buf_syn_parser(), and can be set on the syntax
  structure and has the following signature:

       char  *(*parse) (buf_t *, char *, int, int, row_t *);

  The default init function is buf_syn_init () and can be set on the syntax structure
  with the following signature:

       ftype_t *(*init) (buf_t *);

  The default autoindent callback function does nothing and can be set during filetype
  initialization with the following signature:

       string_t *(*autoindent_fun) (buf_t *, char *);

  In normal mode 'gf' (go to (or get) filename) can call a callback function, that
  can be set during initialization with the following signature:

        char *(*ftype_on_open_fname_under_cursor) (char *, size_t, size_t);

  Note that the C filetype, implements the above two callbacks, and 'gf' and when
  the cursor is on <sys/stat.h>, then it opens the /usr/include/sys/stat.h.

  Tabwidth and Charwidth:
  The library uses the wcwidth() implementation (with minor adjustments for the
  environment) from the termux project:
  https://github.com/termux/wcwidth
  The MIT License (MIT)
  Copyright (c) 2016 Fredrik Fornwall <fredrik@fornwall.net>

  This license applies to parts originating from the
  https://github.com/jquast/wcwidth repository:
  The MIT License (MIT)
  Copyright (c) 2014 Jeff Quast <contact@jeffquast.com>

  Many Thanks.

  The implementation is similar to Markus kuhn's, though looks more updated.
  The code tries hard to avoid needless expensive calls to that function, as
  in almost any movement, the code should account for the width of the character.
  Same exactly goes for the tabwidth, the algorithm doesn't differ, though a tab
  is handled usually earlier.
  The thing is that the calculations are complex and quite possible there are
  conditions that are not handled or improperly handled and because i do not
  work with files (except Makefiles) that have tabs or chars which occupy more
  than one cell, do not help to catch them.

    The semantics:
  In normal mode operations, the cursor should be placed at the end of the width
  (one cell before the next character), while on insert mode to the beginning of
  the width (one cell after the previous character).
  The next or previous line (character) completions (CTRL('e') and CTRL('y') in
  normal mode) are based on the byte index and not on the character position, and
  this might seems that is not (visually) right and probably isn't, and could be
  change in the future).
  Speaking for tabs, by default when editing C file types, a tab can be inserted
  only through CTRL('v') or (through CTRL('e') or CTRL('y')) in normal mode, or by
  setting it to the filetype, or by simply setting C_TAB_ON_INSERT_MODE_INDENTS=0,
  but itsnotgonnabebyme.

  As for the tabwidth, as and all the (compilation) options, can be set individually
  on the specific filetype, by editing it (nothing should matter for the code, as it
  __should__ work in any way, otherwise is a bug in the code).

  As Utility:
  The introduction of the --ex-com="command" will/can allow to create utilities
  by using the machine, e.g.,

  vedas --ex-com="s%::--pat=some_pattern::--sub=replaced::--interactive" --exit file

  This is like a sed functionality, though do not understand a new line in patterns,
  and patterns are limited by the minimal perl compatible regexp machine.
  Nevertheless can work perfectly as search and replace utility.

  The "::" in the command line is being used as a delimeter, otherwise the arg parser,
  will be confused by the whitespace. See also Spelling for another usage, but other
  usefull functions exists and can be developed.

  In this case and at the very least, the machine is being used as a free of charge
  UI (user interface).

  Spelling:
  The application can provide spelling capabilities, using very simple code, based
  on an idea by Peter Norvig at:
  http://norvig.com/spell-correct.html

  The algorithms for transforming the `word', except the case handling are based
  on the checkmate_spell project at: https://github.com/syb0rg/checkmate

  Almost same code at: https://github.com/marcelotoledo/spelling_corrector

  Copyright  (C)  2007  Marcelo Toledo <marcelo@marcelotoledo.com>

  Version: 1.0
  Keywords: spell corrector
  Author: Marcelo Toledo <marcelo@marcelotoledo.com>
  Maintainer: Marcelo Toledo <marcelo@marcelotoledo.com>
  URL: http://marcelotoledo.com

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.

  The word database i use is from:
  https://github.com/first20hours/google-10000-english
  Data files are derived from the Google Web Trillion Word Corpus, as described
  by Thorsten Brants and Alex Franz
  http://googleresearch.blogspot.com/2006/08/all-our-n-gram-are-belong-to-you.html
  and distributed by the Linguistic Data Consortium:
  http://www.ldc.upenn.edu/Catalog/CatalogEntry.jsp?catalogId=LDC2006T13.
  Subsets of this corpus distributed by Peter Novig:
  http://norvig.com/ngrams/
  Corpus editing and cleanup by Josh Kaufman.

  The above dictionary contains the 10000 most frequently used english words,
  and can be extended through the application by pressing 'a' on the dialog.

  This implementation offers ways to check for mispelling words.
  1. using the command line :spell --range=`range'
  2. on visual linewise mode, by pressing `S' or by using tab completion
  3. on visual characterize mode, by pressing `S' or by using tab completion
  4. on 'W' in normal mode
  5. on 'F' in normal mode (file operation mode)

  As it is a very simple approach with really few lines of code, it it is obvious
  that there is not guarantee, that will find and correct all the mispelled words
  in the document, though it can be taught with proper usage.

  This is the initial implementation and some more work is needed to make it more
  accurate, though for a start is serves its purpose quite satisfactory, plus there
  is no requirement and already helped me to this document.

  Scripting:
  The application offers a tiny scripting interface, which is based on Tinyscript:
  https://github.com/totalspectrum
  Copyright 2016 Total Spectrum Software Inc.
  TERMS OF USE: MIT License

  It is a really minimal scripting Language, with just a bit over 1000 lines of
  code, but which has the absolute essentials, for this kind of job we want.
  See an example at: src/lib/i/example.i

  Many many thanks to the above mentioned project.

  [update: Thu 09 Jul 2020]: This was integrated into the library.

  By integrating the interpreter to the library, we gain:

    - one less indirection, as we have straight access to the library
    - lesser complexity (lesser external code - no global variables - easier binding)
    - flexibility and inner capability to develop logic, by using the builtin
      integrated machine, that know each other.

  The whole code is about 1500 of lines (counted with the abstraction part of code).

  The other thing that it might be of interest, is that the mechanism might makes
  easy to use the machine, probably with any PL. It works like this:

  The library expose the capability to register two callback functions:

  The first and the obvious, is simply a function that will interpret the given
  code as an argument to that function (an arraylist of strings), at runtime.

  For instance (and using as an example, the builtin way to do this kind of thing):

    var ed = e_get_ed_current ()
    var win = ed_get_current_win (ed)
    var buf = win_get_current_buf (win)

  Here we use 3 lines that initialize the essentials, so to control the machine.
  This is valid syntax for the ts(interpreter), as variables are just pointers, which
  are typedef'ed as:

    typedef intptr_t ival_t;

  In C all the function parameters is of type ival_t.

  You can pass litteral strings to functions, and this is very convenient, when
  you emit code that you want to be interpreted (the original/upstream ts doesn't
  have this functionality).

  So, with the above initialization code, we have a pointer to the current buffer,
  which simply means, access to the whole machine.

  Why? Because of the interconnection of those 4 following structures -

    1)  ed: An instance of an editor, that holds windows, which they hold our buffers.
        Can be unlimited. They do know nothing about each other, and so can not be
        possible to influnce its other state.
        Those can attached and reattached (with the exact state) with CTRL(j), or
        with previous/next/prevFocused ways.

    2) win: An instance of a window. Likewise with ed. It has of course, its own ways
       to change the focus. Those are the main wised established vim ways, plus a
       couple of extensions.

    3) buf: Likewise with win.

    4)   e: The kernel. It can be only one. It doesn't has code to influence any of the
       above states. Sometimes though helps to share stuff between them.

  All these pointers are part of a chain, that stores a reference to the kid and the
  parent and|or to their root. So there is an access to the enormous structure that
  is exposed by the library.

  The second callback is a function that gets a string that records user actions
  (for now is only a couple of functions that are being used for testing), anytime
  a relative function is called, and stores them to the arraylist.

  In the case of the builtin interpreter, we do not change anything to the string,
  as the exposed schema is valid code for us, as the function names are identical
  in C and in the interpreter (as that is how is stored as a symbol).

  So, its up to the PL to parse the string and evaluate it within its VM.

  For instance, here is what the builtin second callback function does (this cb
  records user actions and writes them in this function argument arraylist, that
  is being passed then to the first above cb().
  This call is being done with '@[123@]' in normal mode.

  private int buf_normal_goto_linenr (buf_t *this, int lnr, int draw) {
    ed_record ($my(root), "buf_normal_goto_linenr (buf, %d, %d)", lnr, draw);

  So, a parser the only (probably) thing it has to do, is probably to break the
  function name into tokens (e.g., in an OOP PL: Buf.normal.goto.linenr (...)),
  or and in this case of our I, does has to do nothing, as it is simply the same name,
  which is an object which was initialized at the instantiation, with:

  { "buf_normal_goto_linenr",(ival_t) i_buf_normal_goto_linenr, 3},

     and the i_buf_normal_goto_linenr() registered function to the memory arena:

  ival_t i_buf_normal_goto_linenr (ival_t i, buf_t *this, int linenum, int draw) {
    (void) i;
    return buf_normal_goto_linenr (this, linenum, draw);
  }

  The first function argument is always the instance of the interpreter, which can
  be retrieved, casted as:

  i_t *this = (i_t *) i;

  Since this instance holds a pointer of the root E Class, then there is read|write
  access to the whole machine functions.

  C compiler:
  The application can compile C code (using the tcc library as a compiler) in
  two ways:
    - in visual linewise mode
    - file operation mode

  It is assumed that an error such a segmentation fault can bring down the whole
  system.

  Programming Language:
  The application can interpret code (using the Dictu programming language with
  a couple of modifications) in two ways:
    - in visual linewise mode
    - file operation mode

    notes:
    - System.exit() is disabled
    - without HAS_CURL=1 the HTTP module is disabled
    - the language is at early stage but usable, and i serve to its development,
      so this is also being used as a test took, for further development

  Application Interface:
  The API state is on the second revision and if for the next one/two years doesn't
  change drastically, it will be considered to completion. That means that probably
  nothing is going to change, to the way that someone access the underlying machine.

  This library can be used with two ways.

    - copy libved.c libved.h and __libved.h to the project directory
      and #include libved.h and either #include libved.c or compile it
      at the same compiler invocation

    - link against the shared or static library and use -lved during
      compilation (if the library was installed in a not standard location
      use -L/dir/where/libved/was/installed)

  It can optionally include libved+.h and libved+.c (recommended). Those
  two files can use the following compilation options.
    HAS_USER_EXTENSIONS, HAS_LOCAL_EXTENSIONS,
    HAS_PROGRAMMING_LANGUAGE (the building of the language is handled by the
    this Makefile automatically, otherwise it should be compiled explicitly:
    note that this specific target language (Dictu), it can use libcurl)
    HAS_TCC (this option requires libtcc installed)

  The underlying code uses an object oriented style, though it is just for practical
  reasons as permits mostly code organization, simplicity, abstraction, compactness,
  relationship, and especially quite a lot of freedom to develop new or overridde the
  existing functionality.
    A generic comment:
      But the main advantage of this approach is that there is no global state;
      the functions act on an instance of a type, or if not, simple their scope
      is narrow to a specific job that can not change state to the environment.

      In this whole library, there is neither one out of function scope variable.
      In the sample application is one (as it is required by the signal handlers).

      In short, a compact, unified and controlled environment, under a root structure,
      with functions that act on an own and known type or|and expected and sanitized
      arguments, within a well defined (with limits) scope.

      The heavy duty seems to be at the entry points or|and to the communication ports,
      that check and sanitize external data, or|and interfere with the outer environment.
      But afterwards, it looks that is just a matter of time, to catch the conditions
      that can be met and those logically have an end.
      A comment about C.
        This is a great advantage for the C language, because the code is not going
        to ever change to adapt a new version; unless is a code mistake, that a new
        compiler uncovered, but correct code is going to work forever.

      This could also speed up execution time (by avoiding un-needed checks of a data
      that is known for certain that is valid, because, either it is produced (usually)
      by the self/own, or has already been checked, by previous users in the toolchain
      stack.
      But the main benefit, is that it brings clarity to the code and concentration
      to the actual details. However that it could also be considered as a functional
      environment (with the no-side-effect meaning) or as an algorithm environment.

      It could be a wise future path for C, if it will concentrate just to implement
      algorithms or standard interfaces.

  Structure Details:
  A buffer instance is a child of a window instance and is responsible to manipulate
  a double linked list that hold the contents. The structure has references to the
  next and previous buffer (if any), to the window instance where belongs (parent)
  and to the editor instance where it's parent belongs (an editor instance).

  A window instance is a child of an editor instance and is responsible to manipulate
  a double linked list that holds the buffers. That means holds the number of buffer
  instances and the current buffer, and has methods to create/delete/change/position
  those buffers. It has also references to the next and previous window (if any) and
  to it's parent (the editor instance where belongs).

  An editor instance is a child of the root instance and is responsible to manipulate
  a double linked list that hold the windows. That means holds the number of window
  instances and has methods to create/delete/change those windows.

  A root instance of the root class "Ed" provides two public functions to initialize
  and deinitialize the library. It has methods that manipulates editor instances with
  a similar way that others do. It has one reference that holds an instance  of the
  class "ed", that is being used as a prototype that all the other editor instances
  inherit methods or|and some of it's properties (like the term property, which should
  be one).
  This "ed" instance initialize all the required structures (like String, Cstring,
  Ustring, Term, Video, File, Path, Re, ...), but also the basic three classes.

  Since those three basic ones have references to it's other, that means everyone
  can use the others and all the domain specific sub-structures.

  The macro Class.method (...) works uniform, as long there is a pointer which
  is declared as: [buf_t|win_t|ed_t] *this.

  Only one "this", can exist in a function, either when is declared in the body of
  the function or usually as the first function argument.

  The "this" pointer can also provide easy access to it's type properties, by using
  the $my([property]) macro.

  Also the self([method]) macro can call any other method of it's class as:
  self(method, [...]).

  But also with "this", anyone can have direct access to any of it's parent or the
  parent of it's parent, by using the $myparents([property]) or $myroots([property])
  macros (where this has a meaning).

  However the above is mostly true only for the buf_t type since is this type that
  usually is doing the actual underlying job throughout the library. The other two
  they are actually present for the interface and do not get involved to it's other
  buisness. The actual editor code would be much less, if it wasn't for the interface
  which should be practical anyway.

  Note that an editor instance can have multiply buffers of the same filename, but
  only one (the first opened) is writable.

  The actual manipulation, even if they are for rows, or for buffers or for windows
  or for editor instances, is done through a double linked list (with a head, a tail
  and a current pointer).

  This might be not the best method as it concerns optimization in cases, but is by
  itself enough to simplify the code a lot, but basically allows quick and accurate
  development and probably better parsing when reading the code (as it is based on
  the intentions) and makes the code looks the same wherever is being used.

  But and many other of the algorithms are based on those list type macros or similar
  macros which act on structures that do not have a tail or a current pointer (like
  stack operations like push/pop).

  Speaking for the current pointer, this can act (at the minimum) as an iterator;
  but (for instance) can speed a lot of the buffer operations, even in huge files
  where jumping around lines is simple arithmetic and so might bring some balance
  since a linked list might not be the best type for it's job. But insertion and
  deletion is superior to most of other containers.

  C:
  This compiles to C11 for two reasons:
    - the fvisibility flags that turns out the defaults, a global scope object
      needs en explicit declaration
    - the statement expressions that allow, local scope code with access to the
      function scope, to return a value; also very useful in macro definitions

  The following macros are being used.

  #define ifnot(__expr__) if (0 == (__expr__))
  clearly for clarity; also same semantics with SLang's ifnot

  #define loop(__num__) for (int $i = 0; $i < (__num__); $i++)
  likewise for both reasons; here also the $i variable can be used inside the
  block, though it will violate the semantics (if the variable change state),
  which is: "loop for `nth' times" and this how is being used in the codebase.
  Anyway this loop macro is being used in a lot of repositories.

  Also purely for linguistic and expressional reasons the followings:

  #define is    ==
  #define isnot !=
  #define and   &&
  #define or    ||
  again, those they being used by others too, see for instance the Cello project
  at github/orangeduck/Cello.

  And for plain purism, the following.
  #define bytelen strlen
  SLang also implements strbytelen(), while strlen() returns character length
  I guess the most precise is bytelen() and charlen() in the utf8 era.

  Defined but not being used, though i could happily use it if it was enforced
  by a standard.
  #define forever for (;;)

  C idiom:
  The inner code it uses a couple of macros to ease the development, like:

   self(method, [arg,...])

  This awaits an accessible "this" declared variable and it passes the specific
  type as the first argument to the calling function.

  In this context "this" is an abstracted variable and works with objects with
  specific fields. Types like these, have a "prop" field that holds object's
  variables and as well a "self" dedicated field for methods (function pointers
  that their function signature contains "this" type as their first argument).

  The properties of such types are accessible with the following way:

  $my(prop)

  Calling a Class method:

  Class.method ([...])

  Note that in this case "Class" doesn't pass any argument[s] to the calling method,
  so any argument[s] should be given explicitly.

  Generally speaking, those macros, are just syntactic sugar, no code optimization,
  or the opposite, neither a single bit of penalty. It allows mainly expressionism
  and focus to the intentions.

  In that spirit, also available are macros, that their sole role is to abstract
  the details over type creation/declaration/allocation, that assists to quick
  development.
  The significant ones: AllocType, NewType, DeclareType. The first argument on those
  macros is the type name but without the _t extension, which is the actual type.

  Finally, a couple of macros that access the root editor type or the parent's
  (win structure) type. Either of these three main structures have access (with one
  way or the other) to all the fields of the root structure, so all the above macros
  works everywhere the same, if there is a proper declared "this" with proper fields.

  The underlying properties of the types are accesible through [gs]etters as none of
  the types exposes their data.

  Memory Interface:
  The library uses the reallocarray() from OpenBSD (a calloc wrapper that catches
  integer overflows), and it exposes a public mutable handler function that is
  invoked on such overflows or when there is not enough memory available errors.

  This function is meant to be set by the user of the library, on the application
  side and scope. The provided one exits the program with a detailed message.
  I do not know the results however, if this function could wait for an answer
  before exits, as (theoretically) the user could free resources, and return for
  a retry. The function signature should change to account for that.

  The code defines two those memory wrappers.
    Alloc (size) and Realloc (object, size).
  Both like their counterparts, they return void *.

  LICENSE:
  I wish we could do without LICENSES. In my world it is natural to give credits
  where they belong, and the open source model is assumed, as it is the way to
  evolution. But in this world and based on the history, we wouldn't be here if it
  wasn't for GPL. Of course, it was the GPL and the GNU products (libc, compiler,
  debugger, linker, coreutils and so on ...), that brought this tremendous code
  evolution, so the whole code universe owes and should respect GNU.

  However the MIT like LICENSES are more close to the described above spirit,
  though they don't emphasize on the code freedom as GPL does (many thanks for
  that).
  The truth is that my heart is with GNU, but my mind refuses to accept the existence
  of the word LICENSE.
  I wish there was a road to an unlicense, together with a certain certainity, that
  we (humans) full of the gained conscience, can guard it and keep it safe, as it
  is also (besides ideology) the road to paradise.

  Anyway.
  Since it seems there is no other way, other than to specify a license to avoid
  stupid, stupid troubles, is licensed under GPL2, but my mind still refuces and
  with a huge right, to accept this word (at least in this domain and at this time
  of time (2019)). There is no virginity here and actually (almost) never was, so
  the code even if it is ours, has a lot of influences of others, as we are just
  another link in the chain, and this is wonderfull. Also we!!! are oweing to the
  environment - our mother, our grandpa, our friends and enemies, our heroes, our
  neighbors, the people we met and we never really met them, the butterflies, the
  frogs, a nice song or a terrible sound, an admirable work, or a chain of words,
  and especially the randomness - so who actually owe the copyrights?

  So it is GPL2.

  Now.
  The work that someone put in a project, should be respected and should be mentioned
  without any second thought and with pleasure, as it is the marrow of the world,
  and it is so nice to be part of a continuation, both as receiver or as a producer.

  Coding Style:
  Easy.
    - every little everything is separated with a space, except in some cases on
      array indexing

    - two spaces for indentation

    - the opening brace is on the same line with the (usually) conditional expression

    - the closed brace is on the same byte index of the first letter of the block

    - the code (and if the compiler permits) do not use braces on conditional branches
      when there is single statement on the block

  NOTE:
  This code it contains quite a lot of idiomatic C and (probably) in many cases it
  might do the wrong thing. It is written by a non programmer, that taughts himself
  C at his fifty two, and it is focused on the code intentionality (if such a word).

  TO DO:
  Seriously! Please do not use this on sensitive documents. There are many conditions
  that i know and for certain quite many that i don't know, that they might need to
  be handled.
  The bad thing here is that there is a workflow and in that workflow range i fix
  things or develop things. But for sure there are conditions or combinations of them
  out of this workflow.

  But Really. The real interest is to use this code as the underline machine to
  create another machine. Of course can have some value as a reference, if there is
  something with a value to deserve that reference.

  But it would also be nice (besides to fix bugs) if i could reserve sometime to fix
  the tabwidth stuff, which is something i do not have the slightest will to do,
  because i hate tabs in code (nothing is perfect in this life)- a tab in my humble
  opinion it should be used in the cases where is significant, as a delimiter, or
  in the Makefile's; the way it is being used by the coders is an abuse (imho).
  So, i'd rather give my time (with low priority though) to split those two different
  concepts (though I should fix this thing!!!

    [update: and should be fixed, as from
    the first (hard physical and mentally) days of September of 2019).

    [update at Mon 20 Jul 2020] do not trust much the above statement (some glitches
    exist under certain conditions with tabs in the current line).

  My wish before some time is to work in the tiny abstraction level that will offer
  the basic functionality of an editor:

    - inserting/deleting lines (this should meet or extend ed specifications)
    - line operations where line as:
      An array of characters that provide information or methods about the actual num
      bytes used to consist each character, the utf8 representation and the width of
      that character.
      Line operations: inserting/deleting/replacing..., based on that type.
    - /undo/redo

  Basically the ed interface, which at the beginning coexisted with the visual one
  (at some point too much code were written at once, and the abstraction level wans't
   enough capable to handle both interfaces, so i had to move on).
  But it is a much much much simpler editor to implement, since there is absolutely
  no need to handle the output or|and to set the pointer to the exact right line at
  the current column, pointing to the right byte index of the edited byte[s] (it is
  very natural such an interface to over complicate the code and such code to have
  then bugs).

  From my humble experience and IMHO, the worst bug that can happen in an editor is
  to give the false interpretation of the pointer position, so the user actually edit
  something else than what he thinks he edit.


  Acknowledgments, references and inspiration (besides the already mentioned):

  - vim editor at vim.org (the vim we love from Bram (we owe him a lot) and his army)
  - kilo editor (https://github.com/antirez/kilo.git) (the inspiration for many)
  - e editor (https://github.com/hellerve/e.git) (a clone of kilo)
  - tte editor (https://github.com/GrenderG/tte.git) (likewise)
  - dit editor (https://github.com/hishamhm/dit.git) (and his very nice C)
  - vis editor (git://repo.or.cz/vis.git) (quite advanced editor)
  - gnu-ed at http://www.gnu.org/software/ed/ed.html (a stable ed)
  - ed2 editor (https://github.com/tylerneylon/ed2.git) (another clone of ed)
  - oed editor (https://github.com/ibara/oed.git) (ed from OpenBSD)
  - neatvi editor (https://github.com/aligrudi/neatvi.git) (an excellent vi!)
  - jed editor at http://www.jedsoft.org/jed/ (the stablest editor in the non-vi[m] world)
  - slre regexp machine (https://github.com/cesanta/slre)
  - utf8 project for the case* functions (https://github.com/sheredom/utf8.h)
  - termux project for wcwidth() (https://github.com/termux/wcwidth)
  - Lukás Chmela for itoa() (http://www.strudel.org.uk/itoa/)
  - checkmate_spell project for the algorithms (https://github.com/syb0rg/checkmate)
  - John Davis for stat_mode_to_string() (http://www.jedsoft.org/slang)
  - jsmn (json decoder) (https://github.com/zserge/jsmn)
  - jsmn-example (the state machine for jsmn) (https://github.com/alisdair/jsmn-example)
  - tinyexpr (https://github.com/codeplea/tinyexpr)
  - argparse (https://github.com/cofyc/argparse)
  - Tinyscript (https://github.com/totalspectrum)
  - Dictu (https://github.com/jason2605/Dictu)
  - Tcc (http://repo.or.cz/tinycc)
  - numerous stackoverflow posts
  - numerous codebases from the open source enormous pool

  My opinion on this editor:
  It is assumed of course, that this product is not meant for wide production use;
  but even if it was, it is far far faaaar away to the completion, but is an editor
  which implements at least the basic operation set that can be considered enough
  to be productive, which runs really low in memory resources and with rather few
  lines of code. Theoretically (assuming the code is good enough) it could be used
  in situations (such primitive environments), since is independent and that is a
  very interesting point.

  And this (probably) is the reason for the persistence to self sufficiency.
  The other (probably again and by digging around the mind) is about some beliefs
  about the language and its ecosystem.

  But first, there isn't such a thing as self sufficiency!

  Today our dependency, our libc environment, is more like a collection of individual
  functions, which their (usually tiny) body is mainly just algorithms (and so and
  machine code as this is a property of C), with few (or usually without) conditional
  branches.

  However those functions and for a reason, are unaware for anything out of their
  own domain. They have no relationship between their (lets group), as they do not
  even have a sense that belong to a group, such: "I am a string type function!
  (and what is a string anyway?)".

  But first, libc'es are not thin at all. It would be great if you could just cherry pick
  (specific functionality) from this pool but without to carry all the weight of the
  pool. So it could be easy to build an own independent pool of functions with a way
  (something like the Linux kernel or gnu-lib projects do).

  It could be great if the programmer could extract the specific code and hook it to
  his program, so it could optimize it for the specific environment.
  As an example:
  At the beginning this program implemented the strdup() function with the standard
  signature, having as "const char *src" as the only argument. Since this function
  it returns an allocated string, it should iterate over the "src" string to find its
  length. At some point i realized that in the 9/10 of the cases, the size was already
  known, so the extra call to strlen() it was overhead for no reason. So i changed the
  signature and added a "size_t len" argument. In this application the str_dup() is
  one of the most called functions. So this extra argument was a huge and for free
  and with total safety optimization. And from understanding, C is about:

    - freedom
    - flexibility and
    - ... optimization

  But how about a little more complex task, like an input functionality.
  It goes like this:
  save terminal state - raw mode - get key - return the key - reset term

  All well except the get key () function. What it will return? An ascii code in
  UTF-8 era is usually useless. You want a signed int (if i'm not wrong).
  So there is no other way, when the first byte indicates a byte sequence, than
  to deal with it, into the getkey() scope. So you probably need an UTF-8 library.
  But in reality is 10/20 lines of code, for example see:
  term_get_input (term_t *this)
  (which is going to be published as a separate project, if time permits).

  It should return reliably the code for all the keyboard keys for all the known
  terminals.
  But this is easy, because is one time job and boring testing, or simply usage from
  different users on different terminals.
  The above mentioned function, it looks that it deals well with xterm/linux/urxvt
  and st from suckless terminals, in 110 lines of code reserved for this.

  This specific example is an example of a function that is written once and works
  forever, without a single change. But it is a function with a broader meaning (the
  functionality), but nevertheless a function. And surely can belong to a standard
  libc, with the logic sense as it makes sense, even if it is rather an interface
  (because it is not a standalone function).
  So point two: libc'es can broad a bit their scope, if C wants to have an evolution
  and a endless future as it deserves.
  It's like forkpty() which wraps perfect the details of three different functions.
  I guess what i'm really talking about is a bit more synthetic than forkpty().

  And it would be even greater if you could easily request a collection, that use
  each other properties, to build a task, like an editor. As standard algrorithm!
  I could [fore]see used a lot. This particular is a prototype and not yet ready,
  but this is the idea and is a general one and it seems that it could be useful
  for all; they just have to connect to C and use these algorithms.

αγαθοκλής

  Grant Finale:
  And the personal reason.

  But why?

  Well, i might die! And this is a proof of logic.

  Seriously,

  This aims at some point in time to be used as an educational tool to
  describe the procedure to write an editor in a UNIX like environment,
  to anser some "why's". To this matter this code qualifies and explains
  quite a lots of "why's". To do this best, the code should adjust to a
  more humanish way. And one day, if all go well, i will flood this code
  with human expressions. And guess this is for free in C.

  As this is for humans that have the desire, but they do need sources.
  As this is for humans that live in strictly environments without tools,
  though they have the desire. As this is for humans that understand that
  the desire is by itself enough to walk the path of learning and creating.
  And with a little bit help of our friends.

  CONCENTRATION:
  Clearly this was designed from C for C with C to C (or close to C anyway,
  though not As Is C: maybe even an even more spartan C (in obvious places:
  where discipline should be practiced with obvious expectations, from both
  sides (programmer - compiler)), but with a concentration to expressionism
  (i could put it even fairlier and|or even a bit voicly (if it was allowed)
  a "tremendous concentration" to expressionism (as an uncontrollable desire
  ))). I mean that was the thought.

  THE FAR AWAY FUTURE:
    - Self Sufficiency, so to be the easiest thing ever to integrate

    - Underlying Machine, so enough capabilities and easy to use them

  ERRORS - WARNINGS - BUGS:
    Compilation:
  This constantly compiled with -Werror -Wextra -Wall and compilation with
  the three compilers should not produce a single warning, else is an error.

  The static targets (since recently in my local copy, where i'm linking against
  libraries (like tinytcc or libcurl), or using functions from the C library) can
  produce warnings like:

    warning: Using 'getpwuid' in statically linked applications requires at runtime
    the shared libraries from the glibc version used for linking

  These warnings can not and should not be hided, but the application at runtime,
  should work/behave correctly, as long this prerequisite/condition is true.

  [update: Last days of December 2019: The above is not quite correct. The static
  targets can produce (like in one case with getaddrinfo()) segfaults.
  So probably (they are not trustable). Plus they are not flexible (though there
  is a noticeable difference in memory usage that is reported by htop(1) and an
  unoticable (almost) better performance). So i lost a bit the desire to put the
  mind to think much of this case when developing.

  This is where things could be improved a bit.

  I do not how but C has to be settle and offer a couple of warrantee's for common
  requests like a string reprecentation of a double.
  I do not use double and such code i will never write probably, but for this i've
  read "today" in zsh mailing list a thread, with people (that are experts in that
  domain) and they found that there is no an established standard way.
  I guess is not totally C faults here, ar they are different approaches and languages
  are free to do whatever they like.

  C++17 added a to_chars() function to handle that case for good.

  So those are small but quite important cases that the mind should think about.

  But the flow is quite important for the programming mind when expressing (probably
  much of the buggish code was written in some of those times that the syncronization
  with the mind and with the actual code is lost).

  So yes, coders do not have to be penaltized about conditions that do not have real
  relation with the actual code, but has to do with details such (for C): is size_t
  enough suitable? Some argue for ptrdiff_t instead, as it helps the compiler to
  detect overflows as size_t is unsigned and ssize_t could be not as big as size_t,
  plus ssize_t (not sure though) is not standard.

  Probably some things should be really settled for good now at the next revision.

  I realize that the knowledge of how the machine works, it is a property of C and
  where C shines (like the golden peak of the Macha-Puchare mountain in a glorious
  morning in a bathe of light).

  But in anycase there quite many of cases that, still today (in 2019?), fail in
  the quite big bucket of undefined behaviors, which is what make people to say
  that C is unsafe, but and other common in 2019 expectations.

  So as a resume, i think that shared targets release a burden when developing or
  the code should handle with #ifdef the case for static targets and exclude code,
  thus loosing functionality. This is both ugly and it can still have unpredictable
  results if there is no care or prior experience. If you don't do that, then you
  still need the same shared libraries that been used during compilation. But then
  you loose the basic advantage of static targets, which is portability, as it is
  supposed indepentable. Unless i'm missing something obvious or the message from
  the linker is misleading.

  But the main disadvantage is (at least when developing) that you can't use quite
  important tools for C, like valgrind, which do not work with static executables.
  If C was created now, valgrind's functionality should definitely considered to
  be (somehow) a built in the language, or at least to the compiler as a special
  mode.

    C strings:
  All the generated strings in the library for the library, are and should be '\0'
  terminated, else is an error.

  Cast'ing from size_t -> int (needs handling)

    API:
    Functions:
  Many of the exposed functions are not safe, especially those for C strings. They
  most made for internal usage, that the data is controlled by the itself code, but
  were made also exposable. But normally, if it wasn't for the extra verbosity, they
  should be post-fixed with an "_un" to denote this unsafety.

  Also it has to be realized, that the intentions to use internal functions that have
  similar functionality with standard libc functions, are not obviously the speed (
  which can not be compared with the optimized functions from libc (here we have to
  deal with mostly small strings, that the difference in execution time, perhaps is
  negligible though)), but:

    - flexibility, as we can tune, by avoiding un-needed checks (since we are making
      these strings, and if we don't make them well, then it is our fault, in any case
      wrong composed strings will fail even in libc functions), and even change the
      signature of the function. Because of this flexibility, then actually we can
      even have a gain in speed, because the conditional checks might be the biggest
      bottleneck actually in the language (perhaps)).

    - self-sufficiency and minimizing of dependencies. I hate to say that, but libces
      are huge beasts for primitive environments. And I even hate to say that, because
      i do not believe in self-sufficiency or anyway self-sufficiency is an utopic
      dream (very nice to believe) and even much more nice to exercise and hunt for
      it like super crazy, but this seems that the route should be the exact opposite.
      Collaboration. But really i do! Many small projects, that trying to assist with
      a smart, generous, practical, simple, easy, suckless, sane/logical way, are already
      have been incorporated in this project, and many others will follow, if we'll
      be blessed with time and motivation (both hard, as we are walking by default
      in a huge unbeliavable high bridge (like the ones made with rope, over the huge
      rivers, on the holy Macha-Puchare mountain on the glorius Annapurna), so we're
      in a so fragile situation, that in a glance of an eye, the time will out, and
      secondly, and i'm positive, that the universe plays very strange games with us).

          As a duty to next humans to come.
          Really there is not so much to tell them, than probably something like this:

          "Look around". "It is You and the Outside of You". "You own a planet.
          Noone else lives in that planet but you."

          "See around. This is the world. What you see is Uniq. No one else can see
          the world from this angle. The only thing that is impossible to ever see,
          is only You. The best thing you can do about this is to see yourself through
          a mirror. So it is You and Your Mirror and the world."

          "You are just the Human Being in that world. A link in this chain, equally
          important like all the others.
          Your planet is important. You really want the best for this planet as You
          are the keeper for this planet. You really want to have total control over
          the environment of your planet, you really want your planet healthy.

          We want the same.

          And it is wise and really smart for You, to feel the same for Us. As the
          outside of You is equally important with You. Though the You - and this
          by default - will always prioritize the self, usually without even the
          self realize it..., it is this magnificent undescribable structure
          that is the real treasure (and this is for you and this is for me
          and this is for all); so this is also (total) equally important with
          You.

          And it is unthinkable important. As this might? is a way to an eternal -
            (where eternal here might translated accurately as "aenaos - αέναος",
             something that has no start as no end in time, it is just this) -
          evolve, so the way to self evolution with no real END defined - even if
          there was a scratch in time, which probably was and probably is wise to
          research this scratch as it might be reproducible. But how this works?
          Probably by just building bridges (like the spiders build their webs).
          Probably just because there is this capability, perhaps it is the only
          required capability. So probably there is a right when we say that the
          Will builds the required chains, by just using the mechanism, which is
          probably free (as free beer here mostly, ... probably).

          So is this web of bridges to each other planets that matters for our ash.
          Because we are alone. As even the paradise is a hell when you are alone."

          "It is Your Time to participate. You got this ticket. Do not wait any other
          ticket like this one. This is THE ticket. You breath for real. You are in
          the game. This is your chance you was hoping for. We are trying again and
          again, hoping that this time we will understand and we will complete the
          mission for the final gift."

          "So you are just a human being, an exceptional miracle.
          Noone will live for you and it is stupid to let others to live for you.
          And probably you do not want to live through others too. Breath Now!"

          Dedicated to the human beings around the Mimbren river, where the self
          sufficiency was a build in their kernel. Especially to their PaPa, to
          Mangas Coloradas.

    - educational, documentation and understanding, as this is a serious personal
      motivation for programming in general, but especially programming in C.

  THANKS:
  We all have to thank each other for their work to open/free source way of thinking
  and life.

  Specifically, we all first owe to our stubborn hero Richard (time will give us all
  the place we actually deserve and rms will have a glory place).

  At the end, we owe all to each other for this perfect __ professionalism __.

  To huge projects like our (G)reat Libc that we owe so much (we just want it a bit
  more flexible and adjust to current environments (current great times that we live
  and comming - difficult times, damn interesting but strange, damn strange. To be
  fair, really really damn strange)).

  But also to all these tiny projects. All those simple humble commands that helped
  us when we need it, as someone generously offered his time, and offered his code
  as he opened her heart or he opened her mind, doesn't really matter here, and he
  contributed it to the community. All these uncountable tiny projects!

  Or geniously code like Lua or crazy one like this by Fabrice of tcc, or beautiful
  code like Ruby. Or complicated that carry also the heaviest responsibility, like
  our Kernel, or blessed tools like our Compiler. Or even a better C like zig.
  Or tools that wrote all that code, like our editor.
  You know the one that has modes! Okey the one of the two editors!

  But and also to those who offered portions of code, that helped the community to
  create excellent products, that even the richest people on earth with all the money
  of the earth can not do.

  And we all know the reasons why.

  Of course first is this fachinating and proud way to evolution that attract us.
  But at the end is that, we really like to do what we do; assisting to our mailing
  lists or writing documentation or managing the infrastructure or writing code.

  Yea i believe this is love and a very very veeeery (really very) smart way to live
  and breath in peace and to feel free, even if you know that you live in a fjail,
      (what you can wait from a world, where our best method to move, our best move
       with our body, where our body it is in its best, it is the only one that we
       can not do it in public - it is absolutely crazy if you think about it),
  that is almost mission impossible to escape. But people did it, so there is a way.
  And if there is a way, we have to found it and describe it.
  So have a good research, as we go back (or forth anyway) to our only duty, which
  it is the way to our Re-Evolution.
  */
```
