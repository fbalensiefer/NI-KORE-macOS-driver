#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <signal.h>
#include <time.h>
#include <sys/time.h>
#include <libusb.h>

#define KORE_VID 0x17CC
#define KORE_PID 0x4711

#define EP_BULK_OUT 0x01
#define EP_BULK_IN  0x81

#define INTERFACE_NUM 0
#define ALT_SETTING 1
#define BUFFER_SIZE 512

static volatile sig_atomic_t running = 1;
static int transfer_finished = 0;

static void handle_signal(int sig)
{
    (void)sig;
    running = 0;
}

static void timestamp(void)
{
    struct timeval tv;
    struct tm *tm_info;
    char buffer[32];

    gettimeofday(&tv, NULL);
    tm_info = localtime(&tv.tv_sec);

    strftime(buffer, sizeof(buffer), "%H:%M:%S", tm_info);
    printf("[%s.%03ld] ", buffer, tv.tv_usec / 1000);
}

static void print_hex(const unsigned char *data, int length)
{
    for (int i = 0; i < length; i++) {
        printf("%02X ", data[i]);
    }
    printf("\n");
}

static void decode_device_info(
    const unsigned char *data, int length)
{
    if (length < 15 || data[0] != 0x01)
        return;

    const unsigned char *p = data + 1;

    unsigned int firmware =
        p[0] | ((unsigned int)p[1] << 8);

    printf("\n");
    printf("========== KORE DEVICE INFO ==========\n");
    printf("Firmware version:       %u\n", firmware);
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
    printf("======================================\n\n");

    fflush(stdout);
}

static void LIBUSB_CALL bulk_in_callback(
    struct libusb_transfer *transfer)
{
    switch (transfer->status) {

    case LIBUSB_TRANSFER_COMPLETED:

        if (transfer->actual_length > 0) {
            timestamp();

            printf("IN  0x81 | %3d bytes | ",
                   transfer->actual_length);

            print_hex(
                transfer->buffer,
                transfer->actual_length
            );

            decode_device_info(
                transfer->buffer,
                transfer->actual_length
            );
        }

        if (running) {
            int r = libusb_submit_transfer(transfer);

            if (r != 0) {
                fprintf(stderr,
                    "Unable to re-arm receiver: %s\n",
                    libusb_error_name(r));

                running = 0;
                transfer_finished = 1;
            }
        } else {
            transfer_finished = 1;
        }

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
        fprintf(stderr,
            "\nUSB transfer error: status %d\n",
            transfer->status);

        running = 0;
        transfer_finished = 1;
        break;
    }
}

int main(void)
{
    libusb_context *ctx = NULL;
    libusb_device_handle *dev = NULL;
    struct libusb_transfer *transfer = NULL;

    unsigned char *buffer = NULL;
    unsigned char command = 0x01;
    unsigned char auto_msg[] = {
    	0x0B, 0x01, 0x0A, 0x05
	};
    int r;
    int transferred = 0;
    int claimed = 0;
    int submitted = 0;

    struct timeval timeout = {
        .tv_sec = 0,
        .tv_usec = 100000
    };

    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    printf("=====================================\n");
    printf(" Native Instruments KORE 1 Monitor\n");
    printf(" USB ID: 17CC:4711\n");
    printf("=====================================\n\n");

    r = libusb_init(&ctx);

    if (r != 0) {
        fprintf(stderr, "libusb init: %s\n",
                libusb_error_name(r));
        goto cleanup;
    }

    dev = libusb_open_device_with_vid_pid(
        ctx, KORE_VID, KORE_PID
    );

    if (!dev) {
        fprintf(stderr,
            "KORE 1 not found or inaccessible.\n");
        goto cleanup;
    }

    printf("KORE 1 found.\n");

    r = libusb_claim_interface(dev, INTERFACE_NUM);

    if (r != 0) {
        fprintf(stderr, "Claim interface: %s\n",
                libusb_error_name(r));
        goto cleanup;
    }

    claimed = 1;

    r = libusb_set_interface_alt_setting(
        dev, INTERFACE_NUM, ALT_SETTING
    );

    if (r != 0) {
        fprintf(stderr, "Alternate setting: %s\n",
                libusb_error_name(r));
        goto cleanup;
    }

    printf("Interface 0 / Alternate Setting 1 selected.\n");

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

    /*
     * Arm permanent asynchronous Bulk-IN receiver
     * BEFORE sending GET_DEVICE_INFO.
     */

    libusb_fill_bulk_transfer(
        transfer,
        dev,
        EP_BULK_IN,
        buffer,
        BUFFER_SIZE,
        bulk_in_callback,
        NULL,
        0
    );

    r = libusb_submit_transfer(transfer);

    if (r != 0) {
        fprintf(stderr, "Submit receiver: %s\n",
                libusb_error_name(r));
        goto cleanup;
    }

    submitted = 1;

    printf("Bulk-IN receiver armed.\n");
    printf("Sending GET_DEVICE_INFO (0x01)...\n");

    /*
     * Only documented, read-only initialization command.
     */

    r = libusb_bulk_transfer(
        dev,
        EP_BULK_OUT,
        &command,
        1,
        &transferred,
        1000
    );

    if (r != 0) {
        fprintf(stderr, "Command transfer: %s\n",
                libusb_error_name(r));
        goto cleanup;
    }

    timestamp();
    printf("OUT 0x01 | %d byte(s) | 01\n", transferred);
/* Enable KORE input messages */

r = libusb_bulk_transfer(
    dev,
    EP_BULK_OUT,
    auto_msg,
    sizeof(auto_msg),
    &transferred,
    1000
);

if (r != 0) {
    fprintf(stderr, "AUTO_MSG failed: %s\n",
            libusb_error_name(r));
    goto cleanup;
}

timestamp();
printf("AUTO_MSG sent: ");
print_hex(auto_msg, transferred);

    printf("\n");
    printf("Monitoring KORE USB traffic.\n");
    printf("Move controls and press buttons to observe input.\n");
    printf("Press Ctrl+C to stop.\n\n");

    fflush(stdout);

    while (running) {
        r = libusb_handle_events_timeout(ctx, &timeout);

        if (r != 0 && r != LIBUSB_ERROR_INTERRUPTED) {
            fprintf(stderr, "USB event error: %s\n",
                    libusb_error_name(r));
            break;
        }
    }

    printf("\nStopping monitor...\n");

cleanup:

    running = 0;

    if (submitted && transfer && !transfer_finished) {
        libusb_cancel_transfer(transfer);

        while (!transfer_finished) {
            struct timeval tv = {
                .tv_sec = 0,
                .tv_usec = 100000
            };

            libusb_handle_events_timeout(ctx, &tv);
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

    printf("Monitor terminated.\n");

    return 0;
}
