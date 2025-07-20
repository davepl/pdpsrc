/*
 * endiantest.c - Test byte order interpretation
 */

#include <stdio.h>

int main()
{
    /* Test structure with known values */
    struct {
        long ps_address;
        short ps_data;
        short ps_psw;
        short ps_mser;
        short ps_cpu_err;
        short ps_mmr0;
        short ps_mmr3;
    } test_panel;
    
    unsigned char *raw;
    int i;
    
    /* Initialize the test structure manually */
    test_panel.ps_address = 0x12345678L;
    test_panel.ps_data = 0x1234;
    test_panel.ps_psw = 0x5678;
    test_panel.ps_mser = 0xABCD;
    test_panel.ps_cpu_err = 0xEF01;
    test_panel.ps_mmr0 = 0x2345;
    test_panel.ps_mmr3 = 0x6789;
    
    raw = (unsigned char *)&test_panel;
    
    printf("Test structure byte order:\n");
    printf("Expected: ps_address=0x12345678, ps_data=0x1234, ps_psw=0x5678\n");
    printf("Raw bytes: ");
    for (i = 0; i < sizeof(test_panel); i++) {
        printf("%02x ", raw[i]);
        if (i == 3 || i == 5 || i == 7 || i == 9 || i == 11 || i == 13) printf(" ");
    }
    printf("\n");
    
    printf("Structure interpretation:\n");
    printf("ps_address: 0x%08lx\n", test_panel.ps_address);
    printf("ps_data:    0x%04x\n", test_panel.ps_data);
    printf("ps_psw:     0x%04x\n", test_panel.ps_psw);
    printf("ps_mser:    0x%04x\n", test_panel.ps_mser);
    printf("ps_cpu_err: 0x%04x\n", test_panel.ps_cpu_err);
    printf("ps_mmr0:    0x%04x\n", test_panel.ps_mmr0);
    printf("ps_mmr3:    0x%04x\n", test_panel.ps_mmr3);
    
    /* Now interpret the actual kernel data */
    printf("\nKernel data interpretation:\n");
    {
        unsigned char kernel_data[16];
        struct {
            long ps_address;
            short ps_data;
            short ps_psw;
            short ps_mser;
            short ps_cpu_err;
            short ps_mmr0;
            short ps_mmr3;
        } *panel_ptr;
        
        /* Initialize kernel data manually */
        kernel_data[0] = 0x00; kernel_data[1] = 0x00; kernel_data[2] = 0x58; kernel_data[3] = 0xa3;
        kernel_data[4] = 0x00; kernel_data[5] = 0x00; kernel_data[6] = 0xbd; kernel_data[7] = 0x00;
        kernel_data[8] = 0x00; kernel_data[9] = 0x00; kernel_data[10] = 0xbf; kernel_data[11] = 0x00;
        kernel_data[12] = 0x00; kernel_data[13] = 0x00; kernel_data[14] = 0xc1; kernel_data[15] = 0x00;
        
        panel_ptr = (void*)kernel_data;
        
        printf("Raw kernel bytes: ");
        for (i = 0; i < 16; i++) {
            printf("%02x ", kernel_data[i]);
            if (i == 3 || i == 5 || i == 7 || i == 9 || i == 11 || i == 13) printf(" ");
        }
        printf("\n");
        
        printf("Interpreted as:\n");
        printf("ps_address: 0x%08lx (%ld)\n", panel_ptr->ps_address, panel_ptr->ps_address);
        printf("ps_data:    0x%04x (%d)\n", panel_ptr->ps_data, panel_ptr->ps_data);
        printf("ps_psw:     0x%04x (%d)\n", panel_ptr->ps_psw, panel_ptr->ps_psw);
        printf("ps_mser:    0x%04x (%d)\n", panel_ptr->ps_mser, panel_ptr->ps_mser);
        printf("ps_cpu_err: 0x%04x (%d)\n", panel_ptr->ps_cpu_err, panel_ptr->ps_cpu_err);
        printf("ps_mmr0:    0x%04x (%d)\n", panel_ptr->ps_mmr0, panel_ptr->ps_mmr0);
        printf("ps_mmr3:    0x%04x (%d)\n", panel_ptr->ps_mmr3, panel_ptr->ps_mmr3);
    }
    
    return 0;
}
