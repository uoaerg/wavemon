![wavemon screenshot](https://cloud.githubusercontent.com/assets/5132989/8635845/56d13580-27fa-11e5-98a1-4506fd6ebde4.png)


## Synopsis

wavemon is a wireless device monitoring application that allows you to watch
signal and noise levels, packet statistics, device configuration and network
parameters of your wireless network hardware. It should work (though with
varying features) with all devices supported by the Linux wireless kernel
extensions by Jean Tourrilhes.

See the man page for an in-depth description of operation and configuration.


### Where to obtain

Apart from debian/ubuntu packages (apt-cache search wavemon) and slackbuild
scripts for wavemon on slackbuilds.org, up-to-date sources are available on

	[https://github.com/uoaerg/wavemon](https://github.com/uoaerg/wavemon)

Please check this page for updates and for further information.
wavemon is distributed under the [GPLv3](http://www.gnu.org/licenses/gpl-3.0.en.html), refer to the file `COPYING`.


## How to build

wavemon uses autoconf, so that in most cases you can simply run
```
	./configure
	make
	sudo make install
```
to build and install the package. Type 'make uninstall' if not happy.
Refer to the file `INSTALL` for generic installation instructions.

To grant users access to restricted networking operations (e.g. reading WEP
keys or performing scan operations), use additionally
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
