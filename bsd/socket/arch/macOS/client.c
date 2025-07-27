/*
 * client.c - macOS panel client
 * Uses Mach APIs to capture CPU state and system information
 * Sends panel data frames to server 30 times per second
 */

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/sysctl.h>
#include <sys/resource.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <time.h>
#include <mach/mach.h>
#include <mach/thread_act.h>
#include <mach/mach_time.h>
#include <libproc.h>

/* Include common functions */
#define SERVER_PORT 4000
#include "../../common.c"

/* Include panel state definitions */
#include "panel_state.h"

/* Architecture-specific includes */
#if defined(__arm64__)
#include <mach/arm/thread_status.h>
#else
#error "This client only supports ARM64 macOS systems"
#endif

/* Function prototypes */
int capture_cpu_state(struct macos_panel_state *panel);
int get_system_stats(struct macos_panel_state *panel);
void send_frames(int sockfd, struct sockaddr_in *server_addr);

int main(int argc, char *argv[])
{
    char *server_ip = "127.0.0.1";
    int sockfd;
    int c;
    struct sockaddr_in server_addr;
    
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
    
    printf("macOS Panel Client (ARM64)\n");
    
    /* Create UDP socket and connect to server */
    sockfd = create_udp_socket(server_ip, &server_addr);
    if (sockfd < 0) {
        exit(1);
    }
    
    printf("Connected to server at %s:%d\n", server_ip, SERVER_PORT);
    printf("Packet size: %d bytes\n", (int)sizeof(struct macos_panel_packet));
    
    /* Send panel data frames */
    send_frames(sockfd, &server_addr);
    
    close(sockfd);
    return 0;
}

int capture_cpu_state(struct macos_panel_state *panel)
{
    task_t task;
    thread_array_t thread_list;
    mach_msg_type_number_t thread_count;
    kern_return_t kr;
    arm_thread_state64_t state;
    mach_msg_type_number_t count;
    static uint64_t counter = 0;
    
    /* Get current task */
    task = mach_task_self();
    
    /* Get list of threads */
    kr = task_threads(task, &thread_list, &thread_count);
    if (kr != KERN_SUCCESS || thread_count == 0) {
        return -1;
    }
    
    /* Try to cycle through different threads to get natural variation */
    thread_t target_thread = thread_list[0];
    if (thread_count > 1) {
        /* Cycle through available threads to get different real PC values */
        target_thread = thread_list[counter % thread_count];
    }
    
    /* Get ARM64 thread state */
    count = ARM_THREAD_STATE64_COUNT;
    kr = thread_get_state(target_thread, ARM_THREAD_STATE64, 
                         (thread_state_t)&state, &count);
    
    /* Clean up thread list */
    vm_deallocate(task, (vm_address_t)thread_list, 
                  thread_count * sizeof(thread_t));
    
    if (kr != KERN_SUCCESS) {
        return -1;
    }
    
    /* Store register values - real data only */
    panel->pc = state.__pc;
    panel->sp = state.__sp;
    panel->fp = state.__fp;
    panel->lr = state.__lr;
    panel->x0 = state.__x[0];
    panel->x1 = state.__x[1];
    
    panel->architecture = 1; /* ARM64 */
    panel->timestamp = (uint32_t)mach_absolute_time();
    
    counter++;
    return 0;
}

int get_system_stats(struct macos_panel_state *panel)
{
    static host_cpu_load_info_data_t prev_cpuinfo;
    static int first_call = 1;
    
    /* Get CPU usage using host_processor_info */
    host_cpu_load_info_data_t cpuinfo;
    mach_msg_type_number_t count = HOST_CPU_LOAD_INFO_COUNT;
    
    if (host_statistics(mach_host_self(), HOST_CPU_LOAD_INFO,
                       (host_info_t)&cpuinfo, &count) == KERN_SUCCESS) {
        
        if (!first_call) {
            /* Calculate delta for more accurate CPU usage */
            natural_t user_delta = cpuinfo.cpu_ticks[CPU_STATE_USER] - prev_cpuinfo.cpu_ticks[CPU_STATE_USER];
            natural_t system_delta = cpuinfo.cpu_ticks[CPU_STATE_SYSTEM] - prev_cpuinfo.cpu_ticks[CPU_STATE_SYSTEM];
            natural_t idle_delta = cpuinfo.cpu_ticks[CPU_STATE_IDLE] - prev_cpuinfo.cpu_ticks[CPU_STATE_IDLE];
            natural_t nice_delta = cpuinfo.cpu_ticks[CPU_STATE_NICE] - prev_cpuinfo.cpu_ticks[CPU_STATE_NICE];
            
            natural_t total_delta = user_delta + system_delta + idle_delta + nice_delta;
            
            if (total_delta > 0) {
                panel->cpu_usage = (uint32_t)((100 * (user_delta + system_delta)) / total_delta);
            }
        } else {
            /* First call - use absolute values */
            natural_t total_ticks = cpuinfo.cpu_ticks[CPU_STATE_USER] +
                                   cpuinfo.cpu_ticks[CPU_STATE_SYSTEM] +
                                   cpuinfo.cpu_ticks[CPU_STATE_IDLE] +
                                   cpuinfo.cpu_ticks[CPU_STATE_NICE];
            
            if (total_ticks > 0) {
                panel->cpu_usage = (uint32_t)((100 * 
                    (cpuinfo.cpu_ticks[CPU_STATE_USER] + cpuinfo.cpu_ticks[CPU_STATE_SYSTEM]))
                    / total_ticks);
            }
            first_call = 0;
        }
        
        prev_cpuinfo = cpuinfo;
    }
    
    /* Get memory usage */
    vm_size_t page_size;
    vm_statistics64_data_t vm_stat;
    count = HOST_VM_INFO64_COUNT;
    
    if (host_page_size(mach_host_self(), &page_size) == KERN_SUCCESS &&
        host_statistics64(mach_host_self(), HOST_VM_INFO64,
                         (host_info64_t)&vm_stat, &count) == KERN_SUCCESS) {
        
        uint64_t total_mem = (vm_stat.free_count + vm_stat.active_count +
                             vm_stat.inactive_count + vm_stat.wire_count) * page_size;
        uint64_t used_mem = (vm_stat.active_count + vm_stat.inactive_count +
                            vm_stat.wire_count) * page_size;
        
        if (total_mem > 0) {
            panel->memory_usage = (uint32_t)((100 * used_mem) / total_mem);
        }
    }
    
    /* Get load average */
    double loadavg[3];
    if (getloadavg(loadavg, 3) != -1) {
        panel->load_average = (uint32_t)(loadavg[0] * 100);
    }
    
    /* Get thread count for current process */
    struct proc_taskinfo task_info;
    if (proc_pidinfo(getpid(), PROC_PIDTASKINFO, 0, &task_info, sizeof(task_info)) > 0) {
        panel->thread_count = task_info.pti_threadnum;
    }
    
    return 0;
}

void send_frames(int sockfd, struct sockaddr_in *server_addr)
{
    struct macos_panel_state panel;
    struct macos_panel_packet packet;
    int frame_count = 0;
    
    while (1) {
        /* Capture current CPU state and system stats */
        if (capture_cpu_state(&panel) < 0) {
            fprintf(stderr, "Failed to capture CPU state\n");
            /* Continue with system stats only */
        }
        
        if (get_system_stats(&panel) < 0) {
            fprintf(stderr, "Failed to get system stats\n");
        }
        
        /* Populate packet structure */
        packet.header.pp_byte_count = sizeof(struct macos_panel_state);
        packet.header.pp_byte_flags = PANEL_MACOS;
        packet.panel_state = panel;
        
        /* Send panel packet via UDP */
        if (sendto(sockfd, &packet, sizeof(packet), 0,
                   (struct sockaddr *)server_addr, sizeof(*server_addr)) < 0) {
            perror("sendto");
            break;
        }
        
        frame_count++;
        if (frame_count % (FRAMES_PER_SECOND * 2) == 0) {  /* Every 2 seconds */
            printf("Frame %d: PC=0x%llx, SP=0x%llx, X0=0x%llx, X1=0x%llx\n", 
                   frame_count, panel.pc, panel.sp, panel.x0, panel.x1);
            printf("  CPU: %d%%, MEM: %d%%, LOAD: %.2f, THREADS: %d\n", 
                   panel.cpu_usage, panel.memory_usage, 
                   panel.load_average / 100.0, panel.thread_count);
        }
        
        /* Debug: Print first few sends */
        if (frame_count <= 5) {
            printf("DEBUG: Sent packet #%d, size=%d bytes\n", frame_count, (int)sizeof(packet));
            printf("DEBUG: PC=0x%llx, X0=0x%llx, X1=0x%llx, timestamp=0x%x\n", 
                   panel.pc, panel.x0, panel.x1, panel.timestamp);
        }
        
        /* Wait for next frame time */
        precise_delay(USEC_PER_FRAME);
    }
}
