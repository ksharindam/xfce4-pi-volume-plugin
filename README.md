# xfce4-pi-volume-plugin
Xfce4 Panel volume changer plugin for Raspberry Pi

Although xfce4 has its own volume plugins, but those have these following problems.  
* Dependency on gstreamer0.10 but other programs use gstreamer1.0  
* Volume can not be increased much.  
* Another plugin depends on pulseaudio, but it causes several sound problem (such as, no sound in dosbox, audio player is paused automatically).  

To solve these problems, i have ported lxvolume plugin to xfce. It depends only on alsa.  

## Build
install these build dependencies...  
* libgtk-3-dev  
* libxfce4panel-2.0-dev  
* libasound2-dev

Unzip the source archive.  
Open terminal and change directory to xfce4-pi-volume-plugin-master directory.  
run this command...  
`make`

### Install and Uninstall
To install run...  
`sudo make install`  

To uninstall run...  
`sudo make uninstall`  

After installing go to panel settings and add this plugin to your panel.

