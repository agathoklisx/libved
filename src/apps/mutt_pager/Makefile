#----------------------------------------------------------#
# This is a simplified Makefile and assumes that both libved and liblai are
# properly installed to $(SYSDIR)/lib.

THIS_LIBNAME := ved

API := 0
REV := 2

THISDIR := $(shell (cd ../.. && pwd))
LIBVED_SRCDIR =  $(THISDIR)

THIS_LIBVERS  :=  $(API).$(REV)

CC            := gcc
CC_STD        := -std=c11

SYSKERNEL     := $(shell uname -s)
SYSARCH       := $(shell uname -m)
SYS           := $(SYSKERNEL)-$(SYSARCH)

DATADIR     =  $(THISDIR)/data

SYSDIR     := $(THISDIR)/sys
SYSLIBDIR   = $(SYSDIR)/lib
SYSBINDIR   = $(SYSDIR)/bin
SYSINCDIR   = $(SYSDIR)/include
SYSDATADIR := $(SYSDIR)/data
SYSTMPDIR  := $(SYSDIR)/tmp

#----------------------------------------------------------#
BASE_FLAGS  := -g -O2 -march=native -fvisibility=hidden -fdollars-in-identifiers
# -Wno-override-init: is for designated initializers and both compilers complain
BASE_FLAGS  +=  -Wno-override-init

DEBUG_FLAGS := -Wextra -Wshadow -Wall -Wunused-function -Wunused-macros

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

WORD_LEXICON_FILE :=

ifdef WORD_LEXICON_FILE
  FLAGS += -DWORD_LEXICON_FILE='"$(WORD_LEXICON_FILE)"'
endif

#----------------------------------------------------------#
APPNAME := mutt_pager

THIS_APPSRC  := $(APPNAME).c
THIS_APPVERS := $(API)$(REV)

APPSHARED    := $(APPNAME)-$(THIS_APPVERS)_shared
APPSTATIC    := $(APPNAME)-$(THIS_APPVERS)_static
SYSAPPSHARED := $(SYSBINDIR)/$(APPSHARED)
SYSAPPSTATIC := $(SYSBINDIR)/$(APPSTATIC)

#----------------------------------------------------------#
REQOBJS   =   $(LIBVED_SRCDIR)/libved+.c $(LIBVED_SRCDIR)/lib/tinyexpr/tinyexpr.c

APPFLAGS :=

HAS_TCC  := 0
HAS_CURL := 0
HAS_PROGRAMMING_LANGUAGE := 1
PROGRAMMING_LANGUAGE_NAME = lai
PROGRAMMING_LANGUAGE_EXT_NAME = led
PROGRAMMING_LANGUAGE_DIR  = $(LIBVED_SRCDIR)/lib/$(PROGRAMMING_LANGUAGE_NAME)

ifneq ($(HAS_PROGRAMMING_LANGUAGE), 0)
  APPFLAGS += -DHAS_PROGRAMMING_LANGUAGE
  REQOBJS  += $(PROGRAMMING_LANGUAGE_DIR)/$(PROGRAMMING_LANGUAGE_EXT_NAME).c
endif

ifneq ($(HAS_JSON), 0)
  REQOBJS += $(LIBVED_SRCDIR)/lib/json/json.c
endif

APPFLAGS  +=   $(REQOBJS)
APPFLAGS  +=  -I$(SYSINCDIR) -L$(SYSLIBDIR) -lved -lm

# This is to set specific order, needed by the linker in the static target
# as it has to be after the required objects but before -lm (if present)
ifneq ($(HAS_PROGRAMMING_LANGUAGE), 0)
  APPFLAGS += -l$(PROGRAMMING_LANGUAGE_NAME)
endif

APPFLAGS += -lm

APPLICATION_FLAGS :=

ifdef APPLICATION_FLAGS
  APPFLAGS += $(APPLICATION_FLAGS)
endif

APPFLAGS += $(FLAGS)

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
APPFLAGS += -DHAS_JSON
APPFLAGS += -DHAS_USER_EXTENSIONS
APPFLAGS += -DVED_DIR='"$(SYSDIR)"'
APPFLAGS += -DVED_DATADIR='"$(SYSDATADIR)"'
APPFLAGS += -DVED_TMPDIR='"$(SYSTMPDIR)"'

app-all: app-static app-shared

clean_app: clean_app_static clean_app_shared
clean_app_shared:
	@$(TEST) ! -f $(SYSAPPSHARED) || $(RM) $(SYSAPPSHARED)
clean_veda_static:
	@$(TEST) ! -f $(SYSAPPSTATIC) || $(RM) $(SYSAPPSTATIC)

app-shared:
	$(CC) -x c $(THIS_APPSRC) $(APPFLAGS) $(SHARED_APP_FLAGS) -o $(APPSHARED)
	@$(INSTALL) -v $(APPSHARED) $(SYSBINDIR)
	@$(RM) $(APPSHARED)

app-static:
	$(CC) -x c $(THIS_APPSRC) $(APPFLAGS) $(STATIC_CFLAGS) -o $(APPSTATIC)
	@$(INSTALL) -v $(APPSTATIC) $(SYSBINDIR)
	@$(RM) $(APPSTATIC)

app-shared-preproc:
	$(CC) -x c $(THIS_APPSRC) $(APPFLAGS) -E

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
	@$(TEST) -d $(SYSDATADIR) || $(MKDIR_P) -m 0700 $(SYSDATADIR)
	@$(TEST) -d $(SYSTMPDIR)  || $(MKDIR_P) -m 0700 $(SYSTMPDIR)

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
