#include <alsa/asoundlib.h>
#include <libxfce4panel/xfce-panel-plugin.h>

#define MAX_LINEAR_DB_SCALE 24

typedef struct {
    /* Graphics. */
    const char* icon;
    GtkWidget * plugin;             /* push-button widget */
    GtkWidget * tray_icon;          // GtkImage that shows icon

    // unloading and error handling
    GIOChannel **channels;          /* Channels that we listen to */
    guint *watches;                 /* Watcher IDs for channels */
    guint num_channels;             /* Number of channels */

    /* ALSA interface. */
    snd_mixer_t * mixer;            /* The mixer */
    snd_mixer_elem_t * master_element;      /* The Master element */
    guint mixer_evt_idle;           /* Timer to handle restarting poll */
    guint restart_idle;
    // Bluetooth Audio
    char device[32];                // current initialized device (hw:0 or bluetooth)
    GDBusObjectManager *objmanager;         /* BlueZ object manager */
    int card_attached;          // whether current device is attached
    //char *bt_conname;                       /* BlueZ name of device - just used during connection */
} VolumeALSAPlugin;

static void volumealsa_constructor(XfcePanelPlugin *plugin);
static void volumealsa_destructor(XfcePanelPlugin *plugin, gpointer user_data);
static void volumealsa_update_display(VolumeALSAPlugin * vol);
static void volumealsa_popup_scale_scrolled (GtkWidget * widget, GdkEventScroll * evt, VolumeALSAPlugin * vol);
static gboolean on_mouse_click (GtkButton *button, gpointer user_data);

static gboolean asound_initialize(VolumeALSAPlugin * vol, const char *device);
static void asound_deinitialize(VolumeALSAPlugin * vol);
//static void asound_get_default_card (char *id);
static gboolean asound_find_elements(VolumeALSAPlugin * vol);
static gboolean asound_is_muted(VolumeALSAPlugin * vol);
static int asound_get_volume(VolumeALSAPlugin * vol);
static void asound_set_volume(VolumeALSAPlugin * vol, int volume);
static double get_normalized_volume(snd_mixer_elem_t *elem, snd_mixer_selem_channel_id_t channel);
static int set_normalized_volume(snd_mixer_elem_t *elem,
                snd_mixer_selem_channel_id_t channel, double volume, int dir);
static gboolean asound_mixer_event(GIOChannel * channel, GIOCondition cond, gpointer vol_gpointer);
static gboolean asound_reset_mixer_evt_idle(VolumeALSAPlugin * vol);
static gboolean asound_restart(gpointer vol_gpointer);
static long lrint_dir(double x, int dir);

static void cb_name_owned (GDBusConnection *connection, const gchar *name, const gchar *owner, gpointer user_data);
static void cb_name_unowned (GDBusConnection *connection, const gchar *name, gpointer user_data);
static void cb_object_added (GDBusObjectManager *manager, GDBusObject *object, gpointer user_data);
static void cb_object_removed (GDBusObjectManager *manager, GDBusObject *object, gpointer user_data);
static int get_bt_device_id (char *id);
void switch_output_device();

static inline gboolean use_linear_dB_scale(long dBmin, long dBmax)
{
    return (dBmax - dBmin <= MAX_LINEAR_DB_SCALE * 100);
}
