#pragma once

#include <cstdint>
#include <string>

#define SERIAL_PACKET_HEADER1   0xAA
#define SERIAL_PACKET_HEADER2   0x55
#define SERIAL_PACKET_FOOTER    0x0D
#define SERIAL_PACKET_SIZE      8

#pragma pack(1)
typedef struct {
    uint8_t  header[2];
    int16_t  x_diff;
    int16_t  y_diff;
    uint8_t  checksum;
    uint8_t  footer;
} serial_packet_t;
#pragma pack()

int  serial_init(const char *device, int baudrate);
void serial_close(int fd);
int  serial_read_byte(int fd, uint8_t *byte);
int  serial_write(int fd, const uint8_t *data, int len);

void serial_build_packet(serial_packet_t *pkt, int16_t x_diff, int16_t y_diff);
int  serial_send_packet(int fd, int16_t x_diff, int16_t y_diff);
