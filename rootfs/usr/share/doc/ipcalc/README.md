# Information about ipcalc

This is a modern tool to assist in network address calculations for IPv4 and IPv6.
It acts both as a tool to output human readable information about a network or address,
as well as a tool suitable to be used by scripts or other programs.

It supports printing a summary about the provided network address, multiple
command line options per information to be printed, transparent IPv6 support,
and in addition it will use libGeoIP if available to provide geographic information.

The project started as a fork of the ipcalc tool in the Fedora distribution
but it has since then replaced the original tool.


# How to build

This project is built using Meson and Ninja. To build it simply type:
```
$ meson setup build
$ ninja -C build
```

## Build options

There are a few build-time options supported by ipcalc. You can learn about
them, and optionally reconfigure the project before building it, by running the
following command after `meson setup build`:
```
$ meson configure build
```

In order to access geo-information, ipcalc depends on the maxmind
database (libmaxminddb). Alternatively it can be built using the older
libgeoip (libgeoip), or with no geo-information whatsoever. Meson will
detect which supported geo-information libraries are available on your system,
if any, and automatically choose the best one. You can override this choice by
providing `-Duse_maxminddb=enabled` to `meson setup` or `meson configure` to
require libmaxminddb or `-Duse_maxminddb=disabled -Duse_geoip=enabled` to
require libgeoip. See `meson configure build` for more options.

For example, the following command will compile with no support for
geo-information.
```
$ meson setup build `-Duse_maxminddb=disabled -Duse_geoip=disabled`
$ ninja -C build
```

To generate the manpage the ronn application is necessary.

## Optimizing the build

By default Meson optimizes for the most common type of build: those done by
developers. Consequently default builds of ipcalc with Meson are without
optimization and with debug information. If you want an optimized build,
pass `--buildtype=release` to `meson setup` or `meson configure`. See
[the Meson documentation](https://mesonbuild.com/Running-Meson.html#configuring-the-build-directory)
for more build options.
```
$ meson setup build --buildtype=release
$ ninja -C build
```


# Running unit tests

After the application has been configured and built for the host platform
(default), you may run the unit test suite against the ipcalc binary as
follows:
```
$ ninja -C build test
```


# Legacy build method

Although the Meson Build System, described above, is the recommended way to
build this application, the legacy GNU Makefile is still supported for older
systems that do not have the requisite versions of Meson and Ninja readily
available. The primary limitations of using the Makefile are that it does not
do automatic detection of libmaxminddb or libgeoip - you may have to specify
support for one or neither of those libraries manually - and there is no easy
way to run the test suite.

To build using the legacy GNU Makefile simply type:
```
$ make
```

In order to access geo-information the application depends on the maxmind
database (libmaxminddb). Alternatively it can be built using the older
libgeoip (libgeoip) or with no geo-information whatsoever. The options
can be provided on the makefile via the variables USE_GEOIP (yes/no),
USE_MAXMIND (yes/no). For example the following command will compile
with no support for geo-information.

```
$ make USE_GEOIP=no USE_MAXMIND=no
```


# Examples

## IPv4

```
$ ipcalc --all-info 193.92.150.2/24
Address:        193.92.150.2
Network:        193.92.150.0/24
Netmask:        255.255.255.0 = 24
Broadcast:      193.92.150.255
Reverse DNS:    150.92.193.in-addr.arpa.

Address space:  Internet
Address class:  Class C
HostMin:        193.92.150.1
HostMax:        193.92.150.254
Hosts/Net:      254

Country code:   GR
Country:        Greece
```

```
$ ipcalc -pnmb --minaddr --maxaddr --geoinfo --addrspace 193.92.150.2/255.255.255.224
NETMASK=255.255.255.224
PREFIX=27
BROADCAST=193.92.150.31
NETWORK=193.92.150.0
MINADDR=193.92.150.1
MAXADDR=193.92.150.30
ADDRSPACE="Internet"
COUNTRY="Greece"
```

## IPv6

```
$ ipcalc --all-info 2a03:2880:20:4f06:face:b00c:0:14/64
Full Address:   2a03:2880:0020:4f06:face:b00c:0000:0014
Address:        2a03:2880:20:4f06:face:b00c:0:14
Full Network:   2a03:2880:0020:4f06:0000:0000:0000:0000/64
Network:        2a03:2880:20:4f06::/64
Netmask:        ffff:ffff:ffff:ffff:: = 64
Reverse DNS:    6.0.f.4.0.2.0.0.0.8.8.2.3.0.a.2.ip6.arpa.

Address space:  Global Unicast
HostMin:        2a03:2880:20:4f06::
HostMax:        2a03:2880:20:4f06:ffff:ffff:ffff:ffff
Hosts/Net:      2^(64) = 18446744073709551616

Country code:   IE
Country:        Ireland
```

```
$ ipcalc -pnmb --minaddr --maxaddr --addrspace --geoinfo 2a03:2880:20:4f06:face:b00c:0:14/64
NETMASK=ffff:ffff:ffff:ffff::
PREFIX=64
NETWORK=2a03:2880:20:4f06::
MINADDR=2a03:2880:20:4f06::
MAXADDR=2a03:2880:20:4f06:ffff:ffff:ffff:ffff
ADDRSPACE="Global Unicast"
COUNTRY="Ireland"
```

## JSON output

```
$ ipcalc --all-info -j 2a03:2880:20:4f06:face:b00c:0:14/64
{
  "FULLADDRESS":"2a03:2880:0020:4f06:face:b00c:0000:0014",
  "ADDRESS":"2a03:2880:20:4f06:face:b00c:0:14",
  "FULLNETWORK":"2a03:2880:0020:4f06:0000:0000:0000:0000",
  "NETWORK":"2a03:2880:20:4f06::",
  "NETMASK":"ffff:ffff:ffff:ffff::",
  "PREFIX":"64",
  "REVERSEDNS":"6.0.f.4.0.2.0.0.0.8.8.2.3.0.a.2.ip6.arpa.",
  "ADDRSPACE":"Global Unicast",
  "MINADDR":"2a03:2880:20:4f06::",
  "MAXADDR":"2a03:2880:20:4f06:ffff:ffff:ffff:ffff",
  "ADDRESSES":"18446744073709551616",
  "COUNTRYCODE":"IE",
  "COUNTRY":"Ireland",
  "COORDINATES":"53.000000,-8.000000"
}
```
