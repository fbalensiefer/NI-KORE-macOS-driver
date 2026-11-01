/*
 * kore-monitor.c -- Native Instruments KORE 1 USB monitor (macOS, libusb)
 *
 * Build:
 *   cc -Wall -Wextra -O2 -o kore-monitor kore-monitor.c \
 *      $(pkg-config --cflags --libs libusb-1.0)
 *
 * Reference: Linux sound/usb/caiaq/{device.c,input.c,device.h}
 * Decoding of ERP / ANALOG / IO follows input.c for USB_PID_KORECONTROLLER.
 * Everything that Linux does not define is shown neutrally (raw values).
 *
 * Usage: ./kore-monitor [options]   (see --help)
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <signal.h>
#include <time.h>
#include <sys/time.h>
#include <libusb.h>

#define KORE_VID 0x17CC
#define KORE_PID 0x4711

#define EP_BULK_OUT 0x01
#define EP_BULK_IN  0x81

#define INTERFACE_NUM 0
#define ALT_SETTING   1
#define BUFFER_SIZE   64   /* Linux: EP1_BUFSIZE */

/* CAIAQ EP1 command ids (device.h) */
#define CMD_GET_DEVICE_INFO 0x01
#define CMD_READ_ERP        0x02
#define CMD_READ_ANALOG     0x03
#define CMD_READ_IO         0x04
#define CMD_WRITE_IO        0x05
#define CMD_MIDI_READ       0x06
#define CMD_MIDI_WRITE      0x07
#define CMD_LCD             0x08
#define CMD_AUDIO_PARAMS    0x09
#define CMD_AUTO_MSG        0x0B
#define CMD_DIMM_LEDS       0x0C

/* KORE 1 payload sizes expected by the Linux decoder */
#define ERP_COUNT       8
#define ERP_MIN_PAYLOAD 16
#define ANALOG_PAYLOAD  6
#define IO_KEY_PAYLOAD  4   /* 32 bits; Linux additionally reads payload[4] */

static volatile sig_atomic_t running = 1;
static int transfer_finished = 0;
static int consecutive_errors = 0;

/* ------------------------------------------------------------------ */
/* Options                                                             */
/* ------------------------------------------------------------------ */

static struct {
    int raw;            /* print every packet as hex                    */
    int no_auto;        /* do not send AUTO_MSG                         */
    int poll;           /* send READ_IO / READ_ANALOG once (diagnostic) */
    int poll_ms;        /* >0: repeat READ_IO / READ_ANALOG every N ms  */
    int no_tail;        /* hide changes in ERP bytes 16..31             */
    int selftest;       /* decode built-in sample packets, no USB       */
    int erp_thr;        /* min. circular change (0..999) to report      */
    int analog_thr;     /* min. change (0..4096) to report              */
    unsigned char auto_digital, auto_analog, auto_erp;
} opt = {
    .erp_thr = 2, .analog_thr = 4,
    .auto_digital = 1, .auto_analog = 10, .auto_erp = 5   /* = Linux KORE */
};

/* ------------------------------------------------------------------ */
/* Helpers                                                             */
/* ------------------------------------------------------------------ */

static void handle_signal(int sig)
{
    (void)sig;
    running = 0;
}

static void ts(void)
{
    struct timeval tv;
    struct tm tmv;
    char buf[16];
    time_t s;

    gettimeofday(&tv, NULL);
    s = tv.tv_sec;
    localtime_r(&s, &tmv);
    strftime(buf, sizeof(buf), "%H:%M:%S", &tmv);
    printf("[%s.%03d] ", buf, (int)(tv.tv_usec / 1000));
}

static void hex_out(const unsigned char *data, int length)
{
    for (int i = 0; i < length; i++)
        printf("%02X ", data[i]);
}

static const char *cmd_name(unsigned char c)
{
    switch (c) {
    case CMD_GET_DEVICE_INFO: return "GET_DEVICE_INFO";
    case CMD_READ_ERP:        return "READ_ERP";
    case CMD_READ_ANALOG:     return "READ_ANALOG";
    case CMD_READ_IO:         return "READ_IO";
    case CMD_WRITE_IO:        return "WRITE_IO";
    case CMD_MIDI_READ:       return "MIDI_READ";
    case CMD_MIDI_WRITE:      return "MIDI_WRITE";
    case CMD_LCD:             return "LCD";
    case CMD_AUDIO_PARAMS:    return "AUDIO_PARAMS";
    case CMD_AUTO_MSG:        return "AUTO_MSG";
    case CMD_DIMM_LEDS:       return "DIMM_LEDS";
    default:                  return "UNKNOWN";
    }
}

/* ------------------------------------------------------------------ */
/* Statistics                                                          */
/* ------------------------------------------------------------------ */

struct cmd_stat {
    unsigned long count;
    int min_len, max_len;
};

static struct cmd_stat stats[256];

static void stats_add(unsigned char cmd, int len)
{
    struct cmd_stat *s = &stats[cmd];

    if (s->count == 0 || len < s->min_len) s->min_len = len;
    if (s->count == 0 || len > s->max_len) s->max_len = len;
    s->count++;
}

static void print_summary(void)
{
    printf("\n================ SUMMARY ================\n");
    printf("cmd   name              packets   len(min..max)\n");

    for (int c = 0; c < 256; c++) {
        if (!stats[c].count)
            continue;
        printf("0x%02X  %-16s %8lu   %d..%d\n", c,
               cmd_name((unsigned char)c), stats[c].count,
               stats[c].min_len, stats[c].max_len);
    }

    if (!stats[CMD_READ_ANALOG].count)
        printf("note: no READ_ANALOG (0x03) packet received.\n");
    if (!stats[CMD_READ_IO].count)
        printf("note: no READ_IO (0x04) packet received.\n");
    printf("=========================================\n");
}

/* ------------------------------------------------------------------ */
/* ERP decoder -- 1:1 port of decode_erp() from Linux input.c          */
/* ------------------------------------------------------------------ */

static unsigned int decode_erp(unsigned char a, unsigned char b)
{
    const int HIGH_PEAK = 268;
    const int LOW_PEAK = -7;

    int range = HIGH_PEAK - LOW_PEAK;
    int deg90 = range / 2;
    int deg180 = range;
    int deg270 = deg90 + deg180;
    int deg360 = deg180 * 2;
    int mid_value = (HIGH_PEAK + LOW_PEAK) / 2;

    int weight_a, weight_b, pos_a, pos_b, ret;

    weight_b = abs(mid_value - a) - (range / 2 - 100) / 2;

    if (weight_b < 0)
        weight_b = 0;
    if (weight_b > 100)
        weight_b = 100;

    weight_a = 100 - weight_b;

    if (a < mid_value) {
        /* 0..90 and 270..360 degrees */
        pos_b = b - LOW_PEAK + deg270;
        if (pos_b >= deg360)
            pos_b -= deg360;
    } else {
        /* 90..270 degrees */
        pos_b = HIGH_PEAK - b + deg90;
    }

    if (b > mid_value)
        /* 0..180 degrees */
        pos_a = a - LOW_PEAK;
    else
        /* 180..360 degrees */
        pos_a = HIGH_PEAK - a + deg180;

    ret = pos_a * weight_a + pos_b * weight_b;

    ret *= 10;
    ret /= deg360;

    if (ret < 0)
        ret += 1000;
    if (ret >= 1000)
        ret -= 1000;

    return (unsigned int)ret;
}

/*
 * Linux input.c, KORE 1: decode_erp(buf[a], buf[b]) with buf = payload
 * (raw packet without the command byte).  Axis names are the Linux
 * input-event names; the physical knob of each channel is UNKNOWN.
 */
static const struct {
    const char *axis;
    int a, b;
} erp_map[ERP_COUNT] = {
    { "HAT0X",  7,  5 },
    { "HAT0Y", 12, 14 },
    { "HAT1X", 15, 13 },
    { "HAT1Y",  0,  2 },
    { "HAT2X",  3,  1 },
    { "HAT2Y",  8, 10 },
    { "HAT3X", 11,  9 },
    { "HAT3Y",  4,  6 },
};

/*
 * Linux keycode_kore[]: bit i of the 32-bit IO field (payload byte i/8,
 * bit i%8) -> Linux key code.  The quoted names are comments from the
 * Linux source, NOT verified against the physical labels.
 */
static const char *io_label[32] = {
    /* 00 */ "KEY_FN_F1 \"menu\"",
    /* 01 */ "KEY_FN_F7 \"lcd backlight\"",
    /* 02 */ "KEY_FN_F2 \"control\"",
    /* 03 */ "KEY_FN_F3 \"enter\"",
    /* 04 */ "KEY_FN_F4 \"view\"",
    /* 05 */ "KEY_FN_F5 \"esc\"",
    /* 06 */ "KEY_FN_F6 \"sound\"",
    /* 07 */ "KEY_FN_F8 (array spacer, never triggered per Linux)",
    /* 08 */ "KEY_RIGHT",
    /* 09 */ "KEY_DOWN",
    /* 10 */ "KEY_UP",
    /* 11 */ "KEY_LEFT",
    /* 12 */ "KEY_SOUND \"listen\"",
    /* 13 */ "KEY_RECORD",
    /* 14 */ "KEY_PLAYPAUSE",
    /* 15 */ "KEY_STOP",
    /* 16 */ "BTN_4 (softkey)",
    /* 17 */ "BTN_3 (softkey)",
    /* 18 */ "BTN_2 (softkey)",
    /* 19 */ "BTN_1 (softkey)",
    /* 20 */ "BTN_8 (softkey)",
    /* 21 */ "BTN_7 (softkey)",
    /* 22 */ "BTN_6 (softkey)",
    /* 23 */ "BTN_5 (softkey)",
    /* 24 */ "KEY_BRL_DOT4 (touch knob)",
    /* 25 */ "KEY_BRL_DOT3 (touch knob)",
    /* 26 */ "KEY_BRL_DOT2 (touch knob)",
    /* 27 */ "KEY_BRL_DOT1 (touch knob)",
    /* 28 */ "KEY_BRL_DOT8 (touch knob)",
    /* 29 */ "KEY_BRL_DOT7 (touch knob)",
    /* 30 */ "KEY_BRL_DOT6 (touch knob)",
    /* 31 */ "KEY_BRL_DOT5 (touch knob)",
};

/* ------------------------------------------------------------------ */
/* Decoder state                                                       */
/* ------------------------------------------------------------------ */

static struct {
    int have_erp;
    unsigned int erp[ERP_COUNT];

    int have_tail;
    int tail_len;
    unsigned char tail[64];

    int have_analog;
    int analog[3];

    int have_io;
    uint32_t io_bits;
    int have_misc;
    int misc;
} st;

static int circ_diff(unsigned int a, unsigned int b)
{
    int d = abs((int)a - (int)b);

    return d > 500 ? 1000 - d : d;
}

/* ------------------------------------------------------------------ */
/* Packet decoders                                                     */
/* ------------------------------------------------------------------ */

static void decode_device_info(const unsigned char *data, int length)
{
    const unsigned char *p = data + 1;

    if (length < 15)
        return;

    printf("\n========== KORE DEVICE INFO ==========\n");
    printf("Firmware version:       %u\n", p[0] | ((unsigned int)p[1] << 8));
    printf("Hardware subtype:       %u\n", p[2]);
    printf("ERP count:              %u\n", p[3]);
    printf("Analog inputs:          %u\n", p[4]);
    printf("Digital inputs:         %u\n", p[5]);
    printf("Digital outputs:        %u\n", p[6]);
    printf("Analog audio outputs:   %u\n", p[7]);
    printf("Analog audio inputs:    %u\n", p[8]);
    printf("Digital audio outputs:  %u\n", p[9]);
    printf("Digital audio inputs:   %u\n", p[10]);
    printf("MIDI outputs:           %u\n", p[11]);
    printf("MIDI inputs:            %u\n", p[12]);
    printf("Data alignment:         %u\n", p[13]);
    printf("(Linux KORE decoder expects: 8 ERP, 3 analog, 32 digital)\n");
    printf("======================================\n\n");
}

static void handle_erp(const unsigned char *d, int len)
{
    const unsigned char *p = d + 1;
    int plen = len - 1;
    unsigned int v[ERP_COUNT];
    int tlen;

    if (plen < ERP_MIN_PAYLOAD) {
        ts();
        printf("ERP short packet: %d payload bytes (need %d), ignored\n",
               plen, ERP_MIN_PAYLOAD);
        return;
    }

    for (int i = 0; i < ERP_COUNT; i++)
        v[i] = decode_erp(p[erp_map[i].a], p[erp_map[i].b]);

    if (!st.have_erp) {
        ts();
        printf("ERP baseline (Linux order, 0..999, mapping to knobs unknown)\n");
        for (int i = 0; i < ERP_COUNT; i++)
            printf("            ERP %d [%s] = %3u   (a=p[%d]=0x%02X b=p[%d]=0x%02X)\n",
                   i + 1, erp_map[i].axis, v[i],
                   erp_map[i].a, p[erp_map[i].a],
                   erp_map[i].b, p[erp_map[i].b]);
        memcpy(st.erp, v, sizeof(v));
        st.have_erp = 1;
    } else {
        char line[512];
        size_t n = 0;

        line[0] = '\0';
        for (int i = 0; i < ERP_COUNT; i++) {
            if (circ_diff(v[i], st.erp[i]) < opt.erp_thr)
                continue;
            n += (size_t)snprintf(line + n, sizeof(line) - n,
                                  " | ERP %d [%s] %u -> %u",
                                  i + 1, erp_map[i].axis, st.erp[i], v[i]);
            if (n >= sizeof(line))
                n = sizeof(line) - 1;
            st.erp[i] = v[i];
        }
        if (n) {
            ts();
            printf("ERP%s\n", line);
        }
    }

    /* Payload bytes 16.. are not used by Linux: meaning UNKNOWN. */
    tlen = plen - ERP_MIN_PAYLOAD;
    if (tlen > (int)sizeof(st.tail))
        tlen = (int)sizeof(st.tail);

    if (tlen > 0) {
        const unsigned char *t = p + ERP_MIN_PAYLOAD;

        if (!st.have_tail) {
            ts();
            printf("ERP tail p[16..%d] baseline: ", ERP_MIN_PAYLOAD + tlen - 1);
            hex_out(t, tlen);
            printf("\n");
        } else if ((tlen != st.tail_len || memcmp(t, st.tail, (size_t)tlen))
                   && !opt.no_tail) {
            ts();
            printf("ERP tail p[16..%d] changed:  ", ERP_MIN_PAYLOAD + tlen - 1);
            hex_out(t, tlen);
            printf("\n");
        }
        memcpy(st.tail, t, (size_t)tlen);
        st.tail_len = tlen;
        st.have_tail = 1;
    }
}

static void handle_analog(const unsigned char *d, int len)
{
    const unsigned char *p = d + 1;
    int plen = len - 1;
    int v[3];

    if (plen < ANALOG_PAYLOAD) {
        ts();
        printf("ANALOG short packet: %d payload bytes (need %d), ignored\n",
               plen, ANALOG_PAYLOAD);
        return;
    }

    /* big endian, as in Linux input.c */
    v[0] = (p[0] << 8) | p[1];
    v[1] = (p[2] << 8) | p[3];
    v[2] = (p[4] << 8) | p[5];

    if (!st.have_analog) {
        ts();
        printf("ANALOG baseline: X=%d Y=%d Z=%d  (Linux ABS_X/Y/Z, "
               "declared range 0..4096)\n", v[0], v[1], v[2]);
        memcpy(st.analog, v, sizeof(v));
        st.have_analog = 1;
        return;
    }

    {
        char line[160];
        size_t n = 0;
        static const char axis[3] = { 'X', 'Y', 'Z' };

        line[0] = '\0';
        for (int i = 0; i < 3; i++) {
            if (abs(v[i] - st.analog[i]) < opt.analog_thr)
                continue;
            n += (size_t)snprintf(line + n, sizeof(line) - n,
                                  " | %c %d -> %d", axis[i], st.analog[i], v[i]);
            if (n >= sizeof(line))
                n = sizeof(line) - 1;
            st.analog[i] = v[i];
        }
        if (n) {
            ts();
            printf("ANALOG%s\n", line);
        }
    }
}

static void handle_io(const unsigned char *d, int len)
{
    const unsigned char *p = d + 1;
    int plen = len - 1;
    uint32_t bits, diff;

    if (plen < IO_KEY_PAYLOAD) {
        ts();
        printf("IO short packet: %d payload bytes (need %d), ignored\n",
               plen, IO_KEY_PAYLOAD);
        return;
    }

    bits = (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);

    if (!st.have_io) {
        ts();
        printf("IO baseline: bits=0x%08X (bit set = pressed/touched, per Linux)\n",
               bits);
        for (int i = 0; i < 32; i++)
            if (bits & (1u << i))
                printf("            active: bit %02d [%s]\n", i, io_label[i]);
        st.io_bits = bits;
        st.have_io = 1;
    } else {
        diff = bits ^ st.io_bits;
        for (int i = 0; i < 32; i++) {
            if (!(diff & (1u << i)))
                continue;
            ts();
            printf("IO bit %02d [byte %d.bit %d] %-8s (Linux: %s)\n",
                   i, i / 8, i % 8,
                   (bits & (1u << i)) ? "PRESSED" : "RELEASED",
                   io_label[i]);
        }
        st.io_bits = bits;
    }

    /* Linux: ABS_MISC = 255 - payload[4] (Linux does not check length) */
    if (plen >= 5) {
        int m = 255 - p[4];

        if (!st.have_misc || m != st.misc) {
            ts();
            printf("IO misc: payload[4]=0x%02X -> ABS_MISC=%d%s\n",
                   p[4], m, st.have_misc ? "" : "  (baseline)");
            st.misc = m;
            st.have_misc = 1;
        }
    }
}

static void handle_packet(const unsigned char *d, int len)
{
    int printed_raw = 0;

    if (len <= 0)
        return;

    stats_add(d[0], len);

    if (opt.raw) {
        ts();
        printf("RAW %-15s | %3d bytes | ", cmd_name(d[0]), len);
        hex_out(d, len);
        printf("\n");
        printed_raw = 1;
    }

    switch (d[0]) {
    case CMD_READ_ERP:
        handle_erp(d, len);
        break;
    case CMD_READ_ANALOG:
        handle_analog(d, len);
        break;
    case CMD_READ_IO:
        handle_io(d, len);
        break;
    default:
        if (!printed_raw) {
            ts();
            printf("IN  %-15s | %3d bytes | ", cmd_name(d[0]), len);
            hex_out(d, len);
            printf("\n");
        }
        if (d[0] == CMD_GET_DEVICE_INFO)
            decode_device_info(d, len);
        break;
    }
}

/* ------------------------------------------------------------------ */
/* Offline self test (no USB access)                                   */
/* ------------------------------------------------------------------ */

static void run_selftest(void)
{
    /* GET_DEVICE_INFO reply and first 17 bytes of an ERP packet from the
     * real log.  Bytes 17..32 of the ERP packet were elided ("00 00 ...")
     * in the log; they are assumed to be 0 here. IO/ANALOG packets are
     * SYNTHETIC and only exercise the parser. */
    static const unsigned char info[] = {
        0x01, 0x29, 0x00, 0x02, 0x08, 0x03, 0x20, 0x00,
        0x04, 0x02, 0x00, 0x00, 0x01, 0x01, 0x02 };
    unsigned char erp1[33] = {
        0x02, 0x36, 0x10, 0x58, 0xA7, 0xFF, 0xB2, 0x6B, 0xFF,
        0x3F, 0x29, 0x3C, 0x64, 0xFF, 0x0B, 0x6B, 0x7E };
    unsigned char erp2[33];
    unsigned char io1[6]   = { 0x04, 0x00, 0x00, 0x00, 0x00, 0xFF };
    unsigned char io2[6]   = { 0x04, 0x00, 0x01, 0x00, 0x00, 0xFF };
    unsigned char io3[6]   = { 0x04, 0x00, 0x00, 0x00, 0x00, 0xFF };
    unsigned char an1[7]   = { 0x03, 0x08, 0x00, 0x08, 0x00, 0x08, 0x00 };
    unsigned char an2[7]   = { 0x03, 0x08, 0x40, 0x08, 0x00, 0x07, 0xF0 };

    printf("--- SELFTEST (offline) ---\n");
    handle_packet(info, sizeof(info));
    handle_packet(erp1, sizeof(erp1));
    handle_packet(erp1, sizeof(erp1));            /* unchanged: silent */
    memcpy(erp2, erp1, sizeof(erp2));
    erp2[6] = 0x20;                               /* payload[5] changes */
    erp2[8] = 0x80;                               /* payload[7] changes */
    handle_packet(erp2, sizeof(erp2));
    handle_packet(io1, sizeof(io1));
    handle_packet(io2, sizeof(io2));              /* bit 08 pressed     */
    handle_packet(io3, sizeof(io3));              /* bit 08 released    */
    handle_packet(an1, sizeof(an1));
    handle_packet(an2, sizeof(an2));
    print_summary();
}

/* ------------------------------------------------------------------ */
/* USB                                                                 */
/* ------------------------------------------------------------------ */

static void rearm(struct libusb_transfer *transfer)
{
    int r = libusb_submit_transfer(transfer);

    if (r != 0) {
        fprintf(stderr, "Unable to re-arm receiver: %s\n", libusb_error_name(r));
        running = 0;
        transfer_finished = 1;
    }
}

static void LIBUSB_CALL bulk_in_callback(struct libusb_transfer *transfer)
{
    switch (transfer->status) {

    case LIBUSB_TRANSFER_COMPLETED:
        consecutive_errors = 0;
        if (transfer->actual_length > 0)
            handle_packet(transfer->buffer, transfer->actual_length);

        if (running)
            rearm(transfer);
        else
            transfer_finished = 1;
        break;

    case LIBUSB_TRANSFER_TIMED_OUT:
    case LIBUSB_TRANSFER_OVERFLOW:
    case LIBUSB_TRANSFER_STALL:
        fprintf(stderr, "\nUSB transfer problem: status %d (%d in a row)\n",
                transfer->status, consecutive_errors + 1);

        if (++consecutive_errors > 5 || !running) {
            running = 0;
            transfer_finished = 1;
            break;
        }
        if (transfer->status == LIBUSB_TRANSFER_STALL)
            libusb_clear_halt(transfer->dev_handle, EP_BULK_IN);
        rearm(transfer);
        break;

    case LIBUSB_TRANSFER_CANCELLED:
        printf("\nUSB transfer cancelled.\n");
        transfer_finished = 1;
        break;

    case LIBUSB_TRANSFER_NO_DEVICE:
        printf("\nKORE disconnected.\n");
        running = 0;
        transfer_finished = 1;
        break;

    default:
        fprintf(stderr, "\nUSB transfer error: status %d\n", transfer->status);
        running = 0;
        transfer_finished = 1;
        break;
    }
}

static int send_quiet;   /* suppress OUT log lines (cyclic polling) */

static int send_command(libusb_device_handle *dev, const unsigned char *data,
                        int len, const char *what)
{
    int transferred = 0;
    int r = libusb_bulk_transfer(dev, EP_BULK_OUT, (unsigned char *)data,
                                 len, &transferred, 1000);

    if (r != 0) {
        fprintf(stderr, "%s failed: %s\n", what, libusb_error_name(r));
        return r;
    }

    if (!send_quiet) {
        ts();
        printf("OUT 0x01 | %d byte(s) | %-15s | ", transferred, what);
        hex_out(data, transferred);
        printf("\n");
    }
    return 0;
}

static void dump_endpoints(libusb_device_handle *dev)
{
    libusb_device *d = libusb_get_device(dev);
    struct libusb_config_descriptor *cfg = NULL;
    static const char *speed[] = { "unknown", "low", "full", "high", "super" };
    int sp = libusb_get_device_speed(d);

    printf("USB speed: %s\n", (sp >= 0 && sp <= 4) ? speed[sp] : "?");

    if (libusb_get_active_config_descriptor(d, &cfg) != 0 || !cfg)
        return;

    if (INTERFACE_NUM < cfg->bNumInterfaces) {
        const struct libusb_interface *itf = &cfg->interface[INTERFACE_NUM];

        for (int a = 0; a < itf->num_altsetting; a++) {
            const struct libusb_interface_descriptor *alt = &itf->altsetting[a];

            if (alt->bAlternateSetting != ALT_SETTING)
                continue;
            for (int e = 0; e < alt->bNumEndpoints; e++) {
                const struct libusb_endpoint_descriptor *ep = &alt->endpoint[e];

                printf("  EP 0x%02X attr=0x%02X maxPacket=%u interval=%u\n",
                       ep->bEndpointAddress, ep->bmAttributes,
                       ep->wMaxPacketSize, ep->bInterval);
            }
        }
    }
    libusb_free_config_descriptor(cfg);
}

/* ------------------------------------------------------------------ */
/* CLI                                                                 */
/* ------------------------------------------------------------------ */

static void usage(const char *prog)
{
    printf("Usage: %s [options]\n\n"
           "  --raw                 print every packet as hex as well\n"
           "  --auto D A E          AUTO_MSG parameters (default 1 10 5 = Linux KORE)\n"
           "  --no-auto             do not send AUTO_MSG at all\n"
           "  --poll                send READ_IO (0x04) and READ_ANALOG (0x03) once\n"
           "                        (diagnostic; not part of the Linux KORE init)\n"
           "  --poll-ms N           repeat READ_IO + READ_ANALOG every N ms (10..5000)\n"
           "                        (diagnostic; shows whether the device ever reports keys)\n"
           "  --erp-threshold N     report ERP changes >= N (0..999, default 2; 0 = all)\n"
           "  --analog-threshold N  report analog changes >= N (default 4)\n"
           "  --no-tail             hide changes of ERP payload bytes 16..31\n"
           "  --selftest            decode built-in sample packets, no USB access\n"
           "  --help                this text\n", prog);
}

static int parse_num(const char *s, int lo, int hi, int *out)
{
    char *end = NULL;
    long v = strtol(s, &end, 0);

    if (!s[0] || *end || v < lo || v > hi)
        return -1;
    *out = (int)v;
    return 0;
}

static int parse_args(int argc, char **argv)
{
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        int v;

        if (!strcmp(a, "--raw")) {
            opt.raw = 1;
        } else if (!strcmp(a, "--no-auto")) {
            opt.no_auto = 1;
        } else if (!strcmp(a, "--poll")) {
            opt.poll = 1;
        } else if (!strcmp(a, "--no-tail")) {
            opt.no_tail = 1;
        } else if (!strcmp(a, "--selftest")) {
            opt.selftest = 1;
        } else if (!strcmp(a, "--help") || !strcmp(a, "-h")) {
            usage(argv[0]);
            return 1;
        } else if (!strcmp(a, "--auto") && i + 3 < argc) {
            int d, an, e;

            if (parse_num(argv[i + 1], 0, 255, &d) ||
                parse_num(argv[i + 2], 0, 255, &an) ||
                parse_num(argv[i + 3], 0, 255, &e)) {
                fprintf(stderr, "--auto needs three values 0..255\n");
                return -1;
            }
            opt.auto_digital = (unsigned char)d;
            opt.auto_analog = (unsigned char)an;
            opt.auto_erp = (unsigned char)e;
            i += 3;
        } else if (!strcmp(a, "--poll-ms") && i + 1 < argc) {
            if (parse_num(argv[++i], 10, 5000, &v)) {
                fprintf(stderr, "bad --poll-ms (10..5000)\n");
                return -1;
            }
            opt.poll_ms = v;
        } else if (!strcmp(a, "--erp-threshold") && i + 1 < argc) {
            if (parse_num(argv[++i], 0, 999, &v)) {
                fprintf(stderr, "bad --erp-threshold\n");
                return -1;
            }
            opt.erp_thr = v;
        } else if (!strcmp(a, "--analog-threshold") && i + 1 < argc) {
            if (parse_num(argv[++i], 0, 4096, &v)) {
                fprintf(stderr, "bad --analog-threshold\n");
                return -1;
            }
            opt.analog_thr = v;
        } else {
            fprintf(stderr, "Unknown or incomplete option: %s\n", a);
            usage(argv[0]);
            return -1;
        }
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* main                                                                */
/* ------------------------------------------------------------------ */

int main(int argc, char **argv)
{
    libusb_context *ctx = NULL;
    libusb_device_handle *dev = NULL;
    struct libusb_transfer *transfer = NULL;
    unsigned char *buffer = NULL;
    unsigned char info_cmd = CMD_GET_DEVICE_INFO;
    int r, claimed = 0, submitted = 0;
    int pa;

    struct timeval timeout = { .tv_sec = 0, .tv_usec = 100000 };

    setvbuf(stdout, NULL, _IOLBF, 0);

    pa = parse_args(argc, argv);
    if (pa != 0)
        return pa > 0 ? 0 : 2;

    if (opt.selftest) {
        run_selftest();
        return 0;
    }

    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    printf("=====================================\n");
    printf(" Native Instruments KORE 1 Monitor\n");
    printf(" USB ID: 17CC:4711\n");
    printf("=====================================\n\n");

    r = libusb_init(&ctx);
    if (r != 0) {
        fprintf(stderr, "libusb init: %s\n", libusb_error_name(r));
        goto cleanup;
    }

    dev = libusb_open_device_with_vid_pid(ctx, KORE_VID, KORE_PID);
    if (!dev) {
        fprintf(stderr, "KORE 1 not found or inaccessible.\n");
        goto cleanup;
    }
    printf("KORE 1 found.\n");

    r = libusb_claim_interface(dev, INTERFACE_NUM);
    if (r != 0) {
        fprintf(stderr, "Claim interface: %s\n", libusb_error_name(r));
        goto cleanup;
    }
    claimed = 1;

    r = libusb_set_interface_alt_setting(dev, INTERFACE_NUM, ALT_SETTING);
    if (r != 0) {
        fprintf(stderr, "Alternate setting: %s\n", libusb_error_name(r));
        goto cleanup;
    }
    printf("Interface 0 / Alternate Setting 1 selected.\n");
    dump_endpoints(dev);

    buffer = malloc(BUFFER_SIZE);
    if (!buffer) {
        fprintf(stderr, "Buffer allocation failed.\n");
        goto cleanup;
    }

    transfer = libusb_alloc_transfer(0);
    if (!transfer) {
        fprintf(stderr, "Transfer allocation failed.\n");
        goto cleanup;
    }

    /* Arm the permanent Bulk-IN receiver BEFORE sending any command
     * (same order as the Linux driver: submit EP1 URB, then command). */
    libusb_fill_bulk_transfer(transfer, dev, EP_BULK_IN, buffer, BUFFER_SIZE,
                              bulk_in_callback, NULL, 0);

    r = libusb_submit_transfer(transfer);
    if (r != 0) {
        fprintf(stderr, "Submit receiver: %s\n", libusb_error_name(r));
        goto cleanup;
    }
    submitted = 1;
    printf("Bulk-IN receiver armed.\n");

    if (send_command(dev, &info_cmd, 1, "GET_DEVICE_INFO"))
        goto cleanup;

    if (!opt.no_auto) {
        unsigned char auto_msg[4] = { CMD_AUTO_MSG, opt.auto_digital,
                                      opt.auto_analog, opt.auto_erp };

        if (send_command(dev, auto_msg, sizeof(auto_msg), "AUTO_MSG"))
            goto cleanup;
    } else {
        printf("AUTO_MSG skipped (--no-auto).\n");
    }

    if (opt.poll) {
        static const unsigned char poll_io[] = { CMD_READ_IO };
        static const unsigned char poll_an[] = { CMD_READ_ANALOG };

        send_command(dev, poll_io, 1, "READ_IO poll");
        send_command(dev, poll_an, 1, "READ_ANALOG poll");
    }

    printf("\nMonitoring KORE USB traffic. Unchanged values are not repeated.\n");
    printf("Move controls and press buttons to observe input.\n");
    printf("Press Ctrl+C to stop.\n\n");

    {
        struct timeval last_poll;

        gettimeofday(&last_poll, NULL);

    while (running) {
        r = libusb_handle_events_timeout(ctx, &timeout);

        if (r != 0 && r != LIBUSB_ERROR_INTERRUPTED) {
            fprintf(stderr, "USB event error: %s\n", libusb_error_name(r));
            break;
        }

        if (opt.poll_ms > 0 && running) {
            struct timeval now;
            long ms;

            gettimeofday(&now, NULL);
            ms = (now.tv_sec - last_poll.tv_sec) * 1000 +
                 (now.tv_usec - last_poll.tv_usec) / 1000;
            if (ms >= opt.poll_ms) {
                static const unsigned char poll_io[] = { CMD_READ_IO };
                static const unsigned char poll_an[] = { CMD_READ_ANALOG };

                send_quiet = 1;
                send_command(dev, poll_io, 1, "READ_IO poll");
                send_command(dev, poll_an, 1, "READ_ANALOG poll");
                send_quiet = 0;
                last_poll = now;
            }
        }
    }
    }

    printf("\nStopping monitor...\n");

cleanup:
    running = 0;

    if (submitted && transfer && !transfer_finished) {
        libusb_cancel_transfer(transfer);

        while (!transfer_finished) {
            struct timeval tv = { .tv_sec = 0, .tv_usec = 100000 };

            if (libusb_handle_events_timeout(ctx, &tv) != 0)
                break;
        }
    }

    if (transfer)
        libusb_free_transfer(transfer);
    free(buffer);

    if (claimed)
        libusb_release_interface(dev, INTERFACE_NUM);
    if (dev)
        libusb_close(dev);
    if (ctx)
        libusb_exit(ctx);

    print_summary();
    printf("Monitor terminated.\n");
    return 0;
}
