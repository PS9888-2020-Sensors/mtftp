#include <string.h>
#include "unity.h"
#include "mtftp.h"
#include "mtftp_server.hpp"

const uint8_t MAX_LEN_PACKET = 250;

#define STORE_SENDPACKET() (sendPacketStats.beforeCalled = sendPacketStats.called)
#define GET_SENDPACKET() (sendPacketStats.called - sendPacketStats.beforeCalled)

#define STORE_READFILE() (readFileStats.beforeCalled = readFileStats.called)
#define GET_READFILE() (readFileStats.called - readFileStats.beforeCalled)

struct {
  uint8_t beforeCalled;
  uint8_t called;
  uint16_t file_index;
  uint32_t file_offset;
  uint16_t btr;
} readFileStats;

uint8_t SAMPLE_DATA[CONFIG_LEN_BLOCK];
uint8_t LEN_SAMPLE_DATA;

static bool readFile(uint16_t file_index, uint32_t file_offset, uint8_t *data, uint16_t btr, uint16_t *br) {
  readFileStats.called ++;
  readFileStats.file_index = file_index;
  readFileStats.file_offset = file_offset;
  readFileStats.btr = btr;

  *br = LEN_SAMPLE_DATA;
  memcpy(data, SAMPLE_DATA, *br);

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
  memset(&readFileStats, 0, sizeof(readFileStats));
  memset(&sendPacketStats, 0, sizeof(sendPacketStats));
}

TEST_CASE("test server", "[server]") {
  const uint16_t SAMPLE_FILE_INDEX = 123;
  const uint32_t SAMPLE_FILE_OFFSET = 0;

  const uint8_t transfer_data[CONFIG_LEN_BLOCK] = { 0x01, 0x02, 0x03, 0x04 };
  memcpy(SAMPLE_DATA, transfer_data, CONFIG_LEN_BLOCK);
  LEN_SAMPLE_DATA = CONFIG_LEN_BLOCK;

  initTestTracking();

  MtftpServer server;
  server.init(&readFile, &sendPacket);

  TEST_ASSERT_EQUAL(MtftpServer::STATE_IDLE, server.getState());
  packet_rrq_t pkt_rrq;
  recv_result_t result;

  pkt_rrq.file_index = SAMPLE_FILE_INDEX;
  pkt_rrq.file_offset = SAMPLE_FILE_OFFSET;
  pkt_rrq.window_size = CONFIG_WINDOW_SIZE;

  result = server.onPacketRecv((uint8_t *) &pkt_rrq, sizeof(pkt_rrq));
  TEST_ASSERT_EQUAL(RECV_OK, result);
  TEST_ASSERT_EQUAL(MtftpServer::STATE_TRANSFER, server.getState());

  // read and send CONFIG_WINDOW_SIZE blocks of data
  for (uint8_t block_no = 0; block_no < CONFIG_WINDOW_SIZE; block_no ++) {
    STORE_READFILE();
    STORE_SENDPACKET();

    TEST_ASSERT_EQUAL(MtftpServer::STATE_TRANSFER, server.getState());
    server.loop();

    TEST_ASSERT_EQUAL_MESSAGE(1, GET_READFILE(), "readFile should be called once");
    TEST_ASSERT_EQUAL_MESSAGE(1, GET_SENDPACKET(), "sendPacket should be called once");

    TEST_ASSERT_EQUAL(SAMPLE_FILE_INDEX, readFileStats.file_index);
    TEST_ASSERT_EQUAL(SAMPLE_FILE_OFFSET + (block_no * CONFIG_LEN_BLOCK), readFileStats.file_offset);
    TEST_ASSERT_EQUAL(CONFIG_LEN_BLOCK, readFileStats.btr);

    packet_data_t *pkt_data = (packet_data_t *) sendPacketStats.data;
    TEST_ASSERT_EQUAL(TYPE_DATA, pkt_data->opcode);
    TEST_ASSERT_EQUAL(block_no, pkt_data->block_no);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(SAMPLE_DATA, &(pkt_data->block), LEN_SAMPLE_DATA);
  }

  // wait for acknowledgement
  TEST_ASSERT_EQUAL(MtftpServer::STATE_WAIT_ACK, server.getState());

  packet_ack_t pkt_ack;
  pkt_ack.block_no = CONFIG_WINDOW_SIZE - 1;

  result = server.onPacketRecv((uint8_t *) &pkt_ack, sizeof(packet_ack_t));
  TEST_ASSERT_EQUAL(RECV_OK, result);

  // send partial block
  LEN_SAMPLE_DATA --;

  STORE_READFILE();
  STORE_SENDPACKET();

  server.loop();

  TEST_ASSERT_EQUAL(1, GET_READFILE());
  TEST_ASSERT_EQUAL(1, GET_SENDPACKET());

  TEST_ASSERT_EQUAL(SAMPLE_FILE_INDEX, readFileStats.file_index);

  // already sent 4 blocks of data, read the next one
  TEST_ASSERT_EQUAL(SAMPLE_FILE_OFFSET + (4 * CONFIG_LEN_BLOCK), readFileStats.file_offset);

  packet_data_t *pkt_data = (packet_data_t *) sendPacketStats.data;
  TEST_ASSERT_EQUAL(TYPE_DATA, pkt_data->opcode);
  TEST_ASSERT_EQUAL(0, pkt_data->block_no);
  TEST_ASSERT_EQUAL_HEX8_ARRAY(SAMPLE_DATA, &(pkt_data->block), LEN_SAMPLE_DATA);

  // acknowledge the single partial block just sent
  TEST_ASSERT_EQUAL(MtftpServer::STATE_WAIT_ACK, server.getState());

  pkt_ack.block_no = 0;

  result = server.onPacketRecv((uint8_t *) &pkt_ack, sizeof(packet_ack_t));
  TEST_ASSERT_EQUAL(RECV_OK, result);

  // end of transfer, should have gone back to idle
  TEST_ASSERT_EQUAL(MtftpServer::STATE_IDLE, server.getState());
}
