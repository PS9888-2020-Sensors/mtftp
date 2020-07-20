#ifndef MTFTP_CLIENT_H
#define MTFTP_CLIENT_H

class MtftpClient {
  public:
    enum client_state {
      STATE_IDLE,
      STATE_TRANSFER,  // RRQ sent, receiving window
      STATE_ACK_SENT,  // ACK sent, waiting for next window
      STATE_NOCHANGE
    };

    const char *client_state_str[4] = {
      "Idle",
      "Transfer",
      "AckSent",
      "NoChange"
    };

    void init(
      bool (*_writeFile)(uint16_t file_index, uint32_t file_offset, uint8_t *data, uint16_t btw),
      void (*_sendPacket)(uint8_t *data, uint8_t len)
    );
    void onPacketRecv(uint8_t *data, uint16_t len_data);
    void beginRead(uint16_t file_index, uint32_t file_offset);
  private:
    enum client_state state;

    struct {
      uint16_t file_index;
      uint32_t file_offset;

      uint16_t window_size;
      
      // int32_t to represent -1 to 65535
      int32_t block_no;
    } transfer_params;

    bool (*writeFile)(uint16_t file_index, uint32_t file_offset, uint8_t *data, uint16_t btw);
    void (*sendPacket)(uint8_t *data, uint8_t len);
};

#endif
