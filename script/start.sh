# Loads the lunix.ko kernel module into the Linux kernel
insmod ./lunix.ko

# Create necessary device nodes.
./mk-lunix-devs.sh

# Attaches the Lunix:TNG system to the serial port /dev/ttyS1.
./lunix-attach /dev/ttyS1
