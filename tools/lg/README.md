
lgpio is a library for Linux Single Board Computers (SBC) which allows control of the General Purpose Input Outputs (GPIO).

## Features

* reading and writing GPIO singly and in groups
* software timed PWM and waves
* callbacks on GPIO level change
* notifications via pipe on GPIO level change
* I2C wrapper
* SPI wrapper
* serial link wrapper

* daemon interface
* access control (daemon interface)
* file handling (daemon interface)
* creating and running scripts (daemon interface)
* network access (daemon interface)

## Archive components

### The base library

* The lgpio C library to control local GPIO.

### The daemon

* The rgpiod daemon offers a socket interface to the lgpio library.
* The rgpio C library to control local and remote GPIO via the daemon.

### Python modules

* The lgpio Python module to control local GPIO.
* The rgpio Python module to control local and remote GPIO via the daemon.

### Utilities

* The rgs shell utility to control local and remote GPIO via the daemon.

## Documentation

See http://abyz.me.uk/lg/

## Example programs

See http://abyz.me.uk/lg/examples.html and the examples in the
EXAMPLES directory.

## GPIO

ALL GPIO are identified by their gpiochip line number.

## Download & Install

### Prerequisites

A few packages are needed during installation of the Python modules.

* the SWIG code generator (to build lgpio.py from the C library)
* the Python development files (to build lgpio.py)
* the Python set up tools (to install lgpio.py and rgpio.py)

On Debian like systems these packages may be installed with the following commands.

```
sudo apt install swig python-dev python3-dev
sudo apt install python-setuptools python3-setuptools

```

### Download & Install

```
wget https://github.com/joan2937/lg/archive/master.zip
unzip master.zip
cd lg-master
make
sudo make install

```

