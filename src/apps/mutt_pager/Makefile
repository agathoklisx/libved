#----------------------------------------------------------#
THIS_LIBNAME := ved

API := 0
REV := 1
VER := 1

THISDIR := $(shell realpath ../..)

THIS_LIBVERS  :=  $(API).$(REV)

THIS_HEADER   :=  lib$(THIS_LIBNAME).h
THIS_LIBSRC   :=  lib$(THIS_LIBNAME).c
THIS_LIB      :=  lib$(THIS_LIBNAME)-$(THIS_LIBVERS)

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
LIBNAME        := $(THIS_LIBNAME)-$(THIS_LIBVERS)

THIS_SHAREDLIB := $(SYSLIBDIR)/$(THIS_LIB).so
THIS_STATICLIB := $(SYSLIBDIR)/$(THIS_LIB).a

BASE_FLAGS  := -g -O2 -march=native -fvisibility=hidden -fdollars-in-identifiers

DEBUG_FLAGS := -Wextra -Wno-override-init -Werror -Wshadow -Wall -Wunused-function -Wunused-macros
# -fsanitize=undefined
# -Wpedantic

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
HAS_LOCAL_EXTENSIONS := 0
LOCAL_EXTENSIONS_FLAGS :=

ifneq ($(HAS_LOCAL_EXTENSIONS), 0)
  FLAGS += -DHAS_LOCAL_EXTENSIONS
  ifdef LOCAL_EXTENSIONS_FLAGS
    FLAGS += $(LOCAL_EXTENSIONS_FLAGS)
  endif
endif

DEBUG := 0

ifneq ($(DEBUG), 0)
  FLAGS += $(DEBUG_FLAGS)
endif

ifeq (-$(SYSKERNEL), -Linux)
  FLAGS += -DSYS_IS_LINUX
endif

FLAGS += -DSYS_NAME='"$(SYSKERNEL)"'

VED_DATA_DIR := $(SYSDATADIR)
TMPDIR       := $(SYSTMPDIR)
USER_EXTENSIONS_FLAGS :=
SPELL_DICTIONARY := $(SYSDATADIR)/spell/spell.txt
SPELL_DICTIONARY_NUM_ENTRIES := 10000
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

FLAGS += -DVED_DATA_DIR='"$(VED_DATA_DIR)"'
FLAGS += -DTMPDIR='"$(TMPDIR)"'
FLAGS += -DENABLE_WRITING
FLAGS += -DHAS_REGEXP
FLAGS += -DHAS_SHELL_COMMANDS
FLAGS += -DHAS_USER_EXTENSIONS
FLAGS += $(USER_EXTENSIONS_FLAGS)
FLAGS += -DHAS_SPELL
FLAGS += -DHAS_EXPR
FLAGS += -DSPELL_DICTIONARY='"$(SPELL_DICTIONARY)"'
FLAGS += -DSPELL_DICTIONARY_NUM_ENTRIES=$(SPELL_DICTIONARY_NUM_ENTRIES)
FLAGS += -DHAS_HISTORY
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


WORD_LEXICON_FILE :=

ifdef WORD_LEXICON_FILE
  FLAGS += -DWORD_LEXICON_FILE='"$(WORD_LEXICON_FILE)"'
endif

#----------------------------------------------------------#
APPNAME := mutt_pager

THIS_APPSRC  := $(APPNAME).c
THIS_APPVERS := $(VER)$(API)$(REV)

APPSHARED    := $(APPNAME)-$(THIS_APPVERS)_shared
APPSTATIC    := $(APPNAME)-$(THIS_APPVERS)_static
SYSAPPSHARED := $(SYSBINDIR)/$(APPSHARED)
SYSAPPSTATIC := $(SYSBINDIR)/$(APPSTATIC)

#----------------------------------------------------------#
APPFLAGS    := -I$(SYSINCDIR) -L$(SYSLIBDIR) -l$(LIBNAME)

APPLICATION_FLAGS :=

ifdef APPLICATION_FLAGS
  APPFLAGS += $(APPLICATION_FLAGS)
endif

APPFLAGS += $(FLAGS)

app-all: app-static app-shared

clean_app: clean_app_static clean_app_shared
clean_app_shared:
	@$(TEST) ! -f $(SYSAPPSHARED) || $(RM) $(SYSAPPSHARED)
clean_veda_static:
	@$(TEST) ! -f $(SYSAPPSTATIC) || $(RM) $(SYSAPPSTATIC)

app-shared: Env
	$(CC) -x c $(THIS_APPSRC) $(APPFLAGS) -o $(APPSHARED)
	@$(INSTALL) -v $(APPSHARED) $(SYSBINDIR)
	@$(RM) $(APPSHARED)

app-static: Env
	$(CC) -x c $(THIS_APPSRC) $(APPFLAGS) $(STATIC_CFLAGS) -o $(APPSTATIC)
	@$(INSTALL) -v $(APPSTATIC) $(SYSBINDIR)
	@$(RM) $(APPSTATIC)

#----------------------------------------------------------#
LD_LIBRARY_PATH := $(SYSLIBDIR)
#----------------------------------------------------------#
AR = ar rs
TEST = test
RM = rm -f
RM_R = rm -r -f
RMDIR = rmdir
MKDIR = mkdir
MKDIR_P = $(MKDIR) -p
CP = cp
CP_R = $(CP) -r
INSTALL = install
MAKE = make
VALGRIND = valgrind
VALGRIND_ARGS = --leak-check=full --show-leak-kinds=all -v --track-origins=yes
GDB = gdb
GDB_ARGS = --quiet -ex "set logging file /tmp/gdb.txt" -ex "set logging on" -ex run --args
#----------------------------------------------------------#

Env: makeenv checkenv
makeenv:
	@$(TEST) -d $(SYSBINDIR)  || $(MKDIR) $(SYSBINDIR)
	@$(TEST) -d $(SYSDATADIR) || $(MKDIR) $(SYSDATADIR)
	@$(TEST) -d $(SYSTMPDIR)  || $(MKDIR) $(SYSTMPDIR)

checkenv:
	@$(TEST) -w $(SYSBINDIR)  || exit 1
	@$(TEST) -w $(SYSDATADIR) || exit 1
	@$(TEST) -w $(SYSTMPDIR)  || exit 1

#----------------------------------------------------------#

VERBOSE := 0
VALGRIND_V :=
ifneq ($(VERBOSE), 0)
  VALGRIND_V = -v
endif

# We want to pass the arguments to the below targets
ARGS = $(filter-out $@,$(MAKECMDGOALS))
# but make interpert command line arguments either as targets or as variables.
# This is a do nothing target, which is executed when there is no matching target.
%:
	@true
# But this is dangerous as silently make will execute, even mispelled targets
# yet legitimate.
# https://stackoverflow.com/a/45003119

# So taking with caution for the sake of simplicity, but is better to use a shell
# script, as described to the above answer to stackoverflow.

run_shared:
	@LD_LIBRARY_PATH=$(LD_LIBRARY_PATH) $(SYSAPPSHARED) $(ARGS)

run_static:
	@$(SYSAPPSTATIC) $(ARGS)

check_app_memory_leaks:
	@LD_LIBRARY_PATH=$(LD_LIBRARY_PATH) $(VALGRIND) $(VALGRIND_ARGS) $(VALGRIND_V) $(SYSAPPSHARED) $(ARGS)

debug_app_static:
	@$(GDB) $(GDB_ARGS) $(SYSAPPSTATIC) $(ARGS)

debug_app_shared:
	@LD_LIBRARY_PATH=$(LD_LIBRARY_PATH) $(GDB) $(GDB_ARGS) $(SYSAPPSHARED) $(ARGS)
