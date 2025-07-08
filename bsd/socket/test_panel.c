/*
 * test_panel.c - Test program to read panel structure from /dev/kmem
 * Compile: cc -o test_panel test_panel.c
 */

#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>

/* Match the kernel structure */
struct panel_state {
    long ps_address;    /* 22-bit address */
    int  ps_data;       /* 16-bit data */
};

int main(int argc, char *argv[])
{
    int kmem_fd;
    struct panel_state panel;
    long panel_addr;
    
    if (argc != 2) {
        printf("Usage: %s <panel_address_in_hex>\n", argv[0]);
        printf("Get panel address from: nm /unix | grep panel\n");
        return 1;
    }
    
    /* Parse the panel address from command line */
    if (sscanf(argv[1], "%lx", &panel_addr) != 1) {
        printf("Invalid hex address: %s\n", argv[1]);
        return 1;
    }
    
    /* Open /dev/kmem */
    kmem_fd = open("/dev/kmem", O_RDONLY);
    if (kmem_fd < 0) {
        perror("Cannot open /dev/kmem");
        printf("Note: You need to run as root\n");
        return 1;
    }
    
    printf("Reading panel structure from kernel address 0x%lx\n", panel_addr);
    
    /* Seek to the panel structure address */
    if (lseek(kmem_fd, panel_addr, SEEK_SET) == -1) {
        perror("lseek failed");
        close(kmem_fd);
        return 1;
    }
    
    /* Read the panel structure */
    if (read(kmem_fd, &panel, sizeof(panel)) != sizeof(panel)) {
        perror("read failed");
        close(kmem_fd);
        return 1;
    }
    
    printf("Panel structure contents:\n");
    printf("  ps_address: 0x%06lx (22-bit)\n", panel.ps_address & 0x3FFFFF);
    printf("  ps_data:    0x%04x (16-bit)\n", panel.ps_data & 0xFFFF);
    
    close(kmem_fd);
    return 0;
}
