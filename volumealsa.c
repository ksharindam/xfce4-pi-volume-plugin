#define _ISOC99_SOURCE /* lrint() */
#define _GNU_SOURCE /* exp10() */

#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <sys/wait.h>
#include <alsa/asoundlib.h>
#include <libxfce4panel/xfce-panel-plugin.h>

// FIXME : icon size must change with panel height.
#define ICON_SIZE 22
#define ICON_BUTTON_TRIM 4


#ifdef __UCLIBC__
/* 10^x = 10^(log e^x) = (e^x)^log10 = e^(x * log 10) */
#define exp10(x) (exp((x) * log(10)))
#endif /* __UCLIBC__ */

#define MAX_LINEAR_DB_SCALE 24

typedef struct {

    /* Graphics. */
    GtkWidget * plugin;             /* Back pointer to the push-button widget */
    GtkWidget * tray_icon;          // GtkImage that shows icon
    /* ALSA interface. */
    snd_mixer_t * mixer;            /* The mixer */
    snd_mixer_elem_t * master_element;      /* The Master element */
    guint mixer_evt_idle;           /* Timer to handle restarting poll */
    guint restart_idle;
    /* unloading and error handling */
    GIOChannel **channels;                      /* Channels that we listen to */
    guint *watches;                             /* Watcher IDs for channels */
    guint num_channels;                         /* Number of channels */

    /* Icons */
    const char* icon;

} VolumeALSAPlugin;

static void volumealsa_constructor(XfcePanelPlugin *plugin);
static void volumealsa_destructor(XfcePanelPlugin *plugin, gpointer user_data);
static void volumealsa_update_display(VolumeALSAPlugin * vol);
static void volumealsa_popup_scale_scrolled (GtkWidget * widget, GdkEventScroll * evt, VolumeALSAPlugin * vol);
static void asound_get_default_card (char *id);
static gboolean asound_initialize(VolumeALSAPlugin * vol);
static gboolean asound_is_muted(VolumeALSAPlugin * vol);
static int asound_get_volume(VolumeALSAPlugin * vol);
static void asound_set_volume(VolumeALSAPlugin * vol, int volume);
static void volumealsa_update_current_icon(VolumeALSAPlugin * vol);
void set_icon (GtkWidget *image, const char *icon, int size);
static void asound_deinitialize(VolumeALSAPlugin * vol);
static void asound_find_valid_device ();
static gboolean asound_find_elements(VolumeALSAPlugin * vol);
static gboolean asound_mixer_event(GIOChannel * channel, GIOCondition cond, gpointer vol_gpointer);
static double get_normalized_volume(snd_mixer_elem_t *elem, snd_mixer_selem_channel_id_t channel);
static int set_normalized_volume(snd_mixer_elem_t *elem, snd_mixer_selem_channel_id_t channel, double volume, int dir);
static gboolean asound_set_bcm_card (void);
static void asound_set_default_card (const char *id);
static gboolean asound_reset_mixer_evt_idle(VolumeALSAPlugin * vol);
static gboolean asound_restart(gpointer vol_gpointer);
static inline gboolean use_linear_dB_scale(long dBmin, long dBmax);
static long lrint_dir(double x, int dir);
static gboolean volumealsa_get_bcm_device_id (gchar *id);
static gboolean volumealsa_is_bcm_device (int num);
static gboolean on_mouse_click (GtkButton *button, gpointer user_data);

XFCE_PANEL_PLUGIN_REGISTER_INTERNAL(volumealsa_constructor);

/* Plugin constructor. */
static void
volumealsa_constructor(XfcePanelPlugin *plugin)
{
    /* Allocate and initialize plugin context and set into Plugin private data pointer. */
    VolumeALSAPlugin * vol = g_new0(VolumeALSAPlugin, 1);
    GtkWidget *p;
    char buffer[128];

    vol->master_element = NULL;

    /* Initialize ALSA if default device isn't Bluetooth */
    asound_get_default_card (buffer);
    if (strcmp (buffer, "bluealsa")) asound_initialize (vol);

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
    //g_signal_connect (G_OBJECT (plugin), "size-changed", G_CALLBACK(on_size_change), NULL);
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

/* Do a full redraw of the display. */
static void
volumealsa_update_display(VolumeALSAPlugin * vol)
{
    gboolean mute;
    int level;

    /* Mute status. */
    mute = asound_is_muted(vol);
    level = asound_get_volume(vol);
    if (mute) level = 0;

    volumealsa_update_current_icon(vol);

    /* Change icon, fallback to default icon if theme doesn't exsit */
    set_icon (vol->tray_icon, vol->icon, 0);
    /* Display current level in tooltip. */
    char * tooltip = g_strdup_printf("%s %d%%", "Volume", level);
    gtk_widget_set_tooltip_text(vol->plugin, tooltip);
    g_free(tooltip);
}

static gboolean
on_mouse_click (GtkButton *button, gpointer user_data)
{
	if ( fork() == 0) {
		wait(NULL);
	} else {
        char arg0[20] = "x-terminal-emulator";
        char arg1[3] =  "-e";
        char arg2[10] = "alsamixer";
        char* const  argv[4] = {arg0, arg1, arg2, NULL};
		execvp(arg0, argv);
		perror("execvp");
	}
    return TRUE;    
}

/* Handler for "scroll-event" signal on popup window vertical scale. */
static void
volumealsa_popup_scale_scrolled (GtkWidget * widget, GdkEventScroll * evt, VolumeALSAPlugin * vol)
{
    /* Get the state of the vertical scale. */
    int val = asound_get_volume(vol);
    /* Dispatch on scroll direction to update the value. */
    if ((evt->direction == GDK_SCROLL_UP) || (evt->direction == GDK_SCROLL_LEFT))
        {
        val += 2;
        if (val > 100) val = 100 ;
        }
    else
        {
        val -= 2;
        if (val < 0) val = 0 ;
        }
    if (!asound_is_muted (vol))
        asound_set_volume(vol, val);

    // Icon and tooltip are autamatically updated by asound_mixer_event callback 
    
}

static void asound_get_default_card (char *id)
{
  char tokenbuf[256], type[16], cid[16], state = 0, indef = 0;
  char *bufptr = tokenbuf;
  int inchar, count;
  char *user_config_file = g_build_filename (g_get_home_dir (), "/.asoundrc", NULL);
  FILE *fp = fopen (user_config_file, "rb");
  type[0] = 0;
  cid[0] = 0;
  if (fp)
  {
    count = 0;
    while ((inchar = fgetc (fp)) != EOF)
    {
        if (inchar == ' ' || inchar == '\t' || inchar == '\n' || inchar == '\r')
        {
            if (bufptr != tokenbuf)
            {
                *bufptr = 0;
                switch (state)
                {
                    case 1 :    strcpy (type, tokenbuf);
                                state = 0;
                                break;
                    case 2 :    strcpy (cid, tokenbuf);
                                state = 0;
                                break;
                    default :   if (!strcmp (tokenbuf, "type") && indef) state = 1;
                                else if (!strcmp (tokenbuf, "card") && indef) state = 2;
                                else if (!strcmp (tokenbuf, "pcm.!default")) indef = 1;
                                else if (!strcmp (tokenbuf, "}")) indef = 0;
                                break;
                }
                bufptr = tokenbuf;
                count = 0;
                if (cid[0] && type[0]) break;
            }
            else
            {
                bufptr = tokenbuf;
                count = 0;
            }
        }
        else
        {
            if (count < 255)
            {
                *bufptr++ = inchar;
                count++;
            }
            else tokenbuf[255] = 0;
        }
    }
    fclose (fp);
  }
  if (!strcmp (type, "bluealsa")) sprintf (id, "bluealsa");
  else if (cid[0] && type[0]) sprintf (id, "%s:%s", type, cid);
  else sprintf (id, "hw:0");
  g_free (user_config_file);
}

/* Initialize the ALSA interface. */
static gboolean asound_initialize(VolumeALSAPlugin * vol)
{
    char device[32];

    asound_get_default_card (device);

    /* Access the "default" device. */
    snd_mixer_open(&vol->mixer, 0);
    if (snd_mixer_attach(vol->mixer, device))
    {
        g_warning ("volumealsa: Couldn't attach mixer - looking for another valid device");
        asound_find_valid_device ();
        asound_get_default_card (device);
        snd_mixer_attach(vol->mixer, device);
    }
    snd_mixer_selem_register(vol->mixer, NULL, NULL);
    snd_mixer_load(vol->mixer);

    /* Find Master element, or Front element, or PCM element, or LineOut element.
     * If one of these succeeds, master_element is valid. */
    if (!asound_find_elements (vol))
    {
        // this is a belt-and-braces check in case a driver has become corrupt...
        g_warning ("volumealsa: Can't find elements - trying to reset to internal");
        snd_mixer_detach (vol->mixer, device);
        snd_mixer_free (vol->mixer);
        asound_find_valid_device ();
        asound_get_default_card (device);
        snd_mixer_open (&vol->mixer, 0);
        snd_mixer_attach (vol->mixer, device);
        snd_mixer_selem_register (vol->mixer, NULL, NULL);
        snd_mixer_load (vol->mixer);
        if (!asound_find_elements (vol)) return FALSE;
   }

    /* Listen to events from ALSA. */
    int n_fds = snd_mixer_poll_descriptors_count(vol->mixer);
    struct pollfd * fds = g_new0(struct pollfd, n_fds);

    vol->channels = g_new0(GIOChannel *, n_fds);
    vol->watches = g_new0(guint, n_fds);
    vol->num_channels = n_fds;

    snd_mixer_poll_descriptors(vol->mixer, fds, n_fds);
    int i;
    for (i = 0; i < n_fds; ++i)
    {
        GIOChannel* channel = g_io_channel_unix_new(fds[i].fd);
        vol->watches[i] = g_io_add_watch(channel, G_IO_IN | G_IO_HUP, asound_mixer_event, vol);
        vol->channels[i] = channel;
    }
    g_free(fds);
    return TRUE;
}

/* Get the condition of the mute control from the sound system. */
static gboolean
asound_is_muted(VolumeALSAPlugin * vol)
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

/*** Graphics ***/

static void volumealsa_update_current_icon(VolumeALSAPlugin * vol)
{
    gboolean mute;
    int level;

    /* Mute status. */
    mute = asound_is_muted(vol);
    level = asound_get_volume(vol);

    /* Change icon according to mute / volume */
    const char* icon="audio-volume-muted";
    if (mute)
    {
         icon="audio-volume-muted";
    }
    else if (level >= 66)
    {
         icon="audio-volume-high";
    }
    else if (level >= 33)
    {
         icon="audio-volume-medium";
    }
    else if (level > 2)
    {
         icon="audio-volume-low";
    }

    vol->icon = icon;
}

void set_icon (GtkWidget *image, const char *icon, int size)
{
    GdkPixbuf *pixbuf;
	GtkIconTheme *theme = gtk_icon_theme_get_default();
    if (size == 0) size = ICON_SIZE - ICON_BUTTON_TRIM;
    if (gtk_icon_theme_has_icon (theme, icon))
    {
        GtkIconInfo *info = gtk_icon_theme_lookup_icon (theme, icon, size, GTK_ICON_LOOKUP_FORCE_SIZE);
        pixbuf = gtk_icon_info_load_icon (info, NULL);
        if (pixbuf != NULL)
        {
            gtk_image_set_from_pixbuf (GTK_IMAGE (image), pixbuf);
            g_object_unref (pixbuf);
        }
        gtk_icon_info_free (info); 
        //g_object_unref (info);// Using g_object_unref Causes segfault (FIXME)
    }
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
        char device[32];
        asound_get_default_card (device);
        if (*device) snd_mixer_detach (vol->mixer, device);
        snd_mixer_close(vol->mixer);
    }
    vol->master_element = NULL;
    vol->mixer = NULL;
}

static void asound_find_valid_device ()
{
    // call this if the current ALSA device is invalid - it tries to find an alternative
    g_warning ("volumealsa: Default ALSA device not valid - resetting to internal");
    if (!asound_set_bcm_card ())
    {
        int num = -1;
        char buf[16];

        g_warning ("volumealsa: Internal device not available - looking for first valid ALSA device...");
        while (1)
        {
            if (snd_card_next (&num) < 0)
            {
                g_warning ("volumealsa: Cannot enumerate devices");
                break;
            }
            if (num == -1) break;

            sprintf (buf, "hw:%d", num);
            g_warning ("volumealsa: Valid ALSA device %s found", buf);
            asound_set_default_card (buf);
            return;
        }
        g_warning ("volumealsa: No ALSA devices found");
    }
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
        res = snd_mixer_handle_events(vol->mixer);
    }

    if (cond & G_IO_IN)
    {
        /* the status of mixer is changed. update of display is needed. */
        volumealsa_update_display(vol);
        return TRUE;
    }

    if ((cond & G_IO_HUP) || (res < 0))
    {
        /* This means there're some problems with alsa. */
        g_warning("volumealsa: ALSA (or pulseaudio) had a problem: "
                "volumealsa: snd_mixer_handle_events() = %d,"
                " cond 0x%x (IN: 0x%x, HUP: 0x%x).", res, cond,
                G_IO_IN, G_IO_HUP);
        gtk_widget_set_tooltip_text(vol->plugin, "ALSA (or pulseaudio) had a problem."
                " Please check the lxpanel logs.");

        if (vol->restart_idle == 0)
            vol->restart_idle = g_timeout_add_seconds(1, asound_restart, vol);

        return FALSE;
    }

    return TRUE;
}

static double
get_normalized_volume(snd_mixer_elem_t *elem, snd_mixer_selem_channel_id_t channel)
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


static gboolean asound_set_bcm_card (void)
{
    char bcmdev[64];
    if (volumealsa_get_bcm_device_id (bcmdev))
    {
        asound_set_default_card (bcmdev);
        return TRUE;
    }
    return FALSE;
}

static void asound_set_default_card (const char *id)
{
  char cmdbuf[256], idbuf[16], type[16], cid[16], *card, *bufptr = cmdbuf, state = 0, indef = 0;
  int inchar, count;
  char *user_config_file = g_build_filename (g_get_home_dir (), "/.asoundrc", NULL);

  // Break the id string into the type (before the colon) and the card number (after the colon)
  strcpy (idbuf, id);
  card = strchr (idbuf, ':') + 1;
  *(strchr (idbuf, ':')) = 0;

  FILE *fp = fopen (user_config_file, "rb");
  if (!fp)
  {
    // File does not exist - create it from scratch
    fp = fopen (user_config_file, "wb");
    fprintf (fp, "pcm.!default {\n\ttype %s\n\tcard %s\n}\n\nctl.!default {\n\ttype %s\n\tcard %s\n}\n", idbuf, card, idbuf, card);
    fclose (fp);
  }
  else
  {
    // File exists - check to see whether it contains a default card
    type[0] = 0;
    cid[0] = 0;
    count = 0;
    while ((inchar = fgetc (fp)) != EOF)
    {
        if (inchar == ' ' || inchar == '\t' || inchar == '\n' || inchar == '\r')
        {
            if (bufptr != cmdbuf)
            {
                *bufptr = 0;
                switch (state)
                {
                    case 1 :    strcpy (type, cmdbuf);
                                state = 0;
                                break;
                    case 2 :    strcpy (cid, cmdbuf);
                                state = 0;
                                break;
                    default :   if (!strcmp (cmdbuf, "type") && indef) state = 1;
                                else if (!strcmp (cmdbuf, "card") && indef) state = 2;
                                else if (!strcmp (cmdbuf, "pcm.!default")) indef = 1;
                                else if (!strcmp (cmdbuf, "}")) indef = 0;
                                break;
                }
                bufptr = cmdbuf;
                count = 0;
                if (cid[0] && type[0]) break;
            }
            else
            {
                bufptr = cmdbuf;
                count = 0;
            }
        }
        else
        {
            if (count < 255)
            {
                *bufptr++ = inchar;
                count++;
            }
            else cmdbuf[255] = 0;
        }
    }
    fclose (fp);
    if (cid[0] && type[0])
    {
        // This piece of sed is surely self-explanatory...
        sprintf (cmdbuf, "sed -i '/pcm.!default\\|ctl.!default/,/}/ { s/type .*/type %s/g; s/card .*/card %s/g; }' %s", idbuf, card, user_config_file);
        system (cmdbuf);
        // Oh, OK then - it looks for type * and card * within the delimiters pcm.!default or ctl.!default and } and replaces the parameters
    }
    else
    {
        // No default card - replace file
        fp = fopen (user_config_file, "wb");
        fprintf (fp, "\n\npcm.!default {\n\ttype %s\n\tcard %s\n}\n\nctl.!default {\n\ttype %s\n\tcard %s\n}\n", idbuf, card, idbuf, card);
        fclose (fp);
    }
  }
  g_free (user_config_file);
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

    asound_deinitialize(vol);

    if (!asound_initialize(vol)) {
        g_warning("volumealsa: Re-initialization failed.");
        return TRUE; // try again in a second
    }

    g_warning("volumealsa: Restarted ALSA interface...");

    vol->restart_idle = 0;
    return FALSE;
}

static inline gboolean use_linear_dB_scale(long dBmin, long dBmax)
{
    return dBmax - dBmin <= MAX_LINEAR_DB_SCALE * 100;
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

static gboolean volumealsa_get_bcm_device_id (gchar *id)
{
    int num = -1;

    while (1)
    {
        if (snd_card_next (&num) < 0)
        {
            g_warning ("volumealsa: Cannot enumerate devices");
            break;
        }
        if (num == -1) break;

        if (volumealsa_is_bcm_device (num))
        {
            if (id) sprintf (id, "hw:%d", num);
            return TRUE;
        }
    }
    return FALSE;
}


static gboolean volumealsa_is_bcm_device (int num)
{
    char buf[16];
    snd_ctl_t *ctl;
    snd_ctl_card_info_t *info;
    snd_ctl_card_info_alloca (&info);

    sprintf (buf, "hw:%d", num);
    if (snd_ctl_open (&ctl, buf, 0) < 0) return FALSE;
    if (snd_ctl_card_info (ctl, info) < 0) return FALSE;
    if (snd_ctl_close (ctl) < 0) return FALSE;
    if (strncmp (snd_ctl_card_info_get_name (info), "bcm2835", 7) == 0) return TRUE;
    return FALSE;
}


