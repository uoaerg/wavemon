dnl Process this file with autoconf to produce a configure script.
AC_PREREQ(2.64)
AC_INIT([wavemon], [0.8.2], [https://github.com/uoaerg/wavemon], [wavemon-current],
	[https://github.com/uoaerg/wavemon])


# Variables
AC_CONFIG_AUX_DIR([config])
AC_CONFIG_FILES([Makefile])

CFLAGS="-O2 -Wall"

# wavemon is only supported on Linux, warn user about futile compilations
AC_CANONICAL_HOST
case "$host_os" in
  *linux*|*LINUX*) ;;
  *) AC_MSG_ERROR([incompatible OS '$host_os' - required is Linux with wireless extensions.])
esac

# Checks for programs.
AC_PROG_CC
AC_PROG_CPP
AC_PROG_INSTALL

# Checks for header files.
AC_HEADER_STDC
AC_HEADER_TIME
AC_CHECK_HEADERS([ncurses.h fcntl.h netdb.h sys/ioctl.h sys/time.h unistd.h\
		  net/if_arp.h netinet/ether.h net/ethernet.h pthread.h],
		 [], [AC_MSG_ERROR($ac_header not found)])

# linux/if.h needs 'struct sockaddr'
# linux/wireless.h in turn depends on linux/if.h
AC_CHECK_HEADERS([sys/socket.h linux/if.h linux/wireless.h], [],
		 [AC_MSG_ERROR(can not use $ac_header)],
		 [#if HAVE_SYS_SOCKET_H
		  # include <sys/socket.h>
		  #endif
		  #if HAVE_LINUX_IF_H
		  # include <linux/if.h>
		  #endif
		 ])
# require minimum version of the wireless extensions
AC_COMPILE_IFELSE([AC_LANG_PROGRAM(
		   [#include <sys/socket.h>
		    #include <linux/wireless.h>
		    #if WIRELESS_EXT < 18
		    #error "Need minimum version WIRELESS_EXT = 18"
		    #endif])],[],
		   [AC_MSG_FAILURE([linux/wireless.h is too old, need WE >= 18])])
AC_HEADER_STDBOOL

# Tests involving libraries
AC_CHECK_LIB([m], [pow], [],              [AC_MSG_ERROR(math library not found)])
AC_CHECK_LIB([ncurses], [waddstr], [],    [AC_MSG_ERROR(ncurses library not found)])
AC_CHECK_LIB([pthread], [pthread_create], [CFLAGS="$CFLAGS -pthread"],
					  [AC_MSG_ERROR(pthread library not found)])

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

# libnl3 with GeNetlink support - wavemon is tracking changes to iw
PKG_CHECK_MODULES([LIBNL3], [libnl-3.0 >= 3.2], [],
		  [CFLAGS="$CFLAGS $LIBNL3_CFLAGS"],
		  [AC_MSG_ERROR(need libnl >= 3.2)])
PKG_CHECK_MODULES([LIBNL3_GENL], [libnl-genl-3.0 >= 3.2],
		  [LIBS="$LIBS $LIBNL3_GENL_LIBS"],
		  [AC_MSG_ERROR(need libnl-genl >= 3.2)])

# Checks for typedefs, structures, and compiler characteristics.
AC_PROG_GCC_TRADITIONAL
AC_C_CONST
AC_C_INLINE

AC_OUTPUT
