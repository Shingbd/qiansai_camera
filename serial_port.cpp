#include "serial_port.h"

#include <fcntl.h>
#include <unistd.h>
#include <termios.h>
#include <cstring>
#include <cerrno>
#include <cstdio>

int serial_init(const char *device, int baudrate)
{
    int fd = open(device, O_RDWR | O_NOCTTY | O_NDELAY);
    if (fd < 0) {
        fprintf(stderr, "serial_init: open %s failed: %s\n", device, strerror(errno));
        return -1;
    }

    struct termios tio;
    memset(&tio, 0, sizeof(tio));

    speed_t speed;
    switch (baudrate) {
        case 9600:   speed = B9600;   break;
        case 19200:  speed = B19200;  break;
        case 38400:  speed = B38400;  break;
        case 115200: speed = B115200; break;
        case 230400: speed = B230400; break;
        case 460800: speed = B460800; break;
        case 921600: speed = B921600; break;
        default:     speed = B115200; break;
    }

    cfsetospeed(&tio, speed);
    cfsetispeed(&tio, speed);

    tio.c_cflag = CS8 | CLOCAL | CREAD;
    tio.c_iflag = IGNPAR | IGNBRK;
    tio.c_oflag = 0;
    tio.c_lflag = 0;

    tio.c_cc[VTIME] = 1;
    tio.c_cc[VMIN]  = 0;

    tcflush(fd, TCIOFLUSH);
    tcsetattr(fd, TCSANOW, &tio);

    printf("serial_init: opened %s @ %d baud\n", device, baudrate);
    return fd;
}

void serial_close(int fd)
{
    if (fd >= 0) close(fd);
}

int serial_read_byte(int fd, uint8_t *byte)
{
    return (int)read(fd, byte, 1);
}

int serial_write(int fd, const uint8_t *data, int len)
{
    return (int)write(fd, data, (size_t)len);
}

void serial_build_packet(serial_packet_t *pkt, int16_t x_diff, int16_t y_diff)
{
    pkt->header[0] = SERIAL_PACKET_HEADER1;
    pkt->header[1] = SERIAL_PACKET_HEADER2;
    pkt->x_diff    = x_diff;
    pkt->y_diff    = y_diff;
    pkt->footer    = SERIAL_PACKET_FOOTER;

    pkt->checksum = 0;
    uint8_t *p = (uint8_t *)pkt;
    for (int i = 0; i < SERIAL_PACKET_SIZE - 1; i++) {
        pkt->checksum += p[i];
    }
}

int serial_send_packet(int fd, int16_t x_diff, int16_t y_diff)
{
    serial_packet_t pkt;
    serial_build_packet(&pkt, x_diff, y_diff);

    int written = serial_write(fd, (uint8_t *)&pkt, sizeof(pkt));
    if (written != sizeof(pkt)) {
        fprintf(stderr, "serial_send_packet: write failed (%d / %zu)\n", written, sizeof(pkt));
        return -1;
    }
    return 0;
}
