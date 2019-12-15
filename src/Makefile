#----------------------------------------------------------#
THIS_LIBNAME := ved

API := 0
REV := 1
VER := 1

THISDIR := $(shell realpath .)

THIS_LIBVERS  :=  $(API).$(REV)

THIS_HEADER     = lib$(THIS_LIBNAME).h
THIS_HEADER_EXT = lib$(THIS_LIBNAME)+.h
THIS_LIBSRC     = lib$(THIS_LIBNAME).c
LIBNAME         = $(THIS_LIBNAME)-$(THIS_LIBVERS)
THIS_LIB        = lib$(LIBNAME)

CC            := gcc
CC_STD        := -std=c11

SYSKERNEL     := $(shell uname -s)
SYSARCH       := $(shell uname -m)
SYS           := $(SYSKERNEL)-$(SYSARCH)

SYSDIR     := $(THISDIR)/sys
SYSLIBDIR  := $(SYSDIR)/lib
SYSBINDIR  := $(SYSDIR)/bin
SYSINCDIR  := $(SYSDIR)/include
SYSDATADIR := $(SYSDIR)/data
SYSTMPDIR  := $(SYSDIR)/tmp

#----------------------------------------------------------#
THIS_SHAREDLIB := $(SYSLIBDIR)/$(THIS_LIB).so
THIS_STATICLIB := $(SYSLIBDIR)/lib$(THIS_LIBNAME).a

BASE_FLAGS  := -g -O2 -march=native -fvisibility=hidden -fdollars-in-identifiers

DEBUG_FLAGS := -Wextra -Wno-override-init -Wshadow -Wall -Wunused-function -Wunused-macros
# -Wno-override-init: is for designated initializers and included in Wextra

ifneq (-$(CC), -tcc)
  DEBUG_FLAGS += -Werror
# while compiling tinyexpr tcc warns with: assigment discards qualifiers
# from pointer target type
# -Wno-error=discarded-qualifiers doesn't have effect
endif

# DEBUG_FLAGS += -fsanitize=undefined
# -Wpedantic if turn on this gives: ISO C forbids braced-groups within expressions

SHARED_CFLAGS := -shared -fPIC
STATIC_CFLAGS := -static

FLAGS         := $(BASE_FLAGS)

CHECK_PTR_BOUNDS_FLAGS := -mmpx -fcheck-pointer-bounds

CHECK_PTR_BOUNDS := 0

ifneq ($(CHECK_PTR_BOUNDS), 0)
  FLAGS += $(CHECK_PTR_BOUNDS_FLAGS)
endif

ifeq (-$(CC), -gcc)
  FLAGS += -Wimplicit-fallthrough=0
endif

#----------------------------------------------------------#
DEBUG := 0
ENABLE_WRITING := 0

ifneq ($(DEBUG), 0)
  FLAGS += $(DEBUG_FLAGS) -DENABLE_WRITING
else
  ifneq ($(ENABLE_WRITING), 0)
    FLAGS += -DENABLE_WRITING $(DEBUG_FLAGS)
  endif
endif

HAS_REGEXP := 0

HAS_SHELL_COMMANDS := 1

ifneq ($(HAS_SHELL_COMMANDS), 0)
  FLAGS += -DHAS_SHELL_COMMANDS
endif

HAS_HISTORY := 0

ifneq ($(HAS_HISTORY), 0)
  FLAGS += -DHAS_HISTORY
endif

VED_DIR := $(SYSDIR)
VED_DATA_DIR := $(SYSDATADIR)
TMPDIR  := $(SYSTMPDIR)

TABWIDTH := 8
UNDO_NUM_ENTRIES := 40
RLINE_HISTORY_NUM_ENTRIES := 20
CLEAR_BLANKLINES := 1
TAB_ON_INSERT_MODE_INDENTS := 0
CARRIAGE_RETURN_ON_NORMAL_IS_LIKE_INSERT_MODE := 1
SPACE_ON_NORMAL_IS_LIKE_INSERT_MODE := 1
SMALL_E_ON_NORMAL_GOES_INSERT_MODE := 1
BACKSPACE_ON_FIRST_IDX_REMOVE_TRAILING_SPACES := 1
BACKSPACE_ON_NORMAL_IS_LIKE_INSERT_MODE := 1
READ_FROM_SHELL := 1

FLAGS += -DVED_DIR='"$(VED_DIR)"'
FLAGS += -DVED_DATA_DIR='"$(VED_DATA_DIR)"'
FLAGS += -DTMPDIR='"$(TMPDIR)"'
FLAGS += -DTABWIDTH=$(TABWIDTH)
FLAGS += -DUNDO_NUM_ENTRIES=$(UNDO_NUM_ENTRIES)
FLAGS += -DRLINE_HISTORY_NUM_ENTRIES=$(RLINE_HISTORY_NUM_ENTRIES)
FLAGS += -DCLEAR_BLANKLINES=$(CLEAR_BLANKLINES)
FLAGS += -DTAB_ON_INSERT_MODE_INDENTS=$(TAB_ON_INSERT_MODE_INDENTS)
FLAGS += -DCARRIAGE_RETURN_ON_NORMAL_IS_LIKE_INSERT_MODE=$(CARRIAGE_RETURN_ON_NORMAL_IS_LIKE_INSERT_MODE)
FLAGS += -DSPACE_ON_NORMAL_IS_LIKE_INSERT_MODE=$(SPACE_ON_NORMAL_IS_LIKE_INSERT_MODE)
FLAGS += -DSMALL_E_ON_NORMAL_GOES_INSERT_MODE=$(SMALL_E_ON_NORMAL_GOES_INSERT_MODE)
FLAGS += -DBACKSPACE_ON_FIRST_IDX_REMOVE_TRAILING_SPACES=$(BACKSPACE_ON_FIRST_IDX_REMOVE_TRAILING_SPACES)
FLAGS += -DREAD_FROM_SHELL=$(READ_FROM_SHELL)

ifneq ($(READ_FROM_SHELL), 0)
  ifeq ($(HAS_SHELL_COMMANDS), 0)
    FLAGS += -DHAS_SHELL_COMMANDS
  endif
endif

ifeq (-$(SYSKERNEL), -Linux)
  FLAGS += -DSYS_IS_LINUX
endif

FLAGS += -DSYS_NAME='"$(SYSKERNEL)"'

#----------------------------------------------------------#
LIBFLAGS := -I. -I$(SYSINCDIR) $(FLAGS)
# clang complains about unused command line arguments
#LIBFLAGS := -I. -I$(SYSINCDIR) -L$(SYSLIBDIR) $(FLAGS)

all: shared static veda-all

prereq: Env

shared: prereq $(THIS_SHAREDLIB) header
$(THIS_SHAREDLIB):
	$(CC) -x c $(THIS_LIBSRC) $(LIBFLAGS) $(SHARED_CFLAGS) -o $(THIS_SHAREDLIB)
	@$(LN_S) -vf $(THIS_SHAREDLIB) $(SYSLIBDIR)/lib$(THIS_LIBNAME).so

static: prereq $(THIS_STATICLIB) header
$(THIS_STATICLIB):
	$(CC) -x c $(THIS_LIBSRC) $(LIBFLAGS) $(STATIC_CFLAGS) -c -o $(THIS_LIB).o
	@$(AR) $(THIS_STATICLIB) $(THIS_LIB).o
	@$(RM) $(THIS_LIB).o

header: $(SYSINCDIR)/$(THIS_HEADER) $(SYSINCDIR)/$(THIS_HEADER_EXT)
$(SYSINCDIR)/$(THIS_HEADER):
	@$(CP) $(THIS_HEADER) $(SYSINCDIR)
$(SYSINCDIR)/$(THIS_HEADER_EXT):
	@$(CP) $(THIS_HEADER_EXT) $(SYSINCDIR)

preproc:
	$(CC) -x c $(THIS_LIBSRC) $(LIBFLAGS) $(SHARED_CFLAGS) -E

clean: clean_shared clean_static clean_header clean_veda
clean_shared: clean_header
	@$(TEST) ! -f $(THIS_SHAREDLIB) || $(RM) $(THIS_SHAREDLIB)
clean_static: clean_header
	@$(TEST) ! -f $(THIS_STATICLIB) || $(RM) $(THIS_STATICLIB)
clean_header:
	@$(TEST) ! -f $(SYSINCDIR)/$(THIS_HEADER) || $(RM) $(SYSINCDIR)/$(THIS_HEADER)
	@$(TEST) ! -f $(SYSINCDIR)/$(THIS_HEADER_EXT) || $(RM) $(SYSINCDIR)/$(THIS_HEADER_EXT)

#----------------------------------------------------------#
EDNAME := veda

THIS_APPSRC  := $(EDNAME).c
THIS_APPVERS := $(VER)$(API)$(REV)

EDSHARED    := $(EDNAME)-$(THIS_APPVERS)_shared
EDSTATIC    := $(EDNAME)-$(THIS_APPVERS)_static
SYSEDSHARED := $(SYSBINDIR)/$(EDSHARED)
SYSEDSTATIC := $(SYSBINDIR)/$(EDSTATIC)

#----------------------------------------------------------#
APPFLAGS = lib/argparse/argparse.c libved+.c
APPFLAGS += -I$(SYSINCDIR) -L$(SYSLIBDIR) -l$(THIS_LIBNAME)

HAS_LOCAL_EXTENSIONS := 0
LOCAL_EXTENSIONS_FLAGS :=

ifneq ($(HAS_LOCAL_EXTENSIONS), 0)
  ifdef LOCAL_EXTENSIONS_FLAGS
    APPFLAGS += $(LOCAL_EXTENSIONS_FLAGS)
  endif
  APPFLAGS += -DHAS_LOCAL_EXTENSIONS
endif

VED_APPLICATION_FLAGS :=

ifdef VED_APPLICATION_FLAGS
  APPFLAGS += $(VED_APPLICATION_FLAGS)
endif

ifneq ($(HAS_REGEXP), 0)
  APPFLAGS += -DHAS_REGEXP
endif

HAS_USER_EXTENSIONS := 0
USER_EXTENSIONS_FLAGS :=

HAS_SPELL := 0
SPELL_DICTIONARY := $(SYSDATADIR)/spell/spell.txt
SPELL_DICTIONARY_NUM_ENTRIES := 10000

HAS_JSON := 0

HAS_EXPR := 0

WORD_LEXICON_FILE :=

ifneq ($(HAS_USER_EXTENSIONS), 0)
  APPFLAGS += -DHAS_USER_EXTENSIONS

  ifdef USER_EXTENSIONS_FLAGS
    APPFLAGS += $(USER_EXTENSIONS_FLAGS)
  endif

  ifneq ($(HAS_SPELL), 0)
    APPFLAGS += -DHAS_SPELL
    APPFLAGS += -DSPELL_DICTIONARY='"$(SPELL_DICTIONARY)"'
    APPFLAGS += -DSPELL_DICTIONARY_NUM_ENTRIES=$(SPELL_DICTIONARY_NUM_ENTRIES)
  endif

  ifneq ($(HAS_JSON), 0)
    APPFLAGS += -DHAS_JSON
  endif

  ifneq ($(HAS_EXPR), 0)
    APPFLAGS += -DHAS_EXPR
    FLAGS += -lm
  endif

  ifdef WORD_LEXICON_FILE
    APPFLAGS += -DWORD_LEXICON_FILE='"$(WORD_LEXICON_FILE)"'
  endif

endif

APPFLAGS += $(FLAGS)

veda-all: veda-static veda-shared

clean_veda: clean_veda_static clean_veda_shared
clean_veda_shared:
	@$(TEST) ! -f $(SYSEDSHARED) || $(RM) $(SYSEDSHARED)
clean_veda_static:
	@$(TEST) ! -f $(SYSEDSTATIC) || $(RM) $(SYSEDSTATIC)

veda-shared: shared $(SYSEDSHARED)
$(SYSEDSHARED):
	$(CC) -x c $(THIS_APPSRC) $(APPFLAGS) -o $(EDSHARED)
	@$(INSTALL) -v $(EDSHARED) $(SYSBINDIR)
	@$(RM) $(EDSHARED)

veda-static: static $(SYSEDSTATIC)
$(SYSEDSTATIC):
	$(CC) -x c $(THIS_APPSRC) $(APPFLAGS) $(STATIC_CFLAGS) -o $(EDSTATIC)
	@$(INSTALL) -v $(EDSTATIC) $(SYSBINDIR)
	@$(RM) $(EDSTATIC)

#----------------------------------------------------------#
LD_LIBRARY_PATH := $(SYSLIBDIR)
#----------------------------------------------------------#
AR = ar rs
TEST = test
STAT = stat
GREP = grep
RM = rm -f
RM_R = rm -r -f
RMDIR = rmdir
MKDIR = mkdir
MKDIR_P = $(MKDIR) -p
CP = cp
CP_R = $(CP) -r
LN_S = ln -s
INSTALL = install
MAKE = make
VALGRIND = valgrind
VALGRIND_ARGS = --leak-check=full --show-leak-kinds=all -v --track-origins=yes
GDB = gdb
GDB_ARGS = --quiet -ex "set logging file /tmp/gdb.txt" -ex "set logging on" -ex run --args
#----------------------------------------------------------#

Env: makeenv checkenv
makeenv:
	@$(TEST) -d $(SYSDIR)     || $(MKDIR_P) $(SYSDIR)
	@$(TEST) -d $(SYSLIBDIR)  || $(MKDIR) $(SYSLIBDIR)
	@$(TEST) -d $(SYSINCDIR)  || $(MKDIR) $(SYSINCDIR)
	@$(TEST) -d $(SYSBINDIR)  || $(MKDIR) $(SYSBINDIR)
	@$(TEST) -d $(SYSDATADIR) || $(MKDIR) --mode=0700 $(SYSDATADIR)
	@$(TEST) -d $(SYSTMPDIR)  || $(MKDIR) --mode=0700 $(SYSTMPDIR)

checkenv:
	@$(TEST) -w $(SYSDIR)     || exit 1
	@$(TEST) -w $(SYSLIBDIR)  || exit 1
	@$(TEST) -w $(SYSINCDIR)  || exit 1
	@$(TEST) -w $(SYSBINDIR)  || exit 1
	@$(TEST) -w $(SYSDATADIR) || exit 1
	@$(TEST) -w $(SYSTMPDIR)  || exit 1

#----------------------------------------------------------#
FILE := libved.c libved.h

VERBOSE := 0
VALGRIND_V :=
ifneq ($(VERBOSE), 0)
  VALGRIND_V = -v
endif

run_shared:
	@LD_LIBRARY_PATH=$(LD_LIBRARY_PATH) $(SYSEDSHARED) $(FILE)

run_static:
	@$(SYSEDSTATIC) $(FILE)

check_veda_memory_leaks:
	@LD_LIBRARY_PATH=$(LD_LIBRARY_PATH) $(VALGRIND) $(VALGRIND_ARGS) $(VALGRIND_V) $(SYSEDSHARED) $(FILE)

debug_veda_static:
	@$(GDB) $(GDB_ARGS) $(SYSEDSTATIC) $(FILE)

debug_veda_shared:
	@LD_LIBRARY_PATH=$(LD_LIBRARY_PATH) $(GDB) $(GDB_ARGS) $(SYSEDSHARED) $(FILE)
