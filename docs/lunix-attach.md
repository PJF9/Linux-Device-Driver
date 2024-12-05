The provided C code is a utility program designed to attach the Lunix:TNG line discipline to a specified terminal (TTY) device. It configures the TTY device for communication with Lunix sensors by setting appropriate serial communication parameters and changing the line discipline. Here's a detailed explanation of what each function does and when it is called:

### Global Variables

```c
struct {
    const char *speed;
    int code;
} tty_speeds[] = {...}
```

- Maps string representations of baud rates to their corresponding termios constants.
- Used to set the TTY line speed.

```c
int tty_fd = -1;
struct termios tty_before, tty_current;
int ldisc_before;
```

- **`tty_fd`:** File descriptor for the TTY device.
- **`tty_before`:** Stores the original termios settings before modification.
- **`tty_current`:** Stores the current termios settings.
- **`ldisc_before`:** Stores the original line discipline before modification.

### `tty_already_locked` Function

```c
static int tty_already_locked(char *nam)
```

- **Purpose:**
    - Checks if a lock file exists for the specified TTY device, indicating that it is already in use.
- **When It's Called:**
    - Before attempting to lock the TTY device in `tty_lock`
- **Functionality:**
    - Tries to open the lock file corresponding to the device.
        - Lock files are typically named `LCK..ttyS0`, located in `/var/lock`.
    - Reads the PID from the lock file.
    - Checks if the process with that PID is still running using `kill(pid, 0)`.
        - If the process exists, returns `1`, indicating the device is locked.
        - If not, returns `0`, indicating the device can be locked.
    

### `tty_lock` Function

```c
static int tty_lock(char *path, int mode)
```

- **Purpose:**
    - Creates or removes a lock file for the TTY device to prevent concurrent access.
- **When It's Called:**
    - **Locking:** Called in `tty_open` to lock the device before opening.
    - **Unlocking:** Called in `tty_close` to unlock the device upon termination.
- **Functionality:**
    - **Lock Mode (`mode == 1`):**
        - Constructs the lock file path (`/var/lock/LCK..ttyS0`).
        - Checks if the device is already locked using `tty_already_locked`.
        - Creates the lock file and writes the current process PID into it.
        - Changes the ownership of the lock file to the `uucp` user (common practice for TTY devices).
    - **Unlock Mode (`mode == 0`):**
        - Deletes the lock file to release the device.

### `tty_find_speed` Function

```c
static int tty_find_speed(const char *speed)
```

- **Purpose:**
    - Converts a string representing a baud rate into its corresponding termios constant.
- **When It's Called:**
    - In `tty_set_speed` to set the line speed.
- **Functionality:**
    - Iterates over the `tty_speeds` array.
    - Compares the input string with known speed strings.
    - Returns the corresponding termios constant if found.
    - Returns `EINVAL` if the speed is not supported.

### `tty_set_stopbits` Function

```c
static int tty_set_stopbits(struct termios *tty, char *stopbits)
```

- **Purpose:** Sets the number of stop bits for the TTY device.
- **When It's Called:** In `tty_open` when configuring the TTY settings.
- **Functionality:**
    - Modifies the `c_cflag` field of the termios structure.
    - **For '1' Stop Bit:**
        - Clears the `CSTOPB` flag.
    - **For '2' Stop Bits:**
        - Sets the `CSTOPB` flag.
    - Returns `0` on success or `EINVAL` if invalid input.

### `tty_set_databits` Function

```c
static int tty_set_databits(struct termios *tty, char *databits)
```

- **Purpose:** Sets the number of data bits per character.
- **When It's Called:** In `tty_open` during TTY configuration.
- **Functionality:**
    - Clears the `CSIZE` bits in `c_cflag`.
    - Sets the appropriate data size flag (`CS5`, `CS6`, `CS7`, `CS8`) based on input.
    - Returns `0` on success or `EINVAL` if invalid input.

### `tty_set_parity` Function

```c
static int tty_set_parity(struct termios *tty, char *parity)
```

- **Purpose:** Configures the parity bit for error detection.
- **When It's Called:** In `tty_open` when setting up the TTY.
- **Functionality:**
    - Modifies the `c_cflag` field based on parity:
        - **No Parity ('N'):**
            - Clears `PARENB` and `PARODD`.
        - **Odd Parity ('O'):**
            - Sets `PARENB` and `PARODD`.
        - **Even Parity ('E'):**
            - Sets `PARENB` and clears `PARODD`.
    - Returns `0` on success or `EINVAL` if invalid input.

### `tty_set_speed` Function

```c
static int tty_set_speed(struct termios *tty, const char *speed)
```

- **Purpose:** Sets the baud rate (line speed) of the TTY device.
- **When It's Called:** In `tty_open` during TTY configuration.
- **Functionality:**
    - Uses `tty_find_speed` to get the termios constant for the specified speed.
    - Clears the existing baud rate bits (`CBAUD`) in `c_cflag`.
    - Sets the new baud rate.
    - Returns `0` on success or an error code if the speed is invalid.

### `tty_set_raw` Function

```c
static int tty_set_raw(struct termios *tty)
```

- **Purpose:** Configures the TTY device to raw mode, making it transparent to data.
- **When It's Called:** In `tty_open` before setting other TTY settings.
- **Functionality:**
    - Clears special characters by setting `c_cc` array elements to `'\0'`.
    - Sets `c_cc[VMIN]` to `1` and `c_cc[VTIME]` to `0` for immediate read.
    - Configures `c_iflag`, `c_oflag`, and `c_lflag` for raw input/output.
    - Preserves the current baud rate.
    - Enables hardware flow control with `CRTSCTS`.
    - Sets `CLOCAL` to ignore modem control lines

### `tty_get_state` Function

```c
static int tty_get_state(struct termios *tty)
```

- **Purpose:** Retrieves the current termios settings of the TTY device.
- **When It's Called:** In `tty_open` and `tty_restore`.
- **Functionality:**
    - Uses `ioctl` with `TCGETS` to get the termios structure.
    - Returns `0` on success or a negative error code.

### `tty_set_state` Function

```c
static int tty_set_state(struct termios *tty)
```

- **Purpose:** Applies new termios settings to the TTY device.
- **When It's Called:** In `tty_open` after modifying termios settings.
- **Functionality:**
    - Uses `ioctl` with `TCSETS` to set the termios structure.
    - Returns `0` on success or a negative error code.

### `tty_get_ldisc` Function

```c
static int tty_get_ldisc(int *disc)
```

- **Purpose:** Sets the line discipline of the TTY device.
- **When It's Called:**
    - In `tty_open` to set the Lunix line discipline (`N_LUNIX_LDISC`).
    - In `tty_close` to restore the original line discipline.
- **Functionality:**
    - Uses `ioctl` with `TIOCSETD` to set the line discipline.
    - Returns `0` on success or a negative error code.

### `tty_restore` Function

```c
static int tty_restore(void)
```

- **Purpose:** Restores the TTY device to its original termios settings.
- **When It's Called:** In `tty_close` when cleaning up before exiting.
- **Functionality:**
    - Copies `tty_before` into a local termios structure.
    - Resets the speed to default (`"0"`), which typically restores the original speed.
    - Calls `tty_set_state` to apply the settings.
    - Returns `0` on success or an error code.

### `tty_close` Function

```c
static int tty_close(void)
```

- **Purpose:** Cleans up and closes the TTY device, restoring original settings.
- **When It's Called:** In the signal handler `sig_catch` or upon program termination.
- **Functionality:**
    - Restores the original line discipline using `tty_set_ldisc`.
    - Calls `tty_restore` to reset termios settings.
    - Unlocks the TTY device using `tty_lock` with mode `0`.
    - Returns `0`.

### `tty_open` Function

```c
static int tty_open(char *name)
```

- **Purpose:** Opens and initializes the specified TTY device, configuring it for use with the Lunix line discipline.
- **When It's Called:** In the `main` function after parsing command-line arguments.
- **Functionality:**
    - **Path Resolution:**
        - Constructs the full device path (e.g., `/dev/ttyS0`) if a relative path is provided.
    - **Locking:**
        - Calls `tty_lock` to lock the device, preventing other processes from using it.
    - **Device Opening:**
        - Opens the TTY device with `O_RDWR|O_NDELAY` flags.
        - Stores the file descriptor in `tty_fd`.
    - **State Retrieval:**
        - Saves the current termios settings in `tty_before`.
        - Saves the current line discipline in `ldisc_before`.
    - **Configuration:**
        - Calls `tty_set_raw` to put the TTY in raw mode.
        - Sets the speed to `57600` baud using `tty_set_speed`.
        - Configures data bits, stop bits, and parity to `8N1` (8 data bits, no parity, 1 stop bit).
    - **Apply Settings:**
        - Calls `tty_set_state` to apply the termios settings.
        - Sets the Lunix line discipline using `tty_set_ldisc` with `N_LUNIX_LDISC`.
    - **Error Handling:**
        - Returns `0` on success or a negative error code on failure.

### `sig_catch` Function

```c
static void sig_catch(int sig)
```

- **Purpose:** Signal handler to catch termination signals and perform cleanup.
- **When It's Called:** When the process receives `SIGHUP`, `SIGINT`, `SIGQUIT`, or `SIGTERM`.
- **Functionality:**
    - Calls `tty_close` to restore the TTY and release resources.
    - Exits the program with status `0`.

### `main` Function

```c
int main(int argc, char *argv[])
```

- **Purpose:**
    - Entry point of the program; parses arguments, sets up the TTY, and waits indefinitely.
- **Functionality:**
    - **Argument Parsing:**
        - Expects exactly one argument: the TTY device name (e.g., `ttyS0`).
        - If the argument count is incorrect, displays usage information and exits.
    - **TTY Setup:**
        - Calls `tty_open` with the provided TTY name to configure the device.
        - If `tty_open` fails, exits with status `1`.
    - **Signal Handling:**
        - Sets up signal handlers for `SIGHUP`, `SIGINT`, `SIGQUIT`, and `SIGTERM` using `signal` function.
    - **Infinite Loop:**
        - Calls `pause()` in a loop to wait indefinitely.
        - The process remains active until it receives a termination signal.
    - **Unreachable Code:**
        - The `return 100;` statement is unreachable due to the infinite loop.
        - Included perhaps as a safety measure or placeholder.

### **Additional Details**

- **Locking Mechanism:**
    - The program ensures exclusive access to the TTY device by creating a lock file in `/var/lock`.
    - The lock file contains the PID of the process holding the lock.
    - Before creating a lock, it checks if a lock already exists and whether the owning process is still running.
- **TTY Configuration:**
    - The TTY is configured to `57600` baud, `8N1` mode, and raw input/output.
    - These settings are necessary for proper communication with the Lunix:TNG devices.
- **Line Discipline:**
    - A line discipline is a layer in the TTY subsystem that processes data between the driver and the user space.
    - The program sets the line discipline to `N_LUNIX_LDISC`, which is specific to the Lunix:TNG system.
    - The original line discipline is saved and restored upon exit.

### **Sequence of Operations**

1. **Program Start:**
    - The user runs the program with the TTY device name as an argument (e.g., `./lunix-attach ttyS0`).
2. **Argument Check:**
    - The program checks if the correct number of arguments is provided.
3. **TTY Opening and Configuration:**
    - Calls `tty_open` with the specified TTY device name.
    - Locks the device to prevent other processes from accessing it.
    - Opens the device file and obtains a file descriptor.
    - Saves the current termios settings and line discipline.
    - Configures the TTY for raw mode and sets the required speed and mode (`57600`, `8N1`).
    - Sets the Lunix line discipline.
4. **Signal Setup:**
    - Registers signal handlers for termination signals.
5. **Wait Indefinitely:**
    - Enters an infinite loop with `pause()`, effectively keeping the process running until it receives a signal.
6. **Termination Signal Handling:**
    - Upon receiving a termination signal, `sig_catch` is called.
    - `tty_close` is invoked to restore the TTY settings and unlock the device.
    - The program exits gracefully.