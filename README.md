# Ethernet over AX.25

The eoax Linux kernel module creates a virtual Ethernet interface for every AX.25 one available. It encapsulates Ethernet frames in AX.25 UI frames, using Protocol Identifier (PID) 0x0D. It does the reverse job of the [BPQ Ethernet](http://www.linux-ax25.org/wiki/BPQ) module. The code can also serve as a basis for experimenting with new routing protocols for packet radio in Linux.

## Motivation and design

To reach another user with packet radio, you have to manually define the path of digipeaters to use, or use a protocol like NET/ROM or ROSE for routing. However, these extra AX.25-based networking schemes may require complicated configuration and specialized use patterns (read an overview [here](https://www.tapr.org/pr_intro.html)).

I started out to investigate the feasibility of an auto-configurable packet network, running TCP/IP, so it would be compatible with modern day applications. Previous experience with WiFi mesh networks, led to the [B.A.T.M.A.N. Advanced](https://www.open-mesh.org/projects/open-mesh/wiki) routing protocol. Nodes in the B.A.T.M.A.N. mesh may automatically act as "digipeaters", depending on network layout and usage. Moreover, because B.A.T.M.A.N. Advanced runs on Layer 2, creating a virtual switch out of participating nodes, IP addresses may be assigned using DHCP. This leads to practically zero configuration requirements.

B.A.T.M.A.N. Advanced relies on an Ethernet substrate. To run B.A.T.M.A.N. Advanced over AX.25, I had to either change the core of the routing protocol, or create an Ethernet emulation layer over AX.25. The second choice seemed simpler. Searching the Internet, I also found references to ETHRAX25, a very old, similar project, that was used in order to run TCP/IP over AX.25 (a description is [here](ftp://ftp.ucsd.edu/hamradio/packet/tcpip/misc/ethrax25.txt)).

AX.25 interfaces use 7 bytes for the hardware address, which store the used callsign in AX.25 format. However, as a callsign is comprised of 7-bit characters, it can easily fit in one byte less - the size of an Ethernet MAC address. In eoax, Ethernet hardware addresses are converted to AX.25 ones in outgoing frames and vice versa for incoming traffic.

## Status

The module is working on my virtual machine setup. For development and testing I run 2 nodes, each with an eoax interface over a BPQ interface (Ethernet over AX.25 over Ethernet). I can assign IP addresses, ping, ssh, and even run B.A.T.M.A.N. Advanced on the eoax interfaces.

Since Ethernet and AX.25 addresses are converted from one another, we can safely strip the Ethernet source and destination fields from outgoing frames and rebuild them at the receive side. This has not been done yet. It would save us 12 bytes per frame for actual data.

## Installation

The eoax module requires some changes to the kernel AX.25 layer in order to compile and run. These changes affect the way AX.25 handles outgoing and incoming UI frames with unknown PIDs, and are included as a patch to kernel sources.

The following is what I do to test the software. It is by no means a "proper" installation method, but rather a quick-and-dirty way to build and run the kernel module. Use at your own risk. The steps have been tested on Ubuntu 14.04.3 LTS and 15.10 and may require changes for other Linux distributions.

**Note that the name of the kernel source folder is different for different Ubuntu versions, as is the corresponding patch.**

I assume you have cloned this repository in `~/eoax`.

* Download and install kernel build tools and sources:

  ```bash
  apt-get build-dep linux-image-$(uname -r)
  apt-get source linux-image-$(uname -r)
  ```

* Change into the kernel source directory and apply the AX.25 patch (names applicable to Ubuntu 14.04.3 LTS, which uses kernel version 3.19 as of this writing):

  ```bash
  cd linux-lts-vivid-3.19.0 
  patch -p0 < ~/eoax/patches/ax25_ui_type-3.19
  ```

* Replace the installed `ax25.h` header file with the patched version (keep a backup of the old file if you need it):

  ```bash
  cp include/net/ax25.h /usr/src/linux-headers-$(uname -r)/include/net/
  ```

* Compile and install the patched AX.25 module:

  ```bash
  cd net/ax25
  make -C /lib/modules/$(uname -r)/build M=$(pwd) modules
  make -C /lib/modules/$(uname -r)/build M=$(pwd) modules_install
  ```

* Instruct modprobe to use the patched version of AX.25:

  ```bash
  echo "override ax25 * extra" >> /etc/depmod.d/ubuntu.conf
  depmod
  ```

* Copy the new `Module.symvers` for AX.25 over to eoax sources, to get the correct symbol versions:

  ```bash
  cp Module.symvers ~/eoax/drivers/net/hamradio/
  ```

* Compile and install the eoax module:

  ```bash
  cd ~/eoax
  make
  make install
  depmod
  ```

## Usage

Load the module with `modprobe eoax`. If you have an AX.25 interface configured, you should now see an `eoax` one. If you don't, the `eoax` interface will show up, as soon as an AX.25 interface is configured. Look at `dmesg` to find out which `eoax` interface corresponds to which AX.25 one.

## Contact

Please contact me for any comments or suggestions.

73 de SV1OAN
