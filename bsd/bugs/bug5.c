

#include <stdio.h>

int main() 
{
    unsigned int i, sum = 0;
    for (i = 10; i >= 0; --i)
        sum += i;

    printf("sum = %d\n", sum);
}


