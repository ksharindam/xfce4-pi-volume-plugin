#include "lxpanel-compat.h"

void
lxpanel_plugin_set_taskbar_icon(XfcePanelPlugin *panel, GtkWidget *image, const char *name)
{
    int size = xfce_panel_plugin_get_size(panel);
    GtkIconTheme *icon_theme = gtk_icon_theme_get_default();
    GdkPixbuf *pixbuf = xfce_panel_pixbuf_from_source(name, icon_theme, size-2);
    gtk_image_set_from_pixbuf (GTK_IMAGE (image), pixbuf);
    if (pixbuf != NULL) g_object_unref (pixbuf);
    gtk_widget_set_size_request(GTK_WIDGET(panel), size+4, size);
}

void lxpanel_plugin_set_menu_icon(XfcePanelPlugin *panel, GtkWidget *image, const char *name)
{
    if (strlen(name)==0) {
        gtk_image_clear(GTK_IMAGE (image));
        return;
    }
    gtk_image_set_from_icon_name (GTK_IMAGE (image), name, GTK_ICON_SIZE_MENU);
}

void lxpanel_plugin_popup_set_position_helper(XfcePanelPlugin *panel, GtkWidget *attach_to,
                                            GtkWidget *popup, int *x, int *y)
{
    xfce_panel_plugin_position_widget(panel, popup, attach_to, x, y);
}

gpointer lxpanel_plugin_get_data(GtkWidget *plugin)
{
    return g_object_get_data(G_OBJECT(plugin), "plugin-data");
}

GtkIconTheme* panel_get_icon_theme(XfcePanelPlugin *panel)
{
    return gtk_icon_theme_get_default();
}
