```C
/* This is a product that was born during C's educational period, and it is
 * nothing else but just another text visual editor, but that can be used as
 * a library and as it is.

 * That means an independed functional (ala vim) editor, that either can be
 * "include" it as a single file unit or by linking to it, without any other
 * prerequisite. It was written (as a confession) somehow at the state in time,
 * a little bit after the vt100 introduction, but with a c11 compiler and with
 * the knowledge that a character can be represented with more than one byte.

 * The current state is at the bootstrap|prototype level. That means that the
 * editor can develop itself, and the basic interface is considered quite close
 * to completion, however:

 * - it doesn't do any validation of the incoming data (the data it produces
 *   is rather controllable, but it cannot handle (for instance) data which
 *   it might be malformed (UTF-8) byte sequences, or escape sequences.

 * - some motions don't behave properly when a character it occupies more than
 *   one cell width, or behave properly if this character is a tab but (for now)
 *   a tab it takes one single cell, much like a space. To implement some of this
 *   functionality an expensive wcwidth() should be included and algorithms should
 *   adjust.

 * - the code is young and fragile in places. It develops based on the itself
 *   development, by usage and needs, and those crude algorithms that had been used
 *   during this initiation, need revision.

 * It is published mainly for recording reasons and a personal one.
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
 * those blocks; if not, I'm sorry but this is probably an omission and should
 * be fixed.

 * The code constantly runs under valgrind, so it is supposed to have no
 * memory leaks, which it is simple not true, because the conditions in a
 * editor are too many to ever be sure and true.

 * Also uncountable segmentation faults were diagnosed and fixed thanks to gdb.

 * The library code can be compiled as a shared or as a static library, with
 * gcc, clang and tcc.

 * The development environment is void-linux and probably the code it won't compile
 * on other unixes without some ifdef's.
 *
 * Also included a very minimal executable that can be used to test the library code.
 * This can be compiled also as static with gcc and clang, but not with tcc.

 * To test the editor issue (using gcc as the default compiler):

   cd src && make veda-shared

   * or to enable regular expression support issue:

   cd src && make HAS_REGEXP=1 veda-shared

   * by default writing is disabled, unless in DEBUG mode:

   cd src && make HAS_REGEXP=1 DEBUG=1 veda-shared

   * and finally to run the executable and open the source files from itself, issue:

   make run

 * similarly with clang and tcc compilers, but the CC Makefile variable should be
 * set and one way to do this, is from the command line:
   make CC=tcc ...

 * to run it under valgring

   make check_veda_memory_leaks

 * and under gdb (which is the preferred way)

   make debug_veda

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
   * macro) if it was enforced by a standard */
  #define forever for (;;)

/* Interface and Semantics.

 * It is a vi[m] like interface with the adition of a topline that can be disabled,
 * and it is based on modes.

 * Every buffer belongs to a window.
 * A window can have unlimited buffers.
 * A window can also be splited in frames.
 * An Editor instance can have unlimited independed windows.

 * There can be unlimited independed editor instances (can be (de|rea)ttached)¹.

 * Modes.

 * These are mostly like vim and which of course lack much of the rich feature set
 * of vim. That would require quite the double code i believe.
 */

                // FIRST DRAFT //

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
¹| CTRL-j            | detach editor [extension]      |
 | q                 | quit (not delete) and when buffer type is pager|

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
 | motion normal mode commands with some differences explained bellow|
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
 * :messages             (message buffer)
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
  * de|initialize the structure and quite a lot of opaque pointers.

  * The code uses an object oriented style, though it is just for practical
  * reasons. It is my belief that this is can be a wise way for C, as permits
  * code organization, simplicity, abstraction, compacteness, relationship,
  * easy integration and also quite a lot of freedom to develop or overide new
  * functionality.
  * But the main advantage of this approach is that there is no global state,
  * the functions acts on a an instance of a type, or if not, simple their scope
  * is narrow to a specific job that can not change state to the environment.
  * In this whole library, there is neither one global variable and just one static
  * that utilizes editor commands (even this can be avoided, by adding a property,
  * however this (besides the needless overhead) abstracts abstraction, while the
  * the route in code like this, should be the other way around)).

  * In short, compact environments that can unified under a root structure, with
  * functions that act on a known type or to expected arguments with narrow scope.

  * The heavy duty is to the entry points or|and to the communication ports, that
  * could check and sanitize external data, or|and to interfere with a known and
  * controlled protocol with the outer environment, respectively.
  * It's just then up to the internal implementation to get the facts right, by
  * checking conditions, but in known and limited and with strong borders now space.

  * This could also speed up execution time (by avoiding un-needed checks of a data
  * that is known for certain that is valid, because, either it is produced (usually)
  * by the self/own, or has already been checked, by previous users in the toolchain
  * stack.
  * But the main benefit, is that it brings clarity to the code and concentration
  * to the actual details. However that it could also be considered as a functional
  * envrironment (with the no-side-effect meaning) or as an algorithm environment.
  * The 'wise future for C' reference to the later mostly.

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
 *
 * The substitution string in the ":substitute command", can use '&' to denote the full
 * captured matched string, but also captures of which denoted with \nth.
 * It is also possible to force caseless searching, by using (like pcre) (?i) in front
 * of the pattern. This option won't work with multibyte characters. Searching for
 * multibyte characters it should work properly though.

 * Memory Interface
 * The library uses the reallocarray() from OpenBSD (a calloc wrapper that catches
 * integer overflows), and it exposes a public mutable handler function that is
 * invoked on such overflows or when there is not enough memory available errors.

 * This function is meant to be set by the user of the library, on the application
 * side and scope. The provided one exits the program with a detailed message.
 * I do not know the results however, if this function could wait for an answer
 * before exits, as (theoretically) the user could free resources, and return for
 * a retry. The function signature should change to account for that.

 * The code defines two those memory wrappers.
 * Alloc (size) and Realloc (object, size). Both like their counterparts, they return
 * void *.

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
 * in between, and finally because C is about algorithms and when implemented properly
 * the code hapilly lives for ever without a single change. Of course the properties
 * are machine properties and such endevour it never ends and needs responsibility,
 * extensive study and so time. I do not know however if (time) is generous.
 */

/* TO DO
 * Really. The real interest is to use this code as an underline machine.
 * It would be great if could catch and fix false conditions, as I fix things as i
 * catch them by using this editor since 20 something of february, (the bad thing
 * however is that i can survive of a current row miscalculation:
 *                             Seriously! ATTENTION BUFGS AROUND!
 * When it happens (probably by the mark|save|restore|state quick implementation)
 * is critical, because you see different than it actually is, and this is the
 * worst that can happen to a visual editor (an ed doesn't suffer from this)).
 * so what i do? either gg|G first, to go to __a known reset point__ and then
 * goback again, but !!not by using the a ` mark, but with an another way, either
 * with a search or a `goto to linenum' kind of thing.

 * That would be nice to catch those bugs. Would also be nice if i could reserve
 * sometime to fix the tabwidth stuff, which is something i do not have the slightest
 * will to do, because i hate tabs with a passion!!! in the code (nothing is perfect
 * in this life).

 * I'd rather give my time (as a side project though) to split those two different
 * consepts. The first level or better the zero level, the actual editor code:

   - inserting/deleting lines (this should meet or extend ed specifications)

   - line operations where line as:
     An array of characters that provide information or methods about the
     actual num bytes used to consist each character and the utf8 representation.

     Line operations: inserting/deleting/replacing..., based on that type.

   - /undo/redo

   - basically the ed interface.

  And the second level the obvious one.

/* My opinion on this editor.

 * It is assumed of course, that this product is not meant for production use; it
 * is far far faaaar away to the completion, but is an editor which implements the
 * basic operation set that can be considered enough to be productive, which runs
 * really low in memory resources. Theoretically (assuming the code is good enough)
 * it could be used in many situations, since is indepented and that is the one
 * and only interesting point of this.

 * And this (probably) is the reason the persistence to self sufficiency.
 * The other (probably again and by digging around the mind) is about some beliefs
 * about the language.
 *
 * But first, there isn't such a thing as self sufficiency!
 * So our dependency, our libc environment, is more like a collection of individual
 * stable functions, which their body are usually only algorithms (and so and machine
 * code, because this is a property of C), and with as less is possible conditional
 * branches (personally, i do not like the situation when i have to code an else,
 * though i feel there is nothing wrong with a linear branching (an if after an if)).
 *
 * However those functions and for good reason, are unware for anything out of their
 * domain, which is execution.
 * They have no relationship between their (lets group), as they do not even have a
 * sense that belong to a group, such: "I am a string type function! (and what is a
 * string anyway?). But when i see code like *s++ = *sa++, I can point/direct to the
 * memory and without can't even care what points to but who to where".
 * Great! Primitive machine code with such an abstraction and freedom (C unbeatable
 * true power).

 * With regards to libc'es, (i strongly believe) that it would be great, if you could
 * just cherry pick (specific functionality) from a pool but without to carry all the
 * weight of the pool, to build an own independend pool, extracting as a code and as
 * (lets say) jit'ed library. As an example: i request your malloc, but an independend
 * malloc. Then i want to offer me an end|entry point to de|initialize this memory
 * interface and a method/point to communicate with you with a standardized protocol.

 * Of course the exact same goes with the protocol whatever the protocol: function
 * calls, or good old sockets or modern ones like json, which is already implemented
 * using the standard and full optimized and known to do the expected thing everytime
 * and for ever, libc functions.
 *
 * But how about a little more complex task, an input functionality. It goes like
 * this: save terminal state - raw mode - get key - return the key - reset term.

 * All well except the get key () function. What it will return? An ascii code in
 * UTF-8 era is usually useless. You want a signed int (if i'm not wrong).
 * So there is no other way, when the first byte indicates a byte sequence, than
 * to deal with it, into the getkey() scope. So you probably need an utf-8 library.
 * This is (excuse me) a bit of stupid. It just needs 10/20 lines of code, see:
 * term_get_input (term_t *this), which i plan to publish it as a separate project.

 * It will also has to deal with the terminal escape sequenses and returns reliably
 * the code for all the keyboard keys. Again this is easy because is one time job
 * and boring testing or simply usage from different users on different terminals.
 * The above mentioned function, it looks that it deals well with xterm/linux/urxvt
 * and st from suckless terminals, in 110 lines of code reserved for this.

 * This specific example is an excellent candidate for this imaginery standard
 * C library. It's like forkpty() which wraps perfect the details. I guess what i'm
 * really talking about is a bit more broader scope/functionality than forkpty().

 * And it would be even greater if you could easily request a collection, that use
 * each other properties, to build a task, like an editor. As standard algrorithm!
 * I could [fore]see used a lot. This particular is a prototype and not yet ready,
 * but this is the idea and is a general one and it seems that it could be usefull
 * for all; they just have to connect to C and use these algorithms.
 */

αγαθοκλής
```
