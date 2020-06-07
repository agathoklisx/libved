NAME    := lai
VERSION := 0.1
SYSDIR  := $(shell realpath ../../sys)
BINDIR   = $(SYSDIR)/bin
INCDIR   = $(SYSDIR)/include
LIBDIR   = $(SYSDIR)/lib

CC          := gcc
CC_STD      := -std=c11
BASE_FLAGS  := -g -O2 -march=native
# -fvisibility=hidden

DEBUG_FLAGS := -Wextra -Wno-shadow -Wall -Wunused-function -Wunused-macros -Wno-unused-parameter -Wno-strict-aliasing

FLAGS       := $(BASE_FLAGS) $(DEBUG_FLAGS) -lm

HEADER       = $(NAME).h

LIB_FILES    = $(NAME).c
INTERP_FILES = main.c

ENABLE_HTTP := 1
ifneq ($(ENABLE_HTTP), 0)
  FLAGS += -lcurl
else
  FLAGS += -DDISABLE_HTTP
endif

INTERP_FLAGS = -L$(LIBDIR) -I$(INCDIR) $(FLAGS)

ENABLE_REPL := 0
ifneq ($(ENABLE_REPL), 0)
  INTERP_FILES += linenoise.c
  INTERP_FLAGS += -DENABLE_REPL
endif

shared-library: $(LIBDIR)/lib$(NAME)-$(VERSION).so $(INCDIR)/$(HEADER)
	@echo $(LIBDIR)/lib$(NAME)-$(VERSION).so
$(INCDIR)/$(HEADER):
	@$(CP) $(HEADER) $(INCDIR)

$(LIBDIR)/lib$(NAME)-$(VERSION).so:
	$(CC) -o $(LIBDIR)/lib$(NAME)-$(VERSION).so -shared -fPIC $(FLAGS) $(LIB_FILES)
	@$(LN_S) -vf $(LIBDIR)/lib$(NAME)-$(VERSION).so $(LIBDIR)/lib$(NAME).so

static-library: $(LIBDIR)/lib$(NAME)-$(VERSION).a

$(LIBDIR)/lib$(NAME)-$(VERSION).a:
	$(CC) $(FLAGS) $(LIB_FILES) -static -c -o lib$(NAME).o
	@$(AR) $(LIBDIR)/lib$(NAME)-$(VERSION).a lib$(NAME).o
	@$(LN_S) -vf $(LIBDIR)/lib$(NAME)-$(VERSION).a $(LIBDIR)/lib$(NAME).a
	@$(RM) lib$(NAME).o

interp: shared-library
	$(CC) -o $(BINDIR)/$(NAME) -lm -l$(NAME) $(INTERP_FLAGS) $(INTERP_FILES)

clean: clean-header clean-shared clean-static

clean-header:
	@$(TEST) ! -f $(INCDIR)/$(HEADER)     || $(RM) $(INCDIR)/$(HEADER)

clean-shared:
	@$(TEST) ! -f $(LIBDIR)/lib$(NAME)-$(VERSION).so || $(RM) $(LIBDIR)/lib$(NAME)-$(VERSION).so
	@$(TEST) ! -L $(LIBDIR)/lib$(NAME).so || $(RM) $(LIBDIR)/lib$(NAME).so

clean-static:
	@$(TEST) ! -f $(LIBDIR)/lib$(NAME)-$(VERSION).a || $(RM) $(LIBDIR)/lib$(NAME)-$(VERSION).a
	@$(TEST) ! -L $(LIBDIR)/lib$(NAME).a || $(RM) $(LIBDIR)/lib$(NAME).a

AR   = ar rs
RM   = rm
CP   = cp
TEST = test
LN   = ln
LN_S =  $(LN) -s
