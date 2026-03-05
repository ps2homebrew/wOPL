# Multi-purpose Memory Card Emulator:File System (MMCE:FS) Protocol v1.0

### Base packet structure:
|      offset       | out  |  in  |        description        |
|-------------------|------|------|---------------------------|
| 0x00              | 0x8b | 0xff | MMCE identifier byte      |
| 0x01              | var  | 0xaa | Cmd                       |
| 0x02              | 0xff | 0x00 | Reserved byte             |
| 0x03-var          | var  | var  | Cmd specific bytes        |
| last cmd byte + 1 | 0xff | 0xff | Termination byte          |

### Multi packet transfer structure:
 - Header (0x8b, CMD) [single packet]
 - Payload [multi packet]
 - Footer (Additional data, terminator) [single packet]

Note:
0xaa is used as a weak validation check. mmceman checks rdbuf[0x1] for 0xaa,
if it's not present the data is considered invalid, and multi-packet commands
will not proceed to the second packet.

### 0x40 - File: open [multi-packet] [Implemented]
Packet #1:
| offset | out  |  in  |  description   |
|--------|------|------|----------------|
| 0x03   | var  | 0x00 | Flags (packed) |
| 0x04   | 0xff | 0xff | Padding        |

Packet #2:
|  offset  | out |  in  | description |
|----------|-----|------|-------------|
| 0x00-var | var | 0x00 | Filename    |

Packet #3:
| offset | out  |  in  |       description       |
|--------|------|------|-------------------------|
| 0x00   | 0xff | 0x00 | Padding                 |
| 0x01   | 0xff | var  | File descriptor (1-249) |
| 0x02   | 0xff | 0xff | Termination byte        |

Notes:

The flags are packed as follows:
```
packed_flags  = (flags & 3) - 1;        //O_RDONLY, O_WRONLY, O_RDWR
packed_flags |= (flags & 0x100) >> 5;   //O_APPEND
packed_flags |= (flags & 0xE00) >> 4;   //O_CREATE, O_TRUNC, O_EXCL
```

If an error occurs, the MMCE is to send a fd value of -1 and copy the errno value
to the status struct for the PS2 to obtain later. (get status not implemented yet)

### 0x41 - File: close [Implemented]
|  offset  | out  |  in  |      description         |
|----------|------|------|--------------------------|
| 0x03     | var  | 0x00 | File descriptor          |
| 0x04     | 0xff | var  | Return value             |
| 0x05     | 0xff | 0xff | Termination byte         |

Notes:
If an error occurs, the MMCE is to send a return value of 1 and copy the errno value
to the status struct for the PS2 to obtain later. (get status not implemented yet)

### 0x42 - File: read [multi-packet] [Implemented]
Packet #1:
| offset | out  |  in  |            description             |
|--------|------|------|------------------------------------|
| 0x03   | var  | 0x00 | Reserved                           |
| 0x04   | var  | 0x00 | File descriptor                    |
| 0x05   | var  | 0x00 | Length >> 24                       |
| 0x06   | var  | 0x00 | Length >> 16                       |
| 0x07   | var  | 0x00 | Length >> 8                        |
| 0x08   | var  | 0x00 | Length                             |
| 0x09   | 0xff | 0x00 | Return value (0 = Okay, 1 = Error) |

Packet #2 - var:
|  offset  | out  | in  | description |
|----------|------|-----|-------------|
| 0x00-var | 0x00 | var | Read data   |

Packet final:
| offset | out  |  in  |   description    |
|--------|------|------|------------------|
| 0x00   | 0xff | 0x00 | Padding          |
| 0x01   | 0xff | var  | Bytes Read << 24 |
| 0x02   | 0xff | var  | Bytes Read << 16 |
| 0x03   | 0xff | var  | Bytes Read << 8  |
| 0x04   | 0xff | var  | Bytes Read       |
| 0x05   | 0xff | 0xff | Termination Byte |

Notes:
Chunk refers to 256 bytes, the maximum size of a single SIO2 transfer element.

FileXio will sometimes break transfers down such that a single read call from the EE
will result in multiple calls for smaller amounts to mmceman.

For example, it may break a write of 180 bytes into 3 separate, individual transfers:
```
[Read Pkt #1] [Read Pkt #2: 40b ] [Read Pkt #3]
[Read Pkt #1] [Read Pkt #2: 100b] [Read Pkt #3]
[Read Pkt #1] [Read Pkt #2: 40b ] [Read Pkt #3]
```

However, for single transfers >256 bytes, packet #2 may be repeated as necessary.
Return the data in 256 byte chunks until just the remaider is left. (size%256)
E.g. to read 600 bytes:
```
[Read Pkt #1] [Read Pkt #2: 256b] [Read Pkt #2: 256b] [Read Pkt #2: 88b] [Read Pkt #3]
```

If a read is requested for more data than the file contains or if an error occurs on the MMCE such that the requested amount of data could not be read from the sdcard, the MMCE device should:
 - still clock out the number of requested bytes using dummy SPI data.
 - return the *actual* number of bytes read as the value in Packet #3 "Bytes Read".

For example: If something requests a 2048 byte read, and only 40 bytes were read (either due to a read error or the file being smaller than that), the MMCE should clock out the remaining 2008 bytes with dummy data and set the bytes read value in the footer packet to 40.
 
The MMCE has up to 2 seconds to respond to the header packet with a return value, during 
this time the MMCE should begin reading the data ahead of time in anticipation of the next packet. 
At minimum the first byte of the requested data is required before the start of the next packet.
This is true for all subsequent data packets as well. This is because the SIO2 will *NOT* wait for 
the /ACK line to be pulled down until *after* the first byte has already been transferred.

|   Bytes:    |  0   |                 |  1   |                 |  2   |
|-------------|------|-----------------|------|-----------------|------|
| PS2 -> MMCE | 0xff |                 | 0xff |                 | 0xff |
| PS2 <- MMCE | var  | [wait /ACK low] | var  | [wait /ACK low] | var  |

Valid data needs to go out on byte 0.

If the fd is invalid or another error has occurred, the MMCE is to send a return value of 1.
The PS2 will not proceed to the second packet. The get status command can be used to 
determine the cause of the error. (get status not implemented yet)

If a return value is not received at all due to the MMCE taking too long to read ahead 
/ the 2 second wait timeout is reached, the PS2 will not proceed to the second packet

SD2PSX specific notes:

The SD2PSX resets all PIO state machines and clears all FIFO data when the chip select line is deasserted. At the start of the next transfer, when the first byte is received, the SD2PSX sends out 0xFF since its TX FIFO is initially empty. After this, the SIO2 waits for the SD2PSX to process the first byte, load a byte into the TX FIFO, and pull down the /ACK line to signal the SIO2 to resume data transfer. If the transfer is not completed within 2 seconds, the SIO2 will timeout.

|   Bytes:      |  0   |                 |  1   |                                |  2   |
|---------------|------|-----------------|------|--------------------------------|------|
| PS2 -> SD2PSX | 0xff |                 | 0xff |                                | 0xff |
| PS2 <- SD2PSX | var  | [wait /ACK low] | var  | <-result of first mc_respond() | var  |

This is undesirable because it limits the maximum transfer size to 255 bytes instead of 256 and also requires a memcpy operation on the IOP.

The current solution involves starting the data read process immediately after receiving the length 
in the header, but before sending the return value in the header packet. This provides the SD2PSX 
with 2 seconds to read 256 bytes from the SD card and ensures the first byte is placed in the TX FIFO 
immediately after reset.

This approach also staggers reads, so that after a brief initial delay, data is almost always ready
for the PS2 as subsequent packets arrive. The delay is further minimized during sequential reads, as 
the SD2PSX reads one additional chunk (256 bytes) beyond what was requested. 

The SD2PSX utilizes a ring buffer, which is filled by core0 until the total bytes read equals the 
requested length, while core1 simultaneously sends the already-read data to the PS2. For all read 
packets, the ring buffer's status is checked before sending the last byte of the packet to ensure the 
next 256-byte chunk is ready, and the first byte can be placed in the TX FIFO immediately after reset.

### 0x43 - File: write [multi-packet] [implemented]
Packet #1:
| offset | out  |  in  |            description             |
|--------|------|------|------------------------------------|
| 0x03   | var  | 0x00 | Reserved                           |
| 0x04   | var  | 0x00 | File descriptor                    |
| 0x05   | var  | 0x00 | Length >> 24                       |
| 0x06   | var  | 0x00 | Length >> 16                       |
| 0x07   | var  | 0x00 | Length >> 8                        |
| 0x08   | var  | 0x00 | Length                             |
| 0x09   | 0xff | 0x00 | Return value (0 = Okay, 1 = Error) |

Packet #2 - n: Wait ready
| offset | out  |  in  |           description            |
|--------|------|------|----------------------------------|
| 0x00   | 0xff | 0x00 | Padding                          |
| 0x01   | 0xff | var  | Ready (0 = Not ready, 1 = Ready) |

Packet #n + 1:
|  offset  | out |  in  | description |
|----------|-----|------|-------------|
| 0x00-var | var | 0x00 | Write data  |

Packet var:
| offset | out  |  in  |     description     |
|--------|------|------|---------------------|
| 0x00   | 0xff | var  | Padding             |
| 0x01   | 0xff | var  | Bytes written >> 24 |
| 0x02   | 0xff | var  | Bytes written >> 16 |
| 0x03   | 0xff | var  | Bytes written >> 8  |
| 0x04   | 0xff | var  | Bytes written       |
| 0x05   | 0xff | 0xff | Termination byte    |

Flow:
- Header
- Wait up to 2 seconds for return value
- 16x 256 byte data packets
- Wait up to 2 seconds for ready while data is written to SD card
- 16x 256 byte data packets
- Wait up to 2 seconds for ready while data is written to SD card
- Footer

Notes:
If an error occurs, the MMCE is to send a return value of 1 and copy the errno value
to the status struct for the PS2 to obtain later. (get status not implemented yet)

If the return value is 1, no further packets will be sent.

Write ready signifies that there is a 4KB empty buffer ready to write to.

SD2PSX specific notes:
The current SD2PSX implementation waits for 4KB or length to be written to the buffer
before starting the write. 


### 0x44 - File: lseek [implemented]
Packet #1:
| offset | out  |  in  |   description    |
|--------|------|------|------------------|
| 0x03   | var  | 0x00 | File descriptor  |
| 0x04   | var  | 0x00 | Offset >> 24     |
| 0x05   | var  | 0x00 | Offset >> 16     |
| 0x06   | var  | 0x00 | Offset >> 8      |
| 0x07   | var  | 0x00 | Offset           |
| 0x08   | var  | 0x00 | Whence           |
| 0x09   | 0xff | var  | Position >> 24   |
| 0x0a   | 0xff | var  | Position >> 16   |
| 0x0b   | 0xff | var  | Position >> 8    |
| 0x0c   | 0xff | var  | Position         |
| 0x0d   | 0xff | 0xff | Termination byte |

Notes:
If an error occurs, the MMCE is to send a pos value of -1/0xFFFFFFFF and copy the errno
value to the status struct for the PS2 to obtain later. (get status not implemented yet)

### 0x45 - File: ioctl [N/A]

### 0x46 - File: remove [multi-packet] [implemented]
Packet #1:
| offset | out  |  in  | description |
|--------|------|------|-------------|
| 0x03   | 0xff | 0x00 | Padding     |

Packet #2:
|  offset  | out |  in  | description |
|----------|-----|------|-------------|
| 0x00-var | var | 0x00 | Filename    |

Packet #3:
| offset | out  |  in  |   description    |
|--------|------|------|------------------|
| 0x00   | 0xff | 0x00 | Padding          |
| 0x01   | 0xff | var  | Return value     |
| 0x02   | 0xff | 0xff | Termination byte |

Notes:
If an error occurs, the MMCE is to send a return value of 1 and copy the errno value
to the status struct for the PS2 to obtain later. (get status not implemented yet)

### 0x47 - File: mkdir [multi-packet] [implemented]
Packet #1:
| offset | out  |  in  | description |
|--------|------|------|-------------|
| 0x03   | 0xff | 0x00 | Padding     |

Packet #2:
|  offset  | out |  in  |  description   |
|----------|-----|------|----------------|
| 0x00-var | var | 0x00 | Directory name |

Packet #3:
| offset | out  |  in  |   description    |
|--------|------|------|------------------|
| 0x00   | 0xff | 0x00 | Padding          |
| 0x01   | 0xff | var  | Return value     |
| 0x02   | 0xff | 0xff | Termination byte |

Notes: 
Fileio's mkdir does not have a flag param

If an error occurs, the MMCE is to send a return value of 1 and copy the errno value
to the status struct for the PS2 to obtain later. (get status not implemented yet)

### 0x48 - File: rmdir [multi-packet] [implemented]
Packet #1:
| offset | out  |  in  | description |
|--------|------|------|-------------|
| 0x03   | 0xff | 0x00 | Padding     |

Packet #2:
|  offset  | out |  in  |  description   |
|----------|-----|------|----------------|
| 0x00-var | var | 0x00 | Directory name |

Packet #3:
| offset | out  |  in  |   description    |
|--------|------|------|------------------|
| 0x00   | 0xff | 0x00 | Padding          |
| 0x01   | 0xff | var  | Return value     |
| 0x02   | 0xff | 0xff | Termination byte |

Notes:
If an error occurs, the MMCE is to send a return value of 1 and copy the errno value
to the status struct for the PS2 to obtain later. (get status not implemented yet)

### 0x49 - File: dopen [multi-packet] [implemented]
Packet #1:
| offset | out  |  in  | description |
|--------|------|------|-------------|
| 0x03   | 0xff | 0x00 | Padding     |

Packet #2:
|  offset  | out |  in  |  description   |
|----------|-----|------|----------------|
| 0x00-var | var | 0x00 | Directory name |

Packet #3:
| offset | out  |  in  |   description    |
|--------|------|------|------------------|
| 0x00   | 0xff | 0x00 | Padding          |
| 0x01   | 0xff | var  | File descriptor  |
| 0x02   | 0xff | 0xff | Termination byte |

Notes:
If an error occurs, the MMCE is to send a fd value of -1 and copy the errno value
to the status struct for the PS2 to obtain later. (get status not implemented yet)

### 0x4a - File: dclose [implemented]
Packet #1
|  offset  | out  |  in  |      description         |
|----------|------|------|--------------------------|
| 0x03     | var  | 0x00 | File descriptor          |
| 0x04     | 0xff | var  | Return value             |
| 0x05     | 0xff | 0xff | Termination byte         |

Notes:
If an error occurs, the MMCE is to send a return value of 1 and copy the errno value
to the status struct for the PS2 to obtain later. (get status not implemented yet)

### 0x4b - File: dread [multi-packet] [implemented]
Packet #1:
| offset | out  |  in  |   description    |
|--------|------|------|------------------|
| 0x03   | var  | 0x00 | File descriptor  |
| 0x04   | 0xff | var  | Return value     |

Packet #2: io_stat_t + filename len
|   offset    | out  |  in  | description  |
|-------------|------|------|--------------|
| 0x00        | 0xff | 0x00 | Padding      |
| 0x01        | 0xff | var  | Mode >> 24   |
| 0x02        | 0xff | var  | Mode >> 16   |
| 0x03        | 0xff | var  | Mode >> 8    |
| 0x04        | 0xff | var  | Mode         |
| 0x05        | 0xff | var  | Attr >> 24   |
| 0x06        | 0xff | var  | Attr >> 16   |
| 0x07        | 0xff | var  | Attr >> 8    |
| 0x08        | 0xff | var  | Attr         |
| 0x09        | 0xff | var  | Size >> 24   |
| 0x0A        | 0xff | var  | Size >> 16   |
| 0x0B        | 0xff | var  | Size >> 8    |
| 0x0C        | 0xff | var  | Size         |
| 0x0D - 0x14 | 0xff | var  | ctime[8]     |
| 0x15 - 0x1C | 0xff | var  | atime[8]     |
| 0x1D - 0x24 | 0xff | var  | mtime[8]     |
| 0x25        | 0xff | var  | Hisize >> 24 |
| 0x26        | 0xff | var  | Hisize >> 16 |
| 0x27        | 0xff | var  | Hisize >> 8  |
| 0x28        | 0xff | var  | Hisize       |
| 0x29        | 0xff | var  | Filename len |

Packet #3:
|  offset  | out  |  in  | description |
|----------|------|------|-------------|
| 0x00-var | 0xff | var  | Filename    |

Packet #4: 
| offset | out  |  in  |   description    |
|--------|------|------|------------------|
| 0x00   | 0xff | 0x00 | Padding          |
| 0x01   | 0xff | 0x00 | it_fd            |
| 0x02   | 0xff | 0xff | Termination byte |

Notes:
The MMCE is expected to convert it's stat data to match that of the PS2's io_stat_t 

The first byte of the filename is needed prior to the start of the filename packet

If an error occurs, the MMCE is to return a nonzero value and copy the errno value
to the status struct for the PS2 to obtain later. (get status not implemented yet)

If the return value is nonzero, no further packets will be sent.

Note: Any nonzero value will work both in the case of general errors and to finish
iteration when there are no more files left to stat() in this directory.
Suggest returning 0xFF errors, and 0x1 for "no errors, but no more files".

Note: OPL will stat() the results of dread() as they happen, so remember to
keep separate vars/caches for that stuff.

Note: You must send a null terminator with the Filename in packet #3, meaning
"Filename len" will be strlen(Filename) + 1;

### 0x4c - File: getstat [multi-packet] [implemented]
Packet #1:
| offset | out  |  in  |   description    |
|--------|------|------|------------------|
| 0x03   | 0xff | 0x00 | Padding          |

Packet #2:
|  offset  | out |  in  | description |
|----------|-----|------|-------------|
| 0x00-var | var | 0x00 | Filename    |

Packet #3:
|   offset    | out  |  in  | description  |
|-------------|------|------|--------------|
| 0x00        | 0xff | 0x00 | Padding      |
| 0x01        | 0xff | var  | Mode >> 24   |
| 0x02        | 0xff | var  | Mode >> 16   |
| 0x03        | 0xff | var  | Mode >> 8    |
| 0x04        | 0xff | var  | Mode         |
| 0x05        | 0xff | var  | Attr >> 24   |
| 0x06        | 0xff | var  | Attr >> 16   |
| 0x07        | 0xff | var  | Attr >> 8    |
| 0x08        | 0xff | var  | Attr         |
| 0x09        | 0xff | var  | Size >> 24   |
| 0x0A        | 0xff | var  | Size >> 16   |
| 0x0B        | 0xff | var  | Size >> 8    |
| 0x0C        | 0xff | var  | Size         |
| 0x0D - 0x14 | 0xff | var  | ctime[8]     |
| 0x15 - 0x1C | 0xff | var  | atime[8]     |
| 0x1D - 0x24 | 0xff | var  | mtime[8]     |
| 0x25        | 0xff | var  | Hisize >> 24 |
| 0x26        | 0xff | var  | Hisize >> 16 |
| 0x27        | 0xff | var  | Hisize >> 8  |
| 0x28        | 0xff | var  | Hisize       |
| 0x29        | 0xff | var  | Return value |
| 0x2a        | 0xff | var  | Term         |

Notes:
The "Filename" packet will be of variable size and should include the null terminator.

The "Mode" value should be converted to a format the PS2 understands,
specifically as defined in the "ps2_fileio_stat_t" type.
Note: the mode flags are not the same as those found in the raw memory card filesystem.

The "Attr" value is not currently used.

### 0x4d - File: chstat [N/A]
### 0x4e - File: rename [N/A]
### 0x4f - File: chdir [N/A]
### 0x50 - File: sync [N/A]
### 0x51 - File: mount [N/A]
### 0x52 - File: umount [N/A]

### 0x53 - File: lseek64 [implemented]
Packet #1:
| offset | out  |  in  |   description    |
|--------|------|------|------------------|
| 0x03   | var  | 0x00 | File descriptor  |
| 0x04   | var  | 0x00 | Offset >> 56     |
| 0x05   | var  | 0x00 | Offset >> 48     |
| 0x06   | var  | 0x00 | Offset >> 40     |
| 0x07   | var  | 0x00 | Offset >> 32     |
| 0x08   | var  | 0x00 | Offset >> 24     |
| 0x09   | var  | 0x00 | Offset >> 16     |
| 0x0a   | var  | 0x00 | Offset >> 8      |
| 0x0b   | var  | 0x00 | Offset           |
| 0x0c   | var  | 0x00 | Whence           |
| 0x0d   | 0xff | var  | Position >> 56   |
| 0x0e   | 0xff | var  | Position >> 48   |
| 0x0f   | 0xff | var  | Position >> 40   |
| 0x10   | 0xff | var  | Position >> 32   |
| 0x11   | 0xff | var  | Position >> 24   |
| 0x12   | 0xff | var  | Position >> 16   |
| 0x13   | 0xff | var  | Position >> 8    |
| 0x14   | 0xff | var  | Position         |
| 0x15   | 0xff | 0xff | Termination byte |

Notes:
If an error occurs, the MMCE is to send a pos value of -1/0xFFFFFFFF and copy the errno
value to the status struct for the PS2 to obtain later. (get status not implemented yet)

### 0x54 - File: devctl [N/A]
### 0x55 - File: symlink [N/A]
### 0x56 - File: readlink [N/A]
### 0x57 - File: ioctl2 [N/A]

### 0x58 - File: Pseudo-sector read [multi-packet] [implemented]
Packet #1:
| offset | out  |  in  |            description             |
|--------|------|------|------------------------------------|
| 0x03   | var  | 0x00 | File descriptor                    |
| 0x04   | var  | 0x00 | Sector >> 16                       |
| 0x05   | var  | 0x00 | Sector >> 8                        |
| 0x06   | var  | 0x00 | Sector                             |
| 0x07   | var  | 0x00 | Length >> 16                       |
| 0x08   | var  | 0x00 | Length >> 8                        |
| 0x09   | var  | 0x00 | Length                             |
| 0x0A   | 0xff | 0x00 | Return value (0 = Okay, 1 = Error) |

Packet #n + 1 - var:
|  offset  | out  | in  | description |
|----------|------|-----|-------------|
| 0x00-var | 0x00 | var | Read data   |

Packet final:
| offset | out  |  in  |     description      |
|--------|------|------|----------------------|
| 0x00   | 0xff | 0x00 | Padding              |
| 0x01   | 0xff | var  | Sectors read >> 16   |
| 0x02   | 0xff | var  | Sectors read >> 8    |
| 0x03   | 0xff | var  | Sectors read         |
| 0x04   | 0xff | 0xff | Termination Byte     |

Notes:
Sector size is 2048

Essentially lseek + read in a single command

Sending the upper 8 bits of the sector and length can be skipped
as the sector count will never be higher than 1 << 23 (0x800000)

As a general rule of thumb, sector reads behave the same as 0x42 - File Read
in terms of the way errors are handled, and how transfers are broken into 
smaller chunks of 256.
