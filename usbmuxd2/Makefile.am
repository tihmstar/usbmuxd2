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
			SockConn.cpp \
			Device.cpp \
			Event.cpp \
			Devices/USBDevice.cpp \
			Devices/WIFIDevice.cpp \
			Manager/Manager.cpp \
			Manager/DeviceManager/DeviceManager.cpp \
			Manager/DeviceManager/USBDeviceManager.cpp \
			Manager/DeviceManager/WIFIDeviceManager-avahi.cpp \
			Manager/DeviceManager/WIFIDeviceManager-mDNS.cpp \
			Manager/ClientManager.cpp \
			sysconf/sysconf.cpp \
			sysconf/preflight.cpp
