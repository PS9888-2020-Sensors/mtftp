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
  client.beginRead(SAMPLE_FILE_INDEX, SAMPLE_FILE_OFFSET);

  packet_data_t pkt_data;

  memcpy(&pkt_data.block, SAMPLE_DATA, CONFIG_LEN_BLOCK);

  STORE_SENDPACKET();
  STORE_WRITEFILE();

  // send WINDOW_SIZE blocks
  for (uint8_t block_no = 0; block_no < WINDOW_SIZE; block_no++) {
    // simulate loss of data block
    if (block_no == (WINDOW_SIZE - 4)) {
      continue;
    }

    pkt_data.block_no = block_no;
    // write block_no into data so we can check for correctness later
    pkt_data.block[0] = block_no;
    client.onPacketRecv((uint8_t *) &pkt_data, LEN_DATA_HEADER + CONFIG_LEN_BLOCK);
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
}
