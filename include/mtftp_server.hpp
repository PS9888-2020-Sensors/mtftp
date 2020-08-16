#ifndef MTFTP_SERVER_H
#define MTFTP_SERVER_H

#include "mtftp.h"

class MtftpServer {
  public:
    enum server_state {
      STATE_IDLE,
      STATE_TRANSFER,        // RRQ received, transmitting window
      STATE_RTX,             // RTX received, retransmissing missing packets
      STATE_AWAIT_RESPONSE,  // window transmitted, waiting for ACK/RTX
      STATE_NOCHANGE
    };

    const char *server_state_str[STATE_NOCHANGE + 1] = {
      "Idle",
      "Transfer",
      "Retransmit",
      "WaitAck",
      "NoChange"
    };

    void init(
      bool (*_readFile)(uint16_t file_index, uint32_t file_offset, uint8_t *data, uint16_t btr, uint16_t *br),
      void (*_sendPacket)(const uint8_t *data, uint8_t len)
    );

    void setOnIdleCb(void (*_onIdle)());
    void setOnTimeoutCb(void (*_onTimeout)());
    recv_result_t onPacketRecv(const uint8_t *data, uint16_t len_data);
    void loop(void);

    server_state getState(void) { return state; };
    bool isIdle(void) { return state == STATE_IDLE; };
  private:
    enum server_state state;

    struct {
      uint16_t file_index;
      uint32_t file_offset;
      uint16_t window_size;

      uint16_t block_no;
      int32_t largest_block_no;
      uint8_t len_largest_block;

      int64_t time_last_packet = 0;

      uint16_t rtx_index;
      uint8_t num_rtx;
      uint16_t rtx_block_nos[CONFIG_LEN_MTFTP_BUFFER];
    } transfer_params;

    bool (*readFile)(uint16_t file_index, uint32_t file_offset, uint8_t *data, uint16_t btr, uint16_t *br) = NULL;
    void (*sendPacket)(const uint8_t *data, uint8_t len) = NULL;
    void (*onIdle)() = NULL;
    void (*onTimeout)() = NULL;

    void onWindowStart(void);
    bool sendBlock(uint16_t block_no, uint16_t *bytes_read);
};

#endif
