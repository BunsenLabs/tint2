#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <locale.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <gtk/gtk.h>
#include <gdk/gdkx.h>

#ifdef GETTEXT_PACKAGE
#include <glib/gi18n-lib.h>
#else
#define _(String) String
#define GETTEXT_PACKAGE "tint2conf"
#endif

#define SNAPSHOT_TICK 190
gboolean update_snapshot(gpointer ignored);
void menuApply();
void refresh_current_theme();
extern GtkWidget *g_window;
