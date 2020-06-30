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
SYSLIBDIR   = $(SYSDIR)/lib
SYSBINDIR   = $(SYSDIR)/bin
SYSINCDIR   = $(SYSDIR)/include
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

DEBUG := 0

ifneq ($(DEBUG), 0)
  FLAGS += $(DEBUG_FLAGS)
endif
#----------------------------------------------------------#
# libved
LIBVED_NAME := libved
LIBVED_VERS := 0.1
LIBVED_SRCDIR := $(THISDIR)

LIBVED_SRC    = $(LIBVED_SRCDIR)/$(LIBVED_NAME).c
LIBVED_HEADER = $(LIBVED_SRCDIR)/$(LIBVED_NAME).h
LIBVED_HEADER_EXT = $(LIBVED_SRCDIR)/$(LIBVED_NAME)+.h

LIBVED_SYSHEADER = $(SYSINCDIR)/$(LIBVED_NAME).h
LIBVED_SYSHEADER_EXT = $(SYSINCDIR)/$(LIBVED_NAME)+.h

LIBVED = $(LIBVED_NAME)-$(LIBVED_VERS)
LIBVED_SHARED_OBJ = $(SYSLIBDIR)/$(LIBVED).so
LIBVED_SHARED_OBJ_LINK = $(SYSLIBDIR)/$(LIBVED_NAME).so

LIBVED_STATIC_OBJ = $(SYSLIBDIR)/$(LIBVED).a
LIBVED_STATIC_OBJ_LINK = $(SYSLIBDIR)/$(LIBVED_NAME).a

LIBVED_FLAGS  = $(FLAGS)

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

READ_FROM_SHELL = 1
SPELL_DICTIONARY := $(SYSDATADIR)/spell/spell.txt
SPELL_DICTIONARY_NUM_ENTRIES := 10000

VED_FLAGS = -lm

LIBVED_FLAGS +=  $(VED_FLAGS)
LIBVED_FLAGS += -DENABLE_WRITING
LIBVED_FLAGS += -DVED_DIR='"$(SYSDIR)"'
LIBVED_FLAGS += -DVED_DATADIR='"$(SYSDATADIR)"'
LIBVED_FLAGS += -DVED_TMPDIR='"$(SYSTMPDIR)"'
LIBVED_FLAGS += -DTABWIDTH=$(TABWIDTH)
LIBVED_FLAGS += -DUNDO_NUM_ENTRIES=$(UNDO_NUM_ENTRIES)
LIBVED_FLAGS += -DRLINE_HISTORY_NUM_ENTRIES=$(RLINE_HISTORY_NUM_ENTRIES)
LIBVED_FLAGS += -DCLEAR_BLANKLINES=$(CLEAR_BLANKLINES)
LIBVED_FLAGS += -DTAB_ON_INSERT_MODE_INDENTS=$(TAB_ON_INSERT_MODE_INDENTS)
LIBVED_FLAGS += -DCARRIAGE_RETURN_ON_NORMAL_IS_LIKE_INSERT_MODE=$(CARRIAGE_RETURN_ON_NORMAL_IS_LIKE_INSERT_MODE)
LIBVED_FLAGS += -DSPACE_ON_NORMAL_IS_LIKE_INSERT_MODE=$(SPACE_ON_NORMAL_IS_LIKE_INSERT_MODE)
LIBVED_FLAGS += -DSMALL_E_ON_NORMAL_GOES_INSERT_MODE=$(SMALL_E_ON_NORMAL_GOES_INSERT_MODE)
LIBVED_FLAGS += -DBACKSPACE_ON_FIRST_IDX_REMOVE_TRAILING_SPACES=$(BACKSPACE_ON_FIRST_IDX_REMOVE_TRAILING_SPACES)
LIBVED_FLAGS += -DREAD_FROM_SHELL=$(READ_FROM_SHELL)

ifeq (-$(SYSKERNEL), -Linux)
  LIBVED_FLAGS += -DSYS_IS_LINUX
endif

LIBVED_FLAGS += -DSYS_NAME='"$(SYSKERNEL)"'

LIBVED_CFLAGS += -I. -I$(SYSINCDIR) $(LIBVED_FLAGS)

HEADERS = $(LIBVED_SYSHEADER) $(LIBVED_SYSHEADER_EXT)

HAS_RUNTIME_INTERPRETER := 0

RUNTIME_INTERPR_NAME = lai
LED_INTERPR_NAME = led
RUNTIME_INTERPR_DIR  = $(LIBVED_SRCDIR)/lib/$(RUNTIME_INTERPR_NAME)

THIS_RUNTIME_INTERPR_HEADER = $(RUNTIME_INTERPR_DIR)/$(RUNTIME_INTERPR_NAME).h
LED_INTERPR_HEADER          = $(RUNTIME_INTERPR_DIR)/$(LED_INTERPR_NAME).h
SYS_RUNTIME_INTERPR_HEADER  = $(SYSINCDIR)/$(RUNTIME_INTERPR_NAME).h
SYS_LED_HEADER              = $(SYSINCDIR)/$(LED_INTERPR_NAME).h

ifneq ($(HAS_RUNTIME_INTERPRETER), 0)
  HEADERS += $(SYS_RUNTIME_INTERPR_HEADER) $(SYS_LED_HEADER)
endif

libved-header: $(HEADERS))
$(LIBVED_SYSHEADER):
	@$(CP) $(LIBVED_HEADER) $(SYSINCDIR)
$(LIBVED_SYSHEADER_EXT):
	@$(CP) $(LIBVED_HEADER_EXT) $(SYSINCDIR)
$(SYS_RUNTIME_INTERPR_HEADER):
	@$(CP) $(THIS_RUNTIME_INTERPR_HEADER) $(SYSINCDIR)
$(SYS_LED_HEADER):
	@$(CP) $(LED_INTERPR_HEADER) $(SYSINCDIR)

libved-static: prereq libved-header $(LIBVED_STATIC_OBJ)
$(LIBVED_STATIC_OBJ):
	$(CC) -x c $(LIBVED_SRC) $(LIBVED_FLAGS) $(STATIC_CFLAGS) -c -o $(LIBVED).o
	@$(AR) $(LIBVED_STATIC_OBJ) $(LIBVED).o
	@$(RM) $(LIBVED).o
	@$(LN_S) -vf $(LIBVED_STATIC_OBJ) $(LIBVED_STATIC_OBJ_LINK)

libved-shared: prereq libved-header $(LIBVED_SHARED_OBJ)
$(LIBVED_SHARED_OBJ):
	$(CC) -x c $(LIBVED_SRC) $(LIBVED_CFLAGS) $(SHARED_CFLAGS) -o $(LIBVED_SHARED_OBJ)
	@$(LN_S) -vf $(LIBVED_SHARED_OBJ) $(LIBVED_SHARED_OBJ_LINK)

clean_libved: clean_libved_shared clean_libved_static clean_libved_header

clean_libved_shared: clean_libved_header
	@$(TEST) ! -f $(LIBVED_SHARED_OBJ)      || $(RM) -v $(LIBVED_SHARED_OBJ)
	@$(TEST) ! -L $(LIBVED_SHARED_OBJ_LINK) || $(RM) -v $(LIBVED_SHARED_OBJ_LINK)

clean_libved_static: clean_libved_header clean_libved_static_obj
clean_libved_static_obj:
	@$(TEST) ! -f $(LIBVED_STATIC_OBJ)      || $(RM) -v $(LIBVED_STATIC_OBJ)
	@$(TEST) ! -L $(LIBVED_STATIC_OBJ_LINK) || $(RM) -v $(LIBVED_STATIC_OBJ_LINK)

clean_libved_header:
	@$(TEST) ! -f $(LIBVED_SYSHEADER)             || $(RM) $(LIBVED_SYSHEADER)
	@$(TEST) ! -f $(LIBVED_SYSHEADER_EXT)         || $(RM) $(LIBVED_SYSHEADER_EXT)
	@$(TEST) ! -f $(SYS_RUNTIME_INTERPR_HEADER)   || $(RM) $(SYSINCDIR)/$(SYS_RUNTIME_INTERPR_HEADER)
	@$(TEST) ! -f $(SYS_LED_HEADER)               || $(RM) $(SYS_LED_HEADER)
#----------------------------------------------------------#

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
REQOBJS   =   $(LIBVED_SRCDIR)/libved+.c
REQOBJS   +=  $(LIBVED_SRCDIR)/lib/argparse/argparse.c
REQOBJS   +=  $(LIBVED_SRCDIR)/lib/map/int_map.c
REQOBJS   +=  $(LIBVED_SRCDIR)/lib/i/i.c

APPFLAGS :=

HAS_TCC  := 0
HAS_CURL := 0

ifneq ($(HAS_TCC), 0)
  REQOBJS += $(LIBVED_SRCDIR)/lib/Ccompilers/tcc/tcc.c
endif

RUNTIME_INTERPR_SHARED_FLAGS =
RUNTIME_INTERPR_STATIC_FLAGS += "ENABLE_HTTP=0"

ifneq ($(HAS_RUNTIME_INTERPRETER), 0)
  APPFLAGS += -DHAS_RUNTIME_INTERPRETER
  REQOBJS  += $(RUNTIME_INTERPR_DIR)/led.c

  REQ_SHARED_LIBS = $(RUNTIME_INTERPR_NAME)-shared
  REQ_STATIC_LIBS = $(RUNTIME_INTERPR_NAME)-static

  ifeq ($(HAS_CURL), 0)
    RUNTIME_INTERPR_SHARED_FLAGS += "ENABLE_HTTP=0"
  else
    RUNTIME_INTERPR_SHARED_FLAGS += "ENABLE_HTTP=1"
  endif

endif

ifneq ($(HAS_JSON), 0)
  REQOBJS += $(LIBVED_SRCDIR)/lib/json/json.c
endif

APPFLAGS  +=   $(REQOBJS)
APPFLAGS  +=  -I$(SYSINCDIR) -L$(SYSLIBDIR) -lved

# This is to set specific order, needed by the linker in the static target
# as it has to be after the required objects but before -lm (if present)
ifneq ($(HAS_RUNTIME_INTERPRETER), 0)
  APPFLAGS += -l$(RUNTIME_INTERPR_NAME)
endif

APPFLAGS += -lm

APPLICATION_FLAGS :=

ifdef APPLICATION_FLAGS
  APPFLAGS += $(APPLICATION_FLAGS)
endif

APPFLAGS += $(FLAGS)

ifeq (-$(SYSKERNEL), -Linux)
  APPFLAGS += -DSYS_IS_LINUX
endif

SHARED_APP_FLAGS :=
ifneq ($(HAS_TCC), 0)
  SHARED_APP_FLAGS += -DHAS_TCC -ltcc -ldl
endif

HAS_LOCAL_EXTENSIONS := 0
LOCAL_EXTENSIONS_FLAGS :=

ifneq ($(HAS_LOCAL_EXTENSIONS), 0)
  ifdef LOCAL_EXTENSIONS_FLAGS
    APPFLAGS += $(LOCAL_EXTENSIONS_FLAGS)
  endif
  APPFLAGS += -DHAS_LOCAL_EXTENSIONS
endif

APPFLAGS += -DHAS_MOONPHASE
APPFLAGS += -DSYS_NAME='"$(SYSKERNEL)"'
APPFLAGS += -DHAS_SHELL_COMMANDS
APPFLAGS += -DHAS_REGEXP
APPFLAGS += -DHAS_HISTORY
APPFLAGS += -DHAS_SPELL
APPFLAGS += -DHAS_JSON
APPFLAGS += -DHAS_EXPR
APPFLAGS += -DHAS_USER_EXTENSIONS
APPFLAGS += -DSPELL_DICTIONARY='"$(SPELL_DICTIONARY)"'
APPFLAGS += -DSPELL_DICTIONARY_NUM_ENTRIES=$(SPELL_DICTIONARY_NUM_ENTRIES)
APPFLAGS += -DVED_DIR='"$(SYSDIR)"'
APPFLAGS += -DVED_DATADIR='"$(SYSDATADIR)"'
APPFLAGS += -DVED_TMPDIR='"$(SYSTMPDIR)"'

app-all: app-static app-shared

clean_app: clean_app_static clean_app_shared
clean_app_shared:
	@$(TEST) ! -f $(SYSAPPSHARED) || $(RM) $(SYSAPPSHARED)
clean_veda_static:
	@$(TEST) ! -f $(SYSAPPSTATIC) || $(RM) $(SYSAPPSTATIC)

app-shared: Env libved-shared $(REQ_SHARED_LIBS)
	$(CC) -x c $(THIS_APPSRC) $(APPFLAGS) $(SHARED_APP_FLAGS) -o $(APPSHARED)
	@$(INSTALL) -v $(APPSHARED) $(SYSBINDIR)
	@$(RM) $(APPSHARED)

app-static: Env libved-static $(REQ_STATIC_LIBS)
	$(CC) -x c $(THIS_APPSRC) $(APPFLAGS) $(STATIC_CFLAGS) -o $(APPSTATIC)
	@$(INSTALL) -v $(APPSTATIC) $(SYSBINDIR)
	@$(RM) $(APPSTATIC)

app-shared-preproc:
	$(CC) -x c $(THIS_APPSRC) $(APPFLAGS) -E

$(RUNTIME_INTERPR_NAME)-shared:
	@cd $(RUNTIME_INTERPR_DIR) && make SYSDIR=$(SYSDIR) $(RUNTIME_INTERPR_SHARED_FLAGS) shared-library
	
$(RUNTIME_INTERPR_NAME)-static:
	@cd $(RUNTIME_INTERPR_DIR) && make SYSDIR=$(SYSDIR) $(RUNTIME_INTERPR_STATIC_FLAGS) static-library

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
	@$(TEST) -d $(SYSBINDIR)  || $(MKDIR) $(SYSBINDIR)
	@$(TEST) -d $(SYSDATADIR) || $(MKDIR_P) --mode=0700 $(SYSDATADIR)
	@$(TEST) -d $(SYSTMPDIR)  || $(MKDIR_P) --mode=0700 $(SYSTMPDIR)

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
