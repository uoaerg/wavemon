![wavemon screenshot](https://cloud.githubusercontent.com/assets/5132989/8640926/1f8436a0-28c6-11e5-9336-a79fd002c324.png)

## Synopsis

`wavemon` is a wireless device monitoring application that allows you to watch
signal and noise levels, packet statistics, device configuration and network
parameters of your wireless network hardware. It should work (though with
varying features) with all devices supported by the Linux kernel.

See the man page for an in-depth description of operation and configuration.

### Where to obtain

Apart from debian/ubuntu packages (`apt-cache search wavemon`) and [slackbuild  scripts for wavemon](https://slackbuilds.org/result/?search=wavemon&sv=), this repository contains the full source code.

### Dependencies

Minimally the following are required:
* the `pkg-config` package,
* netlink `libnl-cli-3-dev` at least version 3.2 (pulls in `libnl-3-dev`, `libnl-genl-3-dev`),
* ncurses development files (`libncursesw6`, `libtinfo6`, `libncurses-dev`).

On Debian/Ubuntu, this can be done using
```bash
apt-get -y install pkg-config libncursesw6 libtinfo6 libncurses-dev libnl-cli-3-dev
```

Please note the "w" in `libncursesw6`, which stands for the _wide-character_ variant of ncurses.
This is required for [proper rendering on UTF-8 terminals](https://github.com/uoaerg/wavemon/issues/70).

## How to build

wavemon uses `autoconf`, so that in most cases you can simply run
```bash
./configure && make && sudo make install
```
to build and install the package. Type `make uninstall` if not happy.

### Using custom `CFLAGS`

Pass additional `CFLAGS` to `configure`, like this:
```bash
CFLAGS="-O2 -pipe" ./configure
```

Passing `CFLAGS` to `make` is not encouraged, since that will replace the settings found by `configure`.

### Installation with privileges

To grant users access to _restricted networking operations_ (scanning), use
```bash
sudo make install-suid-root
```

### Resetting `autoconf` state

If you have changed some of the `autoconf` files or use a `git` version, run
```bash
./config/bootstrap
```
(This requires a recent installation of `autotools`.)
