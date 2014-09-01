#include <fcntl.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/mman.h>

static int fd = 0;
static volatile int *mem_32 = 0;
static volatile short *mem_16 = 0;
static volatile char *mem_8 = 0;
static int *prev_mem_range = 0;

static const char *ltssm_states[] = {
	"S_DETECT_QUIET", // 0x00
	"S_DETECT_ACT", // 0x01
	"S_POLL_ACTIVE", // 0x02
	"S_POLL_COMPLIANCE", // 0x03
	"S_POLL_CONFIG", // 0x04
	"S_PRE_DETECT_QUIET", // 0x05
	"S_DETECT_WAIT", // 0x06
	"S_CFG_LINKWD_START", // 0x07
	"S_CFG_LINKWD_ACEPT", // 0x08
	"S_CFG_LANENUM_WAIT", // 0x09
	"S_CFG_LANENUM_ACEPT", // 0x0A
	"S_CFG_COMPLETE", // 0x0B
	"S_CFG_IDLE", // 0x0C
	"S_RCVRY_LOCK", // 0x0D
	"S_RCVRY_SPEED", // 0x0E
	"S_RCVRY_RCVRCFG", // 0x0F
	"S_RCVRY_IDLE", // 0x10
	"S_L0", // 0x11
	"S_L0S", // 0x12
	"S_L123_SEND_EIDLE", // 0x13
	"S_L1_IDLE", // 0x14
	"S_L2_IDLE", // 0x15
	"S_L2_WAKE", // 0x16
	"S_DISABLED_ENTRY", // 0x17
	"S_DISABLED_IDLE", // 0x18
	"S_DISABLED", // 0x19
	"S_LPBK_ENTRY", // 0x1A
	"S_LPBK_ACTIVE", // 0x1B
	"S_LPBK_EXIT", // 0x1C
	"S_LPBK_EXIT_TIMEOUT", // 0x1D
	"S_HOT_RESET_ENTRY", // 0x1E
	"S_HOT_RESET", // 0x1F
	"S_RCVRY_EQ0", // 0x20
	"S_RCVRY_EQ1", // 0x21
	"S_RCVRY_EQ2", // 0x22
	"S_RCVRY_EQ3", // 0x23
};

static int map_offset(long offset, int virtualized) {
    int *mem_range = (int *)(offset & ~0xFFFF);
    if( mem_range != prev_mem_range ) {
        prev_mem_range = mem_range;

        if(mem_32)
            munmap((void *)mem_32, 0xFFFF);
        if(fd)
            close(fd);

        if(virtualized) {
            fd = open("/dev/kmem", O_RDWR);
            if( fd < 0 ) {
                perror("Unable to open /dev/kmem");
                fd = 0;
                return -1;
            }
        }
        else {
            fd = open("/dev/mem", O_RDWR);
            if( fd < 0 ) {
                perror("Unable to open /dev/mem");
                fd = 0;
                return -1;
            }
        }

        mem_32 = mmap(0, 0xffff, PROT_READ | PROT_WRITE, MAP_SHARED, fd, offset&~0xFFFF);
        if( -1 == (long)mem_32 ) {
            perror("Unable to mmap file");

            if( -1 == close(fd) )
                perror("Also couldn't close file");

            fd=0;
            return -1;
        }
        mem_16 = (short *)mem_32;
        mem_8 = (char *)mem_32;
    }
    return 0;
}
 
static volatile int read_kernel_memory(long offset, int virtualized, int size) {
    int result;
    map_offset(offset, virtualized);
    int scaled_offset = (offset-(offset&~0xFFFF));
    if(size==1)
        result = mem_8[scaled_offset/sizeof(char)];
    else if(size==2)
        result = mem_16[scaled_offset/sizeof(short)];
    else
        result = mem_32[scaled_offset/sizeof(long)];

    return result;
}

static char *yesno(int bit) {
    if (bit)
        return "yes";
    return "no";
}

int main(int argc, char **argv) {
    uint32_t debug0, debug1;
    if (argc != 3 && argc != 1) {
        printf("Usage: %s [[DEBUG0] [DEBUG1]]\n", argv[0]);
	printf("If no parameters are specified, current debug registers are read\n");
        return 1;
    }

    if (argc == 3) {
	    debug0 = strtoul(argv[1], NULL, 0);
	    debug1 = strtoul(argv[2], NULL, 0);
    }
    else {
        debug0 = read_kernel_memory(0x01ffc728, 0, 4);
        debug1 = read_kernel_memory(0x01ffc72c, 0, 4);
    }
	    

    printf("LTSSM current state: 0x%x (%s)\n", (debug0>>0) & 0x3f,
		    ltssm_states[(debug0>>0) & 0x3f]);
    printf("PIPE transmit K indication: %d\n", (debug0>>6) & 3);
    printf("PIPE Transmit data: 0x%x\n", (debug0>>8) & 0xffff);
    printf("Receiver is receiving logical idle: %s\n", yesno((debug0>>25)&1));
    printf("Second symbol is also idle (16-bit PHY interface only): %s\n", yesno((debug0>>24)&1));
    printf("Currently receiving k237 (PAD) in place of link number: %s\n", yesno((debug0>>26)&1));
    printf("Currently receiving k237 (PAD) in place of lane number: %s\n", yesno((debug0>>27)&1));
    printf("Link control bits advertised by link partner: 0x%x\n", (debug0>>28)&0xf);
    printf("Receiver detected lane reversal: %s\n", yesno((debug1>>(32-32))&1));
    printf("TS2 training sequence received: %s\n", yesno((debug1>>(33-32))&1));
    printf("TS1 training sequence received: %s\n", yesno((debug1>>(34-32))&1));
    printf("Receiver reports skip reception: %s\n", yesno((debug1>>(35-32))&1));
    printf("LTSSM reports PHY link up: %s\n", yesno((debug1>>(36-32))&1));
    printf("A skip ordered set has been transmitted: %s\n", yesno((debug1>>(37-32))&1));
    printf("Link number advertised/confirmed by link partner: %d\n", (debug1>>(40-32))&0xff);
    printf("Application request to initiate training reset: %s\n", yesno((debug1>>(51-32))&1));
    printf("PIPE transmit compliance request: %s\n", yesno((debug1>>(52-32))&1));
    printf("PIPE transmit electrical idle request: %s\n", yesno((debug1>>(53-32))&1));
    printf("PIPE receiver detect/loopback request: %s\n", yesno((debug1>>(54-32))&1));
    printf("LTSSM-negotiated link reset: %s\n", yesno((debug1>>(59-32))&1));
    printf("LTSSM testing for polarity reversal: %s\n", yesno((debug1>>(60-32))&1));
    printf("LTSSM performing link training: %s\n", yesno((debug1>>(61-32))&1));
    printf("LTSSM in DISABLE state; link inoperable: %s\n", yesno((debug1>>(62-32))&1));
    printf("Scrambling disabled for the link: %s\n", yesno((debug1>>(63-32))&1));

    return 0;
}
