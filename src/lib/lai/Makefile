NAME    := lai
VERSION := 0.1

ENABLE_REPL := 0
ENABLE_HTTP := 1

SYSDIR  := sys
BINDIR   = $(SYSDIR)/bin
INCDIR   = $(SYSDIR)/include
LIBDIR   = $(SYSDIR)/lib

CC          := gcc
CC_STD      := -std=c11
EXT_FLAGS   :=
BASE_FLAGS  := -g -O2 -march=native $(EXT_FLAGS)
# -fvisibility=hidden

DEBUG_FLAGS := -Wextra -Wno-shadow -Wall -Wunused-function -Wunused-macros

FLAGS       := $(BASE_FLAGS) $(DEBUG_FLAGS) -lm

HEADER       = $(NAME).h

LIB_FILES    = $(NAME).c
INTERP_FILES = main.c

INTERP_FLAGS = -L$(LIBDIR) -I$(INCDIR) $(FLAGS)

#ENABLE_REPL := 0
ifneq ($(ENABLE_REPL), 0)
  INTERP_FILES += linenoise.c
  INTERP_FLAGS += -DENABLE_REPL
endif

SHARED_FLAGS :=
STATIC_FLAGS = -DDISABLE_HTTP -static

#ENABLE_HTTP := 1
ifneq ($(ENABLE_HTTP), 0)
  SHARED_FLAGS += -lcurl
else
  SHARED_FLAGS =-DDISABLE_HTTP
endif

shared-library: Env $(LIBDIR)/lib$(NAME)-$(VERSION).so $(INCDIR)/$(HEADER)

$(INCDIR)/$(HEADER):
	@$(CP) $(HEADER) $(INCDIR)

$(LIBDIR)/lib$(NAME)-$(VERSION).so:
	$(CC) -o $(LIBDIR)/lib$(NAME)-$(VERSION).so -shared -fPIC $(FLAGS) $(SHARED_FLAGS) $(LIB_FILES)
	@cd $(LIBDIR) && $(LN_S) -vf lib$(NAME)-$(VERSION).so lib$(NAME).so

static-library: Env $(LIBDIR)/lib$(NAME)-$(VERSION).a

$(LIBDIR)/lib$(NAME)-$(VERSION).a:
	$(CC) $(FLAGS) $(LIB_FILES) $(STATIC_FLAGS) -c -o lib$(NAME).o
	@$(AR) $(LIBDIR)/lib$(NAME)-$(VERSION).a lib$(NAME).o
	@cd $(LIBDIR) && $(LN_S) -vf lib$(NAME)-$(VERSION).a lib$(NAME).a
	@$(RM) lib$(NAME).o

interpr: shared-library
	$(CC) -o $(BINDIR)/$(NAME) $(INTERP_FLAGS) -l$(NAME) $(SHARED_FLAGS) $(INTERP_FILES)

interpr-static: static-library
	$(CC) $(INTERP_FILES) $(INTERP_FLAGS) -l$(NAME) -lm $(STATIC_FLAGS) -o $(BINDIR)/$(NAME)-static

clean: clean_header clean_shared clean_static

clean_header:
	@$(TEST) ! -f $(INCDIR)/$(HEADER)     || $(RM) $(INCDIR)/$(HEADER)

clean_shared:
	@$(TEST) ! -f $(LIBDIR)/lib$(NAME)-$(VERSION).so || $(RM) $(LIBDIR)/lib$(NAME)-$(VERSION).so
	@$(TEST) ! -L $(LIBDIR)/lib$(NAME).so || $(RM) $(LIBDIR)/lib$(NAME).so

clean_static:
	@$(TEST) ! -f $(LIBDIR)/lib$(NAME)-$(VERSION).a || $(RM) $(LIBDIR)/lib$(NAME)-$(VERSION).a
	@$(TEST) ! -L $(LIBDIR)/lib$(NAME).a || $(RM) $(LIBDIR)/lib$(NAME).a

Env: makeenv checkenv
makeenv:
	@$(TEST) -d $(SYSDIR)  || $(MKDIR_P) $(SYSDIR)
	@$(TEST) -d $(LIBDIR)  || $(MKDIR)   $(LIBDIR)
	@$(TEST) -d $(INCDIR)  || $(MKDIR)   $(INCDIR)
	@$(TEST) -d $(BINDIR)  || $(MKDIR)   $(BINDIR)

checkenv:
	@$(TEST) -w $(SYSDIR)  || exit 1
	@$(TEST) -w $(LIBDIR)  || exit 1
	@$(TEST) -w $(INCDIR)  || exit 1
	@$(TEST) -w $(BINDIR)  || exit 1

AR   = ar rs
RM   = rm
CP   = cp
TEST = test
LN   = ln
LN_S =  $(LN) -s
MKDIR = mkdir
MKDIR_P = $(MKDIR) -p
