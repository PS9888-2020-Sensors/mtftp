#include <string.h>
#include "unity.h"
#include "mtftp.h"
#include "mtftp_client.hpp"

const uint8_t MAX_LEN_PACKET = 250;

#define STORE_SENDPACKET() (sendPacketStats.beforeCalled = sendPacketStats.called)
#define GET_SENDPACKET() (sendPacketStats.called - sendPacketStats.beforeCalled)

#define STORE_WRITEFILE() (writeFileStats.beforeCalled = writeFileStats.called)
#define GET_WRITEFILE() (writeFileStats.called - writeFileStats.beforeCalled)

struct {
  uint8_t beforeCalled;
  uint8_t called;
  uint16_t file_index;
  uint32_t file_offset;
  uint8_t data[CONFIG_LEN_BLOCK];
  uint16_t btw;
} writeFileStats;

static bool writeFile(uint16_t file_index, uint32_t file_offset, const uint8_t *data, uint16_t btw) {
  writeFileStats.called ++;
  writeFileStats.file_index = file_index;
  writeFileStats.file_offset = file_offset;
  memcpy(writeFileStats.data, data, btw);
  writeFileStats.btw = btw;

  return true;
}

struct {
  uint8_t beforeCalled;
  uint8_t called;
  uint8_t data[MAX_LEN_PACKET];
  uint8_t len;
} sendPacketStats;

static void sendPacket(const uint8_t *data, uint8_t len) {
  sendPacketStats.called ++;
  memcpy(sendPacketStats.data, data, len);
  sendPacketStats.len = len;
}

static void initTestTracking(void) {
  memset(&writeFileStats, 0, sizeof(writeFileStats));
  memset(&sendPacketStats, 0, sizeof(sendPacketStats));
}

TEST_CASE("test client", "[client]") {
  const uint16_t SAMPLE_FILE_INDEX = 123;
  const uint32_t SAMPLE_FILE_OFFSET = 0;
  const uint8_t SAMPLE_DATA[CONFIG_LEN_BLOCK] = { 0x01, 0x02, 0x03, 0x04 };

  initTestTracking();

  MtftpClient client;
  client.init(&writeFile, &sendPacket);

  STORE_SENDPACKET();
  client.beginRead(SAMPLE_FILE_INDEX, SAMPLE_FILE_OFFSET);

  TEST_ASSERT_EQUAL_MESSAGE(1, GET_SENDPACKET(), "sendPacket should be called once");
  TEST_ASSERT_EQUAL(sizeof(packet_rrq_t), sendPacketStats.len);

  packet_rrq_t *pkt_rrq = (packet_rrq_t *) sendPacketStats.data;
  TEST_ASSERT_EQUAL(TYPE_READ_REQUEST, pkt_rrq->opcode);
  TEST_ASSERT_EQUAL(SAMPLE_FILE_INDEX, pkt_rrq->file_index);
  TEST_ASSERT_EQUAL(SAMPLE_FILE_OFFSET, pkt_rrq->file_offset);
  TEST_ASSERT_EQUAL(CONFIG_WINDOW_SIZE, pkt_rrq->window_size);

  packet_data_t pkt_data;
  recv_result_t result;

  memcpy(&pkt_data.block, SAMPLE_DATA, CONFIG_LEN_BLOCK);

  STORE_SENDPACKET();

  // send CONFIG_WINDOW_SIZE blocks
  for (uint8_t block_no = 0; block_no < CONFIG_WINDOW_SIZE; block_no++) {
    pkt_data.block_no = block_no;

    STORE_WRITEFILE();

    result = client.onPacketRecv((uint8_t *) &pkt_data, LEN_DATA_HEADER + CONFIG_LEN_BLOCK);
    TEST_ASSERT_EQUAL(RECV_OK, result);
    TEST_ASSERT_EQUAL_MESSAGE(1, GET_WRITEFILE(), "writeFile should be called once");
    TEST_ASSERT_EQUAL_HEX8_ARRAY(SAMPLE_DATA, writeFileStats.data, CONFIG_LEN_BLOCK);
    TEST_ASSERT_EQUAL(SAMPLE_FILE_INDEX, writeFileStats.file_index);
    TEST_ASSERT_EQUAL(SAMPLE_FILE_OFFSET + (block_no * CONFIG_LEN_BLOCK), writeFileStats.file_offset);
    TEST_ASSERT_EQUAL(CONFIG_LEN_BLOCK, writeFileStats.btw);
  }

  // at this point, a full window has been transmitted
  // so should acknowledge the window with the server
  TEST_ASSERT_EQUAL(MtftpClient::STATE_ACK_SENT, client.getState());
  TEST_ASSERT_EQUAL(1, GET_SENDPACKET());
  TEST_ASSERT_EQUAL(sizeof(packet_ack_t), sendPacketStats.len);

  packet_ack_t *pkt_ack = (packet_ack_t *) sendPacketStats.data;
  TEST_ASSERT_EQUAL(TYPE_ACK, pkt_ack->opcode);
  TEST_ASSERT_EQUAL(CONFIG_WINDOW_SIZE - 1, pkt_ack->block_no);

  // send a non full block to mark end of file
  uint8_t len_data = CONFIG_LEN_BLOCK - 1;
  pkt_data.block_no = 0;

  STORE_WRITEFILE();
  result = client.onPacketRecv((uint8_t *) &pkt_data, LEN_DATA_HEADER + len_data);
  TEST_ASSERT_EQUAL(RECV_OK, result);
  TEST_ASSERT_EQUAL(1, GET_WRITEFILE());
  TEST_ASSERT_EQUAL(len_data, writeFileStats.btw);
  TEST_ASSERT_EQUAL_HEX8_ARRAY(SAMPLE_DATA, writeFileStats.data, len_data);

  // transfer should have ended
  TEST_ASSERT_EQUAL(MtftpClient::STATE_IDLE, client.getState());
}

TEST_CASE("test client behavior when missing data packets", "[client]") {
  const uint16_t SAMPLE_FILE_INDEX = 123;
  const uint32_t SAMPLE_FILE_OFFSET = 0;
  const uint8_t SAMPLE_DATA[CONFIG_LEN_BLOCK] = { 0x01, 0x02, 0x03, 0x04 };

  initTestTracking();

  MtftpClient client;
  client.init(&writeFile, &sendPacket);
  client.beginRead(SAMPLE_FILE_INDEX, SAMPLE_FILE_OFFSET);

  packet_data_t pkt_data;

  memcpy(&pkt_data.block, SAMPLE_DATA, CONFIG_LEN_BLOCK);

  STORE_SENDPACKET();

  // send CONFIG_WINDOW_SIZE blocks
  for (uint8_t block_no = 0; block_no < CONFIG_WINDOW_SIZE; block_no++) {
    // simulate loss of second last data block
    if (block_no == (CONFIG_WINDOW_SIZE - 2)) {
      continue;
    }

    pkt_data.block_no = block_no;
    client.onPacketRecv((uint8_t *) &pkt_data, LEN_DATA_HEADER + CONFIG_LEN_BLOCK);
  }

  // at this point, since we lost block_no CONFIG_WINDOW_SIZE - 2,
  // we should expect an ACK for CONFIG_WINDOW_SIZE - 3
  TEST_ASSERT_EQUAL(MtftpClient::STATE_ACK_SENT, client.getState());
  TEST_ASSERT_EQUAL(1, GET_SENDPACKET());
  TEST_ASSERT_EQUAL(sizeof(packet_ack_t), sendPacketStats.len);

  packet_ack_t *pkt_ack = (packet_ack_t *) sendPacketStats.data;
  TEST_ASSERT_EQUAL(TYPE_ACK, pkt_ack->opcode);
  TEST_ASSERT_EQUAL(CONFIG_WINDOW_SIZE - 3, pkt_ack->block_no);
}
