#ifndef HELPERS_H
#define HELPERS_H

#include <string.h>
#include <stdint.h>
#include <sdkconfig.h>

const uint8_t MAX_LEN_PACKET = 250;

extern uint8_t SAMPLE_DATA[CONFIG_LEN_BLOCK];
extern uint8_t LEN_SAMPLE_DATA;

struct readFile_stats_t {
  uint8_t beforeCalled;
  uint8_t called;
  uint16_t file_index;
  uint32_t file_offset;
  uint16_t btr;
};

extern readFile_stats_t readFile_stats;

struct writeFile_stats_t {
  uint8_t beforeCalled;
  uint8_t called;
  uint16_t file_index;
  uint32_t file_offset;
  uint8_t data[CONFIG_LEN_BLOCK];
  uint16_t btw;
};

extern writeFile_stats_t writeFile_stats;

struct sendPacket_stats_t {
  uint8_t beforeCalled;
  uint8_t called;
  uint8_t data[MAX_LEN_PACKET];
  uint8_t len;
};

extern sendPacket_stats_t sendPacket_stats;

#define STORE_READFILE() (readFile_stats.called = 0)
#define GET_READFILE() (readFile_stats.called)

#define STORE_WRITEFILE() (writeFile_stats.called = 0)
#define GET_WRITEFILE() (writeFile_stats.called)

#define STORE_SENDPACKET() (sendPacket_stats.called = 0)
#define GET_SENDPACKET() (sendPacket_stats.called)


bool readFile(uint16_t file_index, uint32_t file_offset, uint8_t *data, uint16_t btr, uint16_t *br);
bool writeFile(uint16_t file_index, uint32_t file_offset, const uint8_t *data, uint16_t btw);
void sendPacket(const uint8_t *data, uint8_t len);
void initTestTracking(void);

#endif
