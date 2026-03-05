# Multi-purpose Memory Card Emulator (MMCE) Protocol v1.0

out = ps2 -> mmce\
in  = ps2 <- mmce

### Base packet structure:
|      offset       | out  |  in  |        description        |
|-------------------|------|------|---------------------------|
| 0x00              | 0x8b | 0xff | MMCE identifier byte      |
| 0x01              | var  | 0xaa | Cmd                       |
| 0x02              | 0xff | 0x00 | Reserved byte             |
| 0x03-var          | var  | var  | Cmd specific bytes        |
| last cmd byte + 1 | 0xff | 0xff | Termination byte          |

Note:
0xaa is used as a weak validation check. mmceman checks rdbuf[0x1] for 0xaa,
if it's not present the data is considered invalid

### 0x01 - Ping [Implemented]
| offset  | out  |  in  |     description      |
|---------|------|------|----------------------|
| 0x03    | 0xff | var  | Product version      |
| 0x04    | 0xff | var  | Product id           |
| 0x05    | 0xff | var  | Product revision     |
| 0x06    | 0xff | 0xff | Termination byte     |

Currently assigned product IDs:
1) SD2PSX
2) MemCard PRO2
3) PicoMemcard+
4) PicoMemcardZero

### 0x02 - Get Status [Partially Implemented]
| offset | out  |  in  |          description          |
|--------|------|------|-------------------------------|
| 0x03   | 0x00 | var  | Status errno                  |
| 0x04   | 0x00 | var  | Status flags                  |
| 0x05   | 0x00 | 0xff | Termination byte              |

TODO: Finish fleshing out spec for status. Currently, bit 0
is used to denote whether the MMCE is busy. (1 = busy, 0 = not busy).
This is polled in OPL after sending the GameID to ensure the card switch 
has finished before starting the game.

### 0x3 - Get Card [Implemented]
| offset | out  |  in  |        description        |
|--------|------|------|---------------------------|
| 0x03   | 0xff | var  | Current card upper 8 bits |
| 0x04   | 0xff | var  | Current card lower 8 bits |
| 0x05   | 0xff | 0xff | Termination byte          |

### 0x4 - Set Card [Implemented]
| offset | out  |  in  |         description         |
|--------|------|------|-----------------------------|
| 0x03   | var  | 0x0  | Type                        |
| .      |      |      | 0x0 = Regular card          |
| .      |      |      | 0x1 = Boot card             |
| 0x04   | var  | 0x0  | Mode                        |
| .      |      |      | 0x0 = Set card to number    |
| .      |      |      | 0x1 = Set card to next card |
| .      |      |      | 0x2 = Set card to prev card |
| 0x05   | var  | 0x0  | Card upper 8 bits           |
| 0x06   | var  | 0x0  | Card lower 8 bits           |
| 0x07   | 0xff | 0xff | Termination byte            |

### 0x5 - Get Channel [Implemented]
| offset | out  |  in  |         description          |
|--------|------|------|------------------------------|
| 0x03   | 0xff | var  | Current channel upper 8 bits |
| 0x04   | 0xff | var  | Current channel lower 8 bits |
| 0x05   | 0xff | 0xff | Termination byte             |

### 0x6 - Set Channel [Implemented]
| offset | out  |  in  |            description            |
|--------|------|------|-----------------------------------|
| 0x03   | var  | 0x0  | Mode                              |
| .      |      |      | 0x0 = Set channel to number       |
| .      |      |      | 0x1 = Set channel to next channel |
| .      |      |      | 0x2 = Set channel to prev channel |
| 0x04   | var  | 0x0  | Channel upper 8 bits              |
| 0x05   | var  | 0x0  | Channel lower 8 bits              |
| 0x06   | 0xff | 0xff | Termination byte                  |

### 0x7 - Get GameID [Implemented]
|  offset  | out  |  in  |      description       |
|----------|------|------|------------------------|
| 0x03     | 0xff | var  | Gameid len             |
| 0x04-var | 0xff | var  | Gameid (max 250 bytes) |
| 0xff     | 0xff | 0xff | Termination byte       |

Note: The SIO2 expects to receive 255 bytes of data, if the GameID is shorter than this padding bytes must be sent.

### 0x8 - Set GameID [Implemented]
|  offset  | out  |  in  |      description       |
|----------|------|------|------------------------|
| 0x03     | var  | 0x00 | Gameid len             |
| 0x04-var | var  | 0x00 | Gameid (max 250 bytes) |
| var + 1  | 0xff | 0xff | Termination byte       |

### 0x9 - Reset [Implemented]
| offset | out  |  in  |   description    |
|--------|------|------|------------------|
| 0x03   | 0xff | 0x00 | Padding          |
| 0x04   | 0xff | 0xff | Termination Byte |

Note: Sent on MMCEMAN initialization after successful ping, used to signal the MMCE to close all open file handles and reset the FS state

### 0x30 [Temporary Assignment] - Unmount Bootcard [Implemented]
| offset | out  |  in  |   description    |
|--------|------|------|------------------|
| 0x03   | 0xff | 0xff | Termination byte |