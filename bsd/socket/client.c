/*
 * client.c - Socket client for PDP-11 load testing
 * Sends 16-word frames to server 30 times per second
 * Compatible with K&R C and 211BSD
 */

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/file.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>

/* Include unistd.h on modern systems for getopt and system calls */
#if defined(__APPLE__) || defined(__NetBSD__)
#include <unistd.h>
#endif

/* Include stdint.h for uint64_t and uintptr_t */
#if defined(__NetBSD__) && (defined(__x86_64__) || defined(__amd64__))
#include <stdint.h>
#include <kvm.h>
#include <nlist.h>
#include <limits.h>
#include <sys/cpu.h>
#elif defined(__APPLE__) || defined(__linux__)
#include <stdint.h>
#endif

/* Define _POSIX2_LINE_MAX if not available */
#ifndef _POSIX2_LINE_MAX
#define _POSIX2_LINE_MAX 2048
#endif

/* Fallback for systems without uintptr_t */
#ifndef uintptr_t
#define uintptr_t unsigned long
#endif

/* Define SEEK constants if not available */
#ifndef SEEK_SET
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2
#endif

/* Declare functions for 211BSD compatibility if not already declared */
#if !defined(__APPLE__) && !defined(__NetBSD__)  /* Only declare these on older systems */
extern int getopt();
extern char *optarg;
extern int optind;
extern int close();
extern long lseek();
extern int read();
extern int open();
#endif

/* Portable type definitions for PDP-11 compatibility */
typedef unsigned short u16_t;   /* Always 16 bits */
typedef unsigned long  u32_t;   /* 32 bits on modern, but we'll mask appropriately */

#define SERVER_PORT 4000
#define FRAMES_PER_SECOND 30
#define USEC_PER_FRAME (1000000 / FRAMES_PER_SECOND)

/* Panel structure - must match kernel definition */
/* PDP-11 uses natural alignment, so we'll send/receive as individual fields */

#ifdef pdp11
struct panel_state {
    long ps_address;	/* panel switches - 32-bit address */
    short ps_data;		/* panel lamps - 16-bit data */
    short ps_psw;
    short ps_mser;
    short ps_cpu_err;
    short ps_mmr0;
    short ps_mmr3;
};
#elif defined(__NetBSD__) && (defined(__x86_64__) || defined(__amd64__))
struct panel_state {
    struct clockframe ps_frame;        /* panel switches - 64-bit address on NetBSD */
};
#else
/* Default fallback for other systems (like macOS for development) */
struct panel_state {
    long ps_address;	/* panel switches - 32-bit address */
    short ps_data;		/* panel lamps - 16-bit data */
    short ps_psw;
    short ps_mser;
    short ps_cpu_err;
    short ps_mmr0;
    short ps_mmr3;
};
#endif

struct panel_state panel;

/* Global variables for kvm access */
#if defined(__NetBSD__) && (defined(__x86_64__) || defined(__amd64__))
static kvm_t *kd = NULL;
static struct nlist nl[] = {
    { "_panel" },
    { NULL }
};
#endif

/* Function prototypes */
int create_udp_socket(char *server_ip, struct sockaddr_in *server_addr);
void send_frames(int sockfd, struct sockaddr_in *server_addr);
void usage(char *progname);
void precise_delay(long usec);
#if defined(__NetBSD__) && (defined(__x86_64__) || defined(__amd64__))
int open_kvm_and_find_panel(void);
int read_panel_from_kvm(struct panel_state *panel);
#else
int open_kmem_and_find_panel(void **panel_addr);
int read_panel_from_kmem(int kmem_fd, void *panel_addr, struct panel_state *panel);
#endif

int main(int argc, char *argv[])
{
    char *server_ip = "127.0.0.1";
    int sockfd;
    int c;
    struct sockaddr_in server_addr;
#if defined(__NetBSD__) && (defined(__x86_64__) || defined(__amd64__))
    /* NetBSD uses kvm library */
#else
    void *panel_addr;
    int kmem_fd;
#endif
    
    /* Parse command line arguments */
    while ((c = getopt(argc, argv, "s:h")) != -1) {
        switch (c) {
        case 's':
            server_ip = optarg;
            break;
        case 'h':
        case '?':
            usage(argv[0]);
            exit(1);
        }
    }
    
    printf("Connecting to server at %s:%d via UDP\n", server_ip, SERVER_PORT);
    
#if defined(__NetBSD__) && (defined(__x86_64__) || defined(__amd64__))
    /* Open kvm and find panel symbol */
    if (open_kvm_and_find_panel() < 0) {
        fprintf(stderr, "Failed to open kvm or find panel symbol\n");
        exit(1);
    }
#else
    /* Open /dev/kmem and find panel symbol */
    kmem_fd = open_kmem_and_find_panel(&panel_addr);
    if (kmem_fd < 0) {
        fprintf(stderr, "Failed to open /dev/kmem or find panel symbol\n");
        exit(1);
    }
#endif
    
#if defined(__NetBSD__) && (defined(__x86_64__) || defined(__amd64__))
    printf("Panel symbol found at kernel address %p\n", (void*)nl[0].n_value);
    printf("This is the ADDRESS OF the panel structure in kernel memory\n");
#else
    printf("Panel symbol found at address %p\n", panel_addr);
#endif
    
    /* Create UDP socket and set up server address */
    sockfd = create_udp_socket(server_ip, &server_addr);
    if (sockfd < 0) {
        fprintf(stderr, "Failed to create UDP socket\n");
#if defined(__NetBSD__) && (defined(__x86_64__) || defined(__amd64__))
        if (kd) kvm_close(kd);
#else
        close(kmem_fd);
#endif
        exit(1);
    }
    
    printf("Connected successfully. Sending panel data at %d Hz...\n", FRAMES_PER_SECOND);
    printf("Packet size: %d bytes (ps_address: %d, ps_data: %d)\n", 
           (int)sizeof(struct panel_state), (int)sizeof(u32_t), (int)sizeof(u16_t));
    
    /* Send frames continuously */
    send_frames(sockfd, &server_addr);
    
    close(sockfd);
#if defined(__NetBSD__) && (defined(__x86_64__) || defined(__amd64__))
    if (kd) kvm_close(kd);
#else
    close(kmem_fd);
#endif
    return 0;
}

int create_udp_socket(char *server_ip, struct sockaddr_in *server_addr)
{
    int sockfd;
    struct hostent *host;
    
    /* Create UDP socket */
    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        perror("socket");
        return -1;
    }
    
    /* Set up server address */
    memset(server_addr, 0, sizeof(*server_addr));
    server_addr->sin_family = AF_INET;
    server_addr->sin_port = htons(SERVER_PORT);
    
    /* Convert IP address */
    server_addr->sin_addr.s_addr = inet_addr(server_ip);
    if (server_addr->sin_addr.s_addr == INADDR_NONE) {
        /* Try to resolve hostname */
        host = gethostbyname(server_ip);
        if (host == NULL) {
            fprintf(stderr, "Invalid server address: %s\n", server_ip);
            close(sockfd);
            return -1;
        }
        memcpy(&server_addr->sin_addr, host->h_addr, host->h_length);
    }
    
    return sockfd;
}

void send_frames(int sockfd, struct sockaddr_in *server_addr)
{
    struct panel_state panel;
    int frame_count = 0;
#if defined(__NetBSD__) && (defined(__x86_64__) || defined(__amd64__))
    /* NetBSD uses kvm library - no static variables needed */
#else
    static void *panel_addr = NULL;
    static int kmem_fd = -1;
    
    /* Open /dev/kmem and find panel address if not already done */
    if (kmem_fd < 0) {
        kmem_fd = open_kmem_and_find_panel(&panel_addr);
        if (kmem_fd < 0) {
            fprintf(stderr, "Cannot access kernel memory\n");
            return;
        }
    }
#endif
    
    while (1) {
        /* Read panel structure from kernel memory */
#if defined(__NetBSD__) && (defined(__x86_64__) || defined(__amd64__))
        if (read_panel_from_kvm(&panel) < 0) {
            fprintf(stderr, "Failed to read panel data from kernel\n");
            break;
        }
#else
        if (read_panel_from_kmem(kmem_fd, panel_addr, &panel) < 0) {
            fprintf(stderr, "Failed to read panel data from kernel\n");
            break;
        }
#endif
        
        /* Send panel data via UDP */
        {
            int bytes_sent = sendto(sockfd, &panel, sizeof(panel), 0,
                       (struct sockaddr *)server_addr, sizeof(*server_addr));
            if (bytes_sent < 0) {
                perror("sendto");
                break;
            }
        }
        
        frame_count++;
        if (frame_count % (FRAMES_PER_SECOND * 1) == 0) {  /* Every 1 second */
            printf("Sent %d panel updates (CONTENTS: ps_address=0x%lx, ps_data=0x%x)\n", 
                   frame_count, (unsigned long)panel.ps_address, (unsigned short)panel.ps_data);
        }
        
        /* Debug: Print first few sends */
        if (frame_count <= 5) {
            printf("DEBUG: Sent packet #%d, size=%d bytes\n", frame_count, (int)sizeof(panel));
            if (frame_count == 1) {
                printf("DEBUG: Panel contents - ps_address=0x%lx, ps_data=0x%lx\n", 
                       (unsigned long)panel.ps_address, (unsigned long)panel.ps_data);
            }
        }
        
        /* Wait for next frame time */
        precise_delay(USEC_PER_FRAME);
    }
}

void usage(char *progname)
{
    printf("Usage: %s [-s server_ip]\n", progname);
    printf("  -s server_ip   IP address of server (default: 127.0.0.1)\n");
    printf("  -h             Show this help\n");
}

int open_kmem_and_find_panel(void **panel_addr)
{
    FILE *fp;
    char line[256];
    char symbol[64];
    uintptr_t addr;
    char type;
    int kmem_fd;
    
    /* Try to find panel symbol in kernel symbol table */
#ifdef pdp11
    /* PDP-11 systems use /unix or /vmunix with octal addresses */
    fp = popen("nm /unix | grep panel", "r");
    if (fp == NULL) {
        fp = popen("nm /vmunix | grep panel", "r");
        if (fp == NULL) {
            fprintf(stderr, "Cannot run nm to find panel symbol\n");
            return -1;
        }
    }
#elif defined(__NetBSD__) && (defined(__x86_64__) || defined(__amd64__))
    /* NetBSD/AMD64 systems use /netbsd with hex addresses */
    fp = popen("nm /netbsd | grep panel", "r");
    if (fp == NULL) {
        fprintf(stderr, "Cannot run nm on /netbsd to find panel symbol\n");
        return -1;
    }
#else
    /* Default fallback - try common kernel locations */
    fp = popen("nm /unix | grep panel", "r");
    if (fp == NULL) {
        fp = popen("nm /vmunix | grep panel", "r");
        if (fp == NULL) {
            fp = popen("nm /netbsd | grep panel", "r");
            if (fp == NULL) {
                fprintf(stderr, "Cannot run nm to find panel symbol\n");
                return -1;
            }
        }
    }
#endif
    
    *panel_addr = NULL;
    while (fgets(line, sizeof(line), fp)) {
        /* Try both octal and hex formats - sscanf will handle the parsing */
        if (sscanf(line, "%lo %c %s", &addr, &type, symbol) == 3 ||
            sscanf(line, "%lx %c %s", &addr, &type, symbol) == 3) {
            if (strcmp(symbol, "panel") == 0 || strcmp(symbol, "_panel") == 0) {
                *panel_addr = (void*)addr;
                break;
            }
        }
    }
    pclose(fp);
    
    if (*panel_addr == NULL) {
        fprintf(stderr, "Panel symbol not found in kernel symbol table\n");
        return -1;
    }
    
    /* Open /dev/kmem */
    kmem_fd = open("/dev/kmem", O_RDONLY);
    if (kmem_fd < 0) {
        perror("open /dev/kmem");
        /* On NetBSD, try /dev/mem as fallback */
        kmem_fd = open("/dev/mem", O_RDONLY);
        if (kmem_fd < 0) {
            perror("open /dev/mem");
            return -1;
        }
        printf("Using /dev/mem instead of /dev/kmem\n");
    }
    
    return kmem_fd;
}

int read_panel_from_kmem(int kmem_fd, void *panel_addr, struct panel_state *panel)
{
    off_t offset = (off_t)(uintptr_t)panel_addr;
    
    /* On NetBSD AMD64, kernel virtual addresses may not be directly accessible */
    /* Check if this is a high kernel virtual address */
#if defined(__NetBSD__) && (defined(__x86_64__) || defined(__amd64__))
    if (offset < 0 || (uintptr_t)panel_addr > 0xffffffff00000000ULL) {
        /* Try to handle kernel virtual address mapping */
        fprintf(stderr, "Warning: High kernel virtual address %p may not be accessible via /dev/kmem\n", panel_addr);
        
        /* On NetBSD, try using /dev/mem instead or adjust the address */
        /* For now, we'll try the lseek anyway and see what happens */
    }
#endif    
    if (lseek(kmem_fd, offset, SEEK_SET) < 0) {
        perror("lseek");
        return -1;
    }
    
    if (read(kmem_fd, panel, sizeof(*panel)) != sizeof(*panel)) {
        perror("read");
        return -1;
    }
    
    return 0;
}

#if defined(__NetBSD__) && (defined(__x86_64__) || defined(__amd64__))
/* NetBSD kvm library functions */
int open_kvm_and_find_panel(void)
{
    char errbuf[_POSIX2_LINE_MAX];
    
    /* Open kernel virtual memory */
    kd = kvm_open(NULL, NULL, NULL, O_RDONLY, errbuf);
    if (kd == NULL) {
        fprintf(stderr, "kvm_open: %s\n", errbuf);
        return -1;
    }
    
    /* Look up the panel symbol */
    if (kvm_nlist(kd, nl) != 0) {
        fprintf(stderr, "kvm_nlist: %s\n", kvm_geterr(kd));
        kvm_close(kd);
        kd = NULL;
        return -1;
    }
    
    if (nl[0].n_value == 0) {
        fprintf(stderr, "Panel symbol not found in kernel symbol table\n");
        kvm_close(kd);
        kd = NULL;
        return -1;
    }
    
    return 0;
}

int read_panel_from_kvm(struct panel_state *panel)
{
    static int first_read = 1;
    
    if (kd == NULL) {
        fprintf(stderr, "kvm not initialized\n");
        return -1;
    }
    
    /* Read panel structure from kernel memory using kvm_read */
    if (kvm_read(kd, nl[0].n_value, panel, sizeof(*panel)) != sizeof(*panel)) {
        fprintf(stderr, "kvm_read: %s\n", kvm_geterr(kd));
        return -1;
    }
    
    /* Debug output for first few reads */
    if (first_read) {
        printf("DEBUG: Reading from kernel address 0x%lx\n", nl[0].n_value);
        printf("DEBUG: Read %d bytes from kernel\n", (int)sizeof(*panel));
        printf("DEBUG: Raw panel.ps_address = 0x%lx (CONTENTS of ps_address field)\n", 
               (unsigned long)panel->ps_address);
        printf("DEBUG: Raw panel.ps_data = 0x%lx (CONTENTS of ps_data field)\n", 
               (unsigned long)panel->ps_data);
        first_read = 0;
    }
    
    return 0;
}
#endif

void precise_delay(long usec)
{
    struct timeval timeout;
    
    /* Convert microseconds to seconds and microseconds */
    timeout.tv_sec = usec / 1000000;
    timeout.tv_usec = usec % 1000000;
    
    /* Use select() with no file descriptors as a portable delay */
    select(0, NULL, NULL, NULL, &timeout);
}
