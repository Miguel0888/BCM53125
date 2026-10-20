/*
 * Broadcom BCM53xx switch mirroring helper (experimental).
 *
 * Target: Asus RT-N66U, DD-WRT Linux MIPS32 little endian.
 * Uses the system C runtime for correct MIPS process startup and IOCTL ABI.
 *
 * "probe" reads mirror config without writing mirror config registers;
 * accessing switch registers does update the MDIO pseudo-PHY page/address.
 *
 * This program has NOT been validated on physical router hardware.
 * License: MIT.
 */
#define _DEFAULT_SOURCE 1

#include <errno.h>
#include <net/if.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>
#include <linux/mii.h>
#include <linux/sockios.h>

#ifndef SIOCDEVPRIVATE
#define SIOCDEVPRIVATE 0x89F0
#endif

#define SIOCGETCPHYRD (SIOCDEVPRIVATE + 9)
#define SIOCSETCPHYWR (SIOCDEVPRIVATE + 10)

#define SWITCH_PHY 0x1e
#define MDIO_PAGE 0x10
#define MDIO_ADDR 0x11
#define MDIO_DATA 0x18
#define MDIO_PAGE_ENABLE 1
#define MDIO_OP_READ 2
#define MDIO_OP_WRITE 1

#define MGMT_PAGE 0x02
#define MIR_CAP 0x10
#define MIR_ING 0x12
#define MIR_EGR 0x1c

#define MIR_ENABLED 0x8000u
#define CPU_PORT 8
#define WAN_PORT 0
#define ICOTERA_PORT 4

enum access_mode { MODE_NONE, MODE_ET_PRIVATE, MODE_STANDARD_MII };

static int socket_fd = -1;
static enum access_mode mode = MODE_NONE;
static const char *if_name = "eth0";

static int mdio_ioctl(uint16_t reg, uint16_t *value, int is_write)
{
    struct ifreq request;
    int error;
    memset(&request, 0, sizeof(request));
    strncpy(request.ifr_name, if_name, IFNAMSIZ - 1);

    if (mode == MODE_ET_PRIVATE) {
        int args[2] = { reg, *value };
        request.ifr_data = (void *)args;
        error = ioctl(socket_fd,
                      is_write ? SIOCSETCPHYWR : SIOCGETCPHYRD, &request);
        if (error < 0) return -errno;
        if (!is_write) *value = (uint16_t)args[1];
        return 0;
    }

    if (mode == MODE_STANDARD_MII) {
        struct mii_ioctl_data *mii = (struct mii_ioctl_data *)&request.ifr_data;
        mii->phy_id = SWITCH_PHY;
        mii->reg_num = reg;
        mii->val_in = *value;
        error = ioctl(socket_fd,
                      is_write ? SIOCSMIIREG : SIOCGMIIREG, &request);
        if (error < 0) return -errno;
        if (!is_write) *value = mii->val_out;
        return 0;
    }

    return -EINVAL;
}

static int mdio_read(uint16_t reg, uint16_t *value)
{
    *value = 0;
    return mdio_ioctl(reg, value, 0);
}

static int mdio_write(uint16_t reg, uint16_t value)
{
    return mdio_ioctl(reg, &value, 1);
}

static int choose_access(void)
{
    uint16_t value = 0;
    int status;
    mode = MODE_ET_PRIVATE;
    status = mdio_read(MDIO_ADDR, &value);
    if (!status) {
        puts("MDIO interface: Broadcom private ethernet ioctl");
        return 0;
    }
    fprintf(stderr, "Broadcom private ioctl failed: %s (%d)\n",
            strerror(-status), -status);

    mode = MODE_STANDARD_MII;
    status = mdio_read(MDIO_ADDR, &value);
    if (!status) {
        puts("MDIO interface: standard Linux MII ioctl");
        return 0;
    }
    fprintf(stderr, "Standard Linux MII ioctl failed: %s (%d)\n",
            strerror(-status), -status);
    mode = MODE_NONE;
    return -1;
}

static int select_register(uint8_t page, uint8_t reg, uint16_t operation)
{
    uint16_t status;
    int error;
    unsigned attempt;

    error = mdio_write(MDIO_PAGE, (uint16_t)((page << 8) | MDIO_PAGE_ENABLE));
    if (error) return error;

    error = mdio_write(MDIO_ADDR, (uint16_t)((reg << 8) | operation));
    if (error) return error;

    for (attempt = 0; attempt < 100; attempt++) {
        error = mdio_read(MDIO_ADDR, &status);
        if (error) return error;
        if (!(status & 3)) return 0;
    }
    return -ETIMEDOUT;
}

static int switch_read16(uint8_t page, uint8_t reg, uint16_t *value)
{
    int error = select_register(page, reg, MDIO_OP_READ);
    if (error) return error;
    return mdio_read(MDIO_DATA, value);
}

static int switch_write16(uint8_t page, uint8_t reg, uint16_t value)
{
    int error = mdio_write(MDIO_DATA, value);
    if (error) return error;
    return select_register(page, reg, MDIO_OP_WRITE);
}

struct mirror_config {
    uint16_t capture, ingress, egress;
};

static int read_mirror(struct mirror_config *config)
{
    int error;
    error = switch_read16(MGMT_PAGE, MIR_CAP, &config->capture);
    if (error) goto failed;
    error = switch_read16(MGMT_PAGE, MIR_ING, &config->ingress);
    if (error) goto failed;
    error = switch_read16(MGMT_PAGE, MIR_EGR, &config->egress);
    if (error) goto failed;
    return 0;
failed:
    fprintf(stderr, "Mirror register read failed: %s (%d)\n",
            strerror(-error), -error);
    return error;
}

static void show_config(const struct mirror_config *config)
{
    printf("capture: 0x%04x\ningress: 0x%04x\negress:  0x%04x\n",
           config->capture, config->ingress, config->egress);
}

static int restore_config(const struct mirror_config *original)
{
    int error;
    error = switch_write16(MGMT_PAGE, MIR_CAP,
                           (uint16_t)(original->capture & ~MIR_ENABLED));
    if (error) goto failed;
    error = switch_write16(MGMT_PAGE, MIR_ING, original->ingress);
    if (error) goto failed;
    error = switch_write16(MGMT_PAGE, MIR_EGR, original->egress);
    if (error) goto failed;
    error = switch_write16(MGMT_PAGE, MIR_CAP, original->capture);
    if (error) goto failed;
    return 0;
failed:
    fprintf(stderr, "Restoring mirroring failed: %s (%d)\n",
            strerror(-error), -error);
    return error;
}

static int enable_mirror(const struct mirror_config *original)
{
    struct mirror_config expected, checked;
    int error;
    if (original->capture & MIR_ENABLED) {
        fputs("Refusing to overwrite existing active mirroring.\n", stderr);
        return -1;
    }
    if (original->capture == 0xffff || original->ingress == 0xffff
        || original->egress == 0xffff) {
        fputs("Refusing implausible register values.\n", stderr);
        return -1;
    }
    expected.capture = (uint16_t)(MIR_ENABLED | CPU_PORT);
    expected.ingress = (uint16_t)((1u << WAN_PORT) | (1u << ICOTERA_PORT));
    expected.egress = 0;

    printf("RESTORE COMMAND: /tmp/switchmirror restore 0x%04x 0x%04x 0x%04x\n",
           original->capture, original->ingress, original->egress);
    fflush(stdout);

    error = switch_write16(MGMT_PAGE, MIR_ING, expected.ingress);
    if (error) goto failed;
    error = switch_write16(MGMT_PAGE, MIR_EGR, expected.egress);
    if (error) goto failed;
    error = switch_write16(MGMT_PAGE, MIR_CAP, expected.capture);
    if (error) goto failed;

    error = read_mirror(&checked);
    if (!error && memcmp(&checked, &expected, sizeof(checked)) == 0) {
        puts("Mirroring registers written and read back successfully.");
        return 0;
    }

    fputs("Mirror verification failed; attempting to restore.\n", stderr);
    restore_config(original);
    return -1;
failed:
    fprintf(stderr, "Enabling mirror failed: %s (%d); restoring.\n",
            strerror(-error), -error);
    restore_config(original);
    return -1;
}

static int parse_hex(const char *s, uint16_t *number)
{
    unsigned long result;
    char *end;
    errno = 0;
    result = strtoul(s, &end, 16);
    if (errno || end == s || *end || result > 0xffff) return -1;
    *number = (uint16_t)result;
    return 0;
}

int main(int argc, char **argv)
{
    struct mirror_config current, original;
    int error;
    int result = 0;
    int is_probe, is_enable, is_restore;

    if (argc < 2) goto usage;
    is_probe = !strcmp(argv[1], "probe");
    is_enable = !strcmp(argv[1], "enable");
    is_restore = !strcmp(argv[1], "restore");
    if (!is_probe && !is_enable && !is_restore) goto usage;
    if (is_restore && argc != 5) goto usage;
    if (!is_restore && argc != 2) goto usage;

    if (is_restore) {
        if (parse_hex(argv[2], &original.capture)
            || parse_hex(argv[3], &original.ingress)
            || parse_hex(argv[4], &original.egress)) goto usage;
    }

    printf("switchmirror: normal libc startup; command=%s\n", argv[1]);
    fflush(stdout);
    socket_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (socket_fd < 0) {
        perror("socket");
        return 3;
    }
    if (choose_access()) {
        fputs("No working switch-MDIO ioctl, leaving switch unchanged.\n",
              stderr);
        result = 4;
        goto done;
    }
    error = read_mirror(&current);
    if (error) {
        result = 5;
        goto done;
    }
    show_config(&current);
    if (current.capture == 0xffff || current.ingress == 0xffff
        || current.egress == 0xffff) {
        fputs("Implausible all-ones register reading; aborting.\n", stderr);
        result = 6;
        goto done;
    }
    if (is_probe) {
        puts("Probe completed. Mirroring settings unchanged.");
    } else if (is_enable) {
        result = enable_mirror(&current) ? 7 : 0;
    } else {
        result = restore_config(&original) ? 8 : 0;
        if (!result) puts("Original mirror registers restored.");
    }

done:
    close(socket_fd);
    return result;

usage:
    fputs("Usage: switchmirror probe | enable | restore HEX_CAPTURE HEX_INGRESS HEX_EGRESS\n",
          stderr);
    return 2;
}
