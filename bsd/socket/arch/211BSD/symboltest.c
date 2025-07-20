/*
 * symboltest.c - Simple test to verify panel symbol lookup
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef unsigned long uintptr_t;

int main()
{
    FILE *fp;
    char line[256];
    char symbol[64];
    uintptr_t addr;
    char type;
    int found_panel = 0;
    
    printf("Testing symbol lookup for 'panel':\n\n");
    
    /* Try /unix first */
    printf("Checking /unix:\n");
    fp = popen("nm /unix 2>/dev/null", "r");
    if (fp != NULL) {
        while (fgets(line, sizeof(line), fp)) {
            if (strstr(line, "panel")) {
                printf("  %s", line);
                if (sscanf(line, "%lo %c %s", &addr, &type, symbol) == 3 ||
                    sscanf(line, "%lx %c %s", &addr, &type, symbol) == 3) {
                    if (strcmp(symbol, "panel") == 0 || strcmp(symbol, "_panel") == 0) {
                        printf("    -> Found: %s at 0x%lx (type %c)\n", symbol, addr, type);
                        found_panel = 1;
                    }
                }
            }
        }
        pclose(fp);
    } else {
        printf("  Cannot access /unix\n");
    }
    
    printf("\nChecking /vmunix:\n");
    fp = popen("nm /vmunix 2>/dev/null", "r");
    if (fp != NULL) {
        while (fgets(line, sizeof(line), fp)) {
            if (strstr(line, "panel")) {
                printf("  %s", line);
                if (sscanf(line, "%lo %c %s", &addr, &type, symbol) == 3 ||
                    sscanf(line, "%lx %c %s", &addr, &type, symbol) == 3) {
                    if (strcmp(symbol, "panel") == 0 || strcmp(symbol, "_panel") == 0) {
                        printf("    -> Found: %s at 0x%lx (type %c)\n", symbol, addr, type);
                        found_panel = 1;
                    }
                }
            }
        }
        pclose(fp);
    } else {
        printf("  Cannot access /vmunix\n");
    }
    
    printf("\nDirect grep test:\n");
    fp = popen("nm /unix 2>/dev/null | grep panel", "r");
    if (fp != NULL) {
        while (fgets(line, sizeof(line), fp)) {
            printf("  %s", line);
        }
        pclose(fp);
    }
    
    if (!found_panel) {
        printf("\nWARNING: No panel symbol found!\n");
        printf("This could mean:\n");
        printf("1. The kernel was not compiled with panel support\n");
        printf("2. The symbol table is not available\n");
        printf("3. The symbol has a different name\n");
    }
    
    return 0;
}
