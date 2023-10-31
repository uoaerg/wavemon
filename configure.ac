dnl Process this file with autoconf to produce a configure script.
AC_PREREQ(2.64)
AC_INIT([wavemon], [0.9.5], [https://github.com/uoaerg/wavemon], [wavemon-current],
	[https://github.com/uoaerg/wavemon])


# Variables
AC_CONFIG_AUX_DIR([config])
AC_CONFIG_FILES([Makefile])

: ${CFLAGS="-O2 -Wall -Wextra -Wpedantic"}

# wavemon is only supported on Linux, warn user about futile compilations
AC_CANONICAL_HOST
case "$host_os" in
  *linux*|*LINUX*) ;;
  *) AC_MSG_ERROR([incompatible OS '$host_os' - required is Linux with nl80211.])
esac

# NCURSES_CHECK_CONFIG(LIBNAME, CONFIG_SCRIPT, ELSE_PART)
m4_define([NCURSES_CHECK_CONFIG],
[
   ncurses_config_script_libs=$([$2] --libs 2> /dev/null)
   ncurses_config_script_cflags=$([$2] --cflags 2> /dev/null)

   ncurses_script_success=no
   ncurses_save_LDFLAGS="$LDFLAGS"
   ncurses_save_CFLAGS="$CFLAGS"
   if test "x$ncurses_config_script_libs" != x; then
      LDFLAGS="$ncurses_config_script_libs $LDFLAGS"
      CFLAGS="$CFLAGS $ncurses_config_script_cflags"
      AC_CHECK_LIB([$1], [addnwstr], [
                     AC_DEFINE([HAVE_LIBNCURSESW], 1)
                     CFLAGS="$CFLAGS -D_XOPEN_SOURCE_EXTENDED"
                     LIBS="$ncurses_config_script_libs $LIBS"
                     ncurses_script_success=yes
                   ], [
                     CFLAGS="$ncurses_save_CFLAGS"
                   ])
      LDFLAGS="$ncurses_save_LDFLAGS"
   fi
   if test "x$ncurses_script_success" == xno; then
      [$3]
   fi
])

# Checks for programs.
AC_PROG_CC_C99
AC_PROG_CPP
AC_PROG_INSTALL

# Checks for header files.
AC_HEADER_STDC
AC_HEADER_TIME
AC_CHECK_HEADERS([ncurses.h fcntl.h netdb.h sys/ioctl.h sys/time.h unistd.h\
		  net/if_arp.h netinet/ether.h net/ethernet.h pthread.h],
		 [], [AC_MSG_ERROR($ac_header not found)])
AC_CHECK_HEADERS([ncursesw/curses.h])

# linux/if.h needs 'struct sockaddr'
AC_CHECK_HEADERS([sys/socket.h linux/if.h], [],
		 [AC_MSG_ERROR(can not use $ac_header)],
		 [#if HAVE_SYS_SOCKET_H
		  # include <sys/socket.h>
		  #endif
		  #if HAVE_LINUX_IF_H
		  # include <linux/if.h>
		  #endif
		 ])
AC_HEADER_STDBOOL

# Tests involving libraries
AC_CHECK_LIB([m], [pow], [],              [AC_MSG_ERROR(math library not found)])
AC_CHECK_LIB([pthread], [pthread_create], [CFLAGS="$CFLAGS -pthread"],
					  [AC_MSG_ERROR(pthread library not found)])

# Check various combinations of ncurses5/6 configuration scripts (based on htop's checks).
NCURSES_CHECK_CONFIG([ncursesw6], "ncursesw6-config",
 NCURSES_CHECK_CONFIG([ncursesw], "ncursesw6-config",
  NCURSES_CHECK_CONFIG([ncursesw], "ncursesw5-config",
   NCURSES_CHECK_CONFIG([ncurses], "ncurses5-config",
    AC_CHECK_LIB([ncursesw], [addnwstr], [],
		 [AC_CHECK_LIB([ncurses], [waddstr], [],
			       [AC_MSG_ERROR(ncurses library not found)])])))))

# libcap - allow to disable, since e.g. OpenWrt does not use it.
# https://github.com/uoaerg/wavemon/issues/54
AC_ARG_WITH([libcap],
	    [AS_HELP_STRING([--without-libcap],
	    [disable support for libcap])],
	    [],
	    [with_libcap=yes])

AS_IF([test "x$with_libcap" != xno], AC_CHECK_LIB([cap], [cap_get_flag]))

# Testing library functions
AC_CHECK_FUNCS([gettimeofday ether_ntohost], [],
	       [AC_MSG_ERROR(function '$ac_func' not supported)])

# libnl3 cli package (pulls in genl and route package).
PKG_CHECK_MODULES([LIBNL3_CLI], [libnl-cli-3.0 >= 3.2.22], [],
	       [AC_MSG_ERROR(need libnl-cli-3.0 >= 3.2.22)])

# Checks for typedefs, structures, and compiler characteristics.
AC_PROG_GCC_TRADITIONAL
AC_C_CONST
AC_C_INLINE

AC_OUTPUT
