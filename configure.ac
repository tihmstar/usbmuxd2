AC_PREREQ([2.69])
AC_INIT([usbmuxd2], m4_esyscmd([git rev-list --count HEAD | tr -d '\n']), [tihmstar@gmail.com])

AC_CANONICAL_SYSTEM

AC_CANONICAL_HOST

AC_CONFIG_SRCDIR([config.h.in])
AC_CONFIG_HEADERS([config.h])
AC_CONFIG_MACRO_DIRS([m4])
AM_PROG_LIBTOOL
AM_INIT_AUTOMAKE([subdir-objects])

AC_DEFINE([VERSION_COMMIT_COUNT], "m4_esyscmd([git rev-list --count HEAD | tr -d '\n'])", [Git commit count])
AC_DEFINE([VERSION_COMMIT_SHA], "m4_esyscmd([git rev-parse HEAD | tr -d '\n'])", [Git commit sha])
AC_SUBST([VERSION_COMMIT_COUNT], ["m4_esyscmd([git rev-list --count HEAD | tr -d '\n'])"])
AC_SUBST([VERSION_COMMIT_SHA], ["m4_esyscmd([git rev-parse HEAD | tr -d '\n'])"])


# Checks for programs.
AC_PROG_CXX
AC_PROG_CC

# Check for operating system
AC_MSG_CHECKING([whether we need platform-specific build settings])
case $host_os in
  darwin* )
        CXXFLAGS+=" -stdlib=libc++"
        have_mdns="yes"
        ;;
  *)
        ;;
esac


CFLAGS+=' -DCUSTOM_LOGGING="\"log.h\""'
CXXFLAGS+=' -DCUSTOM_LOGGING="\"log.h\""'

CXXFLAGS+=" -std=c++20"


PKG_CHECK_MODULES(libgeneral, libgeneral >= 39)
PKG_CHECK_MODULES(libusb, libusb-1.0 >= 1.0.9)
PKG_CHECK_MODULES(libplist, libplist-2.0 >= 2.2.0)
PKG_CHECK_MODULES(libimobiledevice, libimobiledevice-1.0 >= 1.3.0, have_limd=yes, have_limd=no)
PKG_CHECK_MODULES(avahi, avahi-client >= 0.7, have_avahi=yes, have_avahi=no)

AC_ARG_WITH([libimobiledevice],
            [AS_HELP_STRING([--without-libimobiledevice],
            [do not build with libimobiledevice @<:@default=yes@:>@])],
            [with_limd=no],
            [with_limd=yes])

AC_ARG_WITH([wifi],
            [AS_HELP_STRING([--without-wifi],
            [do not build with wifi support @<:@default=yes@:>@])],
            [with_wifi=no],
            [with_wifi=yes])

AC_ARG_ENABLE([debug],
            [AS_HELP_STRING([--enable-debug],
            [enable debug build(default is no)])],
            [debug_build=true],
            [debug_build=false])


if test "$debug_build" = true; then
  echo "*** Note: debug build requested ***"
  CFLAGS+=" -g -O0 -DDEBUG=1"
  CXXFLAGS+=" -g -O0 -DDEBUG=1"
fi

AM_CONDITIONAL(DEBUG, test x"$debug" = x"true")

if test "x$with_limd" == "xyes"; then
  if test "x$have_limd" = "xyes"; then
    AC_DEFINE(HAVE_LIBIMOBILEDEVICE, 1, [Define if you have libimobiledevice support])
    AC_SUBST(libimobiledevice_CFLAGS)
    AC_SUBST(libimobiledevice_LIBS)
  else
    echo "*** Note: libimobiledevice support has been disabled ***"
  fi
fi

if test "x$with_wifi" == "xyes"; then
  if test "x$have_avahi" = "xyes"; then
    AC_DEFINE(HAVE_WIFI_AVAHI, 1, [Define if you have avahi])
    AC_SUBST(avahi_CFLAGS)
    AC_SUBST(avahi_LIBS)
  elif test "x$have_mdns" = "xyes"; then
    AC_DEFINE(HAVE_WIFI_MDNS, 1, [Define if you have mDNS])
  else
    AC_MSG_ERROR([wifi support requested but no wifi backend could not be found])
  fi
else
  echo "*** Note: wifi support has been disabled ***"
fi

AC_ARG_WITH([udevrulesdir],
            AS_HELP_STRING([--with-udevrulesdir=DIR],
            [Directory for udev rules]),
            [],
            [with_udevrulesdir=auto])
if test "x$with_udevrulesdir" = "xauto"; then
  udevdir=$($PKG_CONFIG --variable=udevdir udev)
  if test "x$udevdir" != "x"; then
    with_udevrulesdir=$udevdir"/rules.d"
  else
    with_udevrulesdir=$prefix/lib/udev/rules.d
  fi
fi

AC_ARG_WITH([systemd],
            [AS_HELP_STRING([--without-systemd],
            [do not build with systemd support @<:@default=yes@:>@])],
            [],
            [with_systemd=yes])

AC_ARG_WITH([systemdsystemunitdir],
            [AS_HELP_STRING([--with-systemdsystemunitdir=DIR],
            [directory for systemd service files])],
            [with_systemdsystemunitdir=$withval],
            [with_systemdsystemunitdir=$($PKG_CONFIG --variable=systemdsystemunitdir systemd)])

if test "x$with_systemdsystemunitdir" != xno; then
  AC_SUBST([systemdsystemunitdir], [$with_systemdsystemunitdir])
fi
AM_CONDITIONAL(WANT_SYSTEMD, [test -n "$with_systemdsystemunitdir" -a "x$with_systemdsystemunitdir" != xno -a "x$with_systemd" = "xyes" ])

if test "x$with_systemd" = xyes; then
  udev_activation_rule="ENV{SYSTEMD_WANTS}=\"usbmuxd.service\""
else
  udev_activation_rule="RUN+=\"@sbindir@/usbmuxd --user usbmux --udev\""
fi
AC_SUBST(udev_activation_rule)


# Checks for libraries.
LT_INIT

# Check for operating system
UDEV_SUB=
SYSTEMD_SUB=
case ${host_os} in
  darwin*)
    # No support for launchd
    activation_method="manual"
    ;;
  *)
    UDEV_SUB=udev
    AC_SUBST([udevrulesdir], [$with_udevrulesdir])
    AC_DEFINE(HAVE_UDEV, 1, [Define to enable udev support])
    activation_method="systemd"
    if test "x$with_systemd" != "xyes"; then
      echo "*** Note: support for systemd activation has been disabled, using udev activation instead ***"
      activation_method="udev"
    else
      AC_DEFINE(WANT_SYSTEMD, 1, [Define to enable systemd support])
      SYSTEMD_SUB=systemd
    fi
    ;;
esac

# Check if struct sockaddr has sa_len member
AC_CHECK_MEMBER([struct sockaddr.sa_len],[
  AC_DEFINE([HAVE_STRUCT_SOCKADDR_SIN__LEN], 1, [Define to 1 if struct sockaddr.sin_len member exists])
],[],[#include <netinet/in.h>])

AC_SUBST([UDEV_SUB])
AC_SUBST([SYSTEMD_SUB])

AC_CONFIG_FILES([Makefile
                 udev/Makefile
                 systemd/Makefile
                 usbmuxd2/Makefile])
AC_OUTPUT

echo "
Configuration for $PACKAGE-$VERSION:
-------------------------------------------

  install prefix ..........: $prefix
  Debug build .............: $debug_build
  preflight support .......: $with_limd
  WIFI support ............: $with_wifi
  activation method .......: $activation_method"

if test "x$with_wifi" = "xyes"; then
  if test "x$have_avahi" = "xyes"; then
    echo "  WIFI backend ............: avahi"
  elif test "x$have_mdns" = "xyes"; then
    echo "  WIFI backend ............: mDNS"
  fi
fi

if test "x$activation_method" = "xsystemd"; then
  echo "  systemd unit directory ..: ${systemdsystemunitdir}"
fi

if test -n "$udevrulesdir"; then
  echo "  udev rules directory ....: ${udevrulesdir}"
fi

echo "  compiler ................: ${CC}

  Now type 'make' to build $PACKAGE-$VERSION,
  and then 'make install' for installation.
"