#ifndef MTFTP_H
#define MTFTP_H

#include "sdkconfig.h"
#include <stdint.h>

// length of header of packet_data (minus length of block)
const uint8_t LEN_DATA_HEADER = 3;

enum packet_types {
  TYPE_READ_REQUEST = 1,
  TYPE_DATA,
  TYPE_ACK,
  TYPE_ERR
};

enum err_types {
  ERR_FREAD = 1
};

const char *err_types_str[] = {
  "FileReadErr"
};

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

#endif
