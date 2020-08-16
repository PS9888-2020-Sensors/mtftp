#ifndef MTFTP_CLIENT_H
#define MTFTP_CLIENT_H

#include "mtftp.h"

class MtftpClient {
  public:
    enum client_state {
      STATE_IDLE,
      STATE_TRANSFER,  // RRQ sent, receiving window
      STATE_AWAIT_RTX, // RTX sent, receiving retransmits of data packets
      STATE_ACK_SENT,  // ACK sent, waiting for next window
      STATE_NOCHANGE
    };

    const char *client_state_str[STATE_NOCHANGE + 1] = {
      "Idle",
      "Transfer",
      "AwaitRTX",
      "AckSent",
      "NoChange"
    };

    MtftpClient();
    ~MtftpClient();

    void init(
      bool (*_writeFile)(uint16_t file_index, uint32_t file_offset, const uint8_t *data, uint16_t btw),
      void (*_sendPacket)(const uint8_t *data, uint8_t len)
    );

    void setOnIdleCb(void (*_onIdle)());
    void setOnTimeoutCb(void (*_onTimeout)());
    void setOnTransferEndCb(void (*_onTransferEnd)());
    recv_result_t onPacketRecv(const uint8_t *data, uint16_t len_data);
    void beginRead(uint16_t file_index, uint32_t file_offset);
    void loop(void);
    client_state getState(void) { return state; };
  private:
    enum client_state state;

    struct {
      uint16_t file_index;
      uint32_t file_offset;

      uint16_t window_size;
      
      // int32_t to represent -1 to 65535
      // stores the block no of the last successfully received block
      int32_t block_no;
      int32_t largest_block_no;
      uint8_t len_largest_block;

      int64_t time_last_packet = 0;

      // block number of the first block in the buffer
      int32_t buffer_base_block_no;
      uint8_t *buffer = NULL;
      uint8_t num_missing;
      // 0xFFFF for unused slot, no where near enough memory to buffer 65535 blocks
      uint16_t missing_block_nos[CONFIG_LEN_MTFTP_BUFFER];
    } transfer_params;

    bool (*writeFile)(uint16_t file_index, uint32_t file_offset, const uint8_t *data, uint16_t btw) = NULL;
    void (*sendPacket)(const uint8_t *data, uint8_t len) = NULL;
    void (*onIdle)() = NULL;
    void (*onTimeout)() = NULL;
    void (*onTransferEnd)() = NULL;

    void onWindowStart(void);
    client_state onWindowEnd(void);
};

#endif
