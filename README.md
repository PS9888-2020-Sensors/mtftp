# Modified TFTP (mtftp)

Protocol built for read-only transmission of files, loosely based off the [TFTP RFC](https://tools.ietf.org/html/rfc1350) and the [Windowsize Option RFC](https://tools.ietf.org/html/rfc7440).

Four types of packets are defined:
1. Read Request (RRQ)

    Used to start transfer of a window (consisting of multiple blocks of data)
    ```
    enum packet_types opcode:8;
    // file to read
    uint16_t file_index;
    // offset to start read at
    uint32_t file_offset;
    // number of blocks to transfer in one window
    uint16_t window_size;
    ```
2. Data (DATA)

    Contains a block of data identified by a block number. This block number resets from 0 at the start of every window
    ```
    enum packet_types opcode:8;
    uint16_t block_no;
    uint8_t block[CONFIG_LEN_BLOCK];
    ```
3. Acknowledgement (ACK)

    Used to acknowledge the successful receipt of a window (or partial window)
    ```
    enum packet_types opcode:8;
    uint16_t block_no;
    ```
4. Error (ERR)

    Used to indicate an error has occured
    ```
    enum packet_types opcode:8;
    enum err_types err:8;
    ```

## Workflow
1. __Client__
    Sends RRQ for a specific file, file offset (bytes at which to start the transfer) and window size (how many blocks to transfer before an ACK is required)
2. __Server__
    Sends `window size` DATA packets, numbered `0` to `window size - 1`. The end of file is indicated by sending a DATA packet with less than `CONFIG_LEN_BLOCK` bytes of data (or 0 bytes, if the file length is a multiple of `CONFIG_LEN_BLOCK`)
3. __Client__
    - Receives the DATA packets, keeping track of the block number of DATA packets received to ensure that data is received in-order and complete. Stores the largest correct block number received.
    - Once a DATA packet with less than `CONFIG_LEN_BLOCK` bytes of data is received OR `window size` DATA packets are received, send ACK with the largest correct block number received
4. __Server__
    Increment file offset based on the ACK packet received. If no more data is available from the file, the transmission is complete, else, go to Step 2
