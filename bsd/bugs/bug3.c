

#include <stdio.h>

char* getMessage(void)
{
    char buffer[20];
    sprintf(buffer, "Hello from getMessage subroutine!");
    return buffer;
}

int main(void)
{
    puts(getMessage());
    return 0;
}


