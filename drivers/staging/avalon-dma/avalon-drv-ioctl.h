#ifndef __AVALON_DRV_IOCTL_H__
#define __AVALON_DRV_IOCTL_H__

#define IOCTL_ALARIC_DMA_READ		_IOR(0xC5, 0, struct iovec)
#define IOCTL_ALARIC_DMA_WRITE		_IOW(0xC6, 0, struct iovec)
#define IOCTL_ALARIC_DMA_SIMULTANEOUS	_IOWR(0xC7, 0, struct iovec[2])
#define IOCTL_ALARIC_DMA_READ_SG	_IOR(0xC8, 0, struct iovec)
#define IOCTL_ALARIC_DMA_WRITE_SG	_IOW(0xC9, 0, struct iovec)
#define IOCTL_ALARIC_DMA_SIMULTANEOUS_SG	_IOWR(0xCA, 0, struct iovec[2])
#define IOCTL_ALARIC_DMA_READ_SG_SMP	_IOR(0xCB, 0, struct iovec)
#define IOCTL_ALARIC_DMA_WRITE_SG_SMP	_IOW(0xCD, 0, struct iovec)

#endif
