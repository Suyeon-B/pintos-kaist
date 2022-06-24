#include <stdio.h>
#include <stdlib.h>
void main()
{
    int num;
    int *arr, *arr2;
    printf("배열 크기 입력:");
    scanf("%d", &num);

    arr = (int *)malloc(sizeof(int) * num); // int [10]크기의 배열 동적할당을 합니다.
    arr2 = (int *)calloc(num, sizeof(int)); // int [10]크기의 배열 동적할당을 합니다.
    // printf("malloc으로 할당된 크기: %3d\ncalloc으로 할당된 크기: %3d\n\n", _msize(arr) / sizeof(*arr), _msize(arr2) / sizeof(*arr2)); // 크기확인을 합니다.

    for (int i = 0; i < num; i++)
    {
        printf("malloc 결과: %10d\n", *(arr + i)); // 결과가 쓰레기 값으로 나온것을 알 수 있습니다.
    }
    printf("\n");

    for (int i = 0; i < num; i++)
    {
        printf("calloc 결과:%10d\n", *(arr2 + i)); // 결과가 쓰레기 값으로 나온것을 알 수 있습니다.
    }
    printf("\n");

    //우리는 동적할당으로 잡은 메모리는 해제 시켜야합니다.
    free(arr);
    free(arr2);
}