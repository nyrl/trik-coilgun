#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ([2.65])
AC_INIT([trik-coilgun], [0.0.1])
AM_INIT_AUTOMAKE([foreign color-tests -Wall -Werror])
AC_CONFIG_SRCDIR([src/main.c])
AC_CONFIG_HEADERS([config.h])
AC_CONFIG_MACRO_DIR([m4])

# We want more functions
AC_GNU_SOURCE

# Checks for programs.
AC_PROG_CC
AC_PROG_CC_C99
AC_PROG_CXX
CXXFLAGS+=" -std=c++0x"

# Checks for libraries.
AC_PROG_LIBTOOL
PKG_PROG_PKG_CONFIG


# Checks for header files.


# Checks for typedefs, structures, and compiler characteristics.
AC_HEADER_ASSERT
AC_HEADER_STDBOOL
AC_C_INLINE
AC_C_CONST
AC_TYPE_SIZE_T
AC_TYPE_SSIZE_T

# Checks for library functions.


# Check for C++0x support features
AC_LANG(C++)


# Check for pkgconfig dependencies and setup flags
PKGCONFIG_REQUIRES=""
AC_SUBST([PKGCONFIG_REQUIRES])

#PKG_CHECK_MODULES([PKGCONFIG], [${PKGCONFIG_REQUIRES}])
#CPPFLAGS+=" ${PKGCONFIG_CFLAGS}"
#LIBS+=" ${PKGCONFIG_LIBS}"


AC_CONFIG_FILES([Makefile
		 build/Makefile])

AC_OUTPUT

