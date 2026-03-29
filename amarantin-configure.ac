AC_INIT([myanalysis], [0.1.0], [you@example.org])
AM_INIT_AUTOMAKE([foreign subdir-objects])
AC_CONFIG_SRCDIR([src/main.cpp])
AC_CONFIG_HEADERS([config.h])

AC_PROG_CXX
AC_PROG_INSTALL

AC_LANG([C++])
AX_CXX_COMPILE_STDCXX([17], [noext], [mandatory])

AC_CHECK_HEADERS([iostream vector string])

AC_ARG_WITH([root],
  [AS_HELP_STRING([--with-root=DIR], [ROOT installation prefix])],
  [ROOTSYS="$withval"],
  [ROOTSYS="${ROOTSYS:-}"])

if test "x$ROOTSYS" != "x"; then
  CPPFLAGS="$CPPFLAGS -I$ROOTSYS/include"
  LDFLAGS="$LDFLAGS -L$ROOTSYS/lib"
fi

AC_CHECK_LIB([Core], [main], [], [
  AC_MSG_ERROR([ROOT libraries not found. Source your setup or pass --with-root=DIR])
])

AC_CONFIG_FILES([
  Makefile
  src/Makefile
  myanalysis-config
])
AC_OUTPUT
