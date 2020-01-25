#!/bin/sh

# This is a sample that can be used to compile the source code
# and should provide a shared library and an executable.

# Adjust for the static targets accordingly.

# If the number of args is > 0 then it builds only the executable.

# You may need first to `chmod' this unit to make it executable.

# This sample enables almost everything, except local additions and the dict
# file, which anyway is here for demonstration and might not have any interest
# except the author.

if [ -z $VED_SYSDIR ]; then
  SYSDIR=$PWD/sys
else
  SYSDIR=$VED_SYSDIR
fi

if [ -z $VED_DATADIR ]; then
  SYSDATADIR=$SYSDIR/data
else
  SYSDATADIR=$VED_DATADIR
fi

if [ -z $VED_TMPDIR ]; then
  SYSTMPDIR=$SYSDIR/tmp
else
  SYSTMPDIR=$VED_TMPDIR
fi

if [ 0 = $# ]; then
  make SYSDIR=$SYSDIR clean_shared
fi

make SYSDIR=$SYSDIR clean_veda_shared

if [ -z $CC ]; then
  CC=gcc
fi

make                                                \
    CC=$CC                                          \
    SYSDIR=$SYSDIR                                  \
    SYSDATADIR=$SYSDATADIR                          \
    SYSTMPDIR=$SYSTMPDIR                            \
    DEBUG=1                                         \
    DEBUG_INTERPRETER=1                             \
    HAS_REGEXP=1                                    \
    HAS_SHELL_COMMANDS=1                            \
    HAS_USER_EXTENSIONS=1                           \
    HAS_SPELL=1                                     \
    HAS_JSON=1                                      \
    HAS_EXPR=1                                      \
    HAS_LOCAL_EXTENSIONS=0                          \
    HAS_HISTORY=1                                   \
    CLEAR_BLANKLINES=1                              \
    TAB_ON_INSERT_MODE_INDENTS=0                    \
    TABWIDTH=8                                      \
    CARRIAGE_RETURN_ON_NORMAL_IS_LIKE_INSERT_MODE=1 \
    SPACE_ON_NORMAL_IS_LIKE_INSERT_MODE=1           \
    SMALL_E_ON_NORMAL_GOES_INSERT_MODE=1            \
    BACKSPACE_ON_FIRST_IDX_REMOVE_TRAILING_SPACES=1 \
    BACKSPACE_ON_NORMAL_IS_LIKE_INSERT_MODE=1       \
    READ_FROM_SHELL=1                               \
    veda-shared

#   WORD_LEXICON_FILE="/path/to/some/dict.txt"      \
#   VED_APPLICATION_FLAGS=-lutil                    \
