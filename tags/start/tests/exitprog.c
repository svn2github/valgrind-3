

#include <stdio.h>
#include <stdlib.h>

#define ZILLION 1000000

void main ( void )
{
   int i;
   char* a = malloc(ZILLION * sizeof(char));
   for (i = 0; i <= ZILLION; i++) a[i] = 0;
   a = (char*)177;
   _exit(1);
}
