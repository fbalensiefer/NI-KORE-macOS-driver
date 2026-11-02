#include <stdio.h>
#include <libusb.h>

int main(void) {
    libusb_context *ctx = NULL;

    int r = libusb_init(&ctx);
    if (r != 0) {
        fprintf(stderr, "libusb_init: %s\n", libusb_error_name(r));
        return 1;
    }

    libusb_device **list = NULL;
    ssize_t count = libusb_get_device_list(ctx, &list);

    if (count < 0) {
        fprintf(stderr, "Device list failed\n");
        libusb_exit(ctx);
        return 1;
    }

    int found = 0;

    for (ssize_t i = 0; i < count; i++) {
        struct libusb_device_descriptor d;

        if (libusb_get_device_descriptor(list[i], &d) != 0)
            continue;

        if (d.idVendor != 0x17cc || d.idProduct != 0x4711)
            continue;

        found = 1;

        printf("KORE 1 detected\n");
        printf("VID=%04x PID=%04x\n", d.idVendor, d.idProduct);
        printf("USB=%04x Device=%04x\n", d.bcdUSB, d.bcdDevice);
        printf("Configurations=%u\n\n", d.bNumConfigurations);

        for (int c = 0; c < d.bNumConfigurations; c++) {
            struct libusb_config_descriptor *cfg = NULL;

            r = libusb_get_config_descriptor(list[i], c, &cfg);

            if (r != 0) {
                printf("Configuration %d error: %s\n",
                       c, libusb_error_name(r));
                continue;
            }

            printf("CONFIG %d value=%u interfaces=%u\n",
                   c, cfg->bConfigurationValue,
                   cfg->bNumInterfaces);

            for (int j = 0; j < cfg->bNumInterfaces; j++) {
                const struct libusb_interface *iface =
                    &cfg->interface[j];

                for (int a = 0; a < iface->num_altsetting; a++) {
                    const struct libusb_interface_descriptor *alt =
                        &iface->altsetting[a];

                    printf(
                        "\nINTERFACE %u ALT %u "
                        "class=0x%02x subclass=0x%02x endpoints=%u\n",
                        alt->bInterfaceNumber,
                        alt->bAlternateSetting,
                        alt->bInterfaceClass,
                        alt->bInterfaceSubClass,
                        alt->bNumEndpoints
                    );

                    for (int e = 0; e < alt->bNumEndpoints; e++) {
                        const struct libusb_endpoint_descriptor *ep =
                            &alt->endpoint[e];

                        unsigned type = ep->bmAttributes & 0x03;
                        unsigned transactions =
                            1 + ((ep->wMaxPacketSize >> 11) & 0x03);
                        unsigned packet_size =
                            ep->wMaxPacketSize & 0x07ff;

                        printf(
                            "  EP 0x%02x type=%u "
                            "wMaxPacketSize=0x%04x "
                            "payload=%u transactions=%u "
                            "interval=%u\n",
                            ep->bEndpointAddress,
                            type,
                            ep->wMaxPacketSize,
                            packet_size,
                            transactions,
                            ep->bInterval
                        );
                    }
                }
            }

            libusb_free_config_descriptor(cfg);
        }
    }

    libusb_free_device_list(list, 1);
    libusb_exit(ctx);

    if (!found) {
        fprintf(stderr, "KORE 1 not found\n");
        return 2;
    }

    return 0;
}
