INCLUDES=							\
	-DEVINCEDATADIR=\"$(pkgdatadir)\"				\
	-DGNOMEDATADIR=\"$(datadir)\"				\
	-I$(top_srcdir)						\
	-I$(top_builddir)					\
	-I$(top_srcdir)/cut-n-paste/zoom-control/		\
	-I$(top_srcdir)/cut-n-paste/toolbar-editor/		\
	-I$(top_srcdir)/cut-n-paste/totem-screensaver/		\
	-I$(top_srcdir)/cut-n-paste/gimpcellrenderertoggle/	\
	-I$(top_srcdir)/cut-n-paste/smclient/			\
	-I$(top_srcdir)/libdocument				\
	-I$(top_builddir)/libdocument				\
	-I$(top_srcdir)/libview					\
	-I$(top_builddir)/libview				\
	-I$(top_srcdir)/libmisc					\
	-I$(top_srcdir)/properties				\
	-DGNOMEICONDIR=\""$(datadir)/pixmaps"\" 		\
	-DBINDIR=\""$(bindir)"\"				\
	-DLIBEXECDIR=\""$(libexecdir)"\"			\
	-DEVINCE_COMPILATION					\
	$(SHELL_CFLAGS)						\
	$(WARN_CFLAGS)						\
	$(DISABLE_DEPRECATED)

bin_PROGRAMS=evince

libexec_PROGRAMS=evince-convert-metadata
if ENABLE_DBUS
libexec_PROGRAMS += evinced
endif

evince_SOURCES=				\
	eggfindbar.c			\
	eggfindbar.h			\
	ev-annotation-properties-dialog.h \
	ev-annotation-properties-dialog.c \
	ev-application.c		\
	ev-application.h		\
	ev-file-monitor.h		\
	ev-file-monitor.c		\
	ev-history.c			\
	ev-history.h			\
	ev-keyring.h			\
	ev-keyring.c			\
	ev-marshal.c			\
	ev-marshal.h			\
	ev-message-area.c		\
	ev-message-area.h		\
	ev-metadata.c			\
	ev-metadata.h			\
	ev-navigation-action.c		\
	ev-navigation-action.h		\
	ev-navigation-action-widget.c	\
	ev-navigation-action-widget.h	\
	ev-password-view.h		\
	ev-password-view.c		\
	ev-progress-message-area.h	\
	ev-progress-message-area.c	\
	ev-properties-dialog.c		\
	ev-properties-dialog.h		\
	ev-properties-fonts.c		\
	ev-properties-fonts.h		\
	ev-properties-license.c		\
	ev-properties-license.h		\
	ev-open-recent-action.c		\
	ev-open-recent-action.h		\
	ev-utils.c			\
	ev-utils.h			\
	ev-window.c			\
	ev-window.h			\
	ev-window-title.c		\
	ev-window-title.h		\
	ev-sidebar.c			\
	ev-sidebar.h			\
	ev-sidebar-annotations.c	\
	ev-sidebar-annotations.h	\
	ev-sidebar-attachments.c	\
	ev-sidebar-attachments.h	\
	ev-sidebar-layers.c		\
	ev-sidebar-layers.h		\
	ev-sidebar-links.c		\
	ev-sidebar-links.h		\
	ev-sidebar-page.c		\
	ev-sidebar-page.h		\
	ev-sidebar-thumbnails.c		\
	ev-sidebar-thumbnails.h		\
	main.c

if ENABLE_DBUS
evince_SOURCES += \
	ev-media-player-keys.c \
	ev-media-player-keys.h
endif


evince_LDFLAGS = $(AM_LDFLAGS)

if PLATFORM_WIN32
evince_LDFLAGS += -mwindows
endif

evince_LDADD=										\
	$(top_builddir)/cut-n-paste/zoom-control/libephyzoom.la				\
	$(top_builddir)/cut-n-paste/toolbar-editor/libtoolbareditor.la			\
	$(top_builddir)/cut-n-paste/totem-screensaver/libtotemscrsaver.la		\
	$(top_builddir)/cut-n-paste/gimpcellrenderertoggle/libgimpcellrenderertoggle.la	\
	$(top_builddir)/cut-n-paste/smclient/libsmclient.la				\
	$(top_builddir)/properties/libevproperties.la					\
	$(top_builddir)/libdocument/libevdocument.la					\
	$(top_builddir)/libview/libevview.la						\
	$(top_builddir)/libmisc/libevmisc.la						\
	$(SHELL_LIBS)

if PLATFORM_WIN32
evince_LDADD += evince-icon.o

evince-icon.o: $(srcdir)/evince-icon.rc $(top_srcdir)/data/evince.ico
	$(WINDRES) $(srcdir)/evince-icon.rc -I $(top_srcdir)/data evince-icon.o
endif

BUILT_SOURCES = ev-marshal.h ev-marshal.c

evince_convert_metadata_SOURCES=	\
	ev-convert-metadata.c

evince_convert_metadata_LDADD=		\
	$(SHELL_LIBS)

if ENABLE_DBUS
evinced_SOURCES=			\
	ev-daemon.c

evinced_CFLAGS=				\
	-DEVINCEDATADIR=\"$(pkgdatadir)\"			\
	-DGNOMEDATADIR=\"$(datadir)\"				\
	-I$(top_srcdir)						\
	-I$(top_builddir)					\
	-DLIBEXECDIR=\""$(libexecdir)"\"			\
	-DEVINCE_COMPILATION					\
	$(EV_DAEMON_CFLAGS)					\
	$(WARN_CFLAGS)						\
	$(DISABLE_DEPRECATED)

evinced_LDADD=				\
	$(EV_DAEMON_LIBS)
endif

EXTRA_DIST = ev-marshal.list		\
	evince-icon.rc

ev-marshal.h: $(srcdir)/ev-marshal.list
	$(AM_V_GEN)$(GLIB_GENMARSHAL) --prefix=ev_marshal $(srcdir)/ev-marshal.list --header > ev-marshal.h

ev-marshal.c: $(srcdir)/ev-marshal.list
	echo '#include "ev-marshal.h"' > ev-marshal.c
	$(AM_V_GEN)$(GLIB_GENMARSHAL) --prefix=ev_marshal $(srcdir)/ev-marshal.list --body >> ev-marshal.c

DISTCLEANFILES =

-include $(top_srcdir)/git.mk
