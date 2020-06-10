MODULE = volumealsa
LIBDIR = /usr/lib/arm-linux-gnueabihf
PLUGIN_DIR = $(LIBDIR)/xfce4/panel/plugins
DESKTOP_DIR = /usr/share/xfce4/panel/plugins
DESTDIR =
# dependency : xfce4-panel-dev (or libxfce4panel-2.0-dev for gtk3)
CFLAGS = `pkg-config --cflags --libs libxfce4panel-1.0 alsa` -shared -fPIC -Wall

all:
	gcc -o lib$(MODULE).so $(CFLAGS) $(MODULE).c
install:
	install lib$(MODULE).so $(PLUGIN_DIR)
	install $(MODULE).desktop $(DESKTOP_DIR)
uninstall:
	rm $(PLUGIN_DIR)/lib$(MODULE).so $(DESKTOP_DIR)/$(MODULE).desktop
