```C
/* This is a product that was born during C's educational period, and it
 * is nothing else but just another text visual editor, with the initial
 * intentions to be used as a C library and as it is. That means that it
 * can be used without external dependencies, by either "include" it as a
 * single file unit or by linking to it. Because it was meant to be used
 * in primitive environment, it should minimize (with time) even the libc
 * dependency and should provide the required functions. As such it doesn't
 * require a curses library and doesn't use the mb* family of functions.

 * The current state is at the bootstrap level. That means that the editor
 * can develop itself, as the basic interface is considered quite close to
 * completion, however:

 * - it doesn't do any validation of the incoming data (the data it produces
 *   is rather controllable, but it cannot handle (for instance) data which
 *   it might be malformed (UTF-8) byte sequences, or escape sequences.

 * - some motions don't behave properly when a character it occupies more than
 *   one cell width, or behave properly if this character is a tab but (for now)
 *   a tab it takes one single cell, much like a space. To implement that required
 *   functionality an expensive wcwidth() should be included.

 * - the code is young and is tested (and develops) based on the usage and the
 *   needs, so time is needed for stabilization, and some crude algorithms that
 *   had been used during this bootstapped period, need a revision as the code
 *   learns by itself.

 * - currently searching and substitution is done using literal text and not a
 *   regular expresion. As the prerequisity is to be independed (at some point)
 *   from libc, this has to be a minimal external perl like regexp machine.

 * It is published mainly for recording reasons and a personal one.
 * But mainly because at some point the integration might become very tighted to
 * the environment, and there is no certainity for a satisfactory abstraction.
 * Also because it was written mainly for the C programming language it will
 * integrate the tcc compiler at some point, both in time and space (mean unit).
 * In short is not suited for nothing more than experinment and maybe and if
 * there is something that worths at the end, for inspiration.

 * The original purpose was to record the development process so it could be
 * usefull as a (document) reference for an editor implementation, but the
 * git commits ended as huge incrementals commits and there were unusable to
 * describe this "step by step" guide. Perhaps in the next revision.

 * The project was initiated at the end of the autumn of 2018 and at the begining
 * was based at simted (below is the header of this single unit simted.c) and where
 * the author implements a simple ed (many thanks).

   *  $name: Jason Wu$
   *  $email: jtywu@uvic.ca
   *  $sid: 0032670$
   *  $logon: jtywu$
   *  Simple Editor written in C using Linked List with undo

 * Code snippets from outer sources should mention this source on top of
 * those blocks; if not, this is an omission and should be fixed (sorry
 * if there are such cases).

 * The code constantly runs under valgrind, so it is supposed to have no
 * memory leaks, which it is simple not true, because the conditions in a
 * editor are too many to ever be sure and true.

 * Also uncountable segmentation faults were diagnosed and fixed thanks to gdb.

 * The library code can be compiled as a shared or as a static library, both
 * with gcc and tcc.
 *
 * Also included a very minimal executable that can be used to test the code.
 * This executable can be compiled also as static with gcc, but not with tcc.

 * To test the editor issue (using gcc as the default compiler):

   cd src && make veda-shared && make run

   * to enable regular expression support issue:

   cd src && make HAS_REGEXP=1 veda-shared && make run

 * or with the tcc compiler

   cd src && make CC=tcc HAS_REGEXP=1 veda-shared && make run

 * (this will open the source files of itself)


 * C
 * This compiles to C11 for two reasons:
 * - the fvisibility flags that turns out the defaults, a global scope object
     needs en explicit declaration
 * - the statement expressions that allow, local scope code with access to the
     function scope, to return a value; also very usefull in macro definitions

  * It uses the following macros:

  #define ifnot(__expr__) if (0 == (__expr__))
  /* clearly for clarity; also same semantics with SLang's ifnot */

  #define loop(__num__) for (int $i = 0; $i < (__num__); $i++)
  /* likewise for both reasons; here also the $i variable can be used, though it
   * violates the loop semantics if the variable change state into the block.
   * But for a moment i liked the idea of an close implementation of SLang's _for.
   * Anyway this loop macro is being used in a lot of repositories and though
   * is defined it is not being used in current code.
   */

 /* also purely for linguistic and expressional reasons the followings:
  * (again they being used by others too, see at github/orangeduck/Cello
  */

  #define is    ==
  #define isnot !=
  #define and   &&
  #define or    ||

  /* and for plain purism, the following */
  #define bytelen strlen
  /* SLang also implements strbytelen(), while strlen() returns character length
   * I guess the most precise is bytelen() and charlen() in the utf8 era */

  /* defined but not being used, though i could happily use it (same with the loop
   * macro) if it was enforced */
  #define forever for (;;)

/* Interface
 * It is a vi[m] like interface with the adition of a topline that can be disabled,
 * and it is based on modes.

 * Every buffer belongs to a window.
 * A window can have unlimited buffers.
 * A window can also be splited in frames.
 * An Editor instance can have unlimited windows.
 * There can be unlimited editor instances.

 * The highlight system currently works only for C sources; editing C sources is one
 * of the main reasons for this editor, so it is and will be tight up with C more.

 * Modes and Semantics

 * These are mostly like vim and which of course lack much of the rich feature set
 * of vim. That would require quite the double code i believe.
 */

Normal mode:
 |
 |   key[s]          |  Semantics                     | count
 | __________________|________________________________|_______
 | CTRL-b, PG_UP     | scroll one page backward       | yes
 | CTRL-f, PG_DOWN   | scroll one page forward        | yes
 | HOME, gg          | home row                       |
 | END,  G           | end row                        |
 | h,l               | left|right cursor              | yes
 | ARROW[LEFT|RIGHT] | likewise                       | yes
 | k,j               | up|down line                   | yes
 | ARROW[UP|DOWN]    | likewise                       | yes
 | $                 | end of line                    |
 | 0                 | begining of line               |
 | count [gG]        | goes to line                   |
 | e                 | end of word (goes insert)      | yes
 | E                 | end of word                    | yes
 | ~                 | switch case                    |
 | m[mark]           | mark[a-z]                      |
 | `[mark]           | mark[a-z]                      |
 | ^                 | first non blank character      |
 | CTRL-[A|X]        | [in|de]crements (ints only)    | yes
 | >, <              | indent [in|out]                | yes
 | [yY]              | yank [char|line]wise           | yes
 | [pP]              | put register                   |
 | d[d]              | delete line[s]                 | yes
 | x|DELETE          | delete charecter               | yes
 | D                 | delete to end of line          |
 | X|BACKSPACE       | delete charecter front         | yes
 | r                 | replace character              |
 | C                 | delete to end of line (insert) |
 | J                 | join lines                     |
 | i|a|A|o|0         | insert mode                    |
 | u                 | undo                           |
 | CTRL-R            | redo                           |
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
 |   CTRL-w          | frame forward                  |
 |   w|j|ARROW_DOWN  | likewise                       |
 |   k|ARROW_UP      | frame backward                 |
 |   o               | frame only                     |
 |   s               | split                          |
 |   n               | new window                     |
 |   h, ARROW_LEFT   | window to the left             |
 |   l, ARROW_RIGHT  | window to the right            |
 |   `               | previus focused window         |
 | :                 | command line mode              |
 | CTRL-j            | detach editor [extension]      |

Insert Mode:
 |
 |   key[s]          |  Semantics                     | count
 |___________________|________________________________|_______
 | CTRL-y            | complete based on the prec line|
 | CTRL-e            | complete based on the next line|
 | CTRL-a            | last insert                    |
 | CTRL-x            | completion mode                |
 |   CTRL-l or l     | complete line                  |
 | CTRL-n            | complete word                  |
 | CTRL-v            | insert character (utf8 code)   |
 | CTRL-k            | insert digraph                 |
 | motion normal commands with some differences explained bellow
 | HOME              | goes to the begining of line   |
 | END               | goes to the end of line        |
 | escape            | aborts                         |

Visual Mode:
 |   key[s]          |  Semantics                     | count
 |___________________|________________________________|_______
 | >, <              | indent [in|out]                | yes
 | d                 | delete selected                |
 | y                 | yank selected                  |
 | s                 | search selected [linewise]     |
 | w                 | write selected [linewise]      |
 | i|I               | insert in front [blockwise|    |
 | c                 | change [blockwise]             |
 | x|d               | delete [[block|char]wise]      |
 | escape            | aborts                         |

Search:
 |   key[s]          |  Semantics                     | count
 |___________________|________________________________|_______
 | CTRL-n            | next                           |
 | CTRL-p            | previous                       |
 | carriage return   | accepts                        |
 | escape            | aborts

/* In this implementation while performing a search, the focus do not change
 * until user accepts the match. The results and the dialog, are shown at the
 * bottom lines (the message line as the last line on screen).
 * Also at this state, pattern is just string literal and the code is using
 * strstr() to perform the searching. (UPDATE) The sample application can use
 * now regular expressions.
 * It searches once in the line, and it should highlight the captured string
 * with a proper message with the matched line, line number and byte index.
 */

/* Command line mode:
 * (note) Commands do not get a range as in vi[like], but from the command line
 * switch --range=. Generally speaking the experience in the command line should
 * feel more like a shell (the tab completion here works for commands, arguments
 * (if the token is an '-'), or for filename[s] and the intention is to provide
 * more completion types on every mode).

 * Default command line switches:
 * --range=...
 *    valid ranges:
 *    --range=%    for the whole buffer
 *    --range=linenr,linenr counted from 1
 *    --range=.    for current line
 *    --range=[linenr,.],$  from linenr to the end
 *    --range=linenr,. from linenr to current line
 * without --range, assumed current line number
 *
 * --global          is like the g flag
 * --interactive,-i  is like the c flag
 * --append          is like  >> redirection

 * Commands:
 * ! as the last character indicates force

 * :s[ubstitute] [--range=] --pat=`pat' --sub=`sub' [-i,--interactive] --global
 * At this state `pat' is a string literal, `sub' can contain '&' to denote the
 * captured pattern (an '&' can be inserted by escaping). (UPDATE: It is possible
 * to use regular expressions, see below at the Sample Application section).

 * :w[rite][!] [filename [--range] [--append]]
 * :e[!] filename        (reloading has not been implemented properly)
 * :b[uf]p[rev]          (buffer previous)
 * :b[uf]n[ext]          (buffer next)
 * :b[uf][`|prevfocused] (buffer previously focused)
 * :b[uf]d[elete][!]     (buffer delete)
 * :w[*] (functions that implement the above buf functions, with regards to windows)
 * :enew filename        (new buffer on a new window)
 * :r[ead] filename      (read filename into current buffer)
 * :wq!                  (write quite)
 * :q!                   (quit)
 */

 /* Registers and Marks are supported with the minimal features, same with
  * other myriad details that needs care.
  * But Macros and other myriad endless vim features are not; this space (and
  * time) is reserved for specific extensions. It has to be noted here, that
  * for long, an ed (the venerable ed) interface cooexisted with the visual one.
  */

 /* Application Interface.
  * The library exposes a root struct, two de|init public functions that
  * initialize the structure and quite a lot of opaque pointers.

  * The code uses an object oriented style, though it is just for practical
  * reasons. It is my belief that this is the right way for C, as permits
  * code organization, simplicity, abstraction, compacteness, relationship,
  * easy to integrate and understand and also quite a lot of freedom to develop
  * new functions or to override a method with a custom one.

 * The main structure is the editor structure type, and contains other nested
 * relative structures (that contain others too):

 * (the basic three)
 * - editor type
 * - win type
 * - buf type
 * and
 * - string type
 * - terminal type

 * Many of the algorithms are based on a (usually) double linked list with
 * a head and a tail and in many cases a current pointer, that can act (at
 * the minimum) as an iterator;  but there is no specific list type (just
 * abstracted macros that act on structures. Those structs, and based on
 * the context (specific type), can contain (some or all) of those (everybody
 * C favorites) pointers;  that can be complicated and hard to get them right,
 * but if you got them right, then there is this direct and free of charge,
 * access to the underlying machine and finally, to the bits located to this
 * machine adress;  in my humble opinion is the (quite the one) property of C,
 * or the property that could deserve the pain;  opening and working with a
 * quite big lexicon file, the operations are instant (to move around is just
 * a matter of simple arithmetic, a[n] [in|de]crementation of the current buffer
 * pointer), and the memory consumption is ridicously low, as it is borrowing, and
 * this stuff is not an implementation, it is a property; and of course that was
 * a satisfaction and rather not a boring one).

 * Currently the library consists of two levels actually, the actual editing part
 * and the interface. Those is wise to split, to understand and simplify the code.

 * A couple of notes regarding the inner code.
 * The inner code it uses a couple of macros to ease the development, like:

    self(method, [arg,...])

 * This awaits an accesible "this" declared variable and it passes the specific
 * type as the first argument to the calling function.
 * In this context "this" is an abstracted variable and works with objects with
 * specific fields. Types like these, have a "prop" field that holds object's
 * variables and as well a "self" dedicated field for methods (function pointers
 * that their function signature contains "this" type as their first argument).

 * The properties of such types are accesible with the following way:

   $my(prop)

 * This reminds a lot of perl (i think) and allows great consistency and little
 * thought (you just have to know the properties and not how to access them
 * through the complicated real code). This should save a hell out of time,
 * during development and it gets very quickly a routine.

 * To access a nested structure there is also a very compact and easy to use way:

   My(Class).method (...)

 * This is just another syntactic sugar, to access quickly and with certainity that
 * you got the pointers right, nested structures. As "My" doesn't pass any argument,
 * any argument[s] should be given explicitly.

 * Generally speaking, those macros, are just syntactic sugar, no code optimization,
 * or the oposite, neither a single bit of penalty. It allows mainly expressionism
 * and focus to the intentions and not to __how__ to get right writting.

 * In that spirit, also available also are macros, that their sole role is to
 * abstract the details over type creation/declaration/allocation, that assists to
 * quick development.
 * The significant ones: AllocType, NewType, DeclareType. Those are getting an
 * argument with the type name but without the _t extension, which is the actual
 * type.

 * Finally, a couple of macros that access the root editor type or the parent's
 * (win structure) type. Either of these three main structures have acces (with one
 * way or the other) to all the fields of the root structure, so My(Class) macro
 * as well the others too, works everywhere the same, if there is a proper
 * declared "this".

 * In the sample executable that uses the library, the "My" macro, has the same
 * semantics, but for the others macros should be no need, as they should be
 * covered with the following way:

    Ed.[subclass or method] ([args, ...])

 * this code has acces to the root structure, and should use the get/set
 * specific to types methods to access the underlying properties.

 * Sample Application
 * This application adds regular expression support, by using a slightly modified
 * version of the slre machine, which is an ISO C library that implements a subset
 * of Perl regular expression syntax, see:

     https://github.com/cesanta/slre.git

 * Many thanks.
 * It is implemented outside of the library by overiding methods from the Re structure.
 * To enable it use "HAS_REGEXP=1" during compilation.

 * Memory Interface
 * The library uses the reallocarray() from OpenBSD (a calloc wrapper that catches
 * integer overflows, and exposes a public mutable handler function that is invoked
 * on such overflows or when there is not enough memory available errors.
 * This function is meant to be set by the user of the library, on the application
 * side and scope. The provided one exits the program with a detailed message.

 * It implements two those wrappers. Alloc (size) and Realloc (object, size).
 * Both they return void *.

 /* LICENSE:
  * I wish we could do without LICENSES. In my world it is natural to give credits
  * where they belong, and the open source model is assumed, as it is the way to
  * evolution. But in this world and based on the history, we wouldn't be here if it
  * wasn't for GPL. Of course, it was the GPL and the GNU products (libc, compiler,
  * debugger, linker, coreutils and so on ...), that brought this tremendous code
  * evolution, so the whole code universe owes and should respect GNU.
  *
  * However the MIT like LICENSES are more close to the described above spirit,
  * though they don't emphasize on the code freedom as GPL does (many thanks for
  * that).
  * The trueth is that my heart is with GNU, but my mind refuces to accept the existance
  * of the word LICENSE.
  * I wish there was a road to an unlicense, together with a certain certainity, that
  * we (humans) full of the gained consciense, can guard it and keep it safe, as it
  * is also (besides ideology) the road to paradise.
  */

/* NOTE:
 * This code it contains quite a lot of idiomatic C and (probably) in many cases it
 * might do the wrong thing. It is written by a non programmer, that taughts himself
 * C at his fifty two and it is focused on the code intentionality (if such a word).
 * C was choosen because it is a high level language, but it is tighted up with the
 * machine so that can be considered primitive, and there shouldn't be any interpeter
 * in between, and finaly because C is about algorithms and when implemented properly
 * the code hapilly lives for ever without a single change. Of course the properties
 * are machine properties and such endevour it never ends and needs responsibility,
 * extensive study and so time. I do not know however if (time) is generous.
 */

αγαθοκλής
```
