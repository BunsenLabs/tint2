#ifndef THEME_VIEW
#define THEME_VIEW

#include <gtk/gtk.h>

extern GtkWidget *g_theme_view;
extern GtkListStore *theme_list_store;
enum {
    COL_THEME_FILE = 0,
    COL_THEME_NAME,
    COL_SNAPSHOT,
    COL_WIDTH,
    COL_HEIGHT,
    COL_FORCE_REFRESH,
    NB_COL,
};

GtkWidget *create_view();

void theme_list_append(const gchar *path);

#endif
