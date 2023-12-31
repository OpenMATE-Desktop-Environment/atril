/* this file is part of evince, a gnome document viewer
 *
 *  Copyright (C) 2004 Martin Kretzschmar
 *  Copyright © 2010 Christian Persch
 *
 *  Author:
 *    Martin Kretzschmar <martink@gnome.org>
 *
 * Evince is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * Evince is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 */


#include <config.h>
#include <stdlib.h>
#include <string.h>

#include <glib.h>
#include <glib/gi18n.h>
#include <glib/gstdio.h>
#include <gtk/gtk.h>
#ifdef GDK_WINDOWING_X11
#include <gdk/gdkx.h>
#endif
#include <unistd.h>

#include "totem-scrsaver.h"

#ifdef WITH_SMCLIENT
#include "eggsmclient.h"
#endif

#include "ev-application.h"
#include "ev-file-helpers.h"
#include "ev-stock-icons.h"

#ifdef ENABLE_DBUS
#include "ev-media-player-keys.h"
#endif /* ENABLE_DBUS */

struct _EvApplication {
	GObject base_instance;

	gchar *uri;

	gchar *dot_dir;
	gchar *data_dir;

#ifdef ENABLE_DBUS
	GDBusConnection *connection;
        guint registration_id;
	EvMediaPlayerKeys *keys;
	gboolean doc_registered;
#endif

	TotemScrsaver *scr_saver;

#ifdef WITH_SMCLIENT
	EggSMClient *smclient;
#endif

	gchar *filechooser_open_uri;
	gchar *filechooser_save_uri;
};

struct _EvApplicationClass {
	GObjectClass base_class;
};

static EvApplication *instance;

G_DEFINE_TYPE (EvApplication, ev_application, G_TYPE_OBJECT);

#ifdef ENABLE_DBUS
#define APPLICATION_DBUS_OBJECT_PATH "/org/gnome/evince/Evince"
#define APPLICATION_DBUS_INTERFACE   "org.gnome.evince.Application"

#define EVINCE_DAEMON_SERVICE        "org.gnome.evince.Daemon"
#define EVINCE_DAEMON_OBJECT_PATH    "/org/gnome/evince/Daemon"
#define EVINCE_DAEMON_INTERFACE      "org.gnome.evince.Daemon"
#endif

static const gchar *userdir = NULL;

static void _ev_application_open_uri_at_dest (EvApplication  *application,
					      const gchar    *uri,
					      GdkScreen      *screen,
					      EvLinkDest     *dest,
					      EvWindowRunMode mode,
					      const gchar    *search_string,
					      guint           timestamp);
static void ev_application_open_uri_in_window (EvApplication  *application,
					       const char     *uri,
					       EvWindow       *ev_window,
					       GdkScreen      *screen,
					       EvLinkDest     *dest,
					       EvWindowRunMode mode,
					       const gchar    *search_string,
					       guint           timestamp);

/**
 * ev_application_get_instance:
 *
 * Checks for #EvApplication instance, if it doesn't exist it does create it.
 *
 * Returns: an instance of the #EvApplication data.
 */
EvApplication *
ev_application_get_instance (void)
{
	if (!instance) {
		instance = EV_APPLICATION (g_object_new (EV_TYPE_APPLICATION, NULL));
	}

	return instance;
}

/* Session */
gboolean
ev_application_load_session (EvApplication *application)
{
	GKeyFile *state_file;
	gchar    *uri;

#ifdef WITH_SMCLIENT
	if (egg_sm_client_is_resumed (application->smclient)) {
		state_file = egg_sm_client_get_state_file (application->smclient);
		if (!state_file)
			return FALSE;
	} else
#endif /* WITH_SMCLIENT */
		return FALSE;

	uri = g_key_file_get_string (state_file, "Evince", "uri", NULL);
	if (!uri)
		return FALSE;

	ev_application_open_uri_at_dest (application, uri,
					 gdk_screen_get_default (),
					 NULL, 0, NULL,
					 GDK_CURRENT_TIME);
	g_free (uri);
	g_key_file_free (state_file);

	return TRUE;
}

#ifdef WITH_SMCLIENT

static void
smclient_save_state_cb (EggSMClient   *client,
			GKeyFile      *state_file,
			EvApplication *application)
{
	if (!application->uri)
		return;

	g_key_file_set_string (state_file, "Evince", "uri", application->uri);
}

static void
smclient_quit_cb (EggSMClient   *client,
		  EvApplication *application)
{
	ev_application_shutdown (application);
}
#endif /* WITH_SMCLIENT */

static void
ev_application_init_session (EvApplication *application)
{
#ifdef WITH_SMCLIENT
	application->smclient = egg_sm_client_get ();
	g_signal_connect (application->smclient, "save_state",
			  G_CALLBACK (smclient_save_state_cb),
			  application);
	g_signal_connect (application->smclient, "quit",
			  G_CALLBACK (smclient_quit_cb),
			  application);
#endif
}

/**
 * ev_display_open_if_needed:
 * @name: the name of the display to be open if it's needed.
 *
 * Search among all the open displays if any of them have the same name as the
 * passed name. If the display isn't found it tries the open it.
 *
 * Returns: a #GdkDisplay of the display with the passed name.
 */
static GdkDisplay *
ev_display_open_if_needed (const gchar *name)
{
	GSList     *displays;
	GSList     *l;
	GdkDisplay *display = NULL;

	displays = gdk_display_manager_list_displays (gdk_display_manager_get ());

	for (l = displays; l != NULL; l = l->next) {
		const gchar *display_name = gdk_display_get_name ((GdkDisplay *) l->data);

		if (g_ascii_strcasecmp (display_name, name) == 0) {
			display = l->data;
			break;
		}
	}

	g_slist_free (displays);

	return display != NULL ? display : gdk_display_open (name);
}

static void
child_setup (gpointer user_data)
{
	gchar *startup_id;

	startup_id = g_strdup_printf ("_TIME%lu",
				      (unsigned long)GPOINTER_TO_INT (user_data));
	g_setenv ("DESKTOP_STARTUP_ID", startup_id, TRUE);
	g_free (startup_id);
}

static void
ev_spawn (const char     *uri,
	  GdkScreen      *screen,
	  EvLinkDest     *dest,
	  EvWindowRunMode mode,
	  const gchar    *search_string,
	  guint           timestamp)
{
	gchar   *argv[6];
	guint    arg = 0;
	gint     i;
	gboolean res;
	GError  *error = NULL;

#ifdef G_OS_WIN32
{
	gchar *dir;

	dir = g_win32_get_package_installation_directory_of_module (NULL);
	argv[arg++] = g_build_filename (dir, "bin", "evince", NULL);
	g_free (dir);
}
#else
	argv[arg++] = g_build_filename (BINDIR, "evince", NULL);
#endif

	/* Page label */
	if (dest) {
		const gchar *page_label;

		page_label = ev_link_dest_get_page_label (dest);
		if (page_label)
			argv[arg++] = g_strdup_printf ("--page-label=%s", page_label);
		else
			argv[arg++] = g_strdup_printf ("--page-label=%d",
						       ev_link_dest_get_page (dest));
	}

	/* Find string */
	if (search_string) {
		argv[arg++] = g_strdup_printf ("--find=%s", search_string);
	}

	/* Mode */
	switch (mode) {
	case EV_WINDOW_MODE_FULLSCREEN:
		argv[arg++] = g_strdup ("-f");
		break;
	case EV_WINDOW_MODE_PRESENTATION:
		argv[arg++] = g_strdup ("-s");
		break;
	default:
		break;
	}

	argv[arg++] = (gchar *)uri;
	argv[arg] = NULL;

	res = gdk_spawn_on_screen (screen, NULL /* wd */, argv, NULL /* env */,
				   0,
				   child_setup,
				   GINT_TO_POINTER(timestamp),
				   NULL, &error);
	if (!res) {
		g_warning ("Error launching evince %s: %s\n", uri, error->message);
		g_error_free (error);
	}

	for (i = 0; i < arg - 1; i++) {
		g_free (argv[i]);
	}
}

static GList *
ev_application_get_windows (EvApplication *application)
{
	GList *l, *toplevels;
	GList *windows = NULL;

	toplevels = gtk_window_list_toplevels ();

	for (l = toplevels; l != NULL; l = l->next) {
		if (EV_IS_WINDOW (l->data)) {
			windows = g_list_append (windows, l->data);
		}
	}

	g_list_free (toplevels);

	return windows;
}

static EvWindow *
ev_application_get_empty_window (EvApplication *application,
				 GdkScreen     *screen)
{
	EvWindow *empty_window = NULL;
	GList    *windows = ev_application_get_windows (application);
	GList    *l;

	for (l = windows; l != NULL; l = l->next) {
		EvWindow *window = EV_WINDOW (l->data);

		if (ev_window_is_empty (window) &&
		    gtk_window_get_screen (GTK_WINDOW (window)) == screen) {
			empty_window = window;
			break;
		}
	}

	g_list_free (windows);

	return empty_window;
}


#ifdef ENABLE_DBUS
typedef struct {
	gchar          *uri;
	GdkScreen      *screen;
	EvLinkDest     *dest;
	EvWindowRunMode mode;
	gchar          *search_string;
	guint           timestamp;
} EvRegisterDocData;

static void
ev_register_doc_data_free (EvRegisterDocData *data)
{
	if (!data)
		return;

	g_free (data->uri);
	if (data->search_string)
		g_free (data->search_string);
	if (data->dest)
		g_object_unref (data->dest);

	g_free (data);
}

static void
on_reload_cb (GObject      *source_object,
	      GAsyncResult *res,
	      gpointer      user_data)
{
	GDBusConnection *connection = G_DBUS_CONNECTION (source_object);
	GVariant        *value;
	GError          *error = NULL;

	value = g_dbus_connection_call_finish (connection, res, &error);
	if (!value) {
		g_warning ("Failed to Reload: %s", error->message);
		g_error_free (error);
	}
	g_variant_unref (value);

	ev_application_shutdown (EV_APP);
}

static void
on_register_uri_cb (GObject      *source_object,
		    GAsyncResult *res,
		    gpointer      user_data)
{
	GDBusConnection   *connection = G_DBUS_CONNECTION (source_object);
	EvRegisterDocData *data = (EvRegisterDocData *)user_data;
	EvApplication     *application = EV_APP;
	GVariant          *value;
	const gchar       *owner;
	GVariantBuilder    builder;
	GError            *error = NULL;

	value = g_dbus_connection_call_finish (connection, res, &error);
	if (!value) {
		g_warning ("Error registering document: %s\n", error->message);
		g_error_free (error);

		_ev_application_open_uri_at_dest (application,
						  data->uri,
						  data->screen,
						  data->dest,
						  data->mode,
						  data->search_string,
						  data->timestamp);
		ev_register_doc_data_free (data);

		return;
	}

	g_variant_get (value, "(&s)", &owner);

	/* This means that the document wasn't already registered; go
         * ahead with opening it.
         */
	if (owner[0] == '\0') {
                g_variant_unref (value);

		application->doc_registered = TRUE;

		_ev_application_open_uri_at_dest (application,
						  data->uri,
						  data->screen,
						  data->dest,
						  data->mode,
						  data->search_string,
						  data->timestamp);
		ev_register_doc_data_free (data);

                return;
        }

	/* Already registered */
	g_variant_builder_init (&builder, G_VARIANT_TYPE ("(a{sv}u)"));
        g_variant_builder_open (&builder, G_VARIANT_TYPE ("a{sv}"));
        g_variant_builder_add (&builder, "{sv}",
                               "display",
                               g_variant_new_string (gdk_display_get_name (gdk_screen_get_display (data->screen))));
        g_variant_builder_add (&builder, "{sv}",
                               "screen",
                               g_variant_new_int32 (gdk_screen_get_number (data->screen)));
	if (data->dest) {
                g_variant_builder_add (&builder, "{sv}",
                                       "page-label",
                                       g_variant_new_string (ev_link_dest_get_page_label (data->dest)));
	}
	if (data->search_string) {
                g_variant_builder_add (&builder, "{sv}",
                                       "find-string",
                                       g_variant_new_string (data->search_string));
	}
	if (data->mode != EV_WINDOW_MODE_NORMAL) {
                g_variant_builder_add (&builder, "{sv}",
                                       "mode",
                                       g_variant_new_uint32 (data->mode));
	}
        g_variant_builder_close (&builder);

        g_variant_builder_add (&builder, "u", data->timestamp);

        g_dbus_connection_call (connection,
				owner,
				APPLICATION_DBUS_OBJECT_PATH,
				APPLICATION_DBUS_INTERFACE,
				"Reload",
				g_variant_builder_end (&builder),
				NULL,
				G_DBUS_CALL_FLAGS_NONE,
				-1,
				NULL,
				on_reload_cb,
				NULL);
	g_variant_unref (value);
	ev_register_doc_data_free (data);
}

/*
 * ev_application_register_uri:
 * @application:
 * @uri:
 * @screen:
 * @dest:
 * @mode:
 * @search_string:
 * @timestamp:
 *
 * Registers @uri with evince-daemon.
 *
 */
static void
ev_application_register_uri (EvApplication  *application,
			     const gchar    *uri,
                             GdkScreen      *screen,
                             EvLinkDest     *dest,
                             EvWindowRunMode mode,
                             const gchar    *search_string,
			     guint           timestamp)
{
	EvRegisterDocData *data;

	if (!application->connection)
		return;

	if (application->doc_registered) {
		/* Already registered, reload */
		GList *windows, *l;

		windows = ev_application_get_windows (application);
		for (l = windows; l != NULL; l = g_list_next (l)) {
			EvWindow *ev_window = EV_WINDOW (l->data);

			ev_application_open_uri_in_window (application, uri, ev_window,
							   screen, dest, mode,
							   search_string,
							   timestamp);
		}
		g_list_free (windows);

		return;
	}

	data = g_new (EvRegisterDocData, 1);
	data->uri = g_strdup (uri);
	data->screen = screen;
	data->dest = dest ? g_object_ref (dest) : NULL;
	data->mode = mode;
	data->search_string = search_string ? g_strdup (search_string) : NULL;
	data->timestamp = timestamp;

        g_dbus_connection_call (application->connection,
				EVINCE_DAEMON_SERVICE,
				EVINCE_DAEMON_OBJECT_PATH,
				EVINCE_DAEMON_INTERFACE,
				"RegisterDocument",
				g_variant_new ("(s)", uri),
				G_VARIANT_TYPE ("(s)"),
				G_DBUS_CALL_FLAGS_NONE,
				-1,
				NULL,
				on_register_uri_cb,
				data);
}

static void
ev_application_unregister_uri (EvApplication *application,
			       const gchar   *uri)
{
        GVariant *value;
	GError   *error = NULL;

	if (!application->doc_registered)
		return;

	/* This is called from ev_application_shutdown(),
	 * so it's safe to use the sync api
	 */
        value = g_dbus_connection_call_sync (
		application->connection,
		EVINCE_DAEMON_SERVICE,
		EVINCE_DAEMON_OBJECT_PATH,
		EVINCE_DAEMON_INTERFACE,
		"UnregisterDocument",
		g_variant_new ("(s)", uri),
		NULL,
		G_DBUS_CALL_FLAGS_NO_AUTO_START,
		-1,
		NULL,
		&error);
        if (value == NULL) {
		g_warning ("Error unregistering document: %s\n", error->message);
		g_error_free (error);
	} else {
                g_variant_unref (value);
	}
}
#endif /* ENABLE_DBUS */

static void
ev_application_open_uri_in_window (EvApplication  *application,
				   const char     *uri,
				   EvWindow       *ev_window,
				   GdkScreen      *screen,
				   EvLinkDest     *dest,
				   EvWindowRunMode mode,
				   const gchar    *search_string,
				   guint           timestamp)
{
#ifdef GDK_WINDOWING_X11
	GdkWindow *gdk_window;
#endif

	if (screen) {
		ev_stock_icons_set_screen (screen);
		gtk_window_set_screen (GTK_WINDOW (ev_window), screen);
	}

	/* We need to load uri before showing the window, so
	   we can restore window size without flickering */
	ev_window_open_uri (ev_window, uri, dest, mode, search_string);

	if (!gtk_widget_get_realized (GTK_WIDGET (ev_window)))
		gtk_widget_realize (GTK_WIDGET (ev_window));

#ifdef GDK_WINDOWING_X11
	gdk_window = gtk_widget_get_window (GTK_WIDGET (ev_window));

	if (timestamp <= 0)
		timestamp = gdk_x11_get_server_time (gdk_window);
	gdk_x11_window_set_user_time (gdk_window, timestamp);

	gtk_window_present (GTK_WINDOW (ev_window));
#else
	gtk_window_present_with_time (GTK_WINDOW (ev_window), timestamp);
#endif /* GDK_WINDOWING_X11 */
}

static void
_ev_application_open_uri_at_dest (EvApplication  *application,
				  const gchar    *uri,
				  GdkScreen      *screen,
				  EvLinkDest     *dest,
				  EvWindowRunMode mode,
				  const gchar    *search_string,
				  guint           timestamp)
{
	EvWindow *ev_window;

	ev_window = ev_application_get_empty_window (application, screen);
	if (!ev_window)
		ev_window = EV_WINDOW (ev_window_new ());

	ev_application_open_uri_in_window (application, uri, ev_window,
					   screen, dest, mode,
					   search_string,
					   timestamp);
}

/**
 * ev_application_open_uri_at_dest:
 * @application: The instance of the application.
 * @uri: The uri to be opened.
 * @screen: Thee screen where the link will be shown.
 * @dest: The #EvLinkDest of the document.
 * @mode: The run mode of the window.
 * @timestamp: Current time value.
 */
void
ev_application_open_uri_at_dest (EvApplication  *application,
				 const char     *uri,
				 GdkScreen      *screen,
				 EvLinkDest     *dest,
				 EvWindowRunMode mode,
				 const gchar    *search_string,
				 guint           timestamp)
{
	g_return_if_fail (uri != NULL);

	if (application->uri && strcmp (application->uri, uri) != 0) {
		/* spawn a new evince process */
		ev_spawn (uri, screen, dest, mode, search_string, timestamp);
		return;
	} else if (!application->uri) {
		application->uri = g_strdup (uri);
	}

#ifdef ENABLE_DBUS
	/* Register the uri or send Reload to
	 * remote instance if already registered
	 */
	ev_application_register_uri (application, uri, screen, dest, mode, search_string, timestamp);
#else
	_ev_application_open_uri_at_dest (application, uri, screen, dest, mode, search_string, timestamp);
#endif /* ENABLE_DBUS */
}

/**
 * ev_application_open_window:
 * @application: The instance of the application.
 * @timestamp: Current time value.
 *
 * Creates a new window
 */
void
ev_application_open_window (EvApplication *application,
			    GdkScreen     *screen,
			    guint32        timestamp)
{
	GtkWidget *new_window = ev_window_new ();
#ifdef GDK_WINDOWING_X11
	GdkWindow *gdk_window;
#endif

	if (screen) {
		ev_stock_icons_set_screen (screen);
		gtk_window_set_screen (GTK_WINDOW (new_window), screen);
	}

	if (!gtk_widget_get_realized (new_window))
		gtk_widget_realize (new_window);

#ifdef GDK_WINDOWING_X11
	gdk_window = gtk_widget_get_window (GTK_WIDGET (new_window));

	if (timestamp <= 0)
		timestamp = gdk_x11_get_server_time (gdk_window);
	gdk_x11_window_set_user_time (gdk_window, timestamp);

	gtk_window_present (GTK_WINDOW (new_window));
#else
	gtk_window_present_with_time (GTK_WINDOW (new_window), timestamp);
#endif /* GDK_WINDOWING_X11 */
}

#ifdef ENABLE_DBUS
static void
method_call_cb (GDBusConnection       *connection,
                const gchar           *sender,
                const gchar           *object_path,
                const gchar           *interface_name,
                const gchar           *method_name,
                GVariant              *parameters,
                GDBusMethodInvocation *invocation,
                gpointer               user_data)
{
        EvApplication   *application = EV_APPLICATION (user_data);
	GList           *windows, *l;
        guint            timestamp;
        GVariantIter    *iter;
        const gchar     *key;
        GVariant        *value;
        GdkDisplay      *display = NULL;
        int              screen_number = 0;
	EvLinkDest      *dest = NULL;
	EvWindowRunMode  mode = EV_WINDOW_MODE_NORMAL;
	const gchar     *search_string = NULL;
	GdkScreen       *screen = NULL;

	if (g_strcmp0 (method_name, "Reload") == 0) {
		g_variant_get (parameters, "(a{sv}u)", &iter, &timestamp);

		while (g_variant_iter_loop (iter, "{&sv}", &key, &value)) {
			if (strcmp (key, "display") == 0 && g_variant_classify (value) == G_VARIANT_CLASS_STRING) {
				display = ev_display_open_if_needed (g_variant_get_string (value, NULL));
			} else if (strcmp (key, "screen") == 0 && g_variant_classify (value) == G_VARIANT_CLASS_STRING) {
				screen_number = g_variant_get_int32 (value);
			} else if (strcmp (key, "mode") == 0 && g_variant_classify (value) == G_VARIANT_CLASS_UINT32) {
			mode = g_variant_get_uint32 (value);
			} else if (strcmp (key, "page-label") == 0 && g_variant_classify (value) == G_VARIANT_CLASS_STRING) {
				dest = ev_link_dest_new_page_label (g_variant_get_string (value, NULL));
			} else if (strcmp (key, "find-string") == 0 && g_variant_classify (value) == G_VARIANT_CLASS_STRING) {
				search_string = g_variant_get_string (value, NULL);
			}
		}
		g_variant_iter_free (iter);

		if (display != NULL &&
		    screen_number >= 0 &&
		    screen_number < gdk_display_get_n_screens (display))
			screen = gdk_display_get_screen (display, screen_number);
		else
			screen = gdk_screen_get_default ();

		windows = ev_application_get_windows (application);
		for (l = windows; l != NULL; l = g_list_next (l)) {
			EvWindow *ev_window = EV_WINDOW (l->data);

			ev_application_open_uri_in_window (application, application->uri,
							   ev_window,
							   screen, dest, mode,
							   search_string,
							   timestamp);
		}
		g_list_free (windows);

		if (dest)
			g_object_unref (dest);

		g_dbus_method_invocation_return_value (invocation, g_variant_new ("()"));
	} else if (g_strcmp0 (method_name, "GetWindowList") == 0) {
		GList          *windows = ev_application_get_windows (application);
		GVariantBuilder builder;
		GList	       *l;

		g_variant_builder_init (&builder, G_VARIANT_TYPE ("(ao)"));
		g_variant_builder_open (&builder, G_VARIANT_TYPE ("ao"));

		for (l = windows; l; l = g_list_next (l)) {
			EvWindow *window = (EvWindow *)l->data;

			g_variant_builder_add (&builder, "o", ev_window_get_dbus_object_path (window));
		}

		g_variant_builder_close (&builder);
		g_list_free (windows);

		g_dbus_method_invocation_return_value (invocation, g_variant_builder_end (&builder));
	}
}

static const char introspection_xml[] =
        "<node>"
          "<interface name='org.gnome.evince.Application'>"
            "<method name='Reload'>"
              "<arg type='a{sv}' name='args' direction='in'/>"
              "<arg type='u' name='timestamp' direction='in'/>"
            "</method>"
            "<method name='GetWindowList'>"
              "<arg type='ao' name='window_list' direction='out'/>"
            "</method>"
          "</interface>"
        "</node>";

static const GDBusInterfaceVTable interface_vtable = {
	method_call_cb,
	NULL,
	NULL
};

static GDBusNodeInfo *introspection_data;
#endif /* ENABLE_DBUS */

void
ev_application_open_uri_list (EvApplication *application,
			      GSList        *uri_list,
			      GdkScreen     *screen,
			      guint          timestamp)
{
	GSList *l;

	for (l = uri_list; l != NULL; l = l->next) {
		ev_application_open_uri_at_dest (application, (char *)l->data,
						 screen, NULL, 0, NULL,
						 timestamp);
	}
}

static void
ev_application_accel_map_save (EvApplication *application)
{
	gchar *accel_map_file;
	gchar *tmp_filename;
	gint   fd;

	if (userdir) {
		accel_map_file = g_build_filename (userdir, "accels",
						   "evince", NULL);
	} else {
		accel_map_file = g_build_filename (g_get_home_dir (),
						   ".gnome2", "accels",
						   "evince", NULL);
	}

	tmp_filename = g_strdup_printf ("%s.XXXXXX", accel_map_file);

	fd = g_mkstemp (tmp_filename);
	if (fd == -1) {
		g_free (accel_map_file);
		g_free (tmp_filename);

		return;
	}
	gtk_accel_map_save_fd (fd);
	close (fd);

	if (g_rename (tmp_filename, accel_map_file) == -1) {
		/* FIXME: win32? */
		g_unlink (tmp_filename);
	}

	g_free (accel_map_file);
	g_free (tmp_filename);
}

static void
ev_application_accel_map_load (EvApplication *application)
{
	gchar *accel_map_file;

	if (userdir) {
		accel_map_file = g_build_filename (userdir, "accels",
						   "evince", NULL);
	} else {
		accel_map_file = g_build_filename (g_get_home_dir (),
						   ".gnome2", "accels",
						   "evince", NULL);
	}

	gtk_accel_map_load (accel_map_file);
	g_free (accel_map_file);
}

void
ev_application_shutdown (EvApplication *application)
{
	if (application->uri) {
#ifdef ENABLE_DBUS
		ev_application_unregister_uri (application,
					       application->uri);
#endif
		g_free (application->uri);
		application->uri = NULL;
	}

	ev_application_accel_map_save (application);

	g_object_unref (application->scr_saver);
	application->scr_saver = NULL;

#ifdef ENABLE_DBUS
	if (application->keys) {
		g_object_unref (application->keys);
		application->keys = NULL;
	}
        if (application->registration_id != 0) {
                g_dbus_connection_unregister_object (application->connection,
                                                     application->registration_id);
                application->registration_id = 0;
        }
        if (application->connection != NULL) {
                g_object_unref (application->connection);
                application->connection = NULL;
        }
	if (introspection_data) {
		g_dbus_node_info_ref (introspection_data);
		introspection_data = NULL;
	}
#endif /* ENABLE_DBUS */
	
        g_free (application->dot_dir);
        application->dot_dir = NULL;
        g_free (application->data_dir);
        application->data_dir = NULL;
	g_free (application->filechooser_open_uri);
        application->filechooser_open_uri = NULL;
	g_free (application->filechooser_save_uri);
	application->filechooser_save_uri = NULL;

	g_object_unref (application);
        instance = NULL;
	
	gtk_main_quit ();
}

static void
ev_application_class_init (EvApplicationClass *ev_application_class)
{
}

static void
ev_application_init (EvApplication *ev_application)
{
	GError *error = NULL;

	userdir = g_getenv ("GNOME22_USER_DIR");
	if (userdir)
		ev_application->dot_dir = g_build_filename (userdir, "evince", NULL);
	else
		ev_application->dot_dir = g_build_filename (g_get_home_dir (),
							    ".gnome2",
							    "evince",
							    NULL);

#ifdef G_OS_WIN32
{
	gchar *dir;

	dir = g_win32_get_package_installation_directory_of_module (NULL);
	ev_application->data_dir = g_build_filename (dir, "share", "evince", NULL);
	g_free (dir);
}
#else
	ev_application->data_dir = g_strdup (EVINCEDATADIR);
#endif

	ev_application_init_session (ev_application);

	ev_application_accel_map_load (ev_application);

#ifdef ENABLE_DBUS
        ev_application->connection = g_bus_get_sync (G_BUS_TYPE_SESSION, NULL, &error);
        if (ev_application->connection != NULL) {
                introspection_data = g_dbus_node_info_new_for_xml (introspection_xml, NULL);
                g_assert (introspection_data != NULL);

                ev_application->registration_id =
                    g_dbus_connection_register_object (ev_application->connection,
                                                       APPLICATION_DBUS_OBJECT_PATH,
                                                       introspection_data->interfaces[0],
                                                       &interface_vtable,
                                                       ev_application, NULL,
                                                       &error);
                if (ev_application->registration_id == 0) {
                        g_printerr ("Failed to register bus object: %s\n", error->message);
                        g_error_free (error);
                }
        } else {
                g_printerr ("Failed to get bus connection: %s\n", error->message);
                g_error_free (error);
        }

	ev_application->keys = ev_media_player_keys_new ();
#endif /* ENABLE_DBUS */

	ev_application->scr_saver = totem_scrsaver_new ();
	g_object_set (ev_application->scr_saver,
		      "reason", _("Running in presentation mode"),
		      NULL);
}

GDBusConnection *
ev_application_get_dbus_connection (EvApplication *application)
{
#ifdef ENABLE_DBUS
	return application->connection;
#else
	return NULL;
#endif
}

gboolean
ev_application_has_window (EvApplication *application)
{
	GList    *l, *toplevels;
	gboolean  retval = FALSE;

	toplevels = gtk_window_list_toplevels ();

	for (l = toplevels; l != NULL && !retval; l = l->next) {
		if (EV_IS_WINDOW (l->data))
			retval = TRUE;
	}

	g_list_free (toplevels);

	return retval;
}

guint
ev_application_get_n_windows (EvApplication *application)
{
	GList *l, *toplevels;
	guint  retval = 0;

	toplevels = gtk_window_list_toplevels ();

	for (l = toplevels; l != NULL; l = l->next) {
		if (EV_IS_WINDOW (l->data))
			retval++;
	}

	g_list_free (toplevels);

	return retval;
}

const gchar *
ev_application_get_uri (EvApplication *application)
{
	return application->uri;
}

/**
 * ev_application_get_media_keys:
 * @application: The instance of the application.
 *
 * It gives you access to the media player keys handler object.
 *
 * Returns: A #EvMediaPlayerKeys.
 */
GObject *
ev_application_get_media_keys (EvApplication *application)
{
#ifdef ENABLE_DBUS
	return G_OBJECT (application->keys);
#else
	return NULL;
#endif /* ENABLE_DBUS */
}

void
ev_application_set_filechooser_uri (EvApplication       *application,
				    GtkFileChooserAction action,
				    const gchar         *uri)
{
	if (action == GTK_FILE_CHOOSER_ACTION_OPEN) {
		g_free (application->filechooser_open_uri);
		application->filechooser_open_uri = g_strdup (uri);
	} else if (action == GTK_FILE_CHOOSER_ACTION_SAVE) {
		g_free (application->filechooser_save_uri);
		application->filechooser_save_uri = g_strdup (uri);
	}
}

const gchar *
ev_application_get_filechooser_uri (EvApplication       *application,
				    GtkFileChooserAction action)
{
	if (action == GTK_FILE_CHOOSER_ACTION_OPEN) {
		if (application->filechooser_open_uri)
			return application->filechooser_open_uri;
	} else if (action == GTK_FILE_CHOOSER_ACTION_SAVE) {
		if (application->filechooser_save_uri)
			return application->filechooser_save_uri;
	}

	return NULL;
}

void
ev_application_screensaver_enable (EvApplication *application)
{
	totem_scrsaver_enable (application->scr_saver);
}

void
ev_application_screensaver_disable (EvApplication *application)
{
	totem_scrsaver_disable (application->scr_saver);
}

const gchar *
ev_application_get_dot_dir (EvApplication *application,
                            gboolean create)
{
        if (create)
                g_mkdir_with_parents (application->dot_dir, 0700);

	return application->dot_dir;
}

const gchar *
ev_application_get_data_dir (EvApplication   *application)
{
	return application->data_dir;
}
