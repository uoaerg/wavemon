dnl Process this file with autoconf to produce a configure script.
AC_PREREQ(2.50)
AC_INIT([wavemon], [0.6], [gerrit@erg.abdn.ac.uk], [wavemon-current])


# Variables
AC_CONFIG_AUX_DIR([config])
AC_CONFIG_FILES([Makefile])
AC_DEFINE_UNQUOTED([BUILD_DATE], ["`/bin/date`"],
		   [date wavemon was last configured and built])

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
AC_CHECK_HEADERS([ncurses.h fcntl.h netdb.h sys/ioctl.h sys/time.h unistd.h],
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
AC_HEADER_STDBOOL

# Tests involving libraries
AC_CHECK_LIB([m], [pow])
AC_CHECK_LIB([ncurses], [waddstr])


# Testing library functions
AC_CHECK_FUNCS([modf log10 gettimeofday	\
		memmove memset strdup	\
		strchr strspn strcspn	\
		strcasecmp strncasecmp	\
		strtol], [],
	       [AC_MSG_ERROR(function '$ac_func' not supported)])

# Checks for typedefs, structures, and compiler characteristics.
AC_PROG_GCC_TRADITIONAL
AC_C_CONST
AC_C_INLINE

AC_OUTPUT
