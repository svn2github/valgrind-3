#include <stdio.h>
#include <stdlib.h>

// This function name is long enough to get truncated.

#define A2500 \
abcdefghijABCDEFGHIJabcdefghijABCDEFGHIJabcdefghij\
abcdefghijABCDEFGHIJabcdefghijABCDEFGHIJabcdefghij\
abcdefghijABCDEFGHIJabcdefghijABCDEFGHIJabcdefghij\
abcdefghijABCDEFGHIJabcdefghijABCDEFGHIJabcdefghij\
abcdefghijABCDEFGHIJabcdefghijABCDEFGHIJabcdefghij\
abcdefghijABCDEFGHIJabcdefghijABCDEFGHIJabcdefghij\
abcdefghijABCDEFGHIJabcdefghijABCDEFGHIJabcdefghij\
abcdefghijABCDEFGHIJabcdefghijABCDEFGHIJabcdefghij\
abcdefghijABCDEFGHIJabcdefghijABCDEFGHIJabcdefghij\
abcdefghijABCDEFGHIJabcdefghijABCDEFGHIJabcdefghij\
abcdefghijABCDEFGHIJabcdefghijABCDEFGHIJabcdefghij\
abcdefghijABCDEFGHIJabcdefghijABCDEFGHIJabcdefghij\
abcdefghijABCDEFGHIJabcdefghijABCDEFGHIJabcdefghij\
abcdefghijABCDEFGHIJabcdefghijABCDEFGHIJabcdefghij\
abcdefghijABCDEFGHIJabcdefghijABCDEFGHIJabcdefghij\
abcdefghijABCDEFGHIJabcdefghijABCDEFGHIJabcdefghij\
abcdefghijABCDEFGHIJabcdefghijABCDEFGHIJabcdefghij\
abcdefghijABCDEFGHIJabcdefghijABCDEFGHIJabcdefghij\
abcdefghijABCDEFGHIJabcdefghijABCDEFGHIJabcdefghij\
abcdefghijABCDEFGHIJabcdefghijABCDEFGHIJabcdefghij\
abcdefghijABCDEFGHIJabcdefghijABCDEFGHIJabcdefghij\
abcdefghijABCDEFGHIJabcdefghijABCDEFGHIJabcdefghij\
abcdefghijABCDEFGHIJabcdefghijABCDEFGHIJabcdefghij\
abcdefghijABCDEFGHIJabcdefghijABCDEFGHIJabcdefghij\
abcdefghijABCDEFGHIJabcdefghijABCDEFGHIJabcdefghij\
abcdefghijABCDEFGHIJabcdefghijABCDEFGHIJabcdefghij\
abcdefghijABCDEFGHIJabcdefghijABCDEFGHIJabcdefghij\
abcdefghijABCDEFGHIJabcdefghijABCDEFGHIJabcdefghij\
abcdefghijABCDEFGHIJabcdefghijABCDEFGHIJabcdefghij\
abcdefghijABCDEFGHIJabcdefghijABCDEFGHIJabcdefghij\
abcdefghijABCDEFGHIJabcdefghijABCDEFGHIJabcdefghij\
abcdefghijABCDEFGHIJabcdefghijABCDEFGHIJabcdefghij\
abcdefghijABCDEFGHIJabcdefghijABCDEFGHIJabcdefghij\
abcdefghijABCDEFGHIJabcdefghijABCDEFGHIJabcdefghij\
abcdefghijABCDEFGHIJabcdefghijABCDEFGHIJabcdefghij\
abcdefghijABCDEFGHIJabcdefghijABCDEFGHIJabcdefghij\
abcdefghijABCDEFGHIJabcdefghijABCDEFGHIJabcdefghij\
abcdefghijABCDEFGHIJabcdefghijABCDEFGHIJabcdefghij\
abcdefghijABCDEFGHIJabcdefghijABCDEFGHIJabcdefghij\
abcdefghijABCDEFGHIJabcdefghijABCDEFGHIJabcdefghij\
abcdefghijABCDEFGHIJabcdefghijABCDEFGHIJabcdefghij\
abcdefghijABCDEFGHIJabcdefghijABCDEFGHIJabcdefghij\
abcdefghijABCDEFGHIJabcdefghijABCDEFGHIJabcdefghij\
abcdefghijABCDEFGHIJabcdefghijABCDEFGHIJabcdefghij\
abcdefghijABCDEFGHIJabcdefghijABCDEFGHIJabcdefghij\
abcdefghijABCDEFGHIJabcdefghijABCDEFGHIJabcdefghij\
abcdefghijABCDEFGHIJabcdefghijABCDEFGHIJabcdefghij\
abcdefghijABCDEFGHIJabcdefghijABCDEFGHIJabcdefghij\
abcdefghijABCDEFGHIJabcdefghijABCDEFGHIJabcdefghij\
abcdefghijABCDEFGHIJabcdefghijABCDEFGHIJabcdefghij

void A2500(int n)
{
   if (n > 0) {
      malloc(1000);
      A2500(n-1);
   }
}

int main(void)
{
   A2500(3);
   return 0;
}

