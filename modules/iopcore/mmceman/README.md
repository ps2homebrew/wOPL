# MMCEMAN / MMCEDRV Overview

MMCEMAN (Multi-purpose Memory Card Manager) is a PlayStation 2 IOP module designed for communication with MMCE devices, such as the SD2PSX, MemCard PRO2, PSxMemCard, and others, using the MMCE Protocol.

# Features

### Card Switching
MMCEMAN allows for card management on MMCE devices. It can:
- Request a card or channel change (e.g., select a specific card).
- Retrieve the current card or channel in use.

These operations are accessible via fileXioDevctl.

### Game ID Communication

MMCEMAN can send a Game ID to the MMCE, enabling the device to automatically switch to a dedicated card for that game if this feature is enabled.
Game IDs can be sent and received through fileXioDevctl.

### Custom Commands

MMCEMAN is easy to extend. Custom commands can be easily added and exposed through fileXioDevctl.

### File System Access

MMCEMAN provides access to the MMCE's file system through standard POSIX file I/O calls. It integrates with iomanX and fileXio, enabling access via "mmce0:/" and "mmce1:/" (depending on the slot used).

Supported file I/O operations include:
- `open`
- `close`
- `read`
- `write`
- `lseek`
- `ioctl`
- `remove`
- `mkdir`
- `rmdir`
- `dopen`
- `dclose`
- `dread`
- `getstat`
- `lseek64`
- `devctl`
- `ioctl2`
# MMCEDRV
MMCEDRV is a lightweight module designed for streaming data from MMCE devices in an in-game environment. It is designed to be as small and efficient as possible, containing only the functionality required for data streaming while in-game. As of writing MMCEDRV is only 12.6KB in size.

As such MMCEDRV:
- Does not include support for card switching, Game ID communication, or custom commands.
- It operates independently of iomanX and fileXio
- It requires all files to be opened on the MMCE by MMCEMAN prior to being loaded.

MMCEDRV provides read and write functions for VMCs, similar to those in MMCEMAN, but these are accessed through exports rather than the iomanX "mmce:/" device. For streaming data, MMCEDRV uses a custom, non-POSIX function that emulates sector reads.

# Usage Notes
### MMCEMAN:

MMCEMAN requires both iomanX and fileXio to be loaded before it is initialized.
MMCE devices are accessible via "mmce0:/" and "mmce1:/", depending on the slot used.
Card switching, Game ID communication, and other commands are exposed through fileXioDevctl. Refer to the header for the list of available commands.

### MMCEDRV:

Before launching MMCEDRV, all files must be opened by MMCEMAN. Their file descriptors must either be placed in the mmcedrv_config struct or be passed to MMCEDRV using the mmcedrv_config_set function (export #7).

# Known Issues:

### SIO2MAN Version Compatibility:
The SIO2MAN hook has not yet been updated to support SIO2MAN version 2.7 or newer (SDK 3.0.3). As a result, titles built using SDK 3.0.3 or later are currently unsupported. This limitation also applies to MX4SIO.

### Deadlock Issues:
During early testing, two games were identified to have deadlock issues caused by how the IOP handles semaphores and threads:
- Just Cause: This deadlock occurs on PS2 models ≤ 70K and appears to be specific to MMCEs.
- Midway Arcade Treasures 3: This issue occurs on both PS2 models and affects both MMCEs and MX4SIO.

This is not an exhaustive list, and other games may also be affected by this issue.

More details:
Using Midway Arcade Treasures 3 as an example, the issue arises due to a module used by the game called loader_d.irx. This module is responsible for loading sound and other game data. Its thread priority is set to 0x11, which is higher than padman’s update thread (0x14) but lower than cdvdman’s read thread (0xF).

What happens:
1. Padman acquires the semaphore to use the SIO2 and is then put to sleep in favor of loader_d.
2. loader_d waits on cdvdman, which is in turn waiting for padman to release the semaphore.
3. However, padman is not given the time to run because loader_d has a higher priority, resulting in a deadlock.

# Build notes:
If you are using a relatively older PS2SDK, you will find errors related to the symbol `I_GetIntrmanInternalData` not being defined.

if you come accross this issue, replace inside the file `imports.lst` that `I_GetIntrmanInternalData` with `DECLARE_IMPORT(3, GetIntrmanInternalData)`


# Special Thanks:
- Wisi: SIO2 exploration and documentation
- Maximus32: SIO2MAN hook and their work on MX4SIO