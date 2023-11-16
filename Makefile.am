AUTOMAKE_OPTIONS = foreign
ACLOCAL_AMFLAGS = -I m4
SUBDIRS=usbmuxd2 $(UDEV_SUB) $(SYSTEMD_SUB)

EXTRA_DIST = LICENSE

DISTCHECK_CONFIGURE_FLAGS =				\
	--with-udevrulesdir=$$dc_install_base/$(udevrulesdir) \
	--with-systemdsystemunitdir=$$dc_install_base/$(systemdsystemunitdir)