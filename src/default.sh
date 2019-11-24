#!/bin/sh

# This is a convenience file to compile the shared targets with the default
# options. See convenience.sh for the opposite (compile with all the options
# enabled)

# You may need first to `chmod' this unit to make it executable.

if [ 0 = $# ]; then
  make clean_shared
fi

make clean_veda_shared
if [ -z $CC ]; then
  CC=gcc
fi

make                                                \
    CC=$CC                                          \
    DEBUG=0                                         \
    HAS_REGEXP=0                                    \
    HAS_SHELL_COMMANDS=1                            \
    HAS_USER_EXTENSIONS=0                           \
    HAS_SPELL=0                                     \
    HAS_JSON=0                                      \
    HAS_EXPR=0                                      \
    HAS_LOCAL_EXTENSIONS=0                          \
    HAS_HISTORY=0                                   \
    VED_DATA_DIR="$PWD/sys/data"                    \
    TMPDIR="$PWD/sys/tmp"                           \
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
