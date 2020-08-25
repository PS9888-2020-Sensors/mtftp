#include <string.h>
#include "unity.h"
#include "helpers.h"
#include "mtftp.h"
#include "mtftp_client.hpp"

TEST_CASE("test client", "[client]") {
  const uint16_t SAMPLE_FILE_INDEX = 123;
  const uint32_t SAMPLE_FILE_OFFSET = 0;
  const uint8_t SAMPLE_DATA[CONFIG_LEN_BLOCK] = { 0x01, 0x02, 0x03, 0x04 };

  initTestTracking();

  MtftpClient client;
  client.init(&writeFile, &sendPacket);

  STORE_SENDPACKET();
  client.beginRead(SAMPLE_FILE_INDEX, SAMPLE_FILE_OFFSET, CONFIG_WINDOW_SIZE);

  TEST_ASSERT_EQUAL_MESSAGE(1, GET_SENDPACKET(), "sendPacket should be called once");
  TEST_ASSERT_EQUAL(sizeof(packet_rrq_t), sendPacket_stats.len);

  packet_rrq_t *pkt_rrq = (packet_rrq_t *) sendPacket_stats.data;
  TEST_ASSERT_EQUAL(TYPE_READ_REQUEST, pkt_rrq->opcode);
  TEST_ASSERT_EQUAL(SAMPLE_FILE_INDEX, pkt_rrq->file_index);
  TEST_ASSERT_EQUAL(SAMPLE_FILE_OFFSET, pkt_rrq->file_offset);
  TEST_ASSERT_EQUAL(CONFIG_WINDOW_SIZE, pkt_rrq->window_size);

  packet_data_t pkt_data;

  memcpy(&pkt_data.block, SAMPLE_DATA, CONFIG_LEN_BLOCK);

  STORE_SENDPACKET();

  // send CONFIG_WINDOW_SIZE blocks
  for (uint8_t block_no = 0; block_no < CONFIG_WINDOW_SIZE; block_no++) {
    pkt_data.block_no = block_no;

    STORE_WRITEFILE();

    client.onPacketRecv((uint8_t *) &pkt_data, LEN_DATA_HEADER + CONFIG_LEN_BLOCK);
    client.loop();
    TEST_ASSERT_EQUAL_MESSAGE(1, GET_WRITEFILE(), "writeFile should be called once");
    TEST_ASSERT_EQUAL_HEX8_ARRAY(SAMPLE_DATA, writeFile_stats.data, CONFIG_LEN_BLOCK);
    TEST_ASSERT_EQUAL(SAMPLE_FILE_INDEX, writeFile_stats.file_index);
    TEST_ASSERT_EQUAL(SAMPLE_FILE_OFFSET + (block_no * CONFIG_LEN_BLOCK), writeFile_stats.file_offset);
    TEST_ASSERT_EQUAL(CONFIG_LEN_BLOCK, writeFile_stats.btw);
  }

  // at this point, a full window has been transmitted
  // so should acknowledge the window with the server
  TEST_ASSERT_EQUAL(MtftpClient::STATE_ACK_SENT, client.getState());
  TEST_ASSERT_EQUAL(1, GET_SENDPACKET());
  TEST_ASSERT_EQUAL(sizeof(packet_ack_t), sendPacket_stats.len);

  packet_ack_t *pkt_ack = (packet_ack_t *) sendPacket_stats.data;
  TEST_ASSERT_EQUAL(TYPE_ACK, pkt_ack->opcode);
  TEST_ASSERT_EQUAL(CONFIG_WINDOW_SIZE - 1, pkt_ack->block_no);

  // send a non full block to mark end of file
  uint8_t len_data = CONFIG_LEN_BLOCK - 1;
  pkt_data.block_no = 0;

  STORE_WRITEFILE();
  client.onPacketRecv((uint8_t *) &pkt_data, LEN_DATA_HEADER + len_data);
  client.loop();
  TEST_ASSERT_EQUAL(1, GET_WRITEFILE());
  TEST_ASSERT_EQUAL(len_data, writeFile_stats.btw);
  TEST_ASSERT_EQUAL_HEX8_ARRAY(SAMPLE_DATA, writeFile_stats.data, len_data);

  // transfer should have ended
  TEST_ASSERT_EQUAL(MtftpClient::STATE_IDLE, client.getState());
}
