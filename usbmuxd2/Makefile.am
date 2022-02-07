
if DEBUG
AM_CFLAGS = -g3 -O0
AM_CXXFLAGS = -g3 -O0
else
AM_CFLAGS = -O2
AM_CXXFLAGS = -O2
endif

AM_CFLAGS = -I$(top_srcdir)/usbmuxd2 $(GLOBAL_CFLAGS) $(libplist_CFLAGS) $(libusb_CFLAGS) $(libimobildevice_CFLAGS) $(libgeneral_CFLAGS)
AM_CXXFLAGS = $(GLOBAL_CXXFLAGS) $(libplist_CXXFLAGS) $(avahi_CXXFLAGS)
AM_LDFLAGS = $(libplist_LIBS) $(libusb_LIBS) $(libimobiledevice_LIBS) $(avahi_LIBS) $(libpthread_LIBS) $(libgeneral_LIBS)


sbin_PROGRAMS = usbmuxd

usbmuxd_CFLAGS = $(AM_CFLAGS)
usbmuxd_CXXFLAGS = $(AM_CXXFLAGS) $(AM_CFLAGS)
usbmuxd_LDFLAGS = $(AM_LDFLAGS)
usbmuxd_SOURCES = main.cpp \
			log.c \
			Client.cpp \
			Muxer.cpp \
			TCP.cpp \
			Device.cpp \
			SockConn.cpp \
			sysconf/sysconf.cpp \
			sysconf/preflight.cpp \
			Devices/USBDevice.cpp \
			Devices/WIFIDevice.cpp \
			Manager/USBDeviceManager.cpp \
			Manager/WIFIDeviceManager.cpp \
			Manager/WIFIDeviceManager-avahi.cpp \
			Manager/WIFIDeviceManager-mDNS.cpp \
			Manager/ClientManager.cpp \
			Manager/DeviceManager.cpp
