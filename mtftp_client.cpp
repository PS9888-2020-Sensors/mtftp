#include <string.h>
#include "freertos/FreeRTOS.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "sdkconfig.h"

#include "mtftp.h"
#include "mtftp_client.hpp"

static const char *TAG = "mtftp-client";

MtftpClient::MtftpClient() {
  transfer_params.buffer = (uint8_t *) malloc(CONFIG_LEN_MTFTP_BUFFER * CONFIG_LEN_BLOCK);
  if (transfer_params.buffer == NULL) {
    ESP_LOGW(TAG, "failed to allocate data packet buffer");
  }

  assert(transfer_params.buffer != NULL);
}

MtftpClient::~MtftpClient() {
  free(transfer_params.buffer);
}

void MtftpClient::init(
    bool (*_writeFile)(uint16_t file_index, uint32_t file_offset, const uint8_t *data, uint16_t btw),
    void (*_sendPacket)(const uint8_t *data, uint8_t len)
  ) {
  state = STATE_IDLE;

  writeFile = _writeFile;
  sendPacket = _sendPacket;
}

void MtftpClient::setOnIdleCb(void (*_onIdle)()) {
  onIdle = _onIdle;
}

void MtftpClient::setOnTimeoutCb(void (*_onTimeout)()) {
  onTimeout = _onTimeout;
}

void MtftpClient::setOnTransferEndCb(void (*_onTransferEnd)()) {
  onTransferEnd = _onTransferEnd;
}

void MtftpClient::onWindowStart(void) {
  transfer_params.block_no = -1;
  transfer_params.largest_block_no = -1;
  transfer_params.len_largest_block = 0;

  transfer_params.buffer_base_block_no = -1;
  transfer_params.num_missing = 0;
  memset(transfer_params.missing_block_nos, 0xFF, sizeof(transfer_params.missing_block_nos));
}

enum MtftpClient::client_state MtftpClient::onWindowEnd(void) {
  const char *TAG = "onWindowEnd";

  enum client_state new_state = STATE_NOCHANGE;

  if (transfer_params.num_missing > 0) {
    // if there are buffered packets, we're missing at least one packet
    // send out a RTX
    packet_rtx_t rtx_pkt;

    rtx_pkt.num_elements = transfer_params.num_missing;

    uint16_t local_index = 0;
    for(uint16_t i = 0; i < LEN_RETRANSMIT; i++) {
      if (local_index >= CONFIG_LEN_MTFTP_BUFFER) break;

      // advance local_index until non 0xFFFF block_no
      while (transfer_params.missing_block_nos[local_index] == 0xFFFF) {
        local_index ++;

        if (local_index >= CONFIG_LEN_MTFTP_BUFFER) break;
      }

      rtx_pkt.block_nos[i] = transfer_params.missing_block_nos[local_index];
    }

    ESP_LOGD(TAG, "sending rtx for %d block(s)", rtx_pkt.num_elements);
    sendPacket((uint8_t *) &rtx_pkt, LEN_RTX_HEADER + (rtx_pkt.num_elements * sizeof(uint16_t)));

    new_state = STATE_AWAIT_RTX;
  }
  else {
    // the entire window has been received successfully, ACK
    packet_ack_t ack_pkt;
    ack_pkt.block_no = transfer_params.block_no;

    sendPacket((uint8_t *) &ack_pkt, sizeof(ack_pkt));

    // the largest block is not full (final block), nothing buffered, end of transfer
    if (transfer_params.len_largest_block < CONFIG_LEN_BLOCK) {
      new_state = STATE_IDLE;

      if (*onTransferEnd != NULL) onTransferEnd();
    } else {
      new_state = STATE_ACK_SENT;
    }
  }

  return new_state;
}

recv_result_t MtftpClient::onPacketRecv(const uint8_t *data, uint16_t len_data) {
  const char *TAG = "onPacketRecv";

  if (len_data < 1) {
    ESP_LOGW(TAG, "called with len_data == 0!");
    return RECV_LEN;
  }

  recv_result_t result = RECV_UNSET;

  enum client_state new_state = STATE_NOCHANGE;

  switch(*data) {
    case TYPE_DATA:
    {
      if (len_data < LEN_DATA_HEADER) {
        ESP_LOGW(TAG, "len DATA packet is %d (< %d)", len_data, LEN_DATA_HEADER);

        result = RECV_LEN;
        break;
      }

      if (state != STATE_TRANSFER && state != STATE_AWAIT_RTX && state != STATE_ACK_SENT) {
        ESP_LOGW(TAG, "DATA received in state %s", client_state_str[state]);

        result = RECV_STATE;
        break;
      }

      packet_data_t *data_pkt = (packet_data_t *) data;

      // new window
      if (state == STATE_ACK_SENT) {
        // first block received should be block 0
        if (data_pkt->block_no != 0) {
          ESP_LOGW(TAG, "first block after ACK has non zero block_no: %d", data_pkt->block_no);
          new_state = STATE_IDLE;

          result = RECV_BAD_AFT_ACK;
          break;
        }

        onWindowStart();
        new_state = STATE_TRANSFER;
      }

      uint8_t len_block = len_data - LEN_DATA_HEADER;

      if (data_pkt->block_no >= transfer_params.window_size) {
        ESP_LOGW(TAG, "received block %d when window size is only %d", data_pkt->block_no, transfer_params.window_size);
        new_state = STATE_IDLE;

        result = RECV_BAD_BLOCK_NO;
        break;
      }

      result = RECV_OK;

      if (data_pkt->block_no > transfer_params.largest_block_no) {
        transfer_params.largest_block_no = data_pkt->block_no;
        transfer_params.len_largest_block = len_block;
      }

      bool buffer_packet = true;

      // if in STATE_TRANSFER or STATE_ACK_SENT and we have nothing buffered so far,
      // check whether the block_no is expected and call writeFile if so
      // else, buffer the block (and all future blocks)
      if ((state == STATE_TRANSFER || state == STATE_ACK_SENT) && transfer_params.num_missing == 0) {
        if (data_pkt->block_no == (transfer_params.block_no + 1)) {
          // received the next block with the expected block no
          ESP_LOGD(TAG, "received block %d with len %d", data_pkt->block_no, len_block);
          writeFile(
            transfer_params.file_index,
            transfer_params.file_offset,
            data_pkt->block,
            len_block
          );

          transfer_params.block_no = data_pkt->block_no;

          // advance file_offset by the number of bytes we just received
          transfer_params.file_offset += len_block;

          buffer_packet = false;
        } else {
          // out of order block, attempt to buffer it
          ESP_LOGW(TAG, "out of order packet: expected %d, got %d", transfer_params.block_no + 1, data_pkt->block_no);

          // if no buffered data
          if (transfer_params.buffer_base_block_no == -1) {
            // set the base of the buffer to the missing packet
            transfer_params.buffer_base_block_no = transfer_params.block_no + 1;

            ESP_LOGD(TAG, "buffer_base=%d", transfer_params.buffer_base_block_no);
          }
        }
      }

      // ensure that current block_no does not overflow CONFIG_LEN_MTFTP_BUFFER
      // then buffer the packet
      if (buffer_packet && (data_pkt->block_no - transfer_params.buffer_base_block_no) <= CONFIG_LEN_MTFTP_BUFFER) {
        ESP_LOGD(TAG, "buffering block_no=%d", data_pkt->block_no);

        // if we're in STATE_AWAIT_RTX, the current block_no should be in missing_block_nos
        // remove it since we already have it
        if (state == STATE_AWAIT_RTX) {
          uint16_t found_index = 0xFFFF;

          for(uint16_t i = 0; i < CONFIG_LEN_MTFTP_BUFFER; i++) {
            if (transfer_params.missing_block_nos[i] == data_pkt->block_no) {
              found_index = i;
              break;
            }
          }

          if (found_index == 0xFFFF) {
            ESP_LOGW(TAG, "received block_no=%d but not in missing_block_nos!", data_pkt->block_no);

            result = RECV_BAD_BLOCK_NO;
            break;
          }

          transfer_params.missing_block_nos[found_index] = 0xFFFF;
          transfer_params.num_missing --;
        }

        memcpy(
          transfer_params.buffer + (CONFIG_LEN_BLOCK * (data_pkt->block_no - transfer_params.buffer_base_block_no)),
          data_pkt->block,
          len_block
        );

        if (state == STATE_TRANSFER || state == STATE_ACK_SENT) {
          // add missing block nos
          for(uint16_t block_no = transfer_params.block_no + 1; block_no < data_pkt->block_no; block_no ++) {
            if (
              transfer_params.num_missing > 0 &&
              data_pkt->block_no < transfer_params.missing_block_nos[(uint8_t) transfer_params.num_missing - 1]
            ) {
              ESP_LOGW(TAG, "buffering block_no=%d (but out of order!)", data_pkt->block_no);
            }

            ESP_LOGD(TAG, "marking block_no=%d missing at index=%d", block_no, transfer_params.num_missing);

            transfer_params.missing_block_nos[transfer_params.num_missing++] = block_no;
          }

          transfer_params.block_no = data_pkt->block_no;
        }
      }

      if (state == STATE_TRANSFER || state == STATE_ACK_SENT) {
        // end of the window:
        // receiving less than one full block of data
        if (len_block < CONFIG_LEN_BLOCK) {
          ESP_LOGD(TAG, "end of transfer (partial block of %d bytes)", len_block);
        } else if (data_pkt->block_no == (transfer_params.window_size - 1)) {
          // or this packet is the final block in the window
          // possibility that prior packets have been lost
          ESP_LOGD(TAG, "end of window (%d blocks)", transfer_params.window_size);
        } else {
          break;
        }
      } else if (state == STATE_AWAIT_RTX) {
        if (transfer_params.num_missing > 0) {
          // if there are still packets missing, we cant end the window yet
          break;
        }

        ESP_LOGD(TAG,
          "writing buffer with largest_block_no=%d len_largest=%d buffer_base=%d",
          transfer_params.largest_block_no,
          transfer_params.len_largest_block,
          transfer_params.buffer_base_block_no
        );
        writeFile(
          transfer_params.file_index,
          transfer_params.file_offset,
          transfer_params.buffer,
          // append len_largest_block for the possibility that the largest block
          // isnt a full block
          (transfer_params.largest_block_no - transfer_params.buffer_base_block_no) * CONFIG_LEN_BLOCK + transfer_params.len_largest_block
        );

        ESP_LOGD(TAG, "all missing packets received, ending window");
      }

      enum client_state change = onWindowEnd();
      if (change != STATE_NOCHANGE) {
        new_state = change;
      }

      break;
    }
    case TYPE_ERR:
    {
      if (len_data != sizeof(packet_err_t)) {
        ESP_LOGW(TAG, "onPacketRecv: len ERR packet is %d (!= %d)", len_data, sizeof(packet_err_t));

        result = RECV_LEN;
        break;
      }

      result = RECV_OK;

      packet_err_t *pkt = (packet_err_t *) data;

      ESP_LOGW(TAG, "onPacketRecv: recv err %s", err_types_str[pkt->err]);

      if (state != STATE_IDLE) {
        new_state = STATE_IDLE;
      }
      break;
    }
    default:
      ESP_LOGW(TAG, "onPacketRecv: bad packet opcode: %02X", *data);
      
      result = RECV_BAD_OPCODE;
      break;
  }

  if (new_state != STATE_NOCHANGE) {
    ESP_LOGD(TAG, "onPacketRecv: state change from %s to %s", client_state_str[state], client_state_str[new_state]);

    if (new_state == STATE_IDLE) {
      if (*onIdle != NULL) onIdle();
    }

    state = new_state;
  }

  if (result == RECV_OK) {
    transfer_params.time_last_packet = esp_timer_get_time();
  }

  return result;
}

void MtftpClient::beginRead(uint16_t file_index, uint32_t file_offset) {
  if (state != STATE_IDLE) {
    ESP_LOGW(TAG, "beginRead: called while state == %s", client_state_str[state]);
    return;
  }

  transfer_params.file_index = file_index;
  transfer_params.file_offset = file_offset;
  transfer_params.window_size = CONFIG_WINDOW_SIZE;
  transfer_params.block_no = -1;

  packet_rrq_t rrq_pkt;

  rrq_pkt.file_index = file_index;
  rrq_pkt.file_offset = file_offset;
  rrq_pkt.window_size = CONFIG_WINDOW_SIZE;

  sendPacket((uint8_t *) &rrq_pkt, sizeof(rrq_pkt));

  ESP_LOGI(TAG, "beginRead: sent RRQ for %d at offset %d", file_index, file_offset);
  state = STATE_TRANSFER;

  onWindowStart();
}

void MtftpClient::loop(void) {
  enum client_state new_state = STATE_NOCHANGE;

  if (state != STATE_IDLE && (esp_timer_get_time() - transfer_params.time_last_packet) > CONFIG_TIMEOUT) {
    ESP_LOGW(TAG, "timeout!");

    // can we send an ACK/RTX here if one hasnt been sent for this window yet?
    if (*onTimeout != NULL) onTimeout();
    new_state = STATE_IDLE;
  }

  if (new_state != STATE_NOCHANGE) {
    ESP_LOGI(TAG, "loop: state change from %s to %s", client_state_str[state], client_state_str[new_state]);

    if (new_state == STATE_IDLE) {
      if (*onIdle != NULL) onIdle();
    }

    state = new_state;
  }
}
