#define _ISOC99_SOURCE /* lrint() */
#define _GNU_SOURCE /* exp10() */

#include "volumealsa.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <sys/wait.h>

#define DEBUG g_message

// FIXME : icon size must change with panel height.
#define ICON_SIZE 22
#define ICON_BUTTON_TRIM 2

XFCE_PANEL_PLUGIN_REGISTER_INTERNAL(volumealsa_constructor);

/* Plugin constructor. */
static void
volumealsa_constructor(XfcePanelPlugin *plugin)
{
    /* Allocate and initialize plugin context and set into Plugin private data pointer. */
    VolumeALSAPlugin * vol = g_new0(VolumeALSAPlugin, 1);
    GtkWidget *p;

    vol->master_element = NULL;

    /* Initialize ALSA if default device isn't Bluetooth */
    asound_initialize (vol, "hw:0");
    // set up callbacks to see if BlueZ is on DBus
    g_bus_watch_name (G_BUS_TYPE_SYSTEM, "org.bluez", 0, cb_name_owned, cb_name_unowned, vol, NULL);

    /* Allocate top level widget and set into Plugin widget pointer. */
    vol->plugin = p = gtk_button_new();
    gtk_button_set_relief (GTK_BUTTON (vol->plugin), GTK_RELIEF_NONE);
	gtk_container_add(GTK_CONTAINER(plugin), p);
    xfce_panel_plugin_add_action_widget (plugin, p);
    gtk_widget_set_tooltip_text(p, "Volume control");

    /* Allocate icon as a child of top level. */
    vol->tray_icon = gtk_image_new();
    gtk_container_add(GTK_CONTAINER(p), vol->tray_icon);

    /* Connect signals. */
    g_signal_connect(G_OBJECT(p), "scroll-event", G_CALLBACK(volumealsa_popup_scale_scrolled), vol );
    g_signal_connect(G_OBJECT(p), "clicked", G_CALLBACK(on_mouse_click), NULL);
    g_signal_connect (G_OBJECT (plugin), "free-data", G_CALLBACK (volumealsa_destructor), (gpointer) vol);

    /* Update the display, show the widget, and return. */
    volumealsa_update_display(vol);
    gtk_widget_show_all(p);
    // To switch to analog (NUM=1) or hdmi(NUM=2) run this commands amixer -q cset numid=3 NUM
}

/* Plugin destructor. */
static void volumealsa_destructor(XfcePanelPlugin *plugin, gpointer user_data)
{
    VolumeALSAPlugin * vol = (VolumeALSAPlugin *) user_data;

    asound_deinitialize(vol);

    if (vol->restart_idle)
        g_source_remove(vol->restart_idle);

    //if (vol) /* SF bug #683: crash if constructor failed */
    //g_signal_handlers_disconnect_by_func(panel_get_icon_theme(vol->panel),
    //                                     volumealsa_theme_change, vol);

    /* Deallocate all memory. */
    g_free(vol);
}

// Icon and tooltip are autamatically updated by asound_mixer_event callback
static void
volumealsa_update_display(VolumeALSAPlugin * vol)
{
    gboolean mute;
    int level;
    /* Mute status. */
    mute = asound_is_muted(vol);
    level = asound_get_volume(vol);
    if (mute) level = 0;

    /* Change icon according to mute / volume */
    const char* icon="audio-volume-high";
    if (level==0)
    {
         icon="audio-volume-muted";
    }
    else if (level < 33)
    {
         icon="audio-volume-low";
    }
    else if (level < 66)
    {
         icon="audio-volume-medium";
    }
    vol->icon = icon;

    /* Change icon, fallback to default icon if theme doesn't exsit */
    //set_icon (vol->tray_icon, vol->icon, 0); // this function embeded below
    GdkPixbuf *pixbuf;
	GtkIconTheme *theme = gtk_icon_theme_get_default();
    int size = ICON_SIZE - ICON_BUTTON_TRIM;
    if (gtk_icon_theme_has_icon (theme, icon))
    {
        GtkIconInfo *info = gtk_icon_theme_lookup_icon (theme, icon, size, GTK_ICON_LOOKUP_FORCE_SIZE);
        pixbuf = gtk_icon_info_load_icon (info, NULL);
        if (pixbuf != NULL)
        {
            gtk_image_set_from_pixbuf (GTK_IMAGE (vol->tray_icon), pixbuf);
            g_object_unref (pixbuf);
        }
        gtk_icon_info_free (info); // Using g_object_unref Causes segfault
    }
    /* Display current level in tooltip. */
    char * tooltip = g_strdup_printf("Volume %d%%", level);
    gtk_widget_set_tooltip_text(vol->plugin, tooltip);
    g_free(tooltip);
}

static gboolean
on_mouse_click (GtkButton *button, gpointer user_data)
{
	if ( fork() == 0) {
		wait(NULL);
	} else {
        char* argv[] = {"lxterminal", "-e", "alsamixer", NULL};
		execvp("lxterminal", argv);
		perror("execvp");
	}
    return TRUE;
}

/* Handler for "scroll-event" signal */
static void
volumealsa_popup_scale_scrolled (GtkWidget * widget, GdkEventScroll * evt, VolumeALSAPlugin * vol)
{
    if (asound_is_muted(vol))
        return;
    int val = asound_get_volume(vol);
    /* Dispatch on scroll direction to update the value. */
    if ((evt->direction == GDK_SCROLL_UP) || (evt->direction == GDK_SCROLL_LEFT))
    {
        val += 2;
        if (val > 100) val = 100 ;
    }
    else {
        val -= 2;
        if (val < 0) val = 0 ;
    }
    asound_set_volume(vol, val);
}


/* Initialize the ALSA interface. */
static gboolean asound_initialize(VolumeALSAPlugin * vol, const char *device)
{
    // make sure existing watches are removed by calling deinit
    asound_deinitialize (vol);

    DEBUG("initializing card : %s", device);
    strcpy(vol->device, device);
    // Access the specified device.
    snd_mixer_open(&vol->mixer, 0);
    if (snd_mixer_attach(vol->mixer, device))
    {
        g_warning ("volumealsa: Couldn't attach mixer - attach to hw:0");
        strcpy(vol->device, "hw:0");
        snd_mixer_attach(vol->mixer, "hw:0");
    }

    snd_mixer_selem_register(vol->mixer, NULL, NULL);
    snd_mixer_load(vol->mixer);
    /* Find Master element, or Front element, or PCM element, or LineOut element.
     * If one of these succeeds, master_element is valid. */
    asound_find_elements (vol);

    /* Listen to events from ALSA. */
    int n_fds = snd_mixer_poll_descriptors_count(vol->mixer);
    struct pollfd *fds = g_new0(struct pollfd, n_fds);
    snd_mixer_poll_descriptors(vol->mixer, fds, n_fds);

    vol->channels = g_new0(GIOChannel *, n_fds);
    vol->watches = g_new0(guint, n_fds);
    vol->num_channels = n_fds;

    for (int i = 0; i < n_fds; ++i)
    {
        GIOChannel* channel = g_io_channel_unix_new(fds[i].fd);
        vol->watches[i] = g_io_add_watch(channel, G_IO_IN | G_IO_HUP, asound_mixer_event, vol);
        vol->channels[i] = channel;
    }
    g_free(fds);

    vol->card_attached = 1;
    if (strcmp(device, "bluealsa")==0) {
        asound_set_volume(vol, 50);
    }
    switch_output_device(vol->device);

    return TRUE;
}

static void asound_deinitialize(VolumeALSAPlugin * vol)
{
    guint i;

    if (vol->mixer_evt_idle != 0) {
        g_source_remove(vol->mixer_evt_idle);
        vol->mixer_evt_idle = 0;
    }
    for (i = 0; i < vol->num_channels; i++) {
        g_source_remove(vol->watches[i]);
        g_io_channel_shutdown(vol->channels[i], FALSE, NULL);
        g_io_channel_unref(vol->channels[i]);
    }
    g_free(vol->channels);
    g_free(vol->watches);
    vol->channels = NULL;
    vol->watches = NULL;
    vol->num_channels = 0;

    if (vol->mixer)
    {
        if (vol->device) snd_mixer_detach (vol->mixer, vol->device);
        snd_mixer_close(vol->mixer);
    }
    vol->master_element = NULL;
    vol->mixer = NULL;
}


static gboolean asound_find_elements(VolumeALSAPlugin * vol)
{
    const char *name;
    for (
      vol->master_element = snd_mixer_first_elem(vol->mixer);
      vol->master_element != NULL;
      vol->master_element = snd_mixer_elem_next(vol->master_element))
    {
        if ((snd_mixer_selem_is_active(vol->master_element)))
        {
            name = snd_mixer_selem_get_name(vol->master_element);
            if (!strncasecmp (name, "Master", 6)) return TRUE;
            if (!strncasecmp (name, "Front", 5)) return TRUE;
            if (!strncasecmp (name, "PCM", 3)) return TRUE;
            if (!strncasecmp (name, "LineOut", 7)) return TRUE;
            if (!strncasecmp (name, "Digital", 7)) return TRUE;
            if (!strncasecmp (name, "Headphone", 9)) return TRUE;
            if (!strncasecmp (name, "Speaker", 7)) return TRUE;
            if (!strncasecmp (name + strlen(name) - 4, "a2dp", 4)) return TRUE;
        }
    }
    return FALSE;
}

/* Get the condition of the mute control from the sound system. */
static gboolean asound_is_muted(VolumeALSAPlugin * vol)
{
    /* The switch is on if sound is not muted, and off if the sound is muted.
     * Initialize so that the sound appears unmuted if the control does not exist. */
    int value = 1;
    if (vol->master_element != NULL)
        snd_mixer_selem_get_playback_switch(vol->master_element, 0, &value);
    return (value == 0);
}

/* Get the volume from the sound system.
 * This implementation returns the average of the Front Left and Front Right channels. */
static int asound_get_volume(VolumeALSAPlugin * vol)
{
    double aleft = 0;
    double aright = 0;
    if (vol->master_element != NULL)
    {
        aleft = get_normalized_volume(vol->master_element, SND_MIXER_SCHN_FRONT_LEFT);
        aright = get_normalized_volume(vol->master_element, SND_MIXER_SCHN_FRONT_RIGHT);
    }
    return (int)round((aleft + aright) * 50);
}

/* Set the volume to the sound system.
 * This implementation sets the Front Left and Front Right channels to the specified value. */
static void asound_set_volume(VolumeALSAPlugin * vol, int volume)
{
    int dir = volume - asound_get_volume(vol);
    double vol_perc = (double)volume / 100;

    if (vol->master_element != NULL)
    {
        set_normalized_volume(vol->master_element, SND_MIXER_SCHN_FRONT_LEFT, vol_perc, dir);
        set_normalized_volume(vol->master_element, SND_MIXER_SCHN_FRONT_RIGHT, vol_perc, dir);
    }
}

static double get_normalized_volume(snd_mixer_elem_t *elem,
                    snd_mixer_selem_channel_id_t channel)
{
    long min, max, value;
    double normalized, min_norm;
    int err;

    err = snd_mixer_selem_get_playback_dB_range(elem, &min, &max);
    if (err < 0 || min >= max) {
        err = snd_mixer_selem_get_playback_volume_range(elem, &min, &max);
        if (err < 0 || min == max)
            return 0;

        err = snd_mixer_selem_get_playback_volume(elem, channel, &value);
        if (err < 0)
            return 0;

        return (value - min) / (double)(max - min);
    }

    err = snd_mixer_selem_get_playback_dB(elem, channel, &value);
    if (err < 0)
        return 0;

    if (use_linear_dB_scale(min, max))
        return (value - min) / (double)(max - min);

    normalized = exp10((value - max) / 6000.0);
    if (min != SND_CTL_TLV_DB_GAIN_MUTE) {
        min_norm = exp10((min - max) / 6000.0);
        normalized = (normalized - min_norm) / (1 - min_norm);
    }

    return normalized;
}

static int set_normalized_volume(snd_mixer_elem_t *elem,
                 snd_mixer_selem_channel_id_t channel,
                 double volume,
                 int dir)
{
    long min, max, value;
    double min_norm;
    int err;

    err = snd_mixer_selem_get_playback_dB_range(elem, &min, &max);
    if (err < 0 || min >= max) {
        err = snd_mixer_selem_get_playback_volume_range(elem, &min, &max);
        if (err < 0)
            return err;

        value = lrint_dir(volume * (max - min), dir) + min;
        return snd_mixer_selem_set_playback_volume(elem, channel, value);
    }

    if (use_linear_dB_scale(min, max)) {
        value = lrint_dir(volume * (max - min), dir) + min;
        return snd_mixer_selem_set_playback_dB(elem, channel, value, dir);
    }

    if (min != SND_CTL_TLV_DB_GAIN_MUTE) {
        min_norm = exp10((min - max) / 6000.0);
        volume = volume * (1 - min_norm) + min_norm;
    }
    value = lrint_dir(6000.0 * log10(volume), dir) + max;
    return snd_mixer_selem_set_playback_dB(elem, channel, value, dir);
}


/* Handler for I/O event on ALSA channel. */
static gboolean asound_mixer_event(GIOChannel * channel, GIOCondition cond, gpointer vol_gpointer)
{
    VolumeALSAPlugin * vol = (VolumeALSAPlugin *) vol_gpointer;
    int res = 0;

    if (g_source_is_destroyed(g_main_current_source()))
        return FALSE;

    if (vol->mixer_evt_idle == 0)
    {
        vol->mixer_evt_idle = g_idle_add_full(G_PRIORITY_DEFAULT, (GSourceFunc) asound_reset_mixer_evt_idle, vol, NULL);
        if (vol->mixer) res = snd_mixer_handle_events(vol->mixer);
    }

    if (cond & G_IO_IN)
    {
        /* the status of mixer is changed. update of display is needed. */
        /* don't do this if res > 1, as that seems to happen if a BT device has disconnected... */
        DEBUG("mixer event : gio in %d", res);
        if (res < 2) {
            if (vol->card_attached) volumealsa_update_display(vol);
        }
        // dirty workaround for crash when bluetooth is detached
        else if (res == 10){    // bluetooh has detached
            vol->card_attached = 0;
        }
    }

    if ((cond & G_IO_HUP) || (res < 0))
    {
        /* This means there're some problems with alsa. */
        g_warning("volumealsa: ALSA had a problem: "
                " snd_mixer_handle_events() = %d,"
                " cond 0x%x (IN: 0x%x, HUP: 0x%x).", res, cond, G_IO_IN, G_IO_HUP);

        if (vol->restart_idle == 0)
            vol->restart_idle = g_timeout_add_seconds(1, asound_restart, vol);

        return FALSE;
    }

    return TRUE;
}

static gboolean asound_reset_mixer_evt_idle(VolumeALSAPlugin * vol)
{
    if (!g_source_is_destroyed(g_main_current_source()))
        vol->mixer_evt_idle = 0;
    return FALSE;
}

static gboolean asound_restart(gpointer vol_gpointer)
{
    VolumeALSAPlugin * vol = vol_gpointer;

    if (!g_main_current_source()) return TRUE;
    if (g_source_is_destroyed(g_main_current_source()))
        return FALSE;

    if (!asound_initialize(vol, vol->device)) {
        g_warning("volumealsa: Re-initialization failed.");
        return TRUE; // try again in a second
    }

    g_warning("volumealsa: Restarted ALSA interface...");

    vol->restart_idle = 0;
    return FALSE;
}

static long lrint_dir(double x, int dir)
{
    if (dir > 0)
        return lrint(ceil(x));
    else if (dir < 0)
        return lrint(floor(x));
    else
        return lrint(x);
}

// bluetooth
static void cb_name_owned (GDBusConnection *connection, const gchar *name, const gchar *owner, gpointer user_data)
{
    VolumeALSAPlugin *vol = (VolumeALSAPlugin *) user_data;

    DEBUG ("Name %s owned on DBus", name);

    /* BlueZ exists - get an object manager for it */
    GError *error = NULL;
    vol->objmanager = g_dbus_object_manager_client_new_for_bus_sync (G_BUS_TYPE_SYSTEM, 0, "org.bluez", "/", NULL, NULL, NULL, NULL, &error);
    if (error)
    {
        DEBUG ("Error getting object manager - %s", error->message);
        vol->objmanager = NULL;
        g_error_free (error);
    }
    else
    {
        /* register callbacks for devices being added or removed */
        g_signal_connect (vol->objmanager, "object-added", G_CALLBACK (cb_object_added), vol);
        g_signal_connect (vol->objmanager, "object-removed", G_CALLBACK (cb_object_removed), vol);
    }
}

static void cb_name_unowned (GDBusConnection *connection, const gchar *name, gpointer user_data)
{
    VolumeALSAPlugin *vol = (VolumeALSAPlugin *) user_data;
    DEBUG ("Name %s unowned on DBus", name);

    if (vol->objmanager) g_object_unref (vol->objmanager);
    vol->objmanager = NULL;
}

static void cb_object_added (GDBusObjectManager *manager, GDBusObject *object, gpointer user_data)
{
    VolumeALSAPlugin *vol = (VolumeALSAPlugin *) user_data;
    char device[20];
    const char *obj = g_dbus_object_get_object_path (object);
    DEBUG("added : %s", obj);
    if (get_bt_device_id (device) && strstr (obj, device))
    {
        DEBUG ("Selected Bluetooth audio device has connected");
        asound_initialize (vol, "bluealsa");
        volumealsa_update_display (vol);
    }
}

static void cb_object_removed (GDBusObjectManager *manager, GDBusObject *object, gpointer user_data)
{
    VolumeALSAPlugin *vol = (VolumeALSAPlugin *) user_data;
    char device[20];
    const char *obj = g_dbus_object_get_object_path (object);
    if (get_bt_device_id (device) && strstr (obj, device))
    {
        DEBUG ("Selected Bluetooth audio device has disconnected");
        asound_initialize (vol, "hw:0");
        volumealsa_update_display (vol);
    }
}

// get bluetooth device MAC id in .asoundrc
static int get_bt_device_id (char *id)
{
    char *user_config_file, *ptr, buffer[64];
    FILE *fp;
    int count;

    user_config_file = g_build_filename (g_get_home_dir (), "/.asoundrc", NULL);
    fp = fopen (user_config_file, "rb");
    g_free (user_config_file);

    if (!fp) return 0;

    while (fgets (buffer, sizeof (buffer), fp))
    {
        ptr = strstr (buffer, "device");
        if (ptr)
        {
            // find the opening quote
            while (*ptr && *ptr != '"') ptr++;
            if (*ptr == '"')
            {
                // there should be another quote at the end, 18 chars later
                if (*(ptr + 18) == '"')
                {
                    // replace : with _
                    for (count = 1; count < 6; count++) *(ptr + (count * 3)) = '_';

                    // copy and terminate
                    strncpy (id, ptr + 1, 17);
                    id[17] = 0;

                    fclose (fp);
                    return 1;
                }
            }
        }
    }

    fclose (fp);
    return 0;
}

// swiches to bluetoorh or hardware device
void switch_output_device(char *device)
{
    char *user_config_file = g_build_filename(g_get_home_dir (), "/.asoundrc", NULL);
    // should check if file exists
    char cmdbuf[256];
    if (strcmp(device, "hw:0")==0) {
        sprintf (cmdbuf, "sed -i 's/slave.pcm \"btreceiver\"/slave.pcm \"hw\"/g' %s", user_config_file);
        system (cmdbuf);
    }
    else if (strcmp(device, "bluealsa")==0) {
        sprintf (cmdbuf, "sed -i 's/slave.pcm \"hw\"/slave.pcm \"btreceiver\"/g' %s", user_config_file);
        system (cmdbuf);
    }
}
