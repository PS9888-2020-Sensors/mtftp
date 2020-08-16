#ifndef MTFTP_H
#define MTFTP_H

#include "sdkconfig.h"
#include <stdint.h>

// length of header of packet_data (minus length of block)
const uint8_t LEN_DATA_HEADER = 3;
const uint8_t LEN_RTX_HEADER = 2;
// max number of block nos that can be sent in a TYPE_RETRANSMIT packet
const uint8_t LEN_RETRANSMIT = (250 - 2) / sizeof(uint16_t);

enum packet_types {
  TYPE_READ_REQUEST = 1,
  TYPE_DATA,
  TYPE_RETRANSMIT,
  TYPE_ACK,
  TYPE_ERR
};

enum err_types {
  ERR_FREAD
};

extern const char *err_types_str[1];

typedef struct __attribute__((__packed__)) packet_rrq {
  enum packet_types opcode:8;
  // file to read
  uint16_t file_index;
  // offset to start read at
  uint32_t file_offset;
  // number of chunks to transfer in one window
  uint16_t window_size;

  packet_rrq(): opcode(TYPE_READ_REQUEST) {}
} packet_rrq_t;

typedef struct __attribute__((__packed__)) packet_data {
  enum packet_types opcode:8;
  uint16_t block_no;
  uint8_t block[CONFIG_LEN_BLOCK];

  packet_data(): opcode(TYPE_DATA) {}
} packet_data_t;

typedef struct __attribute__((__packed__)) packet_rtx {
  enum packet_types opcode:8;
  uint8_t num_elements;
  uint16_t block_nos[LEN_RETRANSMIT];

  packet_rtx(): opcode(TYPE_RETRANSMIT) {}
} packet_rtx_t;

typedef struct __attribute__((__packed__)) packet_ack {
  enum packet_types opcode:8;
  uint16_t block_no;

  packet_ack(): opcode(TYPE_ACK) {}
} packet_ack_t;

typedef struct __attribute__((__packed__)) packet_err {
  enum packet_types opcode:8;
  enum err_types err:8;

  packet_err(): opcode(TYPE_ERR) {}
} packet_err_t;

typedef enum {
  RECV_UNSET,
  RECV_OK,
  RECV_LEN,
  RECV_STATE,
  RECV_BAD_OPCODE,
  RECV_BAD_AFT_ACK,
  RECV_BAD_BLOCK_NO
} recv_result_t;

#endif
