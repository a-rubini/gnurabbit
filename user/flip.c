/* Trivial.... */
#include <stdio.h>

int main(int argc, char **argv)
{
    unsigned int c;
    while ( (c = getchar()) != EOF)
	putchar( 0
		 | ((c & 0x80) >> 7)
		 | ((c & 0x40) >> 5)
		 | ((c & 0x20) >> 3)
		 | ((c & 0x10) >> 1)
		 | ((c & 0x08) << 1)
		 | ((c & 0x04) << 3)
		 | ((c & 0x02) << 5)
		 | ((c & 0x01) << 7));
    return 0;
}
