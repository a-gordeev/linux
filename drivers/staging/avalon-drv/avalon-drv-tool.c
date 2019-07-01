#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/uio.h>

#include <uapi/linux/avalon-drv-ioctl.h>

#define DEV_NAME	"/dev/avalon-drv"
#define DMA_IN		"./dma.in"
#define DMA_OUT		"./dma.out"

int main(int argc, char **argv)
{
	int dev, fd_rd, fd_wr;
	void *buf, *buf_rd, *buf_wr;
	size_t len, len_rd, len_wr;
	struct iovec iovec[2];
	struct stat stat;
	int cmd = 0;
	char opt;

	const size_t xfer_len = (0x80000000 - 0x70000000) / 2;

	while ((opt = getopt(argc, argv, "rwsRWSHD")) != -1) {
		switch (opt) {
		case 'r':
			cmd = IOCTL_AVALON_DMA_READ;
			break;
		case 'w':
			cmd = IOCTL_AVALON_DMA_WRITE;
			break;
		case 's':
			cmd = IOCTL_AVALON_DMA_RDWR;
			break;
		case 'R':
			cmd = IOCTL_AVALON_DMA_READ_SG;
			break;
		case 'W':
			cmd = IOCTL_AVALON_DMA_WRITE_SG;
			break;
		case 'H':
			cmd = IOCTL_AVALON_DMA_READ_SG_SMP;
			break;
		case 'D':
			cmd = IOCTL_AVALON_DMA_WRITE_SG_SMP;
			break;
		case 'S':
			cmd = IOCTL_AVALON_DMA_RDWR_SG;
			break;
		default:
			return -1;
		}
	}

	len = len_rd = len_wr = xfer_len;

	switch (cmd) {
	case IOCTL_AVALON_DMA_READ:
	case IOCTL_AVALON_DMA_WRITE:
	case IOCTL_AVALON_DMA_READ_SG:
	case IOCTL_AVALON_DMA_WRITE_SG:
	case IOCTL_AVALON_DMA_READ_SG_SMP:
	case IOCTL_AVALON_DMA_WRITE_SG_SMP:
		buf = buf_rd = buf_wr = malloc(len);
		if (!buf) {
			fprintf(stderr, "malloc(%ld) failed\n", len);
			return -1;
		}

		iovec[0].iov_base = buf;
		iovec[0].iov_len = len;

		printf("buf %p, len %ld\n", buf, len);

		break;

	case IOCTL_AVALON_DMA_RDWR:
	case IOCTL_AVALON_DMA_RDWR_SG:
		buf_rd = malloc(len_rd);
		if (!buf_rd) {
			fprintf(stderr, "malloc(%ld) failed\n", len_rd);
			return -1;
		}

		buf_wr = malloc(len_wr);
		if (!buf_wr) {
			fprintf(stderr, "malloc(%ld) failed\n", len_wr);
			return -1;
		}

		iovec[0].iov_base = buf_rd;
		iovec[0].iov_len = len_rd;

		iovec[1].iov_base = buf_wr;
		iovec[1].iov_len = len_wr;

		printf("buf_rd %p, len_rd %ld; buf_wr %p, len_wr %ld\n",
			buf_rd, len_rd, buf_wr, len_wr);

		break;
	}

	switch (cmd) {
	case IOCTL_AVALON_DMA_WRITE:
	case IOCTL_AVALON_DMA_WRITE_SG:
	case IOCTL_AVALON_DMA_WRITE_SG_SMP:
	case IOCTL_AVALON_DMA_RDWR:
	case IOCTL_AVALON_DMA_RDWR_SG:
		fd_wr = open(DMA_OUT, O_RDONLY);
		if (fd_wr < 0) {
			fprintf(stderr, "open %s failed\n", DMA_OUT);
			return -1;
		}
		if(fstat(fd_wr, &stat) < 0) {
			fprintf(stderr, "fstat %s failed\n", DMA_OUT);
			return -1;
		}
		if (stat.st_size < len_wr) {
			fprintf(stderr,
				"file to read is too small %s\n", DMA_OUT);
			return -1;
		}
		if (read(fd_wr, buf_wr, len_wr) != len_wr) {
			fprintf(stderr, "failed read file %s\n", DMA_OUT);
			return -1;
		}
		close(fd_wr);
	}

	dev = open(DEV_NAME, O_RDWR);
	if (dev < 0) {
		fprintf(stderr, "open %s failed\n", DEV_NAME);
		return -1;
	}

	if (ioctl(dev, cmd, iovec) < 0) {
		fprintf(stderr, "ioctl %x failed\n", cmd);
		return -1;
	}

	close(dev);

	switch (cmd) {
	case IOCTL_AVALON_DMA_READ:
	case IOCTL_AVALON_DMA_READ_SG:
	case IOCTL_AVALON_DMA_READ_SG_SMP:
	case IOCTL_AVALON_DMA_RDWR:
	case IOCTL_AVALON_DMA_RDWR_SG:
		fd_rd = creat(DMA_IN, S_IRUSR | S_IWUSR);
		if (fd_rd < 0) {
			fprintf(stderr, "open %s failed\n", DMA_IN);
			return -1;
		}
		if(fstat(fd_rd, &stat) < 0) {
			fprintf(stderr, "fstat %s failed\n", DMA_IN);
			return -1;
		}
		if (stat.st_size != 0) {
			fprintf(stderr,
				"file to write is not empty %s\n", DMA_IN);
			return -1;
		}
		if (write(fd_rd, buf_rd, len_rd) != len_rd) {
			fprintf(stderr, "failed write file %s\n", DMA_IN);
			return -1;
		}
		close(fd_rd);
	}

	return 0;
}

