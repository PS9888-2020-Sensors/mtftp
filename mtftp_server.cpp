#include <string.h>
#include "esp_log.h"
#include "esp_timer.h"

#include "sdkconfig.h"

#include "mtftp.h"
#include "mtftp_server.hpp"

static const char *TAG = "mtftp-server";

void MtftpServer::init(
    bool (*_readFile)(uint16_t file_index, uint32_t file_offset, uint8_t *data, uint16_t btr, uint16_t *br),
    void (*_sendPacket)(const uint8_t *data, uint8_t len)
  ) {
  state = STATE_IDLE;

  readFile = _readFile;
  sendPacket = _sendPacket;
}

void MtftpServer::setOnIdleCb(void (*_onIdle)()) {
  onIdle = _onIdle;
}

void MtftpServer::setOnTimeoutCb(void (*_onTimeout)()) {
  onTimeout = _onTimeout;
}

void MtftpServer::onWindowStart(void) {
  transfer_params.block_no = 0;
  transfer_params.largest_block_no = -1;
  transfer_params.len_largest_block = 0;
}

recv_result_t MtftpServer::onPacketRecv(const uint8_t *data, uint16_t len_data) {
  const char *TAG = "onPacketRecv";

  if (len_data < 1) {
    ESP_LOGW(TAG, "onPacketRecv: called with len_data == 0!");
    return RECV_LEN;
  }

  recv_result_t result = RECV_UNSET;

  enum server_state new_state = STATE_NOCHANGE;

  switch(*data) {
    case TYPE_READ_REQUEST: 
    {
      if (len_data != sizeof(packet_rrq_t)) {
        ESP_LOGW(TAG, "len RRQ packet is %d (!= %d)", len_data, sizeof(packet_rrq_t));

        result = RECV_LEN;
        break;
      }

      if (state != STATE_IDLE) {
        ESP_LOGW(TAG, "RRQ received in state %s", server_state_str[state]);

        result = RECV_STATE;
        break;
      }

      packet_rrq_t *pkt = (packet_rrq_t *) data;

      ESP_LOGI(TAG, "RRQ for index=%d offset=%d", pkt->file_index, pkt->file_offset);

      transfer_params.file_index = pkt->file_index;
      transfer_params.file_offset = pkt->file_offset;
      transfer_params.window_size = pkt->window_size;

      onWindowStart();

      new_state = STATE_TRANSFER;

      result = RECV_OK;
      break;
    }
    case TYPE_RETRANSMIT:
    {
      if (state != STATE_AWAIT_RESPONSE) {
        ESP_LOGW(TAG, "RTX received in state %s", server_state_str[state]);

        result = RECV_STATE;
        break;
      }

      packet_rtx_t *pkt_rtx = (packet_rtx_t *) data;

      uint8_t len_elements = pkt_rtx->num_elements * sizeof(uint16_t);
      if (len_data != LEN_RTX_HEADER + len_elements) {
        ESP_LOGW(TAG, "len RTX packet is %d (!= %d)", len_data, LEN_RTX_HEADER + len_elements);
      }

      ESP_LOGD(TAG, "RTX received for %d blocks", pkt_rtx->num_elements);

      transfer_params.rtx_index = 0;
      transfer_params.num_rtx = pkt_rtx->num_elements;
      memcpy(transfer_params.rtx_block_nos, pkt_rtx->block_nos, len_elements);

      result = RECV_OK;
      new_state = STATE_RTX;
      break;
    }
    case TYPE_ACK:
    {
      if (len_data != sizeof(packet_ack_t)) {
        ESP_LOGW(TAG, "len ACK packet is %d (!= %d)", len_data, sizeof(packet_ack_t));

        result = RECV_LEN;
        break;
      }

      if (state != STATE_AWAIT_RESPONSE) {
        ESP_LOGW(TAG, "ACK received in state %s", server_state_str[state]);

        result = RECV_STATE;
        break;
      }

      result = RECV_OK;

      packet_ack_t *pkt = (packet_ack_t *) data;

      ESP_LOGD(TAG, "ACK of %d", pkt->block_no);

      // if ACK matches last block number sent AND the last block was not full
      // there is no more data to transfer
      if (pkt->block_no == transfer_params.block_no && transfer_params.len_largest_block < CONFIG_LEN_BLOCK) {
        new_state = STATE_IDLE;
        break;
      }

      // advance file_offset by the number of bytes successfully transferred
      transfer_params.file_offset += (pkt->block_no * CONFIG_LEN_BLOCK) +
      // block_no is one less than actual number of blocks transferred, so add final block
      // final block might be partial, so use bytes read instead of full block
        (pkt->block_no == transfer_params.largest_block_no ? transfer_params.len_largest_block: CONFIG_LEN_BLOCK);

      onWindowStart();

      // start transfer of next window
      new_state = STATE_TRANSFER;
      break;
    }
    default:
      ESP_LOGW(TAG, "bad packet opcode: %02X", *data);
      
      result = RECV_BAD_OPCODE;
      break;
  }

  if (new_state != STATE_NOCHANGE) {
    ESP_LOGD(TAG, "state change from %s to %s", server_state_str[state], server_state_str[new_state]);

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

bool MtftpServer::sendBlock(uint16_t block_no, uint16_t *bytes_read) {
  packet_data_t data_pkt;

  data_pkt.block_no = block_no;

  uint32_t offset = transfer_params.file_offset + (block_no * CONFIG_LEN_BLOCK);
  if (!readFile(
    transfer_params.file_index,
    offset,
    data_pkt.block,
    CONFIG_LEN_BLOCK,
    bytes_read
  )) {
    ESP_LOGW(TAG, "loop: reading from %d at offset %d failed. state=IDLE", transfer_params.file_index, offset);
    packet_err_t err_pkt;

    err_pkt.err = ERR_FREAD;

    sendPacket((uint8_t *) &err_pkt, sizeof(err_pkt));

    return false;
  }

  ESP_LOGD(TAG, "sending block %d len=%d", data_pkt.block_no, *bytes_read);

  sendPacket((uint8_t *) &data_pkt, LEN_DATA_HEADER + *bytes_read);

  if (transfer_params.block_no > transfer_params.largest_block_no) {
    transfer_params.largest_block_no = transfer_params.block_no;
    transfer_params.len_largest_block = *bytes_read;
  }

  return true;
}

void MtftpServer::loop(void) {
  enum server_state new_state = STATE_NOCHANGE;

  switch(state) {
    case STATE_TRANSFER:
    {
      uint16_t bytes_read;
      
      if (!sendBlock(transfer_params.block_no, &bytes_read)) {
        new_state = STATE_IDLE;
        break;
      }

      if (bytes_read < CONFIG_LEN_BLOCK) {
        // just read final block available
        new_state = STATE_AWAIT_RESPONSE;
      } else if (transfer_params.block_no >= (transfer_params.window_size - 1)) {
        // sent transfer_params.window_size blocks
        new_state = STATE_AWAIT_RESPONSE;
      } else {
        transfer_params.block_no ++;
      }

      // update time_last_packet here because the client is not expected to transmit
      // while the window hasnt been completely transferred
      transfer_params.time_last_packet = esp_timer_get_time();
      break;
    }
    case STATE_RTX:
    {
      uint16_t bytes_read;
      if (!sendBlock(transfer_params.rtx_block_nos[transfer_params.rtx_index], &bytes_read)) {
        ESP_LOGW(TAG, "failed to retransmit block_no=%d", transfer_params.rtx_block_nos[transfer_params.rtx_index]);
      }

      transfer_params.rtx_index ++;
      if (transfer_params.rtx_index >= transfer_params.num_rtx) {
        new_state = STATE_AWAIT_RESPONSE;
      }

      transfer_params.time_last_packet = esp_timer_get_time();
      break;
    }
    default:
      break;
  }

  if (state != STATE_IDLE && (esp_timer_get_time() - transfer_params.time_last_packet) > CONFIG_TIMEOUT) {
    ESP_LOGW(TAG, "timeout!");

    if (*onTimeout != NULL) onTimeout();
    new_state = STATE_IDLE;
  }

  if (new_state != STATE_NOCHANGE) {
    ESP_LOGD(TAG, "loop: state change from %s to %s", server_state_str[state], server_state_str[new_state]);

    if (new_state == STATE_IDLE) {
      if (*onIdle != NULL) onIdle();
    }

    state = new_state;
  }
}
