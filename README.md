# rpi

Raspberry Pi stuff

## Links

* [RPi pinout](https://www.raspberrypi.com/documentation/computers/raspberry-pi.html#gpio-and-the-40-pin-header)
* [SIK](https://cdn.sparkfun.com/datasheets/Kits/SIK/SIK_v4_Book_Oct_25_FINAL.pdf)
  * I use components from this kit with my RPi.
* [lgpio](https://abyz.me.uk/lg/index.html)
* [gpiozero](https://gpiozero.readthedocs.io/en/stable/index.html)
* [HC-SR04](https://cdn.sparkfun.com/datasheets/Sensors/Proximity/HCSR04.pdf)
  * The one from the SIK and PiSloth are both HC-SR04
* [Robot Hat](https://github.com/sunfounder/robot-hat)
* [PiSloth](https://github.com/sunfounder/pisloth/tree/v2.0)
  * Make sure to look at v2 branch
* [VNC](https://www.raspberrypi.com/documentation/computers/remote-access.html#enable-the-vnc-server-on-the-command-line)

## VNC

sudo raspi-config

interface options

enable vnc

hostname -I to get IP address of RPi

not sure if this was necessary but also ran this:

vncserver-virtual

this section says realvnc is enabled so maybe not necessary

https://www.raspberrypi.com/documentation/computers/remote-access.html#vnc

for the client side...

go to tigervnc releases

https://github.com/TigerVNC/tigervnc/releases

which links to their sourceforge

https://sourceforge.net/projects/tigervnc/files/stable/1.13.1/

tigervnc-x.x.x.x86_64.tar.gz worked fine

extract the dir

run ./usr/bin/vncviewer

and then walk through tigervnc stuff

https://www.raspberrypi.com/documentation/computers/remote-access.html#connect-to-your-raspberry-pi
