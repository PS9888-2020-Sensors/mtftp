#include "esp_log.h"

#include "sdkconfig.h"

#include "mtftp.h"
#include "mtftp_client.hpp"

static const char *TAG = "mtftp-client";

void MtftpClient::init(
    bool (*_writeFile)(uint16_t file_index, uint32_t file_offset, const uint8_t *data, uint16_t btw),
    void (*_sendPacket)(const uint8_t *data, uint8_t len)
  ) {
  state = STATE_IDLE;

  writeFile = _writeFile;
  sendPacket = _sendPacket;
}

void MtftpClient::onPacketRecv(const uint8_t *data, uint16_t len_data) {
  if (len_data < 1) {
    ESP_LOGW(TAG, "onPacketRecv: called with len_data == 0!");
    return;
  }

  enum client_state new_state = STATE_NOCHANGE;

  switch(*data) {
    case TYPE_DATA:
    {
      if (len_data < LEN_DATA_HEADER) {
        ESP_LOGW(TAG, "onPacketRecv: len DATA packet is %d (< %d)", len_data, LEN_DATA_HEADER);
        break;
      }

      if (state != STATE_TRANSFER && state != STATE_ACK_SENT) {
        ESP_LOGW(TAG, "onPacketRecv: DATA received in state %s", client_state_str[state]);
        break;
      }

      packet_data_t *data_pkt = (packet_data_t *) data;

      // new window
      if (state == STATE_ACK_SENT) {
        // first block received should be block 0
        if (data_pkt->block_no != 0) {
          ESP_LOGW(TAG, "onPacketRecv: first block after ACK has non zero block_no: %d", data_pkt->block_no);
          new_state = STATE_IDLE;
          break;
        }

        transfer_params.block_no = -1;
        new_state = STATE_TRANSFER;
      }

      uint16_t len_block_data = len_data - LEN_DATA_HEADER;

      if (data_pkt->block_no >= transfer_params.window_size) {
        ESP_LOGW(TAG, "onPacketRecv: received block %d when window size is only %d", data_pkt->block_no, transfer_params.window_size);
        new_state = STATE_IDLE;
        break;
      }

      if (data_pkt->block_no == (transfer_params.block_no + 1)) {
        ESP_LOGD(TAG, "onPacketRecv: received block %d with len %d", data_pkt->block_no, len_block_data);
        writeFile(
          transfer_params.file_index,
          transfer_params.file_offset,
          data_pkt->block,
          len_block_data
        );

        transfer_params.block_no = data_pkt->block_no;

        // advance file_offset by the number of bytes we just received
        transfer_params.file_offset += len_block_data;
      } else {
        ESP_LOGW(TAG, "onPacketRecv: out of order packet: expected %d, got %d", transfer_params.block_no + 1, data_pkt->block_no);
      }

      // end of the window:
      // receiving less than one full block of data
      if (len_block_data < CONFIG_LEN_BLOCK) {
        ESP_LOGI(TAG, "onPacketRecv: end of window (partial block of %d bytes)", len_block_data);
      } else if (data_pkt->block_no == (transfer_params.window_size - 1)) {
        // or this packet is the final block in the window
        // possibility that prior packets have been lost
        ESP_LOGI(TAG, "onPacketRecv: end of window (%d blocks)", transfer_params.window_size);
      } else {
        break;
      }

      packet_ack_t ack_pkt;

      // transmit last received block_no
      // transfer_params.block_no is initialised as -1
      // but if we get here, at least one block should have been received
      ack_pkt.block_no = transfer_params.block_no;

      sendPacket((uint8_t *) &ack_pkt, sizeof(ack_pkt));

      // the current block is not full (final block) and the current block has been received successfully
      // so all prior blocks in this window has been received, end of transfer
      if (len_block_data < CONFIG_LEN_BLOCK && data_pkt->block_no == transfer_params.block_no) {
        new_state = STATE_IDLE;
      } else {
      new_state = STATE_ACK_SENT;
      }

      break;
    }
    case TYPE_ERR:
    {
      if (len_data != sizeof(packet_err_t)) {
        ESP_LOGW(TAG, "onPacketRecv: len ERR packet is %d (!= %d)", len_data, sizeof(packet_err_t));
        break;
      }

      packet_err_t *pkt = (packet_err_t *) data;

      ESP_LOGW(TAG, "onPacketRecv: recv err %s", err_types_str[pkt->err]);

      if (state != STATE_IDLE) {
        new_state = STATE_IDLE;
      }
      break;
    }
    default:
      ESP_LOGW(TAG, "onPacketRecv: bad packet opcode: %02X", *data);
  }

  if (new_state != STATE_NOCHANGE) {
    ESP_LOGI(TAG, "onPacketRecv: state change from %s to %s", client_state_str[state], client_state_str[new_state]);
    state = new_state;
  }
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
}
