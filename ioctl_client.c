#include <sys/ioctl.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define OUICHEFS_SHOW_VERSION	_IOR('O', 0, unsigned long)
#define OUICHEFS_RESTORE_VERSION	_IO('O', 1)

int main(int argc, char *argv[])
{
	int fd;
	int ret = -1;

	if (argc < 3)
		return -1;

	fd = open(argv[2], O_RDONLY);
	if (fd == -1) {
		perror("Error opening file");
		return -1;
	}

	if (strcmp(argv[1], "version") == 0) {
		if (argc != 4) {
			ret = -1;
			goto err;
		}

		int version = atoi(argv[3]);

		ret = ioctl(fd, OUICHEFS_SHOW_VERSION, version);
		if (ret == -1) {
			perror("Error during ioctl");
			goto err;
		}
	} else if (strcmp(argv[1], "reset") == 0) {
		if (argc != 3) {
			ret = -1;
			goto err;
		}

		ret = ioctl(fd, OUICHEFS_RESTORE_VERSION);
		if (ret == -1) {
			perror("Error during ioctl");
			goto err;
		}
	} else {
		printf("Unknown operation: %s", argv[1]);
	}

err:
	close(fd);
	return ret;
}
