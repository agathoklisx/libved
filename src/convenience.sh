#!/bin/sh

# This is a sample that can be used to compile the source code
# and should provide a shared library and an executable.

# Adjust for the static targets accordingly.

# If the number of args is > 0 then it builds only the executable.

# You may need first to `chmod' this unit to make it executable.

# This sample enables almost everything, except local additions and the dict
# file, which anyway is here for demonstration and might not have any interest
# except the author.

if [ -z $LIBVED_SYSDIR ]; then
  SYSDIR=$PWD/sys
else
  SYSDIR=$LIBVED_SYSDIR
fi

if [ -z $LIBVED_DATADIR ]; then
  SYSDATADIR=$SYSDIR/data
else
  SYSDATADIR=$LIBVED_DATADIR
fi

if [ -z $LIBVED_TMPDIR ]; then
  SYSTMPDIR=$SYSDIR/tmp
else
  SYSTMPDIR=$LIBVED_TMPDIR
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
    HAS_PROGRAMMING_LANGUAGE=1                      \
    HAS_USER_EXTENSIONS=1                           \
    HAS_JSON=0                                      \
    HAS_TCC=0                                       \
    HAS_LOCAL_EXTENSIONS=0                          \
    CLEAR_BLANKLINES=1                              \
    TAB_ON_INSERT_MODE_INDENTS=0                    \
    TABWIDTH=8                                      \
    CARRIAGE_RETURN_ON_NORMAL_IS_LIKE_INSERT_MODE=0 \
    SPACE_ON_NORMAL_IS_LIKE_INSERT_MODE=0           \
    SMALL_E_ON_NORMAL_GOES_INSERT_MODE=0            \
    BACKSPACE_ON_FIRST_IDX_REMOVE_TRAILING_SPACES=1 \
    BACKSPACE_ON_NORMAL_IS_LIKE_INSERT_MODE=0       \
    BACKSPACE_ON_NORMAL_GOES_UP=1                   \
    BACKSPACE_ON_INSERT_GOES_UP_AND_JOIN=1          \
    veda-shared
