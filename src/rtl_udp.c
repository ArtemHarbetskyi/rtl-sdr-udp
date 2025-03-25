#include <errno.h>
#include <signal.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "rtl-sdr.h"
#include "convenience/convenience.h"

#define DEFAULT_SAMPLE_RATE     2048000
#define DEFAULT_FREQ            100000000
#define DEFAULT_PORT            1234
#define DEFAULT_CTRL_PORT       1235
#define DEFAULT_DEST_IP         "127.0.0.1"
#define BUFFER_SIZE             16384

static pthread_t udp_thread, ctrl_thread;
static volatile int do_exit = 0;
static int sockfd, ctrl_sockfd;
static struct sockaddr_in dest_addr;

static rtlsdr_dev_t *dev = NULL;

void usage(void) {
    fprintf(stderr,
        "rtl_udp, an I/Q spectrum server for RTL2832 based DVB-T receivers\n\n"
        "Usage:\t[-f frequency_to_tune_to [Hz]]\n"
        "\t[-s samplerate (default: 2048000 Hz)]\n"
        "\t[-d device_index (default: 0)]\n"
        "\t[-g gain (default: 0 for auto)]\n"
        "\t[-p listen_port (default: 1234)]\n"
        "\t[-u dest_ip:dest_port (default: 127.0.0.1:1234)]\n");
    exit(1);
}

static void sighandler(int signum) {
    fprintf(stderr, "Signal caught, exiting!\n");
    do_exit = 1;
}

static void *udp_server_thread(void *arg) {
    unsigned char buffer[BUFFER_SIZE];
    int r, n;

    while (!do_exit) {
        r = rtlsdr_read_sync(dev, buffer, BUFFER_SIZE, &n);
        if (r < 0 || n <= 0) {
            fprintf(stderr, "read error or no data\n");
            continue;
        }
        sendto(sockfd, buffer, n, 0, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
    }

    close(sockfd);
    return NULL;
}

static void *udp_control_thread(void *arg) {
    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);
    unsigned char cmd_buffer[1024];
    int n;

    while (!do_exit) {
        n = recvfrom(ctrl_sockfd, cmd_buffer, sizeof(cmd_buffer), 0,
                     (struct sockaddr *)&client_addr, &client_len);
        if (n <= 0) continue;

        switch (cmd_buffer[0]) {
            case 0x01: // Set frequency
                if (n >= 5) {
                    uint32_t freq = ntohl(*(uint32_t *)(cmd_buffer + 1));
                    rtlsdr_set_center_freq(dev, freq);
                }
                break;
            case 0x02: // Set sample rate
                if (n >= 5) {
                    uint32_t rate = ntohl(*(uint32_t *)(cmd_buffer + 1));
                    rtlsdr_set_sample_rate(dev, rate);
                }
                break;
            case 0x03: // Set gain
                if (n >= 5) {
                    int gain = ntohl(*(uint32_t *)(cmd_buffer + 1));
                    rtlsdr_set_tuner_gain(dev, gain);
                }
                break;
            default:
                fprintf(stderr, "Unknown command: %d\n", cmd_buffer[0]);
                break;
        }
    }

    close(ctrl_sockfd);
    return NULL;
}

int main(int argc, char **argv) {
    int r, opt;
    uint32_t freq = DEFAULT_FREQ;
    uint32_t samp_rate = DEFAULT_SAMPLE_RATE;
    int gain = 0; // Auto gain
    int dev_index = 0;
    int port = DEFAULT_PORT;
    char *dest_ip = DEFAULT_DEST_IP;
    int dest_port = DEFAULT_PORT;

    struct sockaddr_in serv_addr, ctrl_addr;
    struct sigaction sigact;

    while ((opt = getopt(argc, argv, "f:s:d:g:p:u:")) != -1) {
        switch (opt) {
            case 'f':
                freq = (uint32_t)atof(optarg);
                break;
            case 's':
                samp_rate = (uint32_t)atof(optarg);
                break;
            case 'd':
                dev_index = atoi(optarg);
                break;
            case 'g':
                gain = (int)(atof(optarg) * 10);
                break;
            case 'p':
                port = atoi(optarg);
                break;
            case 'u':
                dest_ip = strtok(optarg, ":");
                char *port_str = strtok(NULL, ":");
                if (port_str) dest_port = atoi(port_str);
                break;
            default:
                usage();
                break;
        }
    }

    // Initializing the device
    r = rtlsdr_open(&dev, dev_index);
    if (r < 0) {
        fprintf(stderr, "Failed to open rtlsdr device #%d.\n", dev_index);
        exit(1);
    }

    verbose_set_frequency(dev, freq);
    verbose_set_sample_rate(dev, samp_rate);
    verbose_gain_set(dev, gain);
    verbose_reset_buffer(dev);

    // Setting up signals
    sigact.sa_handler = sighandler;
    sigemptyset(&sigact.sa_mask);
    sigact.sa_flags = 0;
    sigaction(SIGINT, &sigact, NULL);
    sigaction(SIGTERM, &sigact, NULL);
    sigaction(SIGQUIT, &sigact, NULL);
    sigaction(SIGPIPE, &sigact, NULL);

    // Creating a UDP socket for data
    sockfd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sockfd < 0) {
        fprintf(stderr, "socket error\n");
        rtlsdr_close(dev);
        exit(1);
    }

    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    serv_addr.sin_port = htons(port);
    if (bind(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        fprintf(stderr, "bind error\n");
        close(sockfd);
        rtlsdr_close(dev);
        exit(1);
    }

    // Setting up the recipient's address
    memset(&dest_addr, 0, sizeof(dest_addr));
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_addr.s_addr = inet_addr(dest_ip);
    dest_addr.sin_port = htons(dest_port);

    // Creating a UDP socket for control
    ctrl_sockfd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (ctrl_sockfd < 0) {
        fprintf(stderr, "control socket error\n");
        close(sockfd);
        rtlsdr_close(dev);
        exit(1);
    }

    memset(&ctrl_addr, 0, sizeof(ctrl_addr));
    ctrl_addr.sin_family = AF_INET;
    ctrl_addr.sin_addr.s_addr = INADDR_ANY;
    ctrl_addr.sin_port = htons(port + 1); // Port for commands
    // TODO: ! add port reopening for management!!!!
    if (bind(ctrl_sockfd, (struct sockaddr *)&ctrl_addr, sizeof(ctrl_addr)) < 0) {
        fprintf(stderr, "control bind error\n");
        close(sockfd);
        close(ctrl_sockfd);
        rtlsdr_close(dev);
        exit(1);
    }

    // Starting Streams
    pthread_create(&udp_thread, NULL, udp_server_thread, dev);
    pthread_create(&ctrl_thread, NULL, udp_control_thread, dev);

    fprintf(stderr, "Streaming to %s:%d, control on port %d\n", dest_ip, dest_port, port + 1);

    // Waiting for completion
    pthread_join(udp_thread, NULL);
    pthread_join(ctrl_thread, NULL);

    rtlsdr_close(dev);
    return 0;
}
