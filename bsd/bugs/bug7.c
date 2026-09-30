

#include <stdio.h>

void processArray(int* arr, int size) 
{
    int i;
    // Print the elements in the supplied array
    for (i = 0; i <= size; ++i) { 
        printf("%d ", arr[i]);
    }
    printf("\n");
}

int numbers[] = {1, 2, 3, 4};

int main() {
    processArray(numbers, 4); 
    return 0;
}


