#ifndef MTFTP_SERVER_H
#define MTFTP_SERVER_H

#include "mtftp.h"

class MtftpServer {
  public:
    enum server_state {
      STATE_IDLE,
      STATE_TRANSFER,  // RRQ received, transmitting window
      STATE_WAIT_ACK,  // window transmitted, waiting for ACK
      STATE_NOCHANGE
    };

    const char *server_state_str[4] = {
      "Idle",
      "Transfer",
      "WaitAck",
      "NoChange"
    };

    void init(
      bool (*_readFile)(uint16_t file_index, uint32_t file_offset, uint8_t *data, uint16_t btr, uint16_t *br),
      void (*_sendPacket)(const uint8_t *data, uint8_t len)
    );

    void setOnIdleCb(void (*_onIdle)());
    recv_result_t onPacketRecv(const uint8_t *data, uint16_t len_data);
    void loop(void);

    bool isIdle(void) { return state == STATE_IDLE; };
  private:
    enum server_state state;

    struct {
      uint16_t file_index;
      uint32_t file_offset;
      uint16_t window_size;

      uint16_t block_no;
      uint16_t bytes_read;

      int64_t time_last_packet = 0;
    } transfer_params;

    bool (*readFile)(uint16_t file_index, uint32_t file_offset, uint8_t *data, uint16_t btr, uint16_t *br);
    void (*sendPacket)(const uint8_t *data, uint8_t len);
    void (*onIdle)();

    int64_t last_packet_received = -1;
};

#endif
