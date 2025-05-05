<center>
<img src="imgs/logo-education.png"  width="300">

Created by Ouroboros Embedded Education.
</center>

## Versions Changelog

V1.0.0

- Initial Release

# YMODEM Protocol Library Documentation

<center><img src="imgs/folder.png"  width="100"></center>

This documentation describes the YMODEM protocol implementation as defined in the provided [`ymodem.c`](#ymodemc) and [`ymodem.h`](#ymodemh) files. The library enables robust file transfer over UART, suitable for embedded systems such as STM32, MSP430, and other microcontrollers.

This library is based on work performed by https://github.com/edholmes2232.

---

## Table of Contents

- [YMODEM Protocol Library Documentation](#ymodem-protocol-library-documentation)
  - [Table of Contents](#table-of-contents)
  - [Overview](#overview)
  - [Features](#features)
  - [Data Structures](#data-structures)
    - [`ymodem_t`](#ymodem_t)
  - [Enumerations](#enumerations)
    - [`ymodem_err_e`](#ymodem_err_e)
    - [`ymodem_file_cb_e`](#ymodem_file_cb_e)
  - [API Reference](#api-reference)
    - [Initialization](#initialization)
    - [Receiving Data](#receiving-data)
    - [Resetting State](#resetting-state)
    - [Aborting Transfer](#aborting-transfer)
  - [Callback Mechanism](#callback-mechanism)
  - [Example Usage](#example-usage)
  - [Implementation Notes](#implementation-notes)
  - [References](#references)
  - [License](#license)

---

## Overview

This library implements the YMODEM protocol for reliable, block-based file transfer over serial connections. It supports both 128-byte and 1K-byte packet sizes, CRC16 error checking, and robust error/abort handling. The implementation is platform-agnostic and requires only a user-supplied function for serial transmission.

---

## Features

- **YMODEM protocol**: Reliable file transfer with error detection.
- **Supports 128B and 1KB packets**: For compatibility and efficiency.
- **CRC16 checking**: Ensures data integrity.
- **Abort and error handling**: Graceful session termination on error.
- **User callback**: Application notified of file name, data, end, or abort events.
- **MCU-independent**: Only requires a user-supplied serial write function.

---

## Data Structures

### `ymodem_t`

The main handle for a YMODEM session. Key fields:


| Field | Description |
| :-- | :-- |
| `fileName` | Received file name |
| `fileSizeStr` | Received file size as string |
| `packetData` | Buffer for current packet |
| `payloadTx` | Buffer for response payload to sender |
| `payloadLen` | Length of response payload |
| `initialized` | Initialization flag |
| `fileSize` | File size as integer |
| `prevC` | Previous received byte |
| `startOfPacket` | Flag for start of packet |
| `eotReceived` | End-of-transmission flag |
| `packetBytes` | Number of bytes received for current packet |
| `packetSize` | Size of current packet |
| `packetsReceived` | Number of packets received |
| `nextStatus` | Status to return after closing connection |
| `serialWriteFxn` | Function pointer for writing data to serial |


---

## Enumerations

### `ymodem_err_e`

Return values for YMODEM functions:

- `YMODEM_OK` - All OK, send next byte
- `YMODEM_TX_PENDING` - Data waiting to be transmitted
- `YMODEM_ABORTED` - Transfer aborted
- `YMODEM_WRITE_ERR` - Error writing to flash
- `YMODEM_SIZE_ERR` - File is bigger than flash
- `YMODEM_COMPLETE` - Transfer completed successfully


### `ymodem_file_cb_e`

Callback event types:

- `YMODEM_FILE_CB_NAME` - File name received
- `YMODEM_FILE_CB_DATA` - File data received
- `YMODEM_FILE_CB_END` - End of file transfer
- `YMODEM_FILE_CB_ABORTED` - Transfer aborted

---

## API Reference

### Initialization

```c
void ymodem_Init(ymodem_t *ymodem, ymodem_fxn_t SerialWriteFxn);
```

Initializes a YMODEM session.

- `ymodem`: Pointer to the YMODEM handle.
- `SerialWriteFxn`: Function pointer for sending data to the sender.

---

### Receiving Data

```c
ymodem_err_e ymodem_ReceiveByte(ymodem_t *ymodem, uint8_t byte);
```

Processes a received byte from the sender.

- Returns a status from `ymodem_err_e`.
- Handles packet assembly, CRC checking, and triggers callbacks as needed.

---

### Resetting State

```c
ymodem_err_e ymodem_Reset(ymodem_t *ymodem);
```

Resets the YMODEM state machine and variables for a new transfer.

---

### Aborting Transfer

```c
ymodem_err_e ymodem_Abort(ymodem_t *ymodem);
```

Aborts the transfer, prepares abort payload, and resets relevant state.

---

## Callback Mechanism

The library uses a callback to notify the application of protocol events:

```c
ymodem_err_e ymodem_FileCallback(
    ymodem_t *ymodem,
    ymodem_file_cb_e e,
    uint8_t *data,
    uint32_t len
);
```

- **YMODEM_FILE_CB_NAME**: `data` points to file name; `len` is file size.
- **YMODEM_FILE_CB_DATA**: `data` points to received file data; `len` is data length.
- **YMODEM_FILE_CB_END**: Transfer completed; `data` and `len` unused.
- **YMODEM_FILE_CB_ABORTED**: Transfer aborted; `data` and `len` unused.

> **Note:**
> The callback may be called from within the receive function. It is recommended to avoid calling `ymodem_ReceiveByte` from an interrupt context; use a ring buffer or queue instead.

---

## Example Usage

```c
#include "ymodem.h"

// User-defined serial write function
uint8_t SerialWrite(uint8_t *data, uint32_t len) {
    // Implement UART transmission here
    return 0;
}

// Application callback
ymodem_err_e ymodem_FileCallback(ymodem_t *ymodem, ymodem_file_cb_e e, uint8_t *data, uint32_t len) {
    switch (e) {
        case YMODEM_FILE_CB_NAME:
            // data: file name, len: file size
            break;
        case YMODEM_FILE_CB_DATA:
            // data: file data, len: data length
            break;
        case YMODEM_FILE_CB_END:
            // Transfer complete
            break;
        case YMODEM_FILE_CB_ABORTED:
            // Transfer aborted
            break;
    }
    return YMODEM_OK;
}

int main(void) {
    ymodem_t ymodem;
    ymodem_Init(&amp;ymodem, SerialWrite);

    // In your UART receive loop:
    uint8_t rx_byte;
    while (1) {
        // rx_byte = ... (received from UART)
        ymodem_ReceiveByte(&amp;ymodem, rx_byte);
    }
}
```


---

## Implementation Notes

- **Packet Sizes:** Supports 128B and 1KB packets, with appropriate header and trailer sizes.
- **CRC16:** Used for packet integrity. CRC polynomial: `0x1021`.
- **Control Characters:** SOH, STX, EOT, ACK, NAK, CA, CRC16, ABORT1, ABORT2.
- **File Name and Size:** Extracted from the first packet and provided to the callback.
- **Flash Writing:** Actual writing is handled by the application via the callback.

---

## References

- [YMODEM protocol reference (textfiles.com)](http://textfiles.com/programming/ymodem.txt)
- [YMODEM protocol learning (programmer.ink)](https://programmer.ink/think/ymodem-protocol-learning.html)

---

## License

Check the License file presenting on this repository, to get more details.

---

**For further details, review the code and comments in `ymodem.c` and `ymodem.h`.**
