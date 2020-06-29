#----------------------------------------------------------#
THIS_LIBNAME := ved
EDNAME       := veda

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

SYSDIR    := $(THISDIR)/sys
SYSLIBDIR  = $(SYSDIR)/lib
SYSBINDIR  = $(SYSDIR)/bin
SYSINCDIR  = $(SYSDIR)/include
SYSDATADIR = $(SYSDIR)/data
SYSTMPDIR  = $(SYSDIR)/tmp

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

HAS_REGEXP := 1

HAS_SHELL_COMMANDS := 1

ifneq ($(HAS_SHELL_COMMANDS), 0)
  FLAGS += -DHAS_SHELL_COMMANDS
endif

HAS_HISTORY := 1

ifneq ($(HAS_HISTORY), 0)
  FLAGS += -DHAS_HISTORY
endif

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

FLAGS += -DVED_DIR='"$(SYSDIR)"'
FLAGS += -DVED_DATADIR='"$(SYSDATADIR)"'
FLAGS += -DVED_TMPDIR='"$(SYSTMPDIR)"'
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

HEADERS = $(SYSINCDIR)/$(THIS_HEADER) $(SYSINCDIR)/$(THIS_HEADER_EXT)

RUNTIME_INTERPR_NAME = lai
LED_INTERPR_NAME = led
RUNTIME_INTERPR_DIR  = lib/$(RUNTIME_INTERPR_NAME)

THIS_RUNTIME_INTERPR_HEADER = $(RUNTIME_INTERPR_DIR)/$(RUNTIME_INTERPR_NAME).h
LED_INTERPR_HEADER          = $(RUNTIME_INTERPR_DIR)/$(LED_INTERPR_NAME).h
SYS_RUNTIME_INTERPR_HEADER  = $(SYSINCDIR)/$(RUNTIME_INTERPR_NAME).h
SYS_LED_HEADER              = $(SYSINCDIR)/$(LED_INTERPR_NAME).h

ifneq ($(HAS_RUNTIME_INTERPRETER), 0)
  HEADERS += $(SYS_RUNTIME_INTERPR_HEADER) $(SYS_LED_HEADER)
endif

header: $(HEADERS)

$(SYSINCDIR)/$(THIS_HEADER):
	@$(CP) $(THIS_HEADER) $(SYSINCDIR)
$(SYSINCDIR)/$(THIS_HEADER_EXT):
	@$(CP) $(THIS_HEADER_EXT) $(SYSINCDIR)
$(SYS_RUNTIME_INTERPR_HEADER):
	@$(CP) $(THIS_RUNTIME_INTERPR_HEADER) $(SYSINCDIR)
$(SYS_LED_HEADER):
	@$(CP) $(LED_INTERPR_HEADER) $(SYSINCDIR)

preproc:
	$(CC) -x c $(THIS_LIBSRC) $(LIBFLAGS) $(SHARED_CFLAGS) -E

clean: clean_shared clean_static clean_header clean_veda
clean_shared: clean_header
	@$(TEST) ! -f $(THIS_SHAREDLIB) || $(RM) $(THIS_SHAREDLIB)
clean_static: clean_header
	@$(TEST) ! -f $(THIS_STATICLIB) || $(RM) $(THIS_STATICLIB)
clean_header:
	@$(TEST) ! -f $(SYSINCDIR)/$(THIS_HEADER)     || $(RM) $(SYSINCDIR)/$(THIS_HEADER)
	@$(TEST) ! -f $(SYSINCDIR)/$(THIS_HEADER_EXT) || $(RM) $(SYSINCDIR)/$(THIS_HEADER_EXT)
	@$(TEST) ! -f $(SYS_RUNTIME_INTERPR_HEADER)   || $(RM) $(SYSINCDIR)/$(SYS_RUNTIME_INTERPR_HEADER)
	@$(TEST) ! -f $(SYS_LED_HEADER)               || $(RM) $(SYS_LED_HEADER)

#----------------------------------------------------------#

THIS_APPSRC  := $(EDNAME).c
THIS_APPVERS := $(VER)$(API)$(REV)

EDSHARED    := $(EDNAME)-$(THIS_APPVERS)_shared
EDSTATIC    := $(EDNAME)-$(THIS_APPVERS)_static
SYSEDSHARED := $(SYSBINDIR)/$(EDSHARED)
SYSEDSTATIC := $(SYSBINDIR)/$(EDSTATIC)
SYSEDSHAREDSH := $(SYSBINDIR)/$(EDNAME)s
SYSEDSTATICLNK := $(SYSBINDIR)/$(EDNAME)

#----------------------------------------------------------#
APPFLAGS  :=
STATIC_APP_FLAGS :=
SHARED_APP_FLAGS :=

HAS_RUNTIME_INTERPRETER := 0

REQOBJS    =  libved+.c
REQOBJS   +=  lib/argparse/argparse.c
REQOBJS   +=  lib/map/int_map.c
REQOBJS   +=  lib/i/i.c

ifneq ($(HAS_JSON), 0)
  REQOBJS += lib/json/json.c
endif

HAS_TCC := 0

ifneq ($(HAS_TCC), 0)
  REQOBJS += lib/Ccompilers/tcc/tcc.c
endif

RUNTIME_INTERPR_SHARED_FLAGS =
RUNTIME_INTERPR_STATIC_FLAGS =

ifneq ($(HAS_RUNTIME_INTERPRETER), 0)
  REQ_SHARED_LIBS = $(RUNTIME_INTERPR_NAME)-shared
  REQ_STATIC_LIBS = $(RUNTIME_INTERPR_NAME)-static

  STATIC_APP_FLAGS += -l$(RUNTIME_INTERPR_NAME)
  SHARED_APP_FLAGS += -l$(RUNTIME_INTERPR_NAME)

  RUNTIME_INTERPR_STATIC_FLAGS += "ENABLE_HTTP=0"

  ifeq ($(HAS_CURL), 0)
    RUNTIME_INTERPR_SHARED_FLAGS += "ENABLE_HTTP=0"
  else
    RUNTIME_INTERPR_SHARED_FLAGS += "ENABLE_HTTP=1"
  endif

  APPFLAGS += -DHAS_RUNTIME_INTERPRETER
  REQOBJS  += $(RUNTIME_INTERPR_DIR)/led.c

endif

APPFLAGS  +=  $(REQOBJS)
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

DEBUG_INTERPRETER := 0

ifneq ($(DEBUG_INTERPRETER), 0)
  APPFLAGS += -DDEBUG_INTERPRETER
endif

HAS_USER_EXTENSIONS := 0
USER_EXTENSIONS_FLAGS :=

HAS_SPELL := 0
SPELL_DICTIONARY := $(SYSDATADIR)/spell/spell.txt
SPELL_DICTIONARY_NUM_ENTRIES := 10000

HAS_CURL := 0
HAS_JSON := 0
HAS_EXPR := 0

WORD_LEXICON_FILE :=

ifneq ($(HAS_SPELL), 0)
  HAS_USER_EXTENSIONS = 1
endif

ifneq ($(HAS_JSON), 0)
  HAS_USER_EXTENSIONS = 1
endif

ifneq ($(HAS_EXPR), 0)
  HAS_USER_EXTENSIONS = 1
endif

ifneq ($(HAS_TCC), 0)
  HAS_USER_EXTENSIONS = 1
endif

ifneq ($(HAS_RUNTIME_INTERPRETER), 0)
  HAS_USER_EXTENSIONS = 1
endif

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

  ifneq ($(HAS_TCC), 0)
    APPFLAGS += -DHAS_TCC -ltcc -ldl
  endif

endif

APPFLAGS += $(FLAGS)

veda-all: veda-static veda-shared

clean_veda: clean_veda_static clean_veda_shared
clean_veda_shared:
	@$(TEST) ! -f $(SYSEDSHARED) || $(RM) $(SYSEDSHARED)
clean_veda_static:
	@$(TEST) ! -f $(SYSEDSTATIC) || $(RM) $(SYSEDSTATIC)

veda-shared: shared $(REQ_SHARED_LIBS) $(SYSEDSHARED)
$(SYSEDSHARED):
	$(CC) -x c $(THIS_APPSRC) $(APPFLAGS) $(SHARED_APP_FLAGS) -o $(EDSHARED)
	@$(INSTALL) -v $(EDSHARED) $(SYSBINDIR)
	@$(RM) $(EDSHARED)
	@$(PRINTF) "#!/bin/sh\n" > $(SYSEDSHAREDSH)
	@$(PRINTF) "LD_LIBRARY_PATH=$(SYSLIBDIR) $(SYSEDSHARED) \"\$$@\"\n" >> $(SYSEDSHAREDSH)
	@$(CHMOD_X) $(SYSEDSHAREDSH)

veda-static: static $(REQ_STATIC_LIBS) $(SYSEDSTATIC)
$(SYSEDSTATIC):
	$(CC) -x c $(THIS_APPSRC) $(APPFLAGS) $(STATIC_APP_FLAGS) $( STATIC_CFLAGS) -o $(EDSTATIC)
	@$(INSTALL) -v $(EDSTATIC) $(SYSBINDIR)
	@$(RM) $(EDSTATIC)
	@$(LN_S) -f $(SYSEDSTATIC) $(SYSEDSTATICLNK)

veda-shared-preproc:
	$(CC) -x c $(THIS_APPSRC) $(APPFLAGS) -E

lai-shared-lib: lai-clean $(RUNTIME_INTERPR_NAME)-shared

$(RUNTIME_INTERPR_NAME)-shared:
	@cd $(RUNTIME_INTERPR_DIR) && make SYSDIR=$(SYSDIR) $(RUNTIME_INTERPR_SHARED_FLAGS) shared-library

lai-static-lib: lai-clean $(RUNTIME_INTERPR_NAME)-static

$(RUNTIME_INTERPR_NAME)-static:
	@cd $(RUNTIME_INTERPR_DIR) && make SYSDIR=$(SYSDIR) $(RUNTIME_INTERPR_STATIC_FLAGS) static-library

lai-clean:
	@cd $(RUNTIME_INTERPR_DIR) && make SYSDIR=$(SYSDIR) clean

lai-interpr:
	@cd $(RUNTIME_INTERPR_DIR) && make SYSDIR=$(SYSDIR) ENABLE_REPL=1 interpr

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
CHMOD = chmod
CHMOD_X = $(CHMOD) 0755
PRINTF = printf
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
	@$(TEST) -d $(SYSDATADIR) || $(MKDIR_P) --mode=0700 $(SYSDATADIR)
	@$(TEST) -d $(SYSTMPDIR)  || $(MKDIR_P) --mode=0700 $(SYSTMPDIR)

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
