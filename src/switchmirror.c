/*
 * Inspect BCM53125 switch mirroring registers on MIPS Linux o32.
 *
 * Probe the driver before modifying switch configuration.
 * Preserve the original values and print a restore command.
 *
 * CAUTION: Test this tool on the target router before trusting register writes.
 */

typedef unsigned int u32;
typedef unsigned short u16;
typedef unsigned char u8;

#define SYS_WRITE 4004
#define SYS_CLOSE 4006
#define SYS_IOCTL 4054
#define SYS_SOCKET 4183

#define AF_INET 2
#define SOCK_DGRAM 2
#define SIOCDEVPRIVATE 0x89f0
#define SIOCGETCPHYRD (SIOCDEVPRIVATE + 9)
#define SIOCSETCPHYWR (SIOCDEVPRIVATE + 10)
#define SIOCGMIIREG 0x8948
#define SIOCSMIIREG 0x8949

#define PHY_ADDR 0x1e
#define REG_PAGE 0x10
#define REG_ADDR 0x11
#define REG_DATA 0x18
#define PAGE_ENABLE 1
#define ADDR_READ 2
#define ADDR_WRITE 1

#define MGMT_PAGE 0x02
#define MIRROR_CAP 0x10
#define MIRROR_ING 0x12
#define MIRROR_EGR 0x1c
#define MIRROR_ENABLE_BIT 0x8000u

struct ifreq_min {
    char name[16];
    union {
        void *data;
        u8 raw[16];
    } payload;
};

struct mii_data {
    u16 phy_id;
    u16 reg_num;
    u16 val_in;
    u16 val_out;
};

static long raw_syscall3(long number, long a, long b, long c)
{
    register long v0 __asm__("$2") = number;
    register long a0 __asm__("$4") = a;
    register long a1 __asm__("$5") = b;
    register long a2 __asm__("$6") = c;
    register long a3 __asm__("$7") = 0;
    __asm__ volatile ("syscall" : "+r"(v0), "+r"(a3)
                      : "r"(a0), "r"(a1), "r"(a2)
                      : "$1", "$3", "memory");
    return a3 ? -v0 : v0;
}

static unsigned length(const char *s)
{
    unsigned n = 0;
    while (s[n]) n++;
    return n;
}

static void print(const char *s)
{
    raw_syscall3(SYS_WRITE, 1, (long)s, length(s));
}

static void hex16(u16 value)
{
    char out[7];
    const char digits[] = "0123456789abcdef";
    int i;
    out[0] = '0';
    out[1] = 'x';
    for (i = 0; i < 4; i++) {
        out[2 + i] = digits[(value >> (12 - 4 * i)) & 15];
    }
    out[6] = 0;
    print(out);
}

static void print_error(const char *action, long error)
{
    print(action);
    print(" failed, errno=");
    hex16((u16)(-error));
    print("\n");
}

static int equal(const char *left, const char *right)
{
    while (*left && *right && *left == *right) {
        left++;
        right++;
    }
    return *left == *right;
}

static int parse_hex16(const char *input, u16 *output)
{
    u32 value = 0;
    int digits_seen = 0;
    if (input[0] == '0' && (input[1] == 'x' || input[1] == 'X')) input += 2;
    while (*input) {
        u32 digit;
        if (*input >= '0' && *input <= '9') digit = (u32)(*input - '0');
        else if (*input >= 'a' && *input <= 'f') digit = (u32)(*input - 'a' + 10);
        else if (*input >= 'A' && *input <= 'F') digit = (u32)(*input - 'A' + 10);
        else return 0;
        if (++digits_seen > 4) return 0;
        value = (value << 4) | digit;
        input++;
    }
    if (!digits_seen) return 0;
    *output = (u16)value;
    return 1;
}

static int ioctl_mode = 0;
static long sockfd = -1;

static int mdio(u16 register_number, u16 *value, int write)
{
    struct ifreq_min ifr = {0};
    long result;
    ifr.name[0] = 'e';
    ifr.name[1] = 't';
    ifr.name[2] = 'h';
    ifr.name[3] = '0';
    if (ioctl_mode == 1) {
        int args[2];
        args[0] = register_number;
        args[1] = *value;
        ifr.payload.data = args;
        result = raw_syscall3(SYS_IOCTL, sockfd,
                              write ? SIOCSETCPHYWR : SIOCGETCPHYRD,
                              (long)&ifr);
        if (result < 0) return (int)result;
        *value = (u16)args[1];
        return 0;
    }
    {
        struct mii_data *mii = (struct mii_data *)&ifr.payload.raw[0];
        mii->phy_id = PHY_ADDR;
        mii->reg_num = register_number;
        mii->val_in = *value;
        mii->val_out = 0;
        result = raw_syscall3(SYS_IOCTL, sockfd,
                              write ? SIOCSMIIREG : SIOCGMIIREG,
                              (long)&ifr);
        if (result < 0) return (int)result;
        *value = mii->val_out;
        return 0;
    }
}

static int mdio_write(u16 reg, u16 value)
{
    return mdio(reg, &value, 1);
}

static int mdio_read(u16 reg, u16 *value)
{
    *value = 0;
    return mdio(reg, value, 0);
}

static int choose_mdio_mode(void)
{
    u16 value;
    int result;
    ioctl_mode = 1;
    result = mdio_read(REG_ADDR, &value);
    if (result == 0) {
        print("MDIO ioctl: Broadcom private et driver\n");
        return 0;
    }
    print_error("Broadcom private ioctl", result);
    ioctl_mode = 2;
    result = mdio_read(REG_ADDR, &value);
    if (result == 0) {
        print("MDIO ioctl: Linux MII driver\n");
        return 0;
    }
    print_error("Linux MII ioctl", result);
    return -1;
}

static int select_register(u8 page, u8 reg, u16 operation)
{
    u16 value;
    int attempt;
    int error = mdio_write(REG_PAGE, (u16)((page << 8) | PAGE_ENABLE));
    if (error) return error;
    error = mdio_write(REG_ADDR, (u16)((reg << 8) | operation));
    if (error) return error;
    for (attempt = 0; attempt < 1000; attempt++) {
        error = mdio_read(REG_ADDR, &value);
        if (error) return error;
        if ((value & 3) == 0) return 0;
    }
    return -110;
}

static int reg_read16(u8 page, u8 reg, u16 *value)
{
    int error = select_register(page, reg, ADDR_READ);
    if (error) return error;
    return mdio_read(REG_DATA, value);
}

static int reg_write16(u8 page, u8 reg, u16 value)
{
    int error = mdio_write(REG_DATA, value);
    if (error) return error;
    return select_register(page, reg, ADDR_WRITE);
}

static int read_mirror(u16 *capture, u16 *ingress, u16 *egress)
{
    int error;
    error = reg_read16(MGMT_PAGE, MIRROR_CAP, capture);
    if (error) { print_error("Read mirror capture", error); return -1; }
    error = reg_read16(MGMT_PAGE, MIRROR_ING, ingress);
    if (error) { print_error("Read mirror ingress", error); return -1; }
    error = reg_read16(MGMT_PAGE, MIRROR_EGR, egress);
    if (error) { print_error("Read mirror egress", error); return -1; }
    return 0;
}

static void print_values(u16 capture, u16 ingress, u16 egress)
{
    print("capture: "); hex16(capture); print("\n");
    print("ingress: "); hex16(ingress); print("\n");
    print("egress:  "); hex16(egress); print("\n");
}

static int restore_values(u16 capture, u16 ingress, u16 egress)
{
    int error;
    /* Disable mirroring before restoring filter masks. */
    error = reg_write16(MGMT_PAGE, MIRROR_CAP, (u16)(capture & ~MIRROR_ENABLE_BIT));
    if (error) goto failed;
    error = reg_write16(MGMT_PAGE, MIRROR_ING, ingress);
    if (error) goto failed;
    error = reg_write16(MGMT_PAGE, MIRROR_EGR, egress);
    if (error) goto failed;
    error = reg_write16(MGMT_PAGE, MIRROR_CAP, capture);
    if (error) goto failed;
    return 0;
failed:
    print_error("Restore register", error);
    return -1;
}

static int enable_mirror(u16 old_capture, u16 old_ingress, u16 old_egress)
{
    u16 capture = 0x8008u;
    u16 ingress = 0x0011u;
    u16 check_capture, check_ingress, check_egress;
    int error;
    if (old_capture & MIRROR_ENABLE_BIT) {
        print("Mirror already enabled. Do not override existing configuration.\n");
        return -1;
    }
    /* Mirror ingress frames from physical ports 0 and 4 to CPU port 8. */
    print("Restore with: /tmp/switchmirror restore ");
    hex16(old_capture); print(" ");
    hex16(old_ingress); print(" ");
    hex16(old_egress); print("\n");
    error = reg_write16(MGMT_PAGE, MIRROR_ING, ingress);
    if (error) goto failed;
    error = reg_write16(MGMT_PAGE, MIRROR_EGR, 0);
    if (error) goto failed;
    error = reg_write16(MGMT_PAGE, MIRROR_CAP, capture);
    if (error) goto failed;
    if (read_mirror(&check_capture, &check_ingress, &check_egress) != 0 ||
        check_capture != capture || check_ingress != ingress || check_egress != 0) {
        print("Verification failed. Restore original values.\n");
        restore_values(old_capture, old_ingress, old_egress);
        return -1;
    }
    print("Mirror enabled and verified.\n");
    return 0;
failed:
    print_error("Enable mirror", error);
    restore_values(old_capture, old_ingress, old_egress);
    return -1;
}

int main(int argc, char **argv)
{
    u16 capture = 0, ingress = 0, egress = 0;
    int result = 0;
    if (argc < 2 ||
        (!equal(argv[1], "probe") && !equal(argv[1], "enable") &&
         !equal(argv[1], "restore"))) {
        print("Usage: switchmirror probe | enable | restore <capture> <ingress> <egress>\n");
        return 2;
    }
    sockfd = raw_syscall3(SYS_SOCKET, AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        print_error("Open socket", sockfd);
        return 3;
    }
    if (choose_mdio_mode() != 0) {
        print("No supported MDIO access. Leave switch unchanged.\n");
        raw_syscall3(SYS_CLOSE, sockfd, 0, 0);
        return 4;
    }
    if (read_mirror(&capture, &ingress, &egress) != 0) {
        raw_syscall3(SYS_CLOSE, sockfd, 0, 0);
        return 5;
    }
    print_values(capture, ingress, egress);
    if (capture == 0xffffu || ingress == 0xffffu || egress == 0xffffu) {
        print("Unexpected register values. Abort without changing mirroring.\n");
        raw_syscall3(SYS_CLOSE, sockfd, 0, 0);
        return 7;
    }
    if (equal(argv[1], "enable")) {
        result = enable_mirror(capture, ingress, egress);
    } else if (equal(argv[1], "restore")) {
        u16 old_capture, old_ingress, old_egress;
        if (argc != 5 ||
            !parse_hex16(argv[2], &old_capture) ||
            !parse_hex16(argv[3], &old_ingress) ||
            !parse_hex16(argv[4], &old_egress)) {
            print("Specify three original hexadecimal values.\n");
            result = -1;
        } else {
            result = restore_values(old_capture, old_ingress, old_egress);
            if (!result) print("Original mirror values restored.\n");
        }
    } else {
        print("Read-only probe complete.\n");
    }
    raw_syscall3(SYS_CLOSE, sockfd, 0, 0);
    return result ? 6 : 0;
}

/* Enter the application without a libc startup dependency. */
__asm__(
    ".section .text\n"
    ".globl _start\n"
    "_start:\n"
    "lui $28, %hi(_gp)\n"
    "addiu $28, $28, %lo(_gp)\n"
    "lw $4, 0($29)\n"
    "addiu $5, $29, 4\n"
    "jal main\n"
    "nop\n"
    "move $4, $2\n"
    "li $2, 4001\n"
    "syscall\n"
    "1: b 1b\n"
    "nop\n"
);
