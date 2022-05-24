#include <sys/ioctl.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define OUICHEFS_SHOW_VERSION	_IOR('O', 0, unsigned long)

int main(int argc, char *argv[])
{
	int fd;
	int ret = -1;

    if (argc != 3) {
        return 1;
    }
    int version = atoi(argv[2]);

	fd = open(argv[1], O_RDONLY);
	if (fd == -1) {
		perror("Error opening file");
		return -1;
	}
	ret = ioctl(fd, OUICHEFS_SHOW_VERSION, version);
	if (ret == -1) {
		perror("Error during ioctl");
		goto err;
	}
err:
	close(fd);
	return ret;
}
