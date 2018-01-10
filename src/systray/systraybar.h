/**************************************************************************
* Copyright (C) 2009 thierry lorthiois (lorthiois@bbsoft.fr)
*
* systraybar
* systray implementation come from 'docker-1.5' by Ben Jansens,
* and from systray/xembed specification (freedesktop.org).
*
**************************************************************************/

#ifndef SYSTRAYBAR_H
#define SYSTRAYBAR_H

#include "common.h"
#include "area.h"
#include "timer.h"
#include <X11/extensions/Xdamage.h>

// XEMBED messages
#define XEMBED_EMBEDDED_NOTIFY 0
// Flags for _XEMBED_INFO
#define XEMBED_MAPPED (1 << 0)

typedef enum SystraySortMethod {
    SYSTRAY_SORT_ASCENDING = 0,
    SYSTRAY_SORT_DESCENDING,
    SYSTRAY_SORT_LEFT2RIGHT,
    SYSTRAY_SORT_RIGHT2LEFT,
} SystraySortMethod;

typedef struct {
    // always start with area
    Area area;

    GSList *list_icons;
    SystraySortMethod sort;
    int alpha, saturation, brightness;
    int icon_size, icons_per_column, icons_per_row, margin;
} Systray;

typedef struct {
    // The actual tray icon window (created by the application)
    Window win;
    // The parent window created by tint2 to embed the icon
    Window parent;
    int x, y;
    int width, height;
    int depth;
    gboolean reparented;
    gboolean embedded;
    // Process PID or zero.
    int pid;
    // A number that is incremented for each new icon, used to sort them by the order in which they were created.
    int chrono;
    // Name of the tray icon window.
    char *name;
    // Members used for rendering
    struct timespec time_last_render;
    int num_fast_renders;
    Timer render_timer;
    // Members used for resizing
    int bad_size_counter;
    struct timespec time_last_resize;
    Timer resize_timer;
    // Icon contents if we are compositing the icon, otherwise null
    Imlib_Image image;
    // XDamage
    Damage damage;
} TrayWindow;

// net_sel_win != None when protocol started
extern Window net_sel_win;
extern Systray systray;
extern gboolean refresh_systray;
extern gboolean systray_enabled;
extern int systray_max_icon_size;
extern int systray_monitor;
extern gboolean systray_profile;
extern char *systray_hide_name_filter;

// default global data
void default_systray();

// freed memory
void cleanup_systray();

// initialize protocol and panel position
void init_systray();
void init_systray_panel(void *p);

void draw_systray(void *obj, cairo_t *c);
gboolean resize_systray(void *obj);
void on_change_systray(void *obj);
gboolean systray_on_monitor(int i_monitor, int num_panels);

// systray protocol
// many tray icon doesn't manage stop/restart of the systray manager
void start_net();
void stop_net();
void handle_systray_event(XClientMessageEvent *e);

gboolean add_icon(Window id);
gboolean reparent_icon(TrayWindow *traywin);
gboolean embed_icon(TrayWindow *traywin);
void remove_icon(TrayWindow *traywin);

void refresh_systray_icons();
void systray_render_icon(void *t);
gboolean request_embed_icon(TrayWindow *traywin);
void systray_resize_request_event(TrayWindow *traywin, XEvent *e);
gboolean request_embed_icon(TrayWindow *traywin);
void systray_reconfigure_event(TrayWindow *traywin, XEvent *e);
void systray_property_notify(TrayWindow *traywin, XEvent *e);
void systray_destroy_event(TrayWindow *traywin);
void kde_update_icons();

TrayWindow *systray_find_icon(Window win);

#endif
