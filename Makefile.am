plugin_LTLIBRARIES = libgstshvideodec.la libgstshvideoenc.la

EXTRA_DIST = \
	depcomp autogen.sh 

ACLOCAL_AMFLAGS = -I common/m4

libgstshvideodec_la_SOURCES = gstshvideodec.c
libgstshvideoenc_la_SOURCES = gstshvideoenc.c cntlfile/ControlFileUtil.c

libgstshvideodec_la_CFLAGS = $(GST_CFLAGS) $(GST_PLUGINS_BASE_CFLAGS) \
	$(LIBSHCODECS_CFLAGS)
libgstshvideodec_la_LIBADD = $(GST_BASE_LIBS) $(GST_PLUGINS_BASE_LIBS) \
        $(LIBSHCODECS_LIBS)
libgstshvideodec_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS) -O2 -lrt \
	-lgstvideo-0.10 -lz -lstdc++ -lgstinterfaces-0.10 -lshcodecs
libgstshvideodec_la_LIBTOOLFLAGS = --tag=disable-static

libgstshvideoenc_la_CFLAGS = $(GST_CFLAGS) $(GST_PLUGINS_BASE_CFLAGS) \
	$(LIBSHCODECS_CFLAGS)
libgstshvideoenc_la_LIBADD = $(GST_BASE_LIBS) $(GST_PLUGINS_BASE_LIBS) \
        $(LIBSHCODECS_LIBS)
libgstshvideoenc_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS) -O2 -lrt \
	-lgstvideo-0.10 -lz -lstdc++ -lgstinterfaces-0.10 -lshcodecs
libgstshvideoenc_la_LIBTOOLFLAGS = --tag=disable-static

noinst_HEADERS = 

check-valgrind:
	@true

check-torture:
	@true
