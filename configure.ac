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

LIBGENERAL_MINVERS_STR="72"
LIBUSB_MINVERS_STR="1.0.9"
LIBPLIST_MINVERS_STR="2.2.0"
AVAHI_MINVERS_STR="0.7"
LIBIMOBILEDEVICE_MINVERS_STR="1.3.0"

LIBGENERAL_REQUIRES_STR="libgeneral >= $LIBGENERAL_MINVERS_STR"
LIBUSB_REQUIRES_STR="libusb-1.0 >= $LIBUSB_MINVERS_STR"
LIBPLIST_REQUIRES_STR="libplist-2.0 >= $LIBPLIST_MINVERS_STR"
AVAHI_REQUIRES_STR="avahi-client >= $AVAHI_MINVERS_STR"
LIBIMOBILEDEVICE_REQUIRES_STR="libimobiledevice-1.0 >= $LIBIMOBILEDEVICE_MINVERS_STR"

PKG_CHECK_MODULES(libgeneral, $LIBGENERAL_REQUIRES_STR)
PKG_CHECK_MODULES(libusb, $LIBUSB_REQUIRES_STR)
PKG_CHECK_MODULES(libplist, $LIBPLIST_REQUIRES_STR)
PKG_CHECK_MODULES(avahi, $AVAHI_REQUIRES_STR, have_avahi=yes, have_avahi=no)
PKG_CHECK_MODULES(libimobiledevice, $LIBIMOBILEDEVICE_REQUIRES_STR, have_limd=yes, have_limd=no)

#Debian Dependencies
LIBGENERAL_DEBIAN_DEP_STR="libgeneral0 (>= 0.$LIBGENERAL_MINVERS_STR),"
LIBUSB_DEBIAN_DEP_STR="libusb-1.0-0 (>= 2:$LIBUSB_MINVERS_STR),"
LIBPLIST_DEBIAN_DEP_STR="libplist4 (>= $LIBPLIST_MINVERS_STR),"
AVAHI_DEBIAN_DEP_STR="avahi-client (>= $AVAHI_MINVERS_STR),"
LIBIMOBILEDEVICE_DEBIAN_DEP_STR="libimobiledevice-1.0 (>= $LIBIMOBILEDEVICE_MINVERS_STR)"

AC_SUBST([libgeneral_debian_dep], [$LIBGENERAL_DEBIAN_DEP_STR])
AC_SUBST([libusb_debian_dep], [$LIBUSB_DEBIAN_DEP_STR])
AC_SUBST([libplist_debian_dep], [$LIBPLIST_DEBIAN_DEP_STR])

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
    LIBIMOBILEDEVICE_DEBIAN_DEP_STR=""
    echo "*** Note: libimobiledevice support has been disabled ***"
  fi
else
  LIBIMOBILEDEVICE_DEBIAN_DEP_STR=""
fi
AC_SUBST([libimobiledevice_debian_dep], [$LIBIMOBILEDEVICE_DEBIAN_DEP_STR])

if test "x$with_wifi" == "xyes"; then
  if test "x$have_avahi" = "xyes"; then
    AC_DEFINE(HAVE_WIFI_AVAHI, 1, [Define if you have avahi])
    AC_SUBST(avahi_CFLAGS)
    AC_SUBST(avahi_LIBS)
  elif test "x$have_mdns" = "xyes"; then
    AVAHI_DEBIAN_DEP_STR=""
    AC_DEFINE(HAVE_WIFI_MDNS, 1, [Define if you have mDNS])
  else
    AC_MSG_ERROR([wifi support requested but no wifi backend could not be found])
  fi
else
  AVAHI_DEBIAN_DEP_STR=""
  echo "*** Note: wifi support has been disabled ***"
fi
AC_SUBST([avahi_debian_dep], [$AVAHI_DEBIAN_DEP_STR])

AC_ARG_WITH([udevrulesdir],
            AS_HELP_STRING([--with-udevrulesdir=DIR],
            [Directory for udev rules]),
            [with_udevrulesdir=$withval],
            [with_udevrulesdir=auto])
if test "x$with_udevrulesdir" = "xauto"; then
  udevdir=$($PKG_CONFIG --variable=udevdir udev)
  if test "x$udevdir" != "x"; then
    with_udevrulesdir=$udevdir"/rules.d"
  else
    if test "x$prefix" != "xNONE"; then
      with_udevrulesdir=$prefix/lib/udev/rules.d
    else
      with_udevrulesdir=/lib/udev/rules.d
    fi
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
            [with_systemdsystemunitdir=auto])

if test "x$with_systemd" != xno; then
  if test "x$with_systemdsystemunitdir" = "xauto"; then
    systemdsystemunitdir=$($PKG_CONFIG --variable=systemdsystemunitdir systemd)
    if test "x$systemdsystemunitdir" != "x"; then
      with_systemdsystemunitdir="${systemdsystemunitdir}"
    else
      with_systemdsystemunitdir="/lib/systemd/system"
    fi
  fi
  AC_SUBST([systemdsystemunitdir], [$with_systemdsystemunitdir])
fi

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

AM_CONDITIONAL(WANT_SYSTEMD, [test "x$activation_method" == "xsystemd" ])


# Check if struct sockaddr has sa_len member
AC_CHECK_MEMBER([struct sockaddr.sa_len],[
  AC_DEFINE([HAVE_STRUCT_SOCKADDR_SIN__LEN], 1, [Define to 1 if struct sockaddr.sin_len member exists])
],[],[#include <netinet/in.h>])

AC_SUBST([UDEV_SUB])
AC_SUBST([SYSTEMD_SUB])

AC_SUBST([DEB_VERSION], [0.${VERSION_COMMIT_COUNT}-git-${VERSION_COMMIT_SHA}])

if test "x${host_cpu}" == "xx86_64"; then
  AC_SUBST([DEB_ARCH], [amd64])
fi

AC_CONFIG_FILES([Makefile
                 udev/Makefile
                 systemd/Makefile
                 usbmuxd2/Makefile
                 debian/usbmuxd2.control])
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