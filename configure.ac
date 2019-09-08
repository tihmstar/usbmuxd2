AC_PREREQ([2.69])
AC_INIT([usbmuxd2], m4_esyscmd([git rev-list --count HEAD | tr -d '\n']), [tihmstar@gmail.com])

AC_CANONICAL_SYSTEM

AC_CANONICAL_HOST

AC_CONFIG_SRCDIR([config.h.in])
AC_CONFIG_HEADERS([config.h])
AC_CONFIG_MACRO_DIRS([m4])
AM_PROG_LIBTOOL
AM_INIT_AUTOMAKE([subdir-objects])

# Checks for programs.
AC_PROG_CXX
AC_PROG_CC

# Check for operating system
AC_MSG_CHECKING([whether we need platform-specific build settings])
case $host_os in
  darwin* )
        CXXFLAGS+=" -stdlib=libc++"
        ;;
esac


CFLAGS+=" -D CUSTOM_LOGGING=1"
CXXFLAGS+=" -D CUSTOM_LOGGING=1 -std=c++17 -lstdc++fs"
CXXFLAGS+=" -D VERSION_COMMIT_COUNT=\\\"$(git rev-list --count HEAD | tr -d '\n')\\\""
CXXFLAGS+=" -D VERSION_COMMIT_SHA=\\\"$(git rev-parse HEAD | tr -d '\n')\\\""


PKG_CHECK_MODULES(libusb, libusb-1.0 >= 1.0.9)
PKG_CHECK_MODULES(libplist, libplist >= 2.0.0)
PKG_CHECK_MODULES(libplistxx, libplist++ >= 2.0.0)
PKG_CHECK_MODULES(libimobiledevice, libimobiledevice-1.0 >= 1.2.1, have_limd=yes, have_limd=no)
PKG_CHECK_MODULES(avahi, avahi-client >= 0.7, have_avahi=yes, have_avahi=no)
AC_CHECK_LIB(pthread, [pthread_create, pthread_mutex_lock], [AC_SUBST(libpthread_LIBS,[-lpthread])], [AC_MSG_ERROR([libpthread is required to build usbmuxd2])])

AC_ARG_WITH([preflight],
            [AS_HELP_STRING([--without-libimobiledevice],
            [do not build with libimobiledevice @<:@default=yes@:>@])],
            [with_limd=no],
            [with_limd=yes])

AC_ARG_WITH([wifi],
            [AS_HELP_STRING([--without-wifi],
            [do not build with wifi support @<:@default=yes@:>@])],
            [with_wifi=no],
            [with_wifi=yes])


if test "x$with_limd" == "xyes"; then
  if test "x$have_limd" = "xyes"; then
    AC_DEFINE(HAVE_LIBIMOBILEDEVICE, 1, [Define if you have libimobiledevice support])
    AC_SUBST(libimobiledevice_CFLAGS)
    AC_SUBST(libimobiledevice_LIBS)

    if test "x$with_wifi" == "xyes"; then
      if test "x$have_avahi" = "xyes"; then
        AC_DEFINE(HAVE_WIFI_SUPPORT, 1, [Define if you have wifi support])
        AC_SUBST(avahi_CFLAGS)
        AC_SUBST(avahi_LIBS)
      else
        AC_MSG_ERROR([wifi support requested but avahi-client could not be found])
      fi
    else
      echo "*** Note: wifi support has been disabled ***"
    fi

  else
    echo "*** Note: libimobiledevice support has been disabled ***"
  fi
else
  if test "x$with_wifi" == "xyes"; then
    AC_MSG_ERROR([wifi support requested but libimobiledevice could not be found])
  fi
fi

# Checks for libraries.

# Checks for header files.
AC_CHECK_HEADERS([arpa/inet.h fcntl.h limits.h netinet/in.h stdint.h stdlib.h string.h sys/socket.h sys/time.h unistd.h])

# Checks for typedefs, structures, and compiler characteristics.
AC_CHECK_HEADER_STDBOOL
AC_TYPE_SIZE_T
AC_TYPE_SSIZE_T
AC_TYPE_UINT16_T
AC_TYPE_UINT32_T
AC_TYPE_UINT64_T
AC_TYPE_UINT8_T

# Checks for library functions.
AC_CHECK_FUNCS([clock_gettime gettimeofday memset socket strdup strerror])

LT_INIT

AC_CONFIG_FILES([Makefile
                 usbmuxd2/Makefile])
AC_OUTPUT
