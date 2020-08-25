#include <string.h>
#include "unity.h"
#include "helpers.h"
#include "mtftp.h"
#include "mtftp_client.hpp"
#include "mtftp_server.hpp"

TEST_CASE("test client retransmit behavior", "[client]") {
  const uint16_t SAMPLE_FILE_INDEX = 123;
  const uint32_t SAMPLE_FILE_OFFSET = 0;
  const uint8_t SAMPLE_DATA[CONFIG_LEN_BLOCK] = { 0x01, 0x02, 0x03, 0x04 };

  const uint8_t WINDOW_SIZE = 8;

  initTestTracking();

  MtftpClient client;
  client.init(&writeFile, &sendPacket);
  client.beginRead(SAMPLE_FILE_INDEX, SAMPLE_FILE_OFFSET, WINDOW_SIZE);

  packet_data_t pkt_data;

  memcpy(&pkt_data.block, SAMPLE_DATA, CONFIG_LEN_BLOCK);

  STORE_SENDPACKET();
  STORE_WRITEFILE();

  // receive WINDOW_SIZE blocks
  for (uint8_t block_no = 0; block_no < WINDOW_SIZE; block_no++) {
    // simulate loss of data block
    if (block_no == (WINDOW_SIZE - 4)) {
      continue;
    }

    pkt_data.block_no = block_no;
    // write block_no into data so we can check for correctness later
    pkt_data.block[0] = block_no;
    client.onPacketRecv((uint8_t *) &pkt_data, LEN_DATA_HEADER + CONFIG_LEN_BLOCK);
    client.loop();
  }

  // at this point, since we lost block_no WINDOW_SIZE - 4,
  // we should expect an RTX for 1 item, block_no WINDOW_SIZE - 4
  TEST_ASSERT_EQUAL(MtftpClient::STATE_AWAIT_RTX, client.getState());
  TEST_ASSERT_EQUAL(1, GET_SENDPACKET());

  // we should only have WINDOW_SIZE - 4 blocks written,
  // since block_no=WINDOW_SIZE - 4 was lost and the blocks after are buffered
  TEST_ASSERT_EQUAL(WINDOW_SIZE - 4, GET_WRITEFILE());

  packet_rtx_t *pkt_rtx = (packet_rtx_t *) sendPacket_stats.data;

  TEST_ASSERT_EQUAL(TYPE_RETRANSMIT, pkt_rtx->opcode);
  // expect that only one block_no would have been sent in the rtx packet
  TEST_ASSERT_EQUAL(1, pkt_rtx->num_elements);
  // check that the overall length of the packet tallies
  TEST_ASSERT_EQUAL(LEN_RTX_HEADER + (pkt_rtx->num_elements * sizeof(uint16_t)), sendPacket_stats.len);
  // check that the block requested for retransmission is correct
  TEST_ASSERT_EQUAL(WINDOW_SIZE - 4, pkt_rtx->block_nos[0]);

  STORE_SENDPACKET();
  STORE_WRITEFILE();

  // send the client the missing block it just requested
  pkt_data.block_no = WINDOW_SIZE - 4;
  pkt_data.block[0] = WINDOW_SIZE - 4;

  client.onPacketRecv((uint8_t *) &pkt_data, LEN_DATA_HEADER + CONFIG_LEN_BLOCK);
  client.loop();

  TEST_ASSERT_EQUAL(MtftpClient::STATE_ACK_SENT, client.getState());

  // the buffer should have just been written to file
  TEST_ASSERT_EQUAL(1, GET_WRITEFILE());
  // block_no=WINDOW_SIZE - 4 to WINDOW_SIZE should just have been written
  TEST_ASSERT_EQUAL(4 * CONFIG_LEN_BLOCK, writeFile_stats.btw);

  uint8_t block_offset = 0;
  for(uint8_t block_no = WINDOW_SIZE - 4; block_no < WINDOW_SIZE; block_no ++) {
    TEST_ASSERT_EQUAL(block_no, writeFile_stats.data[(block_offset ++) * CONFIG_LEN_BLOCK]);
  }

  // an ACK should have been sent for block WINDOW_SIZE - 1
  TEST_ASSERT_EQUAL(1, GET_SENDPACKET());

  packet_ack_t *pkt_ack = (packet_ack_t *) sendPacket_stats.data;
  TEST_ASSERT_EQUAL(TYPE_ACK, pkt_ack->opcode);
  TEST_ASSERT_EQUAL(WINDOW_SIZE - 1, pkt_ack->block_no);

  STORE_SENDPACKET();

  // client receives a partial block here
  pkt_data.block_no = 0;
  pkt_data.block[0] = 0;
  client.onPacketRecv((uint8_t *) &pkt_data, LEN_DATA_HEADER + CONFIG_LEN_BLOCK - 1);
  client.loop();

  TEST_ASSERT_EQUAL(1, GET_SENDPACKET());

  // client should have sent out an ACK to mark the end of the window
  pkt_ack = (packet_ack_t *) sendPacket_stats.data;
  TEST_ASSERT_EQUAL(TYPE_ACK, pkt_ack->opcode);
  TEST_ASSERT_EQUAL(0, pkt_ack->block_no);

  TEST_ASSERT_EQUAL(MtftpClient::STATE_IDLE, client.getState());
}

TEST_CASE("test for correct blocks in RTX", "[server]") {
  const uint16_t SAMPLE_FILE_INDEX = 123;
  const uint32_t SAMPLE_FILE_OFFSET = 0;
  const uint8_t SAMPLE_DATA[CONFIG_LEN_BLOCK] = { 0x01, 0x02, 0x03, 0x04 };

  const uint8_t WINDOW_SIZE = 8;

  initTestTracking();

  MtftpClient client;
  client.init(&writeFile, &sendPacket);
  client.beginRead(SAMPLE_FILE_INDEX, SAMPLE_FILE_OFFSET, WINDOW_SIZE);

  packet_data_t pkt_data;

  memcpy(&pkt_data.block, SAMPLE_DATA, CONFIG_LEN_BLOCK);

  // receive WINDOW_SIZE blocks
  for (uint8_t block_no = 0; block_no < WINDOW_SIZE; block_no++) {
    // simulate loss of data blocks
    if (block_no == (WINDOW_SIZE - 5) || block_no == (WINDOW_SIZE - 3)) {
      continue;
    }

    pkt_data.block_no = block_no;
    client.onPacketRecv((uint8_t *) &pkt_data, LEN_DATA_HEADER + CONFIG_LEN_BLOCK);
    client.loop();
  }

  // expect that the RTX contains the two lost data blocks
  packet_rtx_t *pkt_rtx = (packet_rtx_t *) sendPacket_stats.data;
  TEST_ASSERT_EQUAL(TYPE_RETRANSMIT, pkt_rtx->opcode);
  TEST_ASSERT_EQUAL(2, pkt_rtx->num_elements);
  TEST_ASSERT_EQUAL(WINDOW_SIZE - 5, pkt_rtx->block_nos[0]);
  TEST_ASSERT_EQUAL(WINDOW_SIZE - 3, pkt_rtx->block_nos[1]);
}

TEST_CASE("test server retransmit behavior", "[server]") {
  const uint16_t SAMPLE_FILE_INDEX = 123;
  const uint32_t SAMPLE_FILE_OFFSET = 0;

  const uint8_t WINDOW_SIZE = 8;

  const uint8_t transfer_data[CONFIG_LEN_BLOCK] = { 0x01, 0x02, 0x03, 0x04 };
  memcpy(SAMPLE_DATA, transfer_data, CONFIG_LEN_BLOCK);
  LEN_SAMPLE_DATA = CONFIG_LEN_BLOCK;

  initTestTracking();

  MtftpServer server;
  server.init(&readFile, &sendPacket);

  packet_rrq_t pkt_rrq;
  pkt_rrq.file_index = SAMPLE_FILE_INDEX;
  pkt_rrq.file_offset = SAMPLE_FILE_OFFSET;
  pkt_rrq.window_size = WINDOW_SIZE;
  server.onPacketRecv((uint8_t *) &pkt_rrq, sizeof(pkt_rrq));

  for(uint8_t i = 0; i < WINDOW_SIZE; i++) {
    server.loop();
  }

  TEST_ASSERT_EQUAL(MtftpServer::STATE_AWAIT_RESPONSE, server.getState());

  const uint8_t missing_blocks[] = {3, 5};

  // issue rtx for block 3 and 5
  packet_rtx_t pkt_rtx;
  pkt_rtx.num_elements = 2;
  pkt_rtx.block_nos[0] = missing_blocks[0];
  pkt_rtx.block_nos[1] = missing_blocks[1];

  recv_result_t result;

  result = server.onPacketRecv((uint8_t *) &pkt_rtx, LEN_RTX_HEADER + (pkt_rtx.num_elements * sizeof(uint16_t)));
  TEST_ASSERT_EQUAL(RECV_OK, result);

  // check to ensure that the correct blocks are transmitted
  for (uint8_t i = 0; i < sizeof(missing_blocks) / sizeof(missing_blocks[0]); i++) {
    uint8_t block_no = missing_blocks[i];

    STORE_READFILE();
    STORE_SENDPACKET();

    server.loop();
    TEST_ASSERT_EQUAL(1, GET_READFILE());
    TEST_ASSERT_EQUAL(1, GET_SENDPACKET());

    TEST_ASSERT_EQUAL(SAMPLE_FILE_INDEX, readFile_stats.file_index);
    TEST_ASSERT_EQUAL(SAMPLE_FILE_OFFSET + (block_no * CONFIG_LEN_BLOCK), readFile_stats.file_offset);
    TEST_ASSERT_EQUAL(CONFIG_LEN_BLOCK, readFile_stats.btr);

    packet_data_t *pkt_data = (packet_data_t *) sendPacket_stats.data;
    TEST_ASSERT_EQUAL(TYPE_DATA, pkt_data->opcode);
    TEST_ASSERT_EQUAL(block_no, pkt_data->block_no);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(SAMPLE_DATA, &(pkt_data->block), LEN_SAMPLE_DATA);
  }

  // server has retransmitted the missing packets, expecting another RTX/ACK

  TEST_ASSERT_EQUAL(MtftpServer::STATE_AWAIT_RESPONSE, server.getState());

  packet_ack_t pkt_ack;

  pkt_ack.block_no = WINDOW_SIZE - 1;

  result = server.onPacketRecv((uint8_t *) &pkt_ack, sizeof(packet_ack_t));
  TEST_ASSERT_EQUAL(RECV_OK, result);

  // since server has received an ACK for the full window,
  // it should send the next block of data

  STORE_READFILE();
  STORE_SENDPACKET();

  // server send a partial block
  LEN_SAMPLE_DATA --;

  server.loop();

  TEST_ASSERT_EQUAL(1, GET_READFILE());
  TEST_ASSERT_EQUAL(1, GET_SENDPACKET());

  TEST_ASSERT_EQUAL(SAMPLE_FILE_INDEX, readFile_stats.file_index);
  TEST_ASSERT_EQUAL(SAMPLE_FILE_OFFSET + (WINDOW_SIZE * CONFIG_LEN_BLOCK), readFile_stats.file_offset);
  TEST_ASSERT_EQUAL(CONFIG_LEN_BLOCK, readFile_stats.btr);

  packet_data_t *pkt_data = (packet_data_t *) sendPacket_stats.data;
  TEST_ASSERT_EQUAL(TYPE_DATA, pkt_data->opcode);
  TEST_ASSERT_EQUAL(0, pkt_data->block_no);
  TEST_ASSERT_EQUAL(LEN_DATA_HEADER + LEN_SAMPLE_DATA, sendPacket_stats.len);
  TEST_ASSERT_EQUAL_HEX8_ARRAY(SAMPLE_DATA, &(pkt_data->block), LEN_SAMPLE_DATA);

  // server receives an ACK for block 0

  pkt_ack.block_no = 0;
  result = server.onPacketRecv((uint8_t *) &pkt_ack, sizeof(packet_ack_t));
  TEST_ASSERT_EQUAL(RECV_OK, result);

  TEST_ASSERT_EQUAL(MtftpServer::STATE_IDLE, server.getState());
}
