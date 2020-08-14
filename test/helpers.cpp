#include "helpers.h"

readFile_stats_t readFile_stats;
writeFile_stats_t writeFile_stats;
sendPacket_stats_t sendPacket_stats;

uint8_t SAMPLE_DATA[CONFIG_LEN_BLOCK];
uint8_t LEN_SAMPLE_DATA;

bool readFile(uint16_t file_index, uint32_t file_offset, uint8_t *data, uint16_t btr, uint16_t *br) {
  readFile_stats.called ++;
  readFile_stats.file_index = file_index;
  readFile_stats.file_offset = file_offset;
  readFile_stats.btr = btr;

  *br = LEN_SAMPLE_DATA;
  memcpy(data, SAMPLE_DATA, *br);

  return true;
}

bool writeFile(uint16_t file_index, uint32_t file_offset, const uint8_t *data, uint16_t btw) {
  writeFile_stats.called ++;
  writeFile_stats.file_index = file_index;
  writeFile_stats.file_offset = file_offset;
  memcpy(writeFile_stats.data, data, btw);
  writeFile_stats.btw = btw;

  return true;
}

void sendPacket(const uint8_t *data, uint8_t len) {
  sendPacket_stats.called ++;
  memcpy(sendPacket_stats.data, data, len);
  sendPacket_stats.len = len;
}

void initTestTracking(void) {
  memset(&writeFile_stats, 0, sizeof(writeFile_stats));
  memset(&sendPacket_stats, 0, sizeof(sendPacket_stats));
}
