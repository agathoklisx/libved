#----------------------------------------------------------#
THIS_LIBNAME := ved

API := 0
REV := 1
VER := 1

THISDIR := $(shell realpath .)

THIS_LIBVERS  :=  $(API).$(REV)

THIS_HEADER   :=  lib$(THIS_LIBNAME).h
THIS_LIBSRC   :=  lib$(THIS_LIBNAME).c
THIS_LIB      :=  lib$(THIS_LIBNAME)-$(THIS_LIBVERS)

CC            := gcc
CC_STD        := -std=c11

SYSKERNEL     := $(shell uname -s)
SYSARCH       := $(shell uname -m)
SYS           := $(SYSKERNEL)-$(SYSARCH)

SYSDIR := sys
SYSLIBDIR  := $(SYSDIR)/lib
SYSBINDIR  := $(SYSDIR)/bin
SYSINCDIR  := $(SYSDIR)/include

#----------------------------------------------------------#
LIBNAME        := $(THIS_LIBNAME)-$(THIS_LIBVERS)

THIS_SHAREDLIB := $(SYSLIBDIR)/$(THIS_LIB).so
THIS_STATICLIB := $(SYSLIBDIR)/$(THIS_LIB).a

BASE_FLAGS     := -g -O2 -march=native -fvisibility=hidden -fdollars-in-identifiers
DEBUG_FLAGS    := -Wextra -Werror -Wshadow -Wall -Wunused-function -Wunused-macros
# -fsanitize=undefined
# -Wpedantic
CHECK_PTR_BOUNDS_FLAGS := -mmpx -fcheck-pointer-bounds

SHARED_CFLAGS  := -shared -fPIC
STATIC_CFLAGS  := -static

FLAGS          := $(BASE_FLAGS)

#----------------------------------------------------------#
HAS_REGEXP := 0

ifneq ($(HAS_REGEXP), 0)
  FLAGS += -DHAS_REGEXP
endif

HAS_SHELL_COMMANDS := 1

ifneq ($(HAS_SHELL_COMMANDS), 0)
  FLAGS += -DHAS_SHELL_COMMANDS
endif

DEBUG := 0

ifneq ($(DEBUG), 0)
  FLAGS += -DENABLE_WRITING $(DEBUG_FLAGS)
endif

HAS_USER_EXTENSIONS := 0

ifneq ($(HAS_USER_EXTENSIONS), 0)
  FLAGS += -DHAS_USER_EXTENSIONS
endif

WORD_LEXIKON_FILE :=

ifdef WORD_LEXIKON_FILE
  FLAGS += -DWORD_LEXIKON_FILE='"$(WORD_LEXIKON_FILE)"'
endif

CHECK_PTR_BOUNDS := 0

ifneq ($(CHECK_PTR_BOUNDS), 0)
  FLAGS += $(CHECK_PTR_BOUNDS_FLAGS)
endif

ifeq (-$(CC), -gcc)
  FLAGS += -Wimplicit-fallthrough=0
endif

#----------------------------------------------------------#
LIBFLAGS := -I. -I$(SYSINCDIR) $(FLAGS)
# clang complains about unused command line arguments
#LIBFLAGS := -I. -I$(SYSINCDIR) -L$(SYSLIBDIR) $(FLAGS)
#LIBFLAGS := -I. -I$(SYSINCDIR) -L$(SYSLIBDIR) $(FLAGS)

all: shared static veda-all

prereq: Env

shared: prereq $(THIS_SHAREDLIB) header
$(THIS_SHAREDLIB):
	$(CC) -x c $(THIS_LIBSRC) $(LIBFLAGS) $(SHARED_CFLAGS) -o $(THIS_SHAREDLIB)

static: prereq $(THIS_STATICLIB) header
$(THIS_STATICLIB):
	$(CC) -x c $(THIS_LIBSRC) $(LIBFLAGS) $(STATIC_CFLAGS) -c -o $(THIS_LIB).o
	@$(AR) $(THIS_STATICLIB) $(THIS_LIB).o
	@$(RM) $(THIS_LIB).o

header: $(SYSINCDIR)/$(THIS_HEADER)
$(SYSINCDIR)/$(THIS_HEADER):
	@$(CP) $(THIS_HEADER) $(SYSINCDIR)

preproc:
	$(CC) -x c $(THIS_LIBSRC) $(LIBFLAGS) $(SHARED_CFLAGS) -E

clean: clean_shared clean_static clean_header clean_veda
clean_shared: clean_header
	@$(TEST) ! -f $(THIS_SHAREDLIB) || $(RM) $(THIS_SHAREDLIB)
clean_static: clean_header
	@$(TEST) ! -f $(THIS_STATICLIB) || $(RM) $(THIS_STATICLIB)
clean_header:
	@$(TEST) ! -f $(SYSINCDIR)/$(THIS_HEADER) || $(RM) $(SYSINCDIR)/$(THIS_HEADER)

#----------------------------------------------------------#
EDNAME := veda

THIS_APPSRC  := $(EDNAME).c
THIS_APPVERS := $(VER)$(API)$(REV)

EDSHARED    := $(EDNAME)-$(THIS_APPVERS)_shared
EDSTATIC    := $(EDNAME)-$(THIS_APPVERS)_static
SYSEDSHARED := $(SYSBINDIR)/$(EDSHARED)
SYSEDSTATIC := $(SYSBINDIR)/$(EDSTATIC)

#----------------------------------------------------------#
APPFLAGS    := -I$(SYSINCDIR) -L$(SYSLIBDIR) -l$(LIBNAME) $(FLAGS)

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
	@$(TEST) -d $(SYSDIR) || $(MKDIR_P) $(SYSDIR)
	@$(TEST) -d $(SYSLIBDIR) || $(MKDIR) $(SYSLIBDIR)
	@$(TEST) -d $(SYSINCDIR) || $(MKDIR) $(SYSINCDIR)
	@$(TEST) -d $(SYSBINDIR) || $(MKDIR) $(SYSBINDIR)
checkenv:
	@$(TEST) -w $(SYSDIR) || exit 1
	@$(TEST) -w $(SYSLIBDIR) || exit 1
	@$(TEST) -w $(SYSINCDIR) || exit 1
	@$(TEST) -w $(SYSBINDIR) || exit 1

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
