AM_CFLAGS = -I$(top_srcdir)/usbmuxd2 -I$(top_srcdir)/external/libgeneral/include $(GLOBAL_CFLAGS) $(libplist_CFLAGS) $(libusb_CFLAGS) $(libimobildevice_CFLAGS)
AM_CXXFLAGS = $(AM_CFLAGS) $(GLOBAL_CXXFLAGS) $(libplist_CXXFLAGS) $(avahi_CXXFLAGS)
AM_LDFLAGS = $(libplist_LIBS) $(libplistxx_LIBS) $(libusb_LIBS) $(libimobiledevice_LIBS) $(avahi_LIBS) $(libpthread_LIBS)


sbin_PROGRAMS = usbmuxd

usbmuxd_CFLAGS = $(AM_CFLAGS)
usbmuxd_CXXFLAGS = $(AM_CXXFLAGS)
usbmuxd_LDFLAGS = $(AM_LDFLAGS)
usbmuxd_SOURCES = 	exception.cpp \
			log.c \
			Client.cpp \
			Muxer.cpp \
			TCP.cpp \
			SockConn.cpp \
			Device.cpp \
			Devices/USBDevice.cpp \
			Devices/WIFIDevice.cpp \
			Event.cpp \
			Manager/Manager.cpp \
			Manager/DeviceManager/DeviceManager.cpp \
			Manager/DeviceManager/USBDeviceManager.cpp \
			Manager/DeviceManager/WIFIDeviceManager-avahi.cpp \
			Manager/ClientManager.cpp \
			sysconf/sysconf.cpp \
			sysconf/preflight.cpp \
			main.cpp
