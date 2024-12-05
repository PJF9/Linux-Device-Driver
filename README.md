# Lunix:TNG - Linux Device Driver for Wireless Sensor Networks

Lunix:TNG (The Next Generation) is a Linux device driver designed to interface with a wireless sensor network composed of Crossbow MPR2400CA wireless cards equipped with voltage, temperature, and light sensors (MDA100CB). This driver allows independent and simultaneous access to sensor data from multiple processes, providing a mechanism for system administrators to enforce different access policies.

The wireless sensor network consists of multiple sensors organized in a mesh topology. This ensures data is transmitted via alternative routes, and the network adapts automatically if one or more sensors are out of the base station's range. The base system sends the sensor data via TCP, and we channel them to the ttyS1 device on out machine, where the Lunix:TNG driver operates.

---

## Prerequisites

### Installing QEMU
We created Lunix:TNG on a virtual machine, using QEMU (Quick EMUlator). We configured the project on WSL 2 host, on a Debian GNU/Linux 12 (bookworm) distribution.

We started by installing on the host packages from the repositories of the distribution that we will need below to build QEMU from its source code:
```bash
> sudo apt-get update
> sudo apt-get install build-essential vim bzip2 gzip xz-utils
> sudo apt-get install socat git wget libglib2.0-dev
> sudo apt-get install libfdt-dev libpixman-1-dev zlib1g-dev
> sudo apt-get install ninja-build libcap-ng-dev libattr1-dev
```

Because we want for security and isolation reasons virtual machines to run with user privileges other than root, we created a kvm group, in which we added the user that uses the system (e.g. user), and set the /dev/kvm file to be created with permissions in that group.
```bash
> sudo groupadd kvm
> sudo usermod -aG kvm user
> sudo echo KERNEL==\"kvm\", GROUP=\"kvm\" >> /etc/udev/rules.d/40-permissions.rules
> sudo modprobe -r kvm_amd
> sudo modprobe -r kvm
> sudo modprobe kvm_amd
```

Qemu Installation:
* Download and unzip the QEMU code
* Set the parameters of the compilation
* Lastly compile and install QEMU
```bash
> wget https://download.qemu.org/qemu-7.1.0.tar.xz
>  tar xvJf qemu-7.1.0.tar.xz
>  cd qemu-7.1.0
> ./configure --prefix=$HOME/opt/qemu --enable-kvm --target-list=x86_64-softmmu --enable-virtfs
> make -j2
> make install
```

### Using QEMU

Download the root filesystem of your virtual machine from the course site, unzip it,
using the gunzip tool, and move it to /vm:
```bash
> mkdir vm && cd vm
> wget http://www.cslab.ece.ntua.gr/courses/compsyslab/files/2024-25/cslab_rootfs_bookworm_20241108-0.raw.gz
> gunzip -k cslab_rootfs_bookworm_20241108-0.raw.gz
```

The virtual machine will start, and v.sh will print diagnostic messages about how to connect to it:
```bash
> ./vm.sh
    *** Reading configuration
    VM_CONFIG=./vm.config
    [...]
    *** Starting your Virtual Machine ...
    To connect with X2Go: See below for SSH settings
    To connect with SSH: ssh -p 22223 root@localhost
    To connect with vncviewer: vncviewer localhost:0
```


### Installing new Linux Kernel to the guest

For the code to work we need to download the 6.11 verson of the kernel. You can do that:
```bash
> sudo apt-get update
> apt-get install make gcc flex bison libelf-dev libssl-dev

> wget https://cdn.kernel.org/pub/linux/kernel/v6.x/linux-6.11.tar.xz
>  tar xvf linux-6.11.tar.xz

> cd linux-6.11
> make defconfig
> make kvm_guest.config
> make -j2
```
The previous commands concerning downloading the kernel source code and compiling it can be executed outside the virtual your virtual machine. In fact, we recommend that you run them on your host machine to make the heavy compiling process faster. The next two
commands that involve installing the kernel should definitely be executed inside the virtual machine:
```bash
> cd ~user/shared/linux-6.11/
> make modules_install
> make install

> reboot
```

And you are ready to use the driver!

---

## Installation and Running the Driver
Clone the repository and build the driver:
```bash
> git clone https://github.com/yourusername/lunix-device-driver.git
> cd lunix-device-driver
> make
```

And then run the scripts for runnung the project:
```bash
> cd scripts
> ./start.sh
    (loading the driver on the kernel and attaching the tty device)
```

On another terminal (inside the vm):
```bash
> cd scripts
> ./lunix-tcp.sh
    (connect to a remote TCP endpoint and forward data to a specified tty port)
```

Finally on another terminal (inside the vm):
```
> cat /dev/lunix1-temp
27.791
27.793
27.795
...
```

If no new data is available, the read operation blocks until new data arrives. This behavior ensures efficient CPU usage. To interrupt a blocking read, press Ctrl+C.

### Accessing Different Measurements
Replace `temp` in the device filename with batt or light to access battery voltage or light intensity:

* Battery voltage: /dev/lunixX-batt
* Temperature: /dev/lunixX-temp
* Light intensity: /dev/lunixX-light

(where X is the sensor number 0-16)

---

## Architecture

### Overview
The Lunix:TNG driver is organized into several components:
1. Line Discipline (lunix-ldisc): Captures and redirects data from the serial port to the driver.
2. Protocol Processing (lunix-protocol): Parses raw data packets and extracts measurement values.
3. Sensor Buffers (lunix-sensors): Stores the latest measurements for each sensor and measurement type.
4. Character Device Interface (lunix-chrdev): Provides user-space access through device files, implementing standard file operations.

### Data Flow
1. Data Reception: The base station sends data via a TCP connection, appearing as a virtual serial port (e.g., /dev/ttyS1).
2. Line Discipline Attachment: lunix-attach attaches the Lunix:TNG line discipline to the serial port.
3. Data Processing:
    * The line discipline captures incoming data and forwards it to the protocol handler.
    * The protocol handler parses the data and updates the sensor buffers.
4. User-Space Access:
    * Applications read data from device files (e.g., /dev/lunix0-temp).
    * The character device interface handles read requests, retrieves data from the sensor buffers, and formats it for user-space consumption.


### Synchronization and Concurrency
* Spinlocks: Used to protect sensor buffers during concurrent access.
* Wait Queues: Processes block on wait queues when no new data is available and are woken up when data arrives.


### Data Formatting
* Lookup Tables: Precomputed tables convert raw sensor values to human-readable format without floating-point arithmetic in kernel space.
* Endianess Handling: Uses kernel macros to handle data conversion between little-endian and system-endian formats.

---

## License
This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.

---

## Acknowledgments
* National Technical University of Athens: This driver is based on coursework and materials provided by NTUA.
* Linux Kernel Community: For documentation and examples that aided in driver development
