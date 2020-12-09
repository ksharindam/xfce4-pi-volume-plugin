# Xfce4 Pi Volume Plugin
Xfce4 Panel volume changer plugins for Raspberry Pi  

RPi OS have two volume plugins for lxpanel - lxplug-volume and lxplug-volumepulse  
The first one is used for ALSA and the second one is for PulseAudio server.  
Both plugins are very intuitive and allows to switch to HDMI or 3.5mm jack or any bluetooth device.  

But those plugins are not available for XFCE4 panel.  
So I have ported those plugins for Xfce panel.  

Ported from :
lxplug-volume         v0.21
lxplug-volumepulse v0.1

### Build
install these build dependencies...  
* xfce4-panel-dev  
* libgtk2.0-dev  
* libasound2-dev (for ALSA only)  
* libpulse-dev (for PulseAudio only)  

Unzip the source archive.  
Open terminal and change directory to xfce4-pi-volume-plugin-master directory.  
To build volumealsa, run ...  
`cd volumealsa`  
`make`  
Or build volumepulse by running...  
`cd volumepulse`  
`make`  

### Install and Uninstall
To install run...  
`sudo make install`  

To uninstall run...  
`sudo make uninstall`  

After installing go to panel settings and add this plugin to your panel.  

