![wavemon screenshot](https://cloud.githubusercontent.com/assets/5132989/8640926/1f8436a0-28c6-11e5-9336-a79fd002c324.png)

## Synopsis

wavemon is a wireless device monitoring application that allows you to watch
signal and noise levels, packet statistics, device configuration and network
parameters of your wireless network hardware. It should work (though with
varying features) with all devices supported by the Linux kernel.

Note that wavemon requires a Linux Kernel with wireless extensions enabled. If
your Kernel setup uses `CONFIG_CFG80211` make sure that the config option
`CONFIG_CFG80211_WEXT` is set.

See the man page for an in-depth description of operation and configuration.


### Where to obtain

Apart from debian/ubuntu packages (apt-cache search wavemon) and slackbuild
scripts for wavemon on slackbuilds.org, this repository contains the full
source code.

Please check this page for updates and for further information.
wavemon is distributed under the [GPLv3](http://www.gnu.org/licenses/gpl-3.0.en.html), refer to the file `COPYING`.


## How to build

wavemon uses `autoconf`, so that in most cases you can simply run
```
	./configure
	make
	sudo make install
```
to build and install the package. Type 'make uninstall' if not happy.
Refer to the file `INSTALL` for generic installation instructions.

**Dependencies**: at least version 3.2 of `libnl`, including the Generic Netlink support (`libnl-genl`).
On Debian/Ubuntu, this can be done using
```bash
	apt-get -y install libnl-3-dev libnl-genl-3-dev
```

To grant users access to restricted networking operations (scan operations), use additionally
```
	sudo make install-suid-root
```
If you have changed some of the autoconf files or use a git version, run
```
	./config/bootstrap
```
(requires a recent installation of `autotools`).


## Bugs?

Send bug reports, comments, and suggestions by opening an issue on [github](https://github.com/uoaerg/wavemon/issues).
