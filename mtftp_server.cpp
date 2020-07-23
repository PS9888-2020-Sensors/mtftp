#include "esp_log.h"

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

void MtftpServer::onPacketRecv(const uint8_t *data, uint16_t len_data) {
  if (len_data < 1) {
    ESP_LOGW(TAG, "onPacketRecv: called with len_data == 0!");
    return;
  }

  enum server_state new_state = STATE_NOCHANGE;

  switch(*data) {
    case TYPE_READ_REQUEST: 
    {
      if (len_data != sizeof(packet_rrq_t)) {
        ESP_LOGW(TAG, "onPacketRecv: len RRQ packet is %d (!= %d)", len_data, sizeof(packet_rrq_t));
        break;
      }

      if (state != STATE_IDLE) {
        ESP_LOGW(TAG, "onPacketRecv: RRQ received in state %s", server_state_str[state]);
        break;
      }

      packet_rrq_t *pkt = (packet_rrq_t *) data;

      transfer_params.file_index = pkt->file_index;
      transfer_params.file_offset = pkt->file_offset;
      transfer_params.window_size = pkt->window_size;
      transfer_params.block_no = 0;

      new_state = STATE_TRANSFER;
      break;
    }
    case TYPE_ACK:
    {
      if (len_data != sizeof(packet_ack_t)) {
        ESP_LOGW(TAG, "onPacketRecv: len ACK packet is %d (!= %d)", len_data, sizeof(packet_ack_t));
        break;
      }

      if (state != STATE_WAIT_ACK) {
        ESP_LOGW(TAG, "onPacketRecv: ACK received in state %s", server_state_str[state]);
        break;
      }

      packet_ack_t *pkt = (packet_ack_t *) data;

      ESP_LOGI(TAG, "onPacketRecv: ACK of %d", pkt->block_no);

      // if ACK matches last block number sent AND the last block was not full
      // there is no more data to transfer
      if (pkt->block_no == transfer_params.block_no && transfer_params.bytes_read < CONFIG_LEN_BLOCK) {
        new_state = STATE_IDLE;
        break;
      }

      // advance file_offset by the number of bytes successfully transferred
      transfer_params.file_offset += (pkt->block_no * CONFIG_LEN_BLOCK) +
      // block_no is one less than actual number of blocks transferred, so add final block
      // final block might be partial, so use bytes_read instead of full block
        (pkt->block_no == transfer_params.block_no ? transfer_params.bytes_read: CONFIG_LEN_BLOCK);

      transfer_params.block_no = 0;

      // start transfer of next window
      new_state = STATE_TRANSFER;
      break;
    }
    default:
      ESP_LOGW(TAG, "onPacketRecv: bad packet opcode: %02X", *data);
  }

  if (new_state != STATE_NOCHANGE) {
    ESP_LOGI(TAG, "onPacketRecv: state change from %s to %s", server_state_str[state], server_state_str[new_state]);

    if (new_state == STATE_IDLE) {
      if (*onIdle != NULL) onIdle();
    }

    state = new_state;
  }
}

void MtftpServer::loop(void) {
  enum server_state new_state = STATE_NOCHANGE;

  switch(state) {
    case STATE_TRANSFER:
    {
      packet_data_t data_pkt;

      data_pkt.block_no = transfer_params.block_no;

      uint16_t offset = transfer_params.file_offset + (transfer_params.block_no * CONFIG_LEN_BLOCK);
      if (!readFile(
        transfer_params.file_index,
        offset,
        data_pkt.block,
        CONFIG_LEN_BLOCK,
        &transfer_params.bytes_read
      )) {
        ESP_LOGW(TAG, "loop: reading from %d at offset %d failed. state=IDLE", transfer_params.file_index, offset);
        packet_err_t err_pkt;

        err_pkt.err = ERR_FREAD;

        sendPacket((uint8_t *) &err_pkt, sizeof(err_pkt));

        new_state = STATE_IDLE;
        break;
      }

      sendPacket((uint8_t *) &data_pkt, LEN_DATA_HEADER + transfer_params.bytes_read);

      if (transfer_params.bytes_read < CONFIG_LEN_BLOCK) {
        // just read final block available
        new_state = STATE_WAIT_ACK;
      } else if (transfer_params.block_no >= (transfer_params.window_size - 1)) {
        // sent transfer_params.window_size blocks
        new_state = STATE_WAIT_ACK;
      } else {
        transfer_params.block_no ++;
      }
      break;
    }
    default:
      break;
  }

  if (new_state != STATE_NOCHANGE) {
    ESP_LOGI(TAG, "loop: state change from %s to %s", server_state_str[state], server_state_str[new_state]);

    if (new_state == STATE_IDLE) {
      if (*onIdle != NULL) onIdle();
    }

    state = new_state;
  }
}
