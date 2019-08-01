```C
/* This is the groundwork code that serves, first as the underground keystone to support
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
 */

/* State:
   The current state is little beyond the bootstrap|prototype level as the basic
   interface is considered quite close to completion, however:

   - it doesn't do any validation of the incoming data (the data it produces
     is rather controllable, but it cannot handle (for instance) data which
     it might be malformed (UTF-8) byte sequences, or escape sequences).

   - some motions shouldn't behave properly when a character it occupies more than
     one cell width, or behave properly if this character is a tab but (for now)
     a tab it takes one single cell, much like a space. To implement some of this
     functionality an expensive wcwidth() should be included and algorithms should
     adjust.

   - the code is young and fragile in places and some crude algorithms that had
     been used during this initiation, need revision and functions needs to catch
     more conditions.

   - it doesn't try to catch conditions that don't make logical sense and expects
     a common workflow, and there is not really a desire to overflow the universe
     with conditional branches to catch conditions, that never met, and when met,
     there are ways to know where to meet them.

   - the highlighted system is ridiculously simple (note: this never is going to be
     nor ever intented as a general purpose editor; it strives to achieve this level of
     the functionality that can be considered productive but with quite few lines of code;
     as such is full of hand written calculations, which, yes, are compact but fragile)

   - there are no tests to guarantee correctness and probably never will (it just follows,
     and as an excuse, the waterfall model).

   - there are no comments to the code, so it is probably hard to understand and hack happily
     (it is a rather idiomatic C, though i believe it makes the complicated code more 
     readable, as it is based on intentions; but still hard to understand without comments).

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
 */

/* The project was initiated at the end of the autumn of 2018 and at the beginning
   was based at simted (below is the header of this single unit simted.c) and where
   the author implements a simple ed (many thanks).

   *  $name: Jason Wu$
   *  $email: jtywu@uvic.ca
   *  $sid: 0032670$
   *  $logon: jtywu$
   *  Simple Editor written in C using Linked List with undo

   Other code snippets from outer sources should mention this source on top of
   those blocks; if not, I'm sorry but this is probably an omission and should
   be fixed.

   The code constantly runs under valgrind, so it is supposed to have no
   memory leaks, which it is simple not true, because the conditions in a
   editor are too many to ever be sure and true. Also diagnostics seems to
   be dependable from compiler's version which valgrind's version should adjust.

   Also uncountable segmentation faults were diagnosed and fixed thanks to gdb.
 */

/* Buildind and Testing:
  The library can be compiled as a static or as a shared object, by at least gcc,
  clang and tinycc (likewise for the provided test application except that tcc can
  not provide a static executable). To compile the library and the test application
  using gcc as the default compiler (use the CC variable during compilation to change
  that) issue from the src directory of this distribution (every step implies a zero
  exit code to considered successful (catch that):
 */

```
```sh
   cd src

   # build the shared library
   make shared

   # the executable that links against the shared object
   make veda-shared

   # to enable a basic subset of perl compatible regular expression support

   make HAS_REGEXP=1 veda-shared

   # by default writing is disabled unless in DEBUG mode or with:

   make ENABLE_WRITING=1 veda-shared

   # to run the executable and open the source files from itself and destroy issue:

   make run_shared

   # otherwise the LD_LIBRARY_PATH should be used to find the installed shared library,
   # like:

   LD_LIBRARY_PATH=sys/lib sys/bin/veda-101_shared

   # for static targets, replace "shared" with "static", but in that case setting
     the LD_LIBRARY_PATH in the last step is not required.

   # to run it under valgring (requires the shared targets):

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

```
```C
/* All the compilation options:

   DEBUG=1|0              (en|dis)able debug and also writing (default 0)
   HAS_REGEXP=1|0         (en|dis)able regular expression support (default 0)
   HAS_SHELL_COMMANDS=1|0 (en|dis)able shell commands (default 1)
   ENABLE_WRITING=1|0     (en|dis)able writing (default 0)

   /* this provides a way to extend the behavior and|or as an API documentation,
    * but is intended for development (safely ignore much of it) */
   HAS_USER_EXTENSIONS=1|0 (#in|ex)clude src/usr/usr_libved.c (default 0)

 */

/* C
  This compiles to C11 for two reasons:
  - the fvisibility flags that turns out the defaults, a global scope object
    needs en explicit declaration
  - the statement expressions that allow, local scope code with access to the
    function scope, to return a value; also very useful in macro definitions

  It uses the following macros.
 */

  #define ifnot(__expr__) if (0 == (__expr__))
  /* clearly for clarity; also same semantics with SLang's ifnot */

  #define loop(__num__) for (int $i = 0; $i < (__num__); $i++)
  /* likewise for both reasons; here also the $i variable can be used inside the
     block, though it will violate the semantics (if the variable change state),
     which is: "loop for `nth' times".
     Anyway this loop macro is being used in a lot of repositories.
   */

 /* also purely for linguistic and expressional reasons the followings: */

  #define is    ==
  #define isnot !=
  #define and   &&
  #define or    ||
 /* again, those they being used by others too, see for instance the Cello project
    at github/orangeduck/Cello */

 /* and for plain purism, the following */
  #define bytelen strlen
 /* SLang also implements strbytelen(), while strlen() returns character length
    I guess the most precise is bytelen() and charlen() in the utf8 era */

  /* defined but not being used, though i could happily use it if it was enforced
     by a standard */
  #define forever for (;;)

/* Interface and Semantics.
   The is almost a vi[m] like interface and is based on modes, with some of the
   differences explained below to this document:

     - a topline (the first line on screen) that can be disabled, and which by
       default draws:

         current mode - filetype - pid - time

     - the last line on buffer is the statusline and by default draws:

         filename - line number/total lines - current idx - line len - char integer

     - the message line is the last line on screen; the message should be cleared
       after a keypress

     - the prompt row position is one before the message line

     - the command line grows to the top, if it doesn't fit on the line

     - insert/normal/visual/cline modes

   The structure:
   - Every buffer belongs to a window.

   - A window can have unlimited buffers.

   - A window can be splited in frames.

   - An Editor instance can have unlimited in-depended windows.

   - There can be unlimited independent editor instances that can be (de|rea)ttached¹.

   Modes.

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
 | x|DELETE          | delete character               | yes
 | D                 | delete to end of line          |
 | X|BACKSPACE       | delete character to the left   | yes
 |   BACKSPACE and if set and when current idx is 0, deletes trailing spaces|
 |   BACKSPACE and if set is like insert mode         |
 | r                 | replace character              |
 | C                 | delete to end of line (insert) |
 | J                 | join lines                     |
 | i|a|A|o|0         | insert mode                    |
 | u                 | undo                           |
 | CTRL-R            | redo                           |
 | CTRL-L            | redraw current window          |
 | V                 | visual linewise mode           |
 | v                 | visual characterize mode       |
 | W                 | word operations (menu selection)|
 | CTRL-V            | visual blockwise mode          |
 | /                 | search forward                 |
 | ?                 | search backward                |
 | *                 | search current word forward    |
 | #                 | search current word backward   |
 | n                 | search next                    |
 | N                 | search Next (opposite)         |
 | CTRL-w            |                                |
 |   CTRL-w          | frame forward                  |
 |   w|j|ARROW_DOWN  | likewise                       |
 |   k|ARROW_UP      | frame backward                 |
 |   o               | make current frame the only one|
 |   s               | split                          |
 |   n               | new window                     |
 |   h, ARROW_LEFT   | window to the left             |
 |   l, ARROW_RIGHT  | window to the right            |
 |   `               | previous focused window         |
 | ,                 |                                |
 |   n               | like :bn (next buffer)         |
 |   m               | like :bp (previous buffer)     |
 |   ,               | like :b` (prev focused buffer) |
 | :                 | command line mode              |
¹| CTRL-j            | detach editor [extension]      |
 | q                 | quit (not delete) and when buffer type is pager|

Insert mode:
 |
 |   key[s]          |  Semantics                     | count
 |___________________|________________________________|_______
 | CTRL-y            | complete based on the prec line|
 | CTRL-e            | complete based on the next line|
 | CTRL-a            | last insert                    |
 | CTRL-x            | completion mode                |
 |   CTRL-l or l     | complete line                  |
 |   CTRL-f or f     | complete filename              |
 | CTRL-n            | complete word                  |
 | CTRL-v            | insert character (utf8 code)   |
 | CTRL-k            | insert digraph                 |
 | CTRL-r            | insert register contents (charwise only) |
 | motion normal mode commands with some differences explained bellow|
 | HOME              | goes to the beginning of line   |
 | END               | goes to the end of line        |
 | escape            | aborts                         |

Visual mode:
 |   key[s]          |  Semantics                     | count
 |___________________|________________________________|_______
 | >, <              | indent [in|out]                | yes
 | d                 | delete selected                |
 | y                 | yank selected                  |
 | s                 | search selected [linewise]     |
 | w                 | write selected [linewise]      |
 | i|I               | insert in front [blockwise|    |
 | c                 | change [blockwise]             |
 | both commands above use a readline instance (but without tab|history completion)|
 | x|d               | delete [[block|char]wise]      |
 | +                 | send selected to XA_CLIPBOARD (char|line)wise|
 | *                 | send selected to XA_PRIMARY   (char|line)wise|
 | e                 | edit as filename [charwise]    |
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
 | CTRL-l            | redraw line                    |
 | TAB               | trigger completion[s]          |

Search:
 |   key[s]          |  Semantics                     | count
 |___________________|________________________________|_______
 | CTRL-n            | next                           |
 | CTRL-p            | previous                       |
 | carriage return   | accepts                        |
 | escape            | aborts                         |

/* In this implementation while performing a search, the focus do not change
   until user accepts the match. The results and the dialog, are shown at the
   bottom lines (the message line as the last line on screen).
   Also by default, pattern is just string literal and the code is using strstr()
   to perform the searching. The sample application can use a subset of perl like
   regular expressions.
   It searches just once in a line, and it should highlight the captured string
   with a proper message composed as:

     |line_nr byte_index| matched line

 */

/* Command line mode:
   (note) Commands do not get a range as in vi[like], but from the command line
   switch --range=. Generally speaking the experience in the command line should
   feel more like a shell and specifically the zsh completion way.

   Auto completions (triggered with tab):
    - commands
    - arguments
    - filenames

   If an argument (like a substitution string) needs a space, it should be quoted.

   If a command takes a filename or a bufname as an argument, tab completion
   will quote the argument (for embedded spaces).

   Command completion is triggered when the cursor is at the first word token.

   Arg completion is triggered when the first char word token is an '-' or
   when the current command, gets a bufname as an argument.

   In any other case a filename completion is performed.

   Options are usually long (that means prefixed with two dashes), unless some
   established/unambiguous like (for now):
    -i  for interactive

   Default command line switches:
   --range=...
      valid ranges:
      --range=%    for the whole buffer
      --range=linenr,linenr counted from 1
      --range=.    for current line
      --range=[linenr|.],$  from linenr to the end
      --range=linenr,. from linenr to current line
   without --range, assumed current line number

   --global          is like the g flag on vim substitute
   --interactive,-i  is like the c flag on vim substitute
   --append          is like  >> redirection (used when writing to another file)

   --pat=`pat'       a string describes the pattern which by default is a literal
                     string; the sample application can use a subset (the most basic)
                     of perl regular expressions: in that case pattern can start with
                     (?i) to denote `ignore case` and for syntax support see at:
                     modules/slre/docs/syntax.md

   --sub=`replacement string'
                     - '&' can be used to mean the full captured string
                     -  to include a white space, the string should be (double) quoted,
                     -  if has regular expression support, then \1\2... can be used
                        to mean, `nth' captured substring numbering from one.

   Commands:
   ! as the last character indicates force, unless is a shell command

   :s[ubstitute] [--range=] --pat=`pat' --sub=`sub' [-i,--interactive] --global
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
   :r[ead] filename       (read filename into current buffer)
   :r! cmd                (read into buffer cmd's standard output)
   :!cmd                  (execute command)
   :diff                  (shows a unified diff in a diff buffer, see Unified Diff)
   :diffbuf               (change focus to the `diff' window/buffer)
   :vgrep --pat=`pat' fname[s] (search for `pat' to fname[s])
   :redraw                (redraw current window)
   :searches              (change focus to the `search' window/buffer)
   :messages              (change focus to the message window/buffer)
   :testkey               (test keyboard keys)
   :q[!]                  (quit (if force, do not check for modified buffers))

   User defined (through an API mechanism):
   The test application provides a sample battery command to print the status and capacity
   and which can be invoked as  :~battery  (i thought it makes sense to prefix
   such commands with '~' as it is associated with $HOME (as a user stuff), and
   mainly as a way to distinguish such commands from the core ones, as '~' is ascii
   code 126, so these will be the last printed lines on tab completion or|and can be
   narrowed; but there isn't the prefixed '~' a prerequisite, but in the future is
   logical to use this as pattern to map it in a group that might behave with other
   ways).
 */

/* History Completion Semantics (command line and search)
   - the ARROW_UP key starts from the last entry set in history, and scrolls down
     to the past entries

   - the ARROW_DOWN key starts from the first entry, and scrolls up to most recent
 */

/* Searching on files (a really quite basic emulation of quickfix vim's windows).

   The command :vgrep it takes a pattern and at least a filename as argument[s]:

    :vgrep --pat=`pattern' file[s]

   This should open a unique window intended only for searches and re-accessible
   with:

   :searches  (though it might be wise a `:copen' alias (to match vim's expectation))

   This window should open a frame at the bottom, with the results (if any) and it
   will set the pointer to the first item from the sorted and unique in items list.

   A carriage return should open the filename at the specific line number at the
   frame 0.

   A `q' on the results frame (the last one), will quit the window and focus again
   to the previous state (as it acts like a pager).
  */

/* Unified Diff
  This feature requires (for now) the `diff' utility.

  The :diff command open a dedicated "diff" buffer, with the results (if any) of
  the differences (in a unified format), between the buffer in memory with the one
  that is written on the disk. This buffer can be quickly closed with 'q' as in a
  pager (likewise for the other special buffers, like the message buffer).
  Note that it first clears the previous diff.

  The :diffbuf command gives the focus to this same buffer.

  Another usage of this feature is when quiting normally (without forcing) and
  the buffer has been modified.
  In that case a dialog (below) presents some options:

    "[bufname] has been modified since last change
     continue writing? [yY|nN], [cC]ansel, unified [d]iff?"

   on 'y': write the buffer and continue
   on 'n': continue without writing
   on 'c': cansel operation at this point (some buffers might be closed already)
   on 'd': print to the stdout the unified diff and redo the question (note that
           when printing to the stdout, the previous terminal state is restored;
           any key can bring back the focus)

/* Glob Support
   (for now)
    - this is limited to just one directory depth
    - it uses only '*'
    - and understands (or should):
      `*'
      `/some/dir/*'
      `*string' or `string*string' or `string*'
        (likewise for directories)
 */

/* Registers and Marks are supported with the minimal features, same with
   other myriad details that needs care.

   Mark set:
   [abcdghjklqwertyuiopzxcvbnm1234567890]
   Special Marks:
   - unnamed mark [`] jumps to the previous position

   Register set:
   [abcdghjklqwertyuiopzxcvbnm1234567890]
   Special Registers:
    - unnamed register ["] (default)
    - filename register [%]
    - last search register [/]
    - last command line register [:]
    - registers [+*] send|receive text to|from X clipboard (if xclip is available)
    - blackhole [_] register, which stores nothing
    - CTRL('w') current word
    - [=] expression register (not yet implemented so does nothing)
 */

/* Menus
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
  */

/* Application Interface.
    The library exposes a root struct, two de|init public functions that
    de|initialize the structure and quite a lot of opaque pointers.

    The code uses an object oriented style, though it is just for practical
    reasons as permits mostly code organization, simplicity, abstraction,
    compactness, relationship, and especially quite a lot of freedom to develop
    or overridde functionality.
    A generic comment:
      But the main advantage of this approach is that there is no global state;
      the functions acts on an instance of a type, or if not, simple their scope
      is narrow to a specific job that can not change state to the environment.

      In this whole library, there is neither one out of function scope variable.

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

   The main structure is the editor structure type, and contains other nested
   relative structures (that contain others too):

   (the basic three)
   - editor type
   - win type
   - buf type
   and
   - string type
   - terminal type

   Many of the algorithms are based on a (usually) double linked list with
   a head and a tail and in many cases a current pointer, that can act (at
   the minimum) as an iterator;  but there is no specific list type (just
   abstracted macros that act on structures. Those structs, and based on
   the context (specific type), can contain (some or all) of those (everybody
   C favorites) pointers.

   A couple of notes regarding the inner code.
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

   This reminds a lot of perl (i think) and allows great consistency and little
   thought (you just have to know the properties and not how to access them
   through the complicated real code). This should save a hell out of time,
   during development and it gets very quickly a routine.

   To access a nested structure there is also a very compact and easy to use way:

   My(Class).method (...)

   This is just another syntactic sugar, to access quickly and with certainity that
   you got the pointers right, nested structures. As "My" doesn't pass any argument,
   any argument[s] should be given explicitly.

   Generally speaking, those macros, are just syntactic sugar, no code optimization,
   or the opposite, neither a single bit of penalty. It allows mainly expressionism
   and focus to the intentions and not to __how__ to get right writing.

   In that spirit, also available are macros, that their sole role is to abstract
   the details over type creation/declaration/allocation, that assists to quick
   development.
   The significant ones: AllocType, NewType, DeclareType. Those are getting an
   argument with the type name but without the _t extension, which is the actual
   type.

   Finally, a couple of macros that access the root editor type or the parent's
   (win structure) type. Either of these three main structures have acces (with one
   way or the other) to all the fields of the root structure, so My(Class) macro
   as well the others too, works everywhere the same, if there is a proper
   declared "this".

   In the sample executable that uses the library, the "My" macro, has the same
   semantics, but for the others macros should be no need (though it is too early
   maybe even talk about an API), as they should be covered with the following way:

    Ed.[subclass or method] ([args, ...])

   this code has access to the root structure, and should use the get/set
   specific to types methods to access the underlying properties.
 */

/* Sample Application
   This application adds regular expression support, by using a slightly modified
   version of the slre machine, which is an ISO C library that implements a subset
   of Perl regular expression syntax, see and clone at:

     https://github.com/cesanta/slre.git
   Many thanks.

   It is implemented outside of the library by overriding methods from the Re structure.
   To enable it use "HAS_REGEXP=1" during compilation.

   The substitution string in the ":substitute command", can use '&' to denote the
   full captured matched string, but also captures of which denoted with \nth.
   It is also possible to force caseless searching, by using (like pcre) (?i) in front
   of the pattern. This option won't work with multibyte characters. Searching for
   multibyte characters it should work properly though.

   The application can also run shell commands or to read into current buffer
   the standard output of a shell command. Interactive applications might have
   unexpected behavior in this implementation. To disable these features (as they
   are enabled by default) use "HAS_SHELL_COMMANDS=0" during compilation.
 */

/* Memory Interface
   The library uses the reallocarray() from OpenBSD (a calloc wrapper that catches
   integer overflows), and it exposes a public mutable handler function that is
   invoked on such overflows or when there is not enough memory available errors.

   This function is meant to be set by the user of the library, on the application
   side and scope. The provided one exits the program with a detailed message.
   I do not know the results however, if this function could wait for an answer
   before exits, as (theoretically) the user could free resources, and return for
   a retry. The function signature should change to account for that.

   The code defines two those memory wrappers.
   Alloc (size) and Realloc (object, size). Both like their counterparts, they return
   void *.
 */

/* LICENSE:
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
 */

/* NOTE:
   This code it contains quite a lot of idiomatic C and (probably) in many cases it
   might do the wrong thing. It is written by a non programmer, that taughts himself
   C at his fifty two, and it is focused on the code intentionality (if such a word).
   C was choosen because it is a high level language, but it is tighted up with the
   machine so that can be considered primitive, and there shouldn't be any interpreter
   in between, and finally because C is about algorithms and when implemented properly
   the code happily lives for ever without a single change. Of course the properties
   are machine properties and such endeavor it never ends and needs responsibility,
   extensive study and so time. I do not know however if (time) is generous.
 */

/* TO DO
   Seriously! Please do not use this on sensitive documents. There are many conditions
   that i know and for certain quite many that i don't know, that it might need to be
   handled.
   The bad thing here is that there is a workflow and in that workflow range i fix
   things or develop things. But for sure there are conditions or combinations of them
   out of this workflow.

   But Really. The real interest is to use this code as the underline machine to
   create another machine. Of course can have some value as a reference, if there is
   something with a value to deserve that reference.

   But it would also be nice (besides to fix bugs) if i could reserve sometime to fix
   the tabwidth stuff, which is something i do not have the slightest will to do,
   because i hate tabs in code (nothing is perfect in this life).
   I'd rather give my time (with low priority though) to split those two different
   concepts.

  The first level or better the zero level, the actual editor code:

   - inserting/deleting lines (this should meet or extend ed specifications)

   - line operations where line as:
     An array of characters that provide information or methods about the
     actual num bytes used to consist each character and the utf8 representation.

     Line operations: inserting/deleting/replacing..., based on that type.

   - /undo/redo

   - basically the ed interface, which at the beginning coexisted happily with
     the visual one (at some point too much code were written at once and which
     it should adjust to work in both interfaces, but the abstraction level wasn't
     enough abstracted for this, so i had to move on); but it is a much much simpler
     editor to implement, since there is absolutely no need to handle the output
     or|and to set the pointer to the right line pointed to the right cell at the
     correct byte[s] (it is very natural such an interface to over complicate the
     code and such code to have bugs).
     From my humble experience, the worst bug that can happen is to have a false
     interpretation of the position, so you actually edit something else than what
     you thing you edit.

  And the second level the obvious one (the above mentioned bugged one).
 */

/* Acknowledgments, references and inspiration (besides the already mentioned):

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
    - numerous stackoverflow posts
    - numerous codebases from the open source enormous pool
 */

/* My opinion on this editor.
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
   his program, so it could optimize it for the specific environment. As an example:
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

   It will also has to deal with the terminal escape sequences and returns reliably
   the code for all the keyboard keys for all the known terminals.
   This is easy because is one time job and boring testing, or simply usage from
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
 */

αγαθοκλής

/* Grant Finale
   and the personal reason.

  But why?

  Well, i might die! And this is a proof of logic.
 */
```
