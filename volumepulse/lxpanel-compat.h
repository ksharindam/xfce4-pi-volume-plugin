#pragma once
#include <libxfce4panel/libxfce4panel.h>

void
lxpanel_plugin_set_taskbar_icon(XfcePanelPlugin *panel, GtkWidget *image, const char *name);

void
lxpanel_plugin_set_menu_icon(XfcePanelPlugin *panel, GtkWidget *image, const char *name);

void lxpanel_plugin_popup_set_position_helper(XfcePanelPlugin *panel, GtkWidget *attach_to,
                                            GtkWidget *popup, int *x, int *y);

//void lxpanel_plugin_set_data(GtkWidget *plugin, gpointer data);
#define lxpanel_plugin_set_data(plugin, data, _destroy_func) \
do {\
    g_object_set_data(G_OBJECT(plugin), "plugin-data", data);\
    g_signal_connect (G_OBJECT(plugin), "free-data", G_CALLBACK(_destroy_func), (gpointer) data);\
} while (0);

gpointer lxpanel_plugin_get_data(GtkWidget *plugin);

GtkIconTheme* panel_get_icon_theme(XfcePanelPlugin *panel);
