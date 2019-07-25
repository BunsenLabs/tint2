/**************************************************************************
*
* Tint2 : task
*
* Copyright (C) 2007 Pål Staurland (staura@gmail.com)
* Modified (C) 2008 thierry lorthiois (lorthiois@bbsoft.fr) from Omega distribution
*
* This program is free software; you can redistribute it and/or
* modify it under the terms of the GNU General Public License version 2
* as published by the Free Software Foundation.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
* You should have received a copy of the GNU General Public License
* along with this program; if not, write to the Free Software
* Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
**************************************************************************/

#include <glib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>

#include "panel.h"
#include "server.h"
#include "task.h"
#include "taskbar.h"
#include "timer.h"
#include "tooltip.h"
#include "window.h"

Timer urgent_timer;
GSList *urgent_list;

void task_dump_geometry(void *obj, int indent);
int task_compute_desired_size(void *obj);
void task_refresh_thumbnail(Task *task);
void task_get_content_color(void *obj, Color *color);

char *task_get_tooltip(void *obj)
{
    Task *t = (Task *)obj;
    return strdup(t->title);
}

cairo_surface_t *task_get_thumbnail(void *obj)
{
    if (!panel_config.g_task.thumbnail_enabled)
        return NULL;
    Task *t = (Task *)obj;
    if (!t->thumbnail)
        task_refresh_thumbnail(t);
    taskbar_start_thumbnail_timer(THUMB_MODE_TOOLTIP_WINDOW);
    return t->thumbnail;
}

Task *add_task(Window win)
{
    if (!win)
        return NULL;
    if (window_is_hidden(win))
        return NULL;

    XSelectInput(server.display, win, PropertyChangeMask | StructureNotifyMask);
    XFlush(server.display);

    int monitor = 0;
    if (num_panels > 1) {
        monitor = get_window_monitor(win);
        if (monitor >= num_panels)
            monitor = 0;
    }

    // TODO why do we add the task only to the panel for the current monitor, without checking hide_task_diff_monitor?

    Task task_template;
    memset(&task_template, 0, sizeof(task_template));
    snprintf(task_template.area.name, sizeof(task_template.area.name), "Task %d", (int)win);
    task_template.area.has_mouse_over_effect = panel_config.mouse_effects;
    task_template.area.has_mouse_press_effect = panel_config.mouse_effects;
    task_template.area._dump_geometry = task_dump_geometry;
    task_template.area._is_under_mouse = full_width_area_is_under_mouse;
    task_template.area._get_content_color = task_get_content_color;
    task_template.win = win;
    task_template.desktop = get_window_desktop(win);
    task_template.area.panel = &panels[monitor];
    task_template.current_state = window_is_iconified(win) ? TASK_ICONIFIED : TASK_NORMAL;
    get_window_coordinates(win, &task_template.win_x, &task_template.win_y, &task_template.win_w, &task_template.win_h);

    // allocate only one title and one icon
    // even with task_on_all_desktop and with task_on_all_panel
    task_template.title = NULL;
    for (int k = 0; k < TASK_STATE_COUNT; ++k) {
        task_template.icon[k] = NULL;
    }
    task_update_title(&task_template);
    task_update_icon(&task_template);
    snprintf(task_template.area.name,
             sizeof(task_template.area.name),
             "Task %d %s",
             (int)win,
             task_template.title ? task_template.title : "null");

    // get application name
    // use res_class property of WM_CLASS as res_name is easily overridable by user
    XClassHint *classhint = XAllocClassHint();
    if (classhint && XGetClassHint(server.display, win, classhint))
        task_template.application = strdup(classhint->res_class);
    else
        task_template.application = strdup("Untitled");
    if (classhint) {
        if (classhint->res_name)
            XFree(classhint->res_name);
        if (classhint->res_class)
            XFree(classhint->res_class);
        XFree(classhint);
    }

    GPtrArray *task_buttons = g_ptr_array_new();
    for (int j = 0; j < panels[monitor].num_desktops; j++) {
        if (task_template.desktop != ALL_DESKTOPS && task_template.desktop != j)
            continue;

        Taskbar *taskbar = &panels[monitor].taskbar[j];
        Task *task_instance = calloc(1, sizeof(Task));
        memcpy(&task_instance->area, &panels[monitor].g_task.area, sizeof(Area));
        task_instance->area.has_mouse_over_effect = panel_config.mouse_effects;
        task_instance->area.has_mouse_press_effect = panel_config.mouse_effects;
        task_instance->area._dump_geometry = task_dump_geometry;
        task_instance->area._is_under_mouse = full_width_area_is_under_mouse;
        task_instance->area._compute_desired_size = task_compute_desired_size;
        task_instance->area._get_content_color = task_get_content_color;
        task_instance->win = task_template.win;
        task_instance->desktop = task_template.desktop;
        task_instance->win_x = task_template.win_x;
        task_instance->win_y = task_template.win_y;
        task_instance->win_w = task_template.win_w;
        task_instance->win_h = task_template.win_h;
        task_instance->current_state = TASK_UNDEFINED; // to update the current state later in set_task_state...
        if (task_instance->desktop == ALL_DESKTOPS && server.desktop != j) {
            task_instance->area.on_screen = always_show_all_desktop_tasks;
        }
        task_instance->title = task_template.title;
        task_instance->application = task_template.application;
        if (panels[monitor].g_task.tooltip_enabled) {
            task_instance->area._get_tooltip_text = task_get_tooltip;
            task_instance->area._get_tooltip_image = task_get_thumbnail;
        }
        task_instance->icon_color = task_template.icon_color;
        task_instance->icon_color_hover = task_template.icon_color_hover;
        task_instance->icon_color_press = task_template.icon_color_press;
        for (int k = 0; k < TASK_STATE_COUNT; ++k) {
            task_instance->icon[k] = task_template.icon[k];
            task_instance->icon_hover[k] = task_template.icon_hover[k];
            task_instance->icon_press[k] = task_template.icon_press[k];
        }
        task_instance->icon_width = task_template.icon_width;
        task_instance->icon_height = task_template.icon_height;

        add_area(&task_instance->area, &taskbar->area);
        g_ptr_array_add(task_buttons, task_instance);
    }
    Window *key = calloc(1, sizeof(Window));
    *key = task_template.win;
    g_hash_table_insert(win_to_task, key, task_buttons);

    set_task_state((Task *)g_ptr_array_index(task_buttons, 0), task_template.current_state);

    sort_taskbar_for_win(win);

    if (taskbar_mode == MULTI_DESKTOP) {
        Panel *panel = (Panel *)task_template.area.panel;
        panel->area.resize_needed = TRUE;
    }

    if (window_is_urgent(win)) {
        add_urgent((Task *)g_ptr_array_index(task_buttons, 0));
    }

    if (hide_taskbar_if_empty)
        update_all_taskbars_visibility();

    return (Task *)g_ptr_array_index(task_buttons, 0);
}

void task_remove_icon(Task *task)
{
    if (!task)
        return;
    for (int k = 0; k < TASK_STATE_COUNT; ++k) {
        if (task->icon[k]) {
            imlib_context_set_image(task->icon[k]);
            imlib_free_image();
            task->icon[k] = 0;
        }
        if (task->icon_hover[k]) {
            imlib_context_set_image(task->icon_hover[k]);
            imlib_free_image();
            task->icon_hover[k] = 0;
        }
        if (task->icon_press[k]) {
            imlib_context_set_image(task->icon_press[k]);
            imlib_free_image();
            task->icon_press[k] = 0;
        }
    }
}

void remove_task(Task *task)
{
    if (!task)
        return;

    if (taskbar_mode == MULTI_DESKTOP) {
        Panel *panel = task->area.panel;
        panel->area.resize_needed = 1;
    }

    Window win = task->win;

    // free title, icon and application name just for the first task
    // even with task_on_all_desktop and with task_on_all_panel
    if (task->title)
        free(task->title);
    if (task->thumbnail)
        cairo_surface_destroy(task->thumbnail);
    if (task->application)
        free(task->application);
    task_remove_icon(task);

    GPtrArray *task_buttons = g_hash_table_lookup(win_to_task, &win);
    for (int i = 0; i < task_buttons->len; ++i) {
        Task *task2 = g_ptr_array_index(task_buttons, i);
        if (task2 == active_task)
            active_task = 0;
        if (task2 == task_drag)
            task_drag = 0;
        if (g_slist_find(urgent_list, task2))
            del_urgent(task2);
        if (g_tooltip.area == &task2->area)
            tooltip_hide(NULL);
        remove_area((Area *)task2);
        free(task2);
    }
    g_hash_table_remove(win_to_task, &win);
    if (hide_taskbar_if_empty)
        update_all_taskbars_visibility();
}

gboolean task_update_title(Task *task)
{
    Panel *panel = task->area.panel;

    if (!panel->g_task.has_text && !panel->g_task.tooltip_enabled && taskbar_sort_method != TASKBAR_SORT_TITLE)
        return FALSE;

    char *name = server_get_property(task->win, server.atom._NET_WM_VISIBLE_NAME, server.atom.UTF8_STRING, 0);
    if (!name || !strlen(name)) {
        name = server_get_property(task->win, server.atom._NET_WM_NAME, server.atom.UTF8_STRING, 0);
        if (!name || !strlen(name)) {
            name = server_get_property(task->win, server.atom.WM_NAME, XA_STRING, 0);
        }
    }

    char *title;
    if (name && strlen(name)) {
        title = strdup(name);
    } else {
        title = strdup("Untitled");
    }
    if (name)
        XFree(name);

    if (task->title) {
        // check unecessary title change
        if (strcmp(task->title, title) == 0) {
            free(title);
            return FALSE;
        } else {
            free(task->title);
        }
    }

    task->title = title;
    GPtrArray *task_buttons = get_task_buttons(task->win);
    if (task_buttons) {
        for (int i = 0; i < task_buttons->len; ++i) {
            Task *task2 = g_ptr_array_index(task_buttons, i);
            task2->title = task->title;
            schedule_redraw(&task2->area);
        }
    }
    return TRUE;
}

Imlib_Image task_get_icon(Window win, int icon_size)
{
    Imlib_Image img = NULL;

    if (!img) {
        int len;
        gulong *data = server_get_property(win, server.atom._NET_WM_ICON, XA_CARDINAL, &len);
        if (data) {
            if (len > 0) {
                // get ARGB icon
                int w, h;
                gulong *tmp_data = get_best_icon(data, get_icon_count(data, len), len, &w, &h, icon_size);
                if (tmp_data) {
                    DATA32 icon_data[w * h];
                    for (int j = 0; j < w * h; ++j)
                        icon_data[j] = tmp_data[j];
                    img = imlib_create_image_using_copied_data(w, h, icon_data);
                }
            }
            XFree(data);
        }
    }

    if (!img) {
        XWMHints *hints = XGetWMHints(server.display, win);
        if (hints) {
            if (hints->flags & IconPixmapHint && hints->icon_pixmap != 0) {
                // get width, height and depth for the pixmap
                Window root;
                int icon_x, icon_y;
                unsigned border_width, bpp;
                unsigned w, h;

                XGetGeometry(server.display, hints->icon_pixmap, &root, &icon_x, &icon_y, &w, &h, &border_width, &bpp);
                imlib_context_set_drawable(hints->icon_pixmap);
                img = imlib_create_image_from_drawable(hints->icon_mask, 0, 0, w, h, 0);
            }
            XFree(hints);
        }
    }

    if (img == NULL) {
        imlib_context_set_image(default_icon);
        img = imlib_clone_image();
    }

    return img;
}

void task_set_icon_color(Task *task, Imlib_Image icon)
{
    get_image_mean_color(icon, &task->icon_color);
    if (panel_config.mouse_effects) {
        task->icon_color_hover = task->icon_color;
        adjust_color(&task->icon_color_hover,
                     panel_config.mouse_over_alpha,
                     panel_config.mouse_over_saturation,
                     panel_config.mouse_over_brightness);
        task->icon_color_press = task->icon_color;
        adjust_color(&task->icon_color_press,
                     panel_config.mouse_pressed_alpha,
                     panel_config.mouse_pressed_saturation,
                     panel_config.mouse_pressed_brightness);
    }
}

void task_update_icon(Task *task)
{
    Panel *panel = task->area.panel;
    if (!panel->g_task.has_icon) {
        if (panel_config.g_task.has_content_tint) {
            Imlib_Image img = task_get_icon(task->win, panel->g_task.icon_size1);
            task_set_icon_color(task, img);
            imlib_context_set_image(img);
            imlib_free_image();
        }
        return;
    }

    task_remove_icon(task);

    Imlib_Image img = task_get_icon(task->win, panel->g_task.icon_size1);
    task_set_icon_color(task, img);

    // transform icons
    imlib_context_set_image(img);
    imlib_image_set_has_alpha(1);
    int w = imlib_image_get_width();
    int h = imlib_image_get_height();
    Imlib_Image orig_image =
        imlib_create_cropped_scaled_image(0, 0, w, h, panel->g_task.icon_size1, panel->g_task.icon_size1);
    imlib_free_image();

    imlib_context_set_image(orig_image);
    task->icon_width = imlib_image_get_width();
    task->icon_height = imlib_image_get_height();
    for (int k = 0; k < TASK_STATE_COUNT; ++k) {
        task->icon[k] = adjust_icon(orig_image,
                                    panel->g_task.alpha[k],
                                    panel->g_task.saturation[k],
                                    panel->g_task.brightness[k]);
        if (panel_config.mouse_effects) {
            task->icon_hover[k] = adjust_icon(task->icon[k],
                                              panel_config.mouse_over_alpha,
                                              panel_config.mouse_over_saturation,
                                              panel_config.mouse_over_brightness);
            task->icon_press[k] = adjust_icon(task->icon[k],
                                              panel_config.mouse_pressed_alpha,
                                              panel_config.mouse_pressed_saturation,
                                              panel_config.mouse_pressed_brightness);
        }
    }
    imlib_context_set_image(orig_image);
    imlib_free_image();

    GPtrArray *task_buttons = get_task_buttons(task->win);
    if (task_buttons) {
        for (int i = 0; i < task_buttons->len; ++i) {
            Task *task2 = (Task *)g_ptr_array_index(task_buttons, i);
            task2->icon_width = task->icon_width;
            task2->icon_height = task->icon_height;
            task2->icon_color = task->icon_color;
            task2->icon_color_hover = task->icon_color_hover;
            task2->icon_color_press = task->icon_color_press;
            for (int k = 0; k < TASK_STATE_COUNT; ++k) {
                task2->icon[k] = task->icon[k];
                task2->icon_hover[k] = task->icon_hover[k];
                task2->icon_press[k] = task->icon_press[k];
            }
            schedule_redraw(&task2->area);
        }
    }
}

// TODO icons look too large when the panel is large
void draw_task_icon(Task *task, int text_width)
{
    if (!task->icon[task->current_state])
        return;

    // Find pos
    Panel *panel = (Panel *)task->area.panel;
    if (panel->g_task.centered) {
        if (panel->g_task.has_text)
            task->_icon_x = (task->area.width - text_width - panel->g_task.icon_size1) / 2;
        else
            task->_icon_x = (task->area.width - panel->g_task.icon_size1) / 2;
    } else {
        task->_icon_x = left_border_width(&task->area) + task->area.paddingxlr * panel->scale;
    }

    // Render

    Imlib_Image image;
    // Render
    if (panel_config.mouse_effects) {
        if (task->area.mouse_state == MOUSE_OVER)
            image = task->icon_hover[task->current_state];
        else if (task->area.mouse_state == MOUSE_DOWN)
            image = task->icon_press[task->current_state];
        else
            image = task->icon[task->current_state];
    } else {
        image = task->icon[task->current_state];
    }

    imlib_context_set_image(image);
    task->_icon_y = (task->area.height - panel->g_task.icon_size1) / 2;
    render_image(task->area.pix, task->_icon_x, task->_icon_y);
}

void draw_task(void *obj, cairo_t *c)
{
    Task *task = (Task *)obj;
    Panel *panel = (Panel *)task->area.panel;

    task->_text_width = 0;
    if (panel->g_task.has_text) {
        PangoContext *context = pango_cairo_create_context(c);
        pango_cairo_context_set_resolution(context, 96 * panel->scale);
        PangoLayout *layout = pango_layout_new(context);
        pango_layout_set_font_description(layout, panel->g_task.font_desc);
        pango_layout_set_text(layout, task->title, -1);

        pango_layout_set_width(layout, (((Taskbar *)task->area.parent)->text_width + TINT2_PANGO_SLACK) * PANGO_SCALE);
        pango_layout_set_height(layout, panel->g_task.text_height * PANGO_SCALE);
        pango_layout_set_wrap(layout, PANGO_WRAP_WORD_CHAR);
        pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_END);

        if (panel->g_task.centered)
            pango_layout_set_alignment(layout, PANGO_ALIGN_CENTER);
        else
            pango_layout_set_alignment(layout, PANGO_ALIGN_LEFT);

        pango_layout_get_pixel_size(layout, &task->_text_width, &task->_text_height);
        task->_text_posy = (panel->g_task.area.height - task->_text_height) / 2.0;

        Color *config_text = &panel->g_task.font[task->current_state];
        draw_text(layout, c, panel->g_task.text_posx, task->_text_posy, config_text, panel->font_shadow ? layout : NULL);

        g_object_unref(layout);
        g_object_unref(context);
    }

    if (panel->g_task.has_icon)
        draw_task_icon(task, task->_text_width);
}

void task_dump_geometry(void *obj, int indent)
{
    Task *task = (Task *)obj;
    Panel *panel = (Panel *)task->area.panel;

    fprintf(stderr,
            "tint2: %*sText: x = %d, y = %d, w = %d, h = %d, align = %s, text = %s\n",
            indent,
            "",
            (int)panel->g_task.text_posx,
            (int)task->_text_posy,
            task->_text_width,
            task->_text_height,
            panel->g_task.centered ? "center" : "left",
            task->title);
    fprintf(stderr,
            "tint2: %*sIcon: x = %d, y = %d, w = h = %d\n",
            indent,
            "",
            task->_icon_x,
            task->_icon_y,
            panel->g_task.icon_size1);
}

void task_get_content_color(void *obj, Color *color)
{
    Task *task = (Task *)obj;
    Color *content_color = NULL;
    if (panel_config.mouse_effects) {
        if (task->area.mouse_state == MOUSE_OVER)
            content_color = &task->icon_color_hover;
        else if (task->area.mouse_state == MOUSE_DOWN)
            content_color = &task->icon_color_press;
        else
            content_color = &task->icon_color;
    } else {
        content_color = &task->icon_color;
    }
    if (content_color)
        *color = *content_color;
}

int task_compute_desired_size(void *obj)
{
    Task *task = (Task *)obj;
    Panel *panel = (Panel *)task->area.panel;
    int size = (panel_horizontal ? panel->g_task.maximum_width : panel->g_task.maximum_height) * panel->scale;
    return size;
}

void on_change_task(void *obj)
{
    Task *task = (Task *)obj;
    Panel *panel = (Panel *)task->area.panel;

    if (task->area.on_screen) {
        long value[] = {panel->posx + task->area.posx,
                        panel->posy + task->area.posy,
                        task->area.width,
                        task->area.height};
        XChangeProperty(server.display,
                        task->win,
                        server.atom._NET_WM_ICON_GEOMETRY,
                        XA_CARDINAL,
                        32,
                        PropModeReplace,
                        (unsigned char *)value,
                        4);
    } else {
        XDeleteProperty(server.display, task->win, server.atom._NET_WM_ICON_GEOMETRY);
    }
}

Task *find_active_task(Task *current_task)
{
    if (active_task == NULL)
        return current_task;

    Taskbar *taskbar = (Taskbar *)current_task->area.parent;

    GList *l0 = taskbar->area.children;
    if (taskbarname_enabled)
        l0 = l0->next;
    for (; l0; l0 = l0->next) {
        Task *task = (Task *)l0->data;
        if (task->win == active_task->win)
            return task;
    }

    return current_task;
}

Task *next_task(Task *task)
{
    if (!task)
        return NULL;

    Taskbar *taskbar = task->area.parent;

    GList *l0 = taskbar->area.children;
    if (taskbarname_enabled)
        l0 = l0->next;
    GList *lfirst_task = l0;
    for (; l0; l0 = l0->next) {
        Task *task1 = l0->data;
        if (task1 == task) {
            l0 = l0->next ? l0->next : lfirst_task;
            return l0->data;
        }
    }

    return NULL;
}

Task *prev_task(Task *task)
{
    if (!task)
        return 0;

    Taskbar *taskbar = task->area.parent;

    Task *task2 = NULL;
    GList *l0 = taskbar->area.children;
    if (taskbarname_enabled)
        l0 = l0->next;
    GList *lfirst_task = l0;
    for (; l0; l0 = l0->next) {
        Task *task1 = l0->data;
        if (task1 == task) {
            if (l0 == lfirst_task) {
                l0 = g_list_last(l0);
                task2 = l0->data;
            }
            return task2;
        }
        task2 = task1;
    }

    return NULL;
}

void reset_active_task()
{
    if (active_task) {
        set_task_state(active_task, window_is_iconified(active_task->win) ? TASK_ICONIFIED : TASK_NORMAL);
        active_task = NULL;
    }

    Window w1 = get_active_window();

    if (w1) {
        if (!get_task_buttons(w1)) {
            Window w2;
            while (XGetTransientForHint(server.display, w1, &w2))
                w1 = w2;
        }
        set_task_state((active_task = get_task(w1)), TASK_ACTIVE);
    }
}

void task_refresh_thumbnail(Task *task)
{
    if (!panel_config.g_task.thumbnail_enabled)
        return;
    if (task->current_state == TASK_ICONIFIED)
        return;
    Panel *panel = (Panel*)task->area.panel;
    double now = get_time();
    if (now - task->thumbnail_last_update < 0.1)
        return;
    if (debug_thumbnails)
        fprintf(stderr, "tint2: thumbnail for window: %s" RESET "\n", task->title ? task->title : "");
    cairo_surface_t *thumbnail = get_window_thumbnail(task->win, panel_config.g_task.thumbnail_width * panel->scale);
    if (!thumbnail)
        return;
    if (task->thumbnail)
        cairo_surface_destroy(task->thumbnail);
    task->thumbnail = thumbnail;
    task->thumbnail_last_update = get_time();
    if (debug_thumbnails)
        fprintf(stderr,
                YELLOW "tint2: %s took %f ms (window: %s)" RESET "\n",
                __func__,
                1000 * (task->thumbnail_last_update - now),
                task->title ? task->title : "");
    if (g_tooltip.mapped && (g_tooltip.area == &task->area)) {
        tooltip_update_contents_for(&task->area);
        tooltip_update();
    }
}

void set_task_state(Task *task, TaskState state)
{
    if (!task || state == TASK_UNDEFINED || state >= TASK_STATE_COUNT)
        return;

    if (!task->thumbnail)
        task_refresh_thumbnail(task);
    if (state == TASK_ACTIVE) {
        // For active windows, we get the thumbnail twice with a small delay in between.
        // This is because they sometimes redraw their windows slowly.
        taskbar_start_thumbnail_timer(THUMB_MODE_ACTIVE_WINDOW);
    }

    if (state == TASK_ACTIVE && task->current_state != state) {
        clock_gettime(CLOCK_MONOTONIC, &task->last_activation_time);
        if (taskbar_sort_method == TASKBAR_SORT_LRU || taskbar_sort_method == TASKBAR_SORT_MRU) {
            GPtrArray *task_buttons = get_task_buttons(task->win);
            if (task_buttons) {
                for (int i = 0; i < task_buttons->len; ++i) {
                    Task *task1 = g_ptr_array_index(task_buttons, i);
                    Taskbar *taskbar = (Taskbar *)task1->area.parent;
                    sort_tasks(taskbar);
                }
            }
        }
    }

    if (task->current_state != state || hide_task_diff_monitor || hide_task_diff_desktop) {
        GPtrArray *task_buttons = get_task_buttons(task->win);
        if (task_buttons) {
            for (int i = 0; i < task_buttons->len; ++i) {
                Task *task1 = g_ptr_array_index(task_buttons, i);
                task1->current_state = state;
                task1->area.bg = panels[0].g_task.background[state];
                free_area_gradient_instances(&task1->area);
                instantiate_area_gradients(&task1->area);
                schedule_redraw(&task1->area);
                if (state == TASK_ACTIVE && g_slist_find(urgent_list, task1))
                    del_urgent(task1);
                gboolean hide = FALSE;
                Taskbar *taskbar = (Taskbar *)task1->area.parent;
                if (task->desktop == ALL_DESKTOPS && server.desktop != taskbar->desktop) {
                    // Hide ALL_DESKTOPS task on non-current desktop
                    hide = !always_show_all_desktop_tasks;
                }
                if (hide_inactive_tasks) {
                    // Show only the active task
                    if (state != TASK_ACTIVE) {
                        hide = TRUE;
                    }
                }
                if (hide_task_diff_desktop) {
                    if (taskbar->desktop != server.desktop)
                        hide = TRUE;
                }
                if (get_window_monitor(task->win) != ((Panel *)task->area.panel)->monitor &&
                    (hide_task_diff_monitor || num_panels > 1)) {
                    hide = TRUE;
                }
                if ((!hide) != task1->area.on_screen) {
                    task1->area.on_screen = !hide;
                    schedule_redraw(&task1->area);
                    Panel *p = (Panel *)task->area.panel;
                    task->area.resize_needed = TRUE;
                    p->taskbar->area.resize_needed = TRUE;
                    p->area.resize_needed = TRUE;
                }
            }
            schedule_panel_redraw();
        }
    }
}

void blink_urgent(void *arg)
{
    GSList *urgent_task = urgent_list;
    while (urgent_task) {
        Task *t = urgent_task->data;
        if (t->urgent_tick <= max_tick_urgent) {
            if (++t->urgent_tick % 2)
                set_task_state(t, TASK_URGENT);
            else
                set_task_state(t, window_is_iconified(t->win) ? TASK_ICONIFIED : TASK_NORMAL);
        }
        urgent_task = urgent_task->next;
    }
    schedule_panel_redraw();
}

void add_urgent(Task *task)
{
    if (!task)
        return;

    // some programs set urgency hint although they are active
    if (active_task && active_task->win == task->win)
        return;

    task = get_task(task->win); // always add the first task for the task buttons (omnipresent windows)
    task->urgent_tick = 0;
    if (g_slist_find(urgent_list, task))
        return;

    // not yet in the list, so we have to add it
    urgent_list = g_slist_prepend(urgent_list, task);

    if (!urgent_timer.enabled_)
        change_timer(&urgent_timer, true, 10, 1000, blink_urgent, 0);

    Panel *panel = task->area.panel;
    if (panel->is_hidden)
        autohide_show(panel);
}

void del_urgent(Task *task)
{
    urgent_list = g_slist_remove(urgent_list, task);
    if (!urgent_list)
        stop_timer(&urgent_timer);
}

void task_handle_mouse_event(Task *task, MouseAction action)
{
    if (!task)
        return;
    switch (action) {
    case NONE:
        break;
    case CLOSE:
        close_window(task->win);
        break;
    case TOGGLE:
        activate_window(task->win);
        break;
    case ICONIFY:
        XIconifyWindow(server.display, task->win, server.screen);
        break;
    case TOGGLE_ICONIFY:
        if (active_task && task->win == active_task->win)
            XIconifyWindow(server.display, task->win, server.screen);
        else
            activate_window(task->win);
        break;
    case SHADE:
        toggle_window_shade(task->win);
        break;
    case MAXIMIZE_RESTORE:
        toggle_window_maximized(task->win);
        break;
    case MAXIMIZE:
        toggle_window_maximized(task->win);
        break;
    case RESTORE:
        toggle_window_maximized(task->win);
        break;
    case DESKTOP_LEFT: {
        if (task->desktop == 0)
            break;
        int desktop = task->desktop - 1;
        change_window_desktop(task->win, desktop);
        if (desktop == server.desktop)
            activate_window(task->win);
        break;
    }
    case DESKTOP_RIGHT: {
        if (task->desktop == server.num_desktops)
            break;
        int desktop = task->desktop + 1;
        change_window_desktop(task->win, desktop);
        if (desktop == server.desktop)
            activate_window(task->win);
        break;
    }
    case NEXT_TASK: {
        Task *task1 = next_task(find_active_task(task));
        activate_window(task1->win);
    } break;
    case PREV_TASK: {
        Task *task1 = prev_task(find_active_task(task));
        activate_window(task1->win);
    }
    }
}

void task_update_desktop(Task *task)
{
    Window win = task->win;
    remove_task(task);
    task = add_task(win);
    reset_active_task();
    schedule_panel_redraw();
}
