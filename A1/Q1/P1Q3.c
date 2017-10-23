#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>

int main()
{
	int filefd = open("redirect.txt", O_WRONLY|O_CREAT|O_TRUNC, 0666);
	close(1);
	dup(filefd);
	printf("A simple program output.");
	return 0;
}