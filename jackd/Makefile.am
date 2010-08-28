MAINTAINERCLEANFILES = Makefile.in jackd.1 jack_md5.h 
MOSTLYCLEANFILES =

if USE_CAPABILITIES
CAP_PROGS = jackstart
CAP_LIBS = -lcap
install-exec-hook:
	if [ "`id -u`" = "0" ]; \
	then	chown root.root $(DESTDIR)$(bindir)/jackd; \
			chmod 555 $(DESTDIR)$(bindir)/jackd; \
			chown root.root $(DESTDIR)$(bindir)/jackstart; \
			chmod 4555 $(DESTDIR)$(bindir)/jackstart; \
	else    echo "You must be root to set ownership and permissions"; \
			echo " for jackd and jackstart."; \
	fi
else
CAP_PROGS =
CAP_LIBS =
install-exec-hook:
	@echo "Nothing to make for $@."
endif

bin_PROGRAMS = jackd $(CAP_PROGS)

if HAVE_DBUS
bin_PROGRAMS += jackdbus
endif

AM_CFLAGS = $(JACK_CFLAGS) -DJACK_LOCATION=\"$(bindir)\" $(DBUS_CFLAGS) $(LIBXML2_CFLAGS)

COMMON_SOURCES = engine.c clientengine.c transengine.c
COMMON_LIBS = libjackserver.la $(CAP_LIBS) @OS_LDFLAGS@

jackd_SOURCES = jackd.c $(COMMON_SOURCES)
jackd_LDADD = $(COMMON_LIBS)

noinst_HEADERS = jack_md5.h md5.h md5_loc.h \
		 clientengine.h transengine.h

BUILT_SOURCES = jack_md5.h

jack_md5.h: jackd
if STRIPPED_JACKD
	strip -R .note -R .comment .libs/jackd
endif
	echo "#define JACKD_MD5_SUM \"`md5sum .libs/jackd | awk '{print $$1}'`\"" > jack_md5.h

jackstart_SOURCES = jackstart.c md5.c
jackstart_LDFLAGS = -lcap

lib_LTLIBRARIES	= libjackserver.la

libjackserver_la_CFLAGS = $(AM_CFLAGS) -DLIBJACKSERVER

libjackserver_la_SOURCES = engine.c clientengine.c transengine.c \
	../libjack/systemtest.c ../libjack/sanitycheck.c \
	../libjack/client.c ../libjack/driver.c ../libjack/intclient.c \
        ../libjack/messagebuffer.c ../libjack/pool.c ../libjack/port.c \
        ../libjack/midiport.c ../libjack/ringbuffer.c ../libjack/shm.c \
        ../libjack/thread.c ../libjack/time.c  ../libjack/transclient.c \
        ../libjack/unlock.c
libjackserver_la_LIBADD  = simd.lo @OS_LDFLAGS@
libjackserver_la_LDFLAGS  = -export-dynamic -version-info @JACK_SO_VERSION@

simd.lo: $(srcdir)/../libjack/simd.c
	$(LIBTOOL) --mode=compile $(CC) -I$(top_builddir) $(JACK_CORE_CFLAGS) $(SIMD_CFLAGS) -c -o simd.lo $(srcdir)/../libjack/simd.c

man_MANS = jackd.1 jackstart.1
EXTRA_DIST = $(man_MANS)

if HAVE_DBUS
noinst_HEADERS += jackdbus.h jackctl.h jackcontroller.h jackcontroller_xml.h jackcontroller_internal.h
jackdbus_SOURCES = jackdbus.c jackctl.c $(COMMON_SOURCES) jackcontroller.c
jackdbus_SOURCES += jackcontroller_iface_introspectable.c
jackdbus_SOURCES += jackcontroller_iface_control.c
jackdbus_SOURCES += jackcontroller_iface_configure.c
jackdbus_SOURCES += jackcontroller_iface_patchbay.c
jackdbus_SOURCES += jackcontroller_iface_transport.c
jackdbus_LDADD = $(COMMON_LIBS) $(DBUS_LIBS)

if SETTINGS_PERSISTENCE_USE_LIBXML2
jackdbus_SOURCES += jackcontroller_xml_libxml.c jackcontroller_xml.c
jackdbus_LDADD += $(LIBXML2_LIBS)
endif

if SETTINGS_PERSISTENCE_USE_EXPAT
jackdbus_SOURCES += jackcontroller_xml_expat.c jackcontroller_xml.c jackcontroller_xml_write_raw.c
jackdbus_LDADD += -lexpat
endif

if SETTINGS_PERSISTENCE_DISABLED
jackdbus_SOURCES += jackcontroller_xml_nop.c
endif

# Dbus service file
servicedir = $(DBUS_SERVICES_DIR)
service_in_files = org.jackaudio.service.in
service_DATA = $(service_in_files:.service.in=.service)

# Rule to make the service file with bindir expanded
$(service_DATA): $(service_in_files) Makefile
	@sed -e "s|\@bindir\@|$(bindir)|" $< > $@

EXTRA_DIST += $(service_in_files)
MOSTLYCLEANFILES += $(service_DATA)
endif

dist-hook:
	rm -f $(distdir)/jack_md5.h
distclean-local:
	rm -f $(top_builddir)/jackd/jack_md5.h
