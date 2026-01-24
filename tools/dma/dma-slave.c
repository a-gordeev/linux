// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2026 Alexander Gordeev <a.gordeev.box@gmail.com>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <getopt.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/uio.h>
#include <sys/mman.h>
#include <stdbool.h>
#include <linux/types.h>
#include <linux/dma-slave.h>

static void dump_mem(const void *mem, size_t mem_len)
{
	unsigned int i;

	if (mem_len > 16)
		mem_len = 16;
	printf("[ ");
	for (i = 0; i < mem_len; i++)
		printf("%02X ", ((const unsigned char *)mem)[i]);
	printf("]\n");
}

static bool check_args(unsigned int cmd, const char *file, struct dma_slave_config_uapi *ucfg)
{
	if (cmd == IOCTL_DMA_SLAVE_READ) {
		if (!ucfg->data.iov_len)
			return false;
		if (!ucfg->src_addr)
			return false;
	} else if (cmd == IOCTL_DMA_SLAVE_WRITE) {
		if (!ucfg->dst_addr)
			return false;
	} else {
		return false;
	}
	if (!file)
		return false;
	return true;
}

static int read_peripheral_config(const char *conf, struct iovec *peripheral_config)
{
	struct stat stat;
	void *buf;
	int fd;
	int ret;

	if (!conf)
		return 0;

	fd = open(conf, O_RDONLY);
	if (fd < 0) {
		ret = errno;
		goto err_ret;
	}
	if (fstat(fd, &stat) < 0) {
		ret = errno;
		goto err_close_fd;
	}
	buf = malloc(stat.st_size);
	if (!buf) {
		ret = errno;
		goto err_close_fd;
	}
	if (read(fd, buf, stat.st_size) < 0) {
		ret = errno;
		goto err_free;
	}

	peripheral_config->iov_base = buf;
	peripheral_config->iov_len = stat.st_size;
	ret = 0;

err_free:
	free(buf);
err_close_fd:
	close(fd);
err_ret:
	return ret;
}

static void print_usage(void)
{
	printf("A utility to transfer the contents of a file to a DMA slave device.\n");
	printf("Requires the companion 'dma-slave' device driver to be loaded.\n\n");

	printf("Usage:\n");
	printf("  dma-slave [OPTIONS]\n\n");

	printf("Transfer direction (required, select one):\n");
	printf("  -r, --read                     Perform DMA read (device to file)\n");
	printf("  -w, --write                    Perform DMA write (file to device)\n\n");

	printf("Transfer address (required):\n");
	printf("  -S, --src-addr <addr>          Source address (hex,dec,oct)\n");
	printf("  -D, --dst-addr <addr>          Destination address (hex,dec,oct)\n\n");

	printf("Transfer size (required on read, optional on write):\n");
	printf("  -s, --size <bytes>             Transfer size in bytes\n\n");

	printf("Transfer parameters (optional):\n");
	printf("      --src-addr-width <n>       Source address width\n");
	printf("      --dst-addr-width <n>       Destination address width\n");
	printf("      --src-maxburst <n>         Source max burst size\n");
	printf("      --dst-maxburst <n>         Destination max burst size\n");
	printf("      --src-port-window-size <n> Source port window size\n");
	printf("      --dst-port-window-size <n> Destination port window size\n");
	printf("      --device-fc                Enable device flow control\n");
	printf("  -p, --peripheral-config <file> Peripheral configuration file (raw)\n\n");

	printf("User stored data (recreated on read, must exist on write):\n");
	printf("  -f, --file <file>              Transfer contents (raw)\n\n");

	printf("Target DMA channel (optional, auto-selected if not provided):\n");
	printf("  -c, --channel-name <name>      DMA channel name (in /sys/class/dma)\n");

	printf("Advanced parameters (optional):\n");
	printf("  -d, --dump                     Dump transferred data (16 bytes at most)\n\n");

	printf("Examples:\n");
	printf("  dma-slave --read  -c dma0chan0 -S 0x20000000 --file output.bin -s 4096\n");
	printf("  dma-slave --write -c dma0chan0 -D 0x10000000 --file input.bin\n");
}

enum {
	OPT_SRC_ADDR_WIDTH,
	OPT_DST_ADDR_WIDTH,
	OPT_SRC_MAXBURST,
	OPT_DST_MAXBURST,
	OPT_SRC_PORT_WINDOW_SIZE,
	OPT_DST_PORT_WINDOW_SIZE,
	OPT_DEVICE_FC,
};

static const struct option long_opts[] = {
	{ "help",			no_argument,		NULL, 'h' },
	{ "read",			no_argument,		NULL, 'r' },
	{ "write",			no_argument,		NULL, 'w' },
	{ "dump",			no_argument,		NULL, 'd' },
	{ "channel-name",		required_argument,	NULL, 'c' },
	{ "peripheral-config",		required_argument,	NULL, 'p' },
	{ "file",			required_argument,	NULL, 'f' },
	{ "size",			required_argument,	NULL, 's' },
	{ "src-addr",			required_argument,	NULL, 'S' },
	{ "dst-addr",			required_argument,	NULL, 'D' },
	{ "src-addr-width",		required_argument,	NULL, OPT_SRC_ADDR_WIDTH },
	{ "dst-addr-width",		required_argument,	NULL, OPT_DST_ADDR_WIDTH },
	{ "src-maxburst",		required_argument,	NULL, OPT_SRC_MAXBURST },
	{ "dst-maxburst",		required_argument,	NULL, OPT_DST_MAXBURST },
	{ "src-port-window-size",	required_argument,	NULL, OPT_SRC_PORT_WINDOW_SIZE },
	{ "dst-port-window-size",	required_argument,	NULL, OPT_DST_PORT_WINDOW_SIZE },
	{ "device-fc",			no_argument,		NULL, OPT_DEVICE_FC },
	{ NULL,				0,			NULL, 0  }
};

int main(int argc, char **argv)
{
	char *file = NULL, *conf = NULL, *endptr;
	struct dma_slave_config_uapi ucfg = {};
	int fd, fd_dev;
	unsigned int cmd = 0;
	bool dump = false;
	struct stat stat;
	char opt;
	int ret;

	if (argc == 1) {
print_usage:
		print_usage();
		return 0;
	}

	while ((opt = getopt_long(argc, argv, "hrwdc:s:p:f:S:D:", long_opts, NULL)) != -1) {
		switch (opt) {
		case 'h':
			goto print_usage;
		case 'r':
			if (cmd == IOCTL_DMA_SLAVE_WRITE)
				goto err_args;
			cmd = IOCTL_DMA_SLAVE_READ;
			break;
		case 'w':
			if (cmd == IOCTL_DMA_SLAVE_READ)
				goto err_args;
			cmd = IOCTL_DMA_SLAVE_WRITE;
			break;
		case 'd':
			dump = true;
			break;
		case 'c':
			strncpy(ucfg.channel_name, optarg, sizeof(ucfg.channel_name) - 1);
			break;
		case 'p':
			conf = optarg;
			break;
		case 'f':
			file = optarg;
			break;
		case 's':
			ucfg.data.iov_len = strtoull(optarg, &endptr, 0);
			if (endptr[0])
				goto err_args;
			break;
		case 'S':
			ucfg.src_addr = strtoull(optarg, &endptr, 0);
			if (endptr[0])
				goto err_args;
			break;
		case 'D':
			ucfg.dst_addr = strtoull(optarg, &endptr, 0);
			if (endptr[0])
				goto err_args;
			break;
		case OPT_SRC_ADDR_WIDTH:
			ucfg.src_addr_width = strtoul(optarg, &endptr, 10);
			if (endptr[0])
				goto err_args;
			break;
		case OPT_DST_ADDR_WIDTH:
			ucfg.dst_addr_width = strtoul(optarg, &endptr, 10);
			if (endptr[0])
				goto err_args;
			break;
		case OPT_SRC_MAXBURST:
			ucfg.src_maxburst = strtoul(optarg, &endptr, 10);
			if (endptr[0])
				goto err_args;
			break;
		case OPT_DST_MAXBURST:
			ucfg.dst_maxburst = strtoul(optarg, &endptr, 10);
			if (endptr[0])
				goto err_args;
			break;
		case OPT_SRC_PORT_WINDOW_SIZE:
			ucfg.src_port_window_size = strtoul(optarg, &endptr, 10);
			if (endptr[0])
				goto err_args;
			break;
		case OPT_DST_PORT_WINDOW_SIZE:
			ucfg.dst_port_window_size = strtoul(optarg, &endptr, 10);
			if (endptr[0])
				goto err_args;
			break;
		case OPT_DEVICE_FC:
			ucfg.device_fc = true;
			break;
		default:
			goto err_args;
		}
	}

	ret = read_peripheral_config(conf, &ucfg.peripheral_config);
	if (ret)
		return ret;

	if (!check_args(cmd, file, &ucfg)) {
err_args:
		print_usage();
		return EINVAL;
	}

	fd_dev = open("/dev/" DMA_SLAVE_DEVICE, O_RDWR);
	if (fd_dev < 0)
		return errno;

	switch (cmd) {
	case IOCTL_DMA_SLAVE_READ:
		fd = open(file, O_CREAT | O_RDWR | O_TRUNC, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP);
		if (fd < 0)
			return errno;
		if (ftruncate(fd, ucfg.data.iov_len) < 0)
			return errno;
		ucfg.data.iov_base = mmap(NULL, ucfg.data.iov_len, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
		if (ucfg.data.iov_base == MAP_FAILED) {
			ret = errno;
			unlink(file);
			return ret;
		}
		close(fd);
		break;

	case IOCTL_DMA_SLAVE_WRITE:
		fd = open(file, O_RDONLY);
		if (fd < 0)
			return errno;
		if (fstat(fd, &stat) < 0)
			return errno;
		if (!stat.st_size)
			return EINVAL;
		if (!ucfg.data.iov_len || (size_t)stat.st_size < ucfg.data.iov_len)
			ucfg.data.iov_len = stat.st_size;
		ucfg.data.iov_base = mmap(NULL, ucfg.data.iov_len, PROT_READ, MAP_SHARED, fd, 0);
		if (ucfg.data.iov_base == MAP_FAILED)
			return errno;
		close(fd);
		if (dump)
			dump_mem(ucfg.data.iov_base, ucfg.data.iov_len);
		break;
	}

	if (ioctl(fd_dev, cmd, &ucfg) < 0) {
		ret = errno;
		if (cmd == IOCTL_DMA_SLAVE_READ)
			unlink(file);
		return ret;
	}

	if (cmd == IOCTL_DMA_SLAVE_READ && dump)
		dump_mem(ucfg.data.iov_base, ucfg.data.iov_len);

	munmap(ucfg.data.iov_base, ucfg.data.iov_len);
	close(fd_dev);

	return 0;
}
