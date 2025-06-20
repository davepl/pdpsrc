

#include <stdio.h>

int main() 
{
    char buf[80];
    printf("Enter your name: ");
    gets(buf);
    if (buf == "backdoor")
        printf("Access granted!\n");
    else
        printf("Access denied!\n");
}


