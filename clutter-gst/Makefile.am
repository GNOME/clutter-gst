include $(top_srcdir)/build/autotools/Makefile.am.silent

NULL = #

SUBDIRS =

CLEANFILES = #
DISTCLEANFILES = #
EXTRA_DIST = #
BUILT_SOURCES = #

# glib-mkenums
glib_enum_h = clutter-gst-enum-types.h
glib_enum_c = clutter-gst-enum-types.c
glib_enum_headers = $(source_h)
include $(top_srcdir)/build/autotools/Makefile.am.enums

# glib-genmarshal
glib_marshal_list = clutter-gst-marshal.list
glib_marshal_prefix = _clutter_gst_marshal
include $(top_srcdir)/build/autotools/Makefile.am.marshal

source_h = 				\
	clutter-gst-types.h		\
	clutter-gst-util.h		\
	clutter-gst-camera.h		\
	clutter-gst-camera-manager.h	\
	clutter-gst-camera-device.h	\
	clutter-gst-playback.h		\
	clutter-gst-player.h		\
	clutter-gst-aspectratio.h	\
	clutter-gst-crop.h		\
	clutter-gst-content.h		\
	clutter-gst-video-sink.h	\
	$(NULL)

source_priv_h =				\
	clutter-gst-debug.h		\
	clutter-gst-private.h		\
	$(NULL)

source_c = 				\
	clutter-gst-debug.c		\
	clutter-gst-types.c		\
	clutter-gst-player.c		\
	clutter-gst-camera.c		\
	clutter-gst-camera-manager.c	\
	clutter-gst-camera-device.c	\
	clutter-gst-playback.c		\
	clutter-gst-util.c		\
	clutter-gst-aspectratio.c	\
	clutter-gst-crop.c		\
	clutter-gst-content.c		\
	clutter-gst-video-sink.c	\
	$(NULL)

libclutter_gst_@CLUTTER_GST_MAJORMINOR@_la_SOURCES =	\
	$(BUILT_SOURCES)				\
	$(source_c)      				\
	$(source_priv_h) 				\
	$(source_h)					\
	$(NULL)

AM_CPPFLAGS =					\
	-DCLUTTER_GST_COMPILATION		\
	-DG_DISABLE_SINGLE_INCLUDES		\
	-DCOGL_ENABLE_EXPERIMENTAL_API		\
	-DCLUTTER_ENABLE_EXPERIMENTAL_API	\
	-DG_LOG_DOMAIN=\"Clutter-Gst\"		\
	-DCLUTTER_DISABLE_DEPRECATION_WARNINGS	\
	-I$(top_srcdir)				\
	-I$(top_builddir)			\
	$(NULL)

AM_CFLAGS = 				\
	$(MAINTAINER_CFLAGS)		\
	$(CLUTTER_GST_DEBUG_CFLAGS)	\
	$(CLUTTER_GST_CFLAGS)		\
	$(GST_CFLAGS)			\
	$(GUDEV_CFLAGS)			\
	$(HW_CFLAGS)			\
	$(NULL)

lib_LTLIBRARIES = libclutter-gst-@CLUTTER_GST_MAJORMINOR@.la

libclutter_gst_@CLUTTER_GST_MAJORMINOR@_la_LIBADD  = @CLUTTER_GST_LIBS@ $(GST_LIBS) $(GUDEV_LIBS) $(HW_LIBS) $(LIBM)
libclutter_gst_@CLUTTER_GST_MAJORMINOR@_la_LDFLAGS =	\
	$(CLUTTER_GST_LT_LDFLAGS)			\
	$(GL_LDFLAGS)					\
	-export-dynamic					\
	-export-symbols-regex "^clutter_gst.*"		\
	-rpath $(libdir)
	$(NULL)

cluttergstheadersdir = $(includedir)/clutter-gst-@CLUTTER_GST_API_VERSION@/clutter-gst
cluttergstheaders_HEADERS = 	\
	$(source_h) 		\
	clutter-gst.h		\
	$(NULL)
nodist_cluttergstheaders_HEADERS = $(glib_enum_h) clutter-gst-version.h

#
# GStreamer plugin
#

plugin_source_c = 			\
	clutter-gst-plugin.c		\
	clutter-gst-auto-video-sink.c	\
	clutter-gst-auto-video-sink.h	\
	$(NULL)

libcluttergst@CLUTTER_GST_MAJOR_VERSION@_la_SOURCES = \
	$(plugin_source_c) \
	$(NULL)

plugin_LTLIBRARIES = libcluttergst@CLUTTER_GST_MAJOR_VERSION@.la

libcluttergst@CLUTTER_GST_MAJOR_VERSION@_la_LIBADD = \
	$(PLUGIN_LIBS) \
	$(HW_LIBS) \
	$(LIBM) \
	libclutter-gst-@CLUTTER_GST_MAJORMINOR@.la
libcluttergst@CLUTTER_GST_MAJOR_VERSION@_la_LDFLAGS = \
	$(GL_LDFLAGS) \
	-module -avoid-version \
	-no-undefined \
	$(NULL)

#
# Introspection data
#

if HAVE_INTROSPECTION
BUILT_GIRSOURCES =

ClutterGst-@CLUTTER_GST_API_VERSION@.gir: $(INTROSPECTION_SCANNER) libclutter-gst-@CLUTTER_GST_API_VERSION@.la
	$(QUIET_GEN) \
		GST_PLUGIN_SYSTEM_PATH_1_0="" GST_PLUGIN_PATH_1_0="" GST_REGISTRY_UPDATE=no DISPLAY="" \
		CPPFLAGS="$(CPPFLAGS)" CFLAGS="$(CFLAGS)" LDFLAGS="$(LDFLAGS)" CC="$(CC)" PKG_CONFIG="$(PKG_CONFIG)" DLLTOOL="$(DLLTOOL)" \
		$(INTROSPECTION_SCANNER) -v \
		--namespace ClutterGst --nsversion=@CLUTTER_GST_API_VERSION@ \
		--quiet --warn-all \
		--add-include-path=$(top_srcdir)/clutter-gst \
		--add-include=path=$(top_builddir)/clutter-gst \
		--c-include="clutter-gst/clutter-gst.h" \
		--include=GObject-2.0 \
		--include=Cogl-1.0 \
		--include=Clutter-1.0 \
		--include=GdkPixbuf-2.0 \
		--add-include-path=`$(PKG_CONFIG) --variable=girdir gstreamer-1.0` \
		--include=Gst-1.0 \
		--add-include-path=`$(PKG_CONFIG) --variable=girdir gstreamer-base-1.0` \
		--include=GstBase-1.0 \
		--add-include-path=`$(PKG_CONFIG) --variable=girdir gstreamer-video-1.0` \
		--include=GstVideo-1.0 \
		--add-include-path=`$(PKG_CONFIG) --variable=girdir gstreamer-audio-1.0` \
		--include=GstAudio-1.0 \
		--add-include-path=`$(PKG_CONFIG) --variable=girdir gstreamer-pbutils-1.0` \
		--include=GstPbutils-1.0 \
		--add-init-section="extern void gst_init (int *argc, char *argv[]); gst_init(0, NULL);" \
		--library=libclutter-gst-@CLUTTER_GST_API_VERSION@.la \
		--libtool="${LIBTOOL}" \
		--output $@ \
		--pkg gobject-2.0 \
		--pkg cogl-2.0-experimental \
		--pkg clutter-1.0 \
		--pkg gdk-pixbuf-2.0 \
		--pkg gstreamer-1.0 \
		--pkg gstreamer-base-1.0 \
		--pkg-export clutter-gst-@CLUTTER_GST_API_VERSION@ \
		--cflags-begin \
		$(AM_CPPFLAGS) \
		$(CLUTTER_GST_CFLAGS) \
		$(GST_CFLAGS) \
		$(GUDEV_CFLAGS)	\
		$(HW_CFLAGS) \
		-DGST_USE_UNSTABLE_API \
		--cflags-end \
		$(addprefix $(srcdir)/, $(source_h)) \
		$(addprefix $(srcdir)/, $(source_c)) \
		$(nodist_cluttergstheaders_HEADERS)

BUILT_GIRSOURCES += ClutterGst-@CLUTTER_GST_API_VERSION@.gir

# INTROSPECTION_GIRDIR/INTROSPECTION_TYPELIBDIR aren't the right place to
# install anything - we need to install inside our prefix.
girdir = $(datadir)/gir-1.0
nodist_gir_DATA = $(BUILT_GIRSOURCES)

typelibsdir = $(libdir)/girepository-1.0/
nodist_typelibs_DATA = $(BUILT_GIRSOURCES:.gir=.typelib)

%.typelib: %.gir $(INTROSPECTION_COMPILER)
	$(QUIET_GEN)$(INTROSPECTION_COMPILER) \
		--includedir=$(srcdir) \
		--includedir=. \
		--includedir=`$(PKG_CONFIG) --variable=girdir gstreamer-1.0` \
		--includedir=`$(PKG_CONFIG) --variable=girdir gstreamer-base-1.0` \
		--includedir=`$(PKG_CONFIG) --variable=girdir gstreamer-video-1.0` \
		--includedir=`$(PKG_CONFIG) --variable=girdir gstreamer-audio-1.0` \
		--includedir=`$(PKG_CONFIG) --variable=girdir gstreamer-pbutils-1.0` \
		$(INTROSPECTION_COMPILER_OPTS) $< -o $(builddir)/$(@F)

CLEANFILES += $(BUILT_GIRSOURCES) $(nodist_typelibs_DATA)
endif
