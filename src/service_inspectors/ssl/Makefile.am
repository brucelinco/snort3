
file_list = \
ssl_config.h \
ssl_inspector.cc \
ssl_inspector.h \
ssl_module.cc \
ssl_module.h \
ips_ssl_state.cc \
ips_ssl_version.cc

# can't be linked dynamically yet
#if STATIC_INSPECTORS
noinst_LIBRARIES = libssl.a
libssl_a_SOURCES = $(file_list)
#else
#shlibdir = $(pkglibdir)/inspectors
#shlib_LTLIBRARIES = libssl.la
#libssl_la_CXXFLAGS = $(AM_CXXFLAGS) -DBUILDING_SO
#libssl_la_LDFLAGS = $(AM_LDFLAGS) -module -export-dynamic -avoid-version -shared
#libssl_la_SOURCES = $(file_list)
#endif

