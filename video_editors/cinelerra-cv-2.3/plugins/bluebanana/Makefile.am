plugin_LTLIBRARIES = bluebanana.la
bluebanana_la_LDFLAGS = -avoid-version -module -shared 
bluebanana_la_LIBADD = $(top_builddir)/plugins/colors/libcolors.la
bluebanana_la_SOURCES = bluebanana.C bluebananaconfig.C bluebananaslider.C bluebananawindow.C \
			bluebananalookups.C bluebananaengine.C
AM_CXXFLAGS = $(LARGEFILE_CFLAGS)

AM_CPPFLAGS = -I$(top_srcdir)/guicast -I$(top_srcdir)/cinelerra -I$(top_srcdir)/quicktime -I$(top_srcdir)/plugins/colors
LIBTOOL = $(SHELL) $(top_builddir)/libtool $(LTCXX_FLAGS)

noinst_HEADERS = \
	bluebanana.h bluebananaconfig.h bluebananaslider.h \
	bluebananacolor.c \
	bluebananawindow.h
EXTRA_DIST = picon.png

BUILT_SOURCES = picon_png.h
picon_png.h: picon.png
	$(top_builddir)/guicast/pngtoh $^ || { rm -f $@; exit 1; }
clean-local:
	$(RM) picon_png.h
