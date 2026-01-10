#include <linux/err.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/dmaengine.h>
#include <linux/freezer.h>
#include <linux/init.h>
#include <linux/kthread.h>
#include <linux/sched/task.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/pagemap.h>
#include <linux/random.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/wait.h>

struct dmatest_slave_config {
	struct dma_slave_config	cfg;
	struct sg_table		sgt;
	struct page		**pages;
	unsigned int		nr_pages;
	struct file		*filp;
	ssize_t			size;
};

static struct dmatest_slave_config *sc;

static int transfer_direction = DMA_MEM_TO_DEV;
//static int transfer_direction = DMA_DEV_TO_MEM;
module_param(transfer_direction, int, 0644);
MODULE_PARM_DESC(transfer_direction, "Transfer direction (DMA_DEV_TO_MEM)");

char *filename = "/var/dummy.bin";
int transfer_size = 0x1000 * 0x1000;

static int prep_slave_config_wr(struct dmatest_slave_config *sc)
{
	struct address_space *mapping;
	int ret;
	int i;

	pr_err("%s(%d)\n", __FUNCTION__, __LINE__);
	sc->filp = filp_open(filename, O_RDONLY, 0);
	if (IS_ERR(sc->filp))
		return PTR_ERR(sc->filp);
	mapping = sc->filp->f_mapping;

	pr_err("%s(%d)\n", __FUNCTION__, __LINE__);
	sc->size = i_size_read(file_inode(sc->filp));
	sc->nr_pages = DIV_ROUND_UP(sc->size, PAGE_SIZE);

	pr_err("%s(%d)\n", __FUNCTION__, __LINE__);
	sc->pages = kcalloc(sc->nr_pages, sizeof(sc->pages[0]), GFP_KERNEL | GFP_DMA);
	if (!sc->pages) {
		ret = -ENOMEM;
		goto err_close_file;
	}

	pr_err("%s(%d)\n", __FUNCTION__, __LINE__);
	for (i = 0; i < sc->nr_pages; i++) {
		sc->pages[i] = read_mapping_page(mapping, i, sc->filp);
		if (IS_ERR(sc->pages[i])) {
			ret = PTR_ERR(sc->pages[i]);
			goto err_free_pages_arr;
		}
		lock_page(sc->pages[i]);
	}

	pr_err("%s(%d)\n", __FUNCTION__, __LINE__);
	ret = sg_alloc_table_from_pages(&sc->sgt, sc->pages, sc->nr_pages,
					0, sc->size, GFP_KERNEL);
	if (ret)
		goto err_unlock_pages;

	pr_err("%s(%d)\n", __FUNCTION__, __LINE__);
	return 0;

err_unlock_pages:
	pr_err("%s(%d)\n", __FUNCTION__, __LINE__);
	while (i--) {
		unlock_page(sc->pages[i]);
		put_page(sc->pages[i]);
	}
err_free_pages_arr:
	pr_err("%s(%d)\n", __FUNCTION__, __LINE__);
	kfree(sc->pages);
err_close_file:
	pr_err("%s(%d)\n", __FUNCTION__, __LINE__);
	filp_close(sc->filp, NULL);

	pr_err("%s(%d)\n", __FUNCTION__, __LINE__);
	return ret;
}

static void unprep_slave_config_wr(struct dmatest_slave_config *sc)
{
	int i;

	pr_err("%s(%d)\n", __FUNCTION__, __LINE__);
	sg_free_table(&sc->sgt);
	pr_err("%s(%d)\n", __FUNCTION__, __LINE__);
	for (i = 0; i < sc->nr_pages; i++) {
		unlock_page(sc->pages[i]);
		put_page(sc->pages[i]);
	}
	pr_err("%s(%d)\n", __FUNCTION__, __LINE__);
	kfree(sc->pages);
	pr_err("%s(%d)\n", __FUNCTION__, __LINE__);
	filp_close(sc->filp, NULL);
	pr_err("%s(%d)\n", __FUNCTION__, __LINE__);
}

static int prep_slave_config_rd(struct dmatest_slave_config *sc)
{
	unsigned int nr_pages;
	int ret;
	int i;
 
	pr_err("%s(%d)\n", __FUNCTION__, __LINE__);
	sc->filp = filp_open(filename, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (IS_ERR(sc->filp))
		return PTR_ERR(sc->filp);

	sc->size = transfer_size;
	sc->nr_pages = DIV_ROUND_UP(sc->size, PAGE_SIZE);
	pr_err("%s(%d) size %ld nr_pages %d\n", __FUNCTION__, __LINE__, sc->size, sc->nr_pages);

	sc->pages = kcalloc(sc->nr_pages, sizeof(sc->pages[0]), GFP_KERNEL);
	if (!sc->pages) {
		ret = -ENOMEM;
		goto err_close_file;
	}

	nr_pages = alloc_pages_bulk(GFP_KERNEL, sc->nr_pages, sc->pages);
	pr_err("%s(%d) nr_pages %d sc->nr_pages %d\n", __FUNCTION__, __LINE__, nr_pages, sc->nr_pages);
	if (nr_pages != sc->nr_pages) {
		ret = -ENOMEM;
		goto err_free_pages_arr;
	}

	pr_err("%s(%d)\n", __FUNCTION__, __LINE__);
	ret = sg_alloc_table_from_pages(&sc->sgt, sc->pages, sc->nr_pages,
					0, sc->size, GFP_KERNEL);
	if (ret)
		goto err_free_pages;

	pr_err("%s(%d)\n", __FUNCTION__, __LINE__);
	return 0;

err_free_pages:
	pr_err("%s(%d)\n", __FUNCTION__, __LINE__);
	for (i = 0; i < nr_pages; i++)
		free_page(i);
err_free_pages_arr:
	pr_err("%s(%d)\n", __FUNCTION__, __LINE__);
	kfree(sc->pages);
err_close_file:
	pr_err("%s(%d)\n", __FUNCTION__, __LINE__);
	filp_close(sc->filp, NULL);

	pr_err("%s(%d)\n", __FUNCTION__, __LINE__);
	return ret;
}

static void unprep_slave_config_rd(struct dmatest_slave_config *sc)
{
	int ret = -ENOMEM;
	loff_t off = 0;
	void *buf;
	int i;

	pr_err("%s(%d)\n", __FUNCTION__, __LINE__);
	buf = vm_map_ram(sc->pages, sc->nr_pages, NUMA_NO_NODE);
	pr_err("%s(%d)\n", __FUNCTION__, __LINE__);
	if (buf) {
		pr_err("%s(%d)\n", __FUNCTION__, __LINE__);
		memset(buf, 0x55, sc->size);
		ret = kernel_write(sc->filp, buf, sc->size, &off);
		pr_err("%s(%d) ret %d\n", __FUNCTION__, __LINE__, ret);
		vm_unmap_ram(buf, sc->nr_pages);
	}

	pr_err("%s(%d)\n", __FUNCTION__, __LINE__);
	sg_free_table(&sc->sgt);
	pr_err("%s(%d)\n", __FUNCTION__, __LINE__);
	for (i = 0; i < sc->nr_pages; i++)
		__free_pages(sc->pages[i], 0);
	pr_err("%s(%d)\n", __FUNCTION__, __LINE__);
	kfree(sc->pages);
	pr_err("%s(%d)\n", __FUNCTION__, __LINE__);
	if (ret < 0) {
		pr_err("%s(%d)\n", __FUNCTION__, __LINE__);
		struct mnt_idmap *idmap = mnt_idmap(sc->filp->f_path.mnt);
		struct dentry *dentry = sc->filp->f_path.dentry;
		struct inode *dir = d_inode(dentry->d_parent);

		pr_err("%s(%d)\n", __FUNCTION__, __LINE__);
		vfs_unlink(idmap, dir, dentry, NULL);
	}
	pr_err("%s(%d)\n", __FUNCTION__, __LINE__);
	filp_close(sc->filp, NULL);
	pr_err("%s(%d)\n", __FUNCTION__, __LINE__);

}

static struct dmatest_slave_config *prep_slave_config(void)
{
	struct dmatest_slave_config *sc;
	int ret;

	sc = kzalloc(sizeof(*sc), GFP_KERNEL);
	if (!sc)
		return ERR_PTR(-ENOMEM);

	if (transfer_direction == DMA_DEV_TO_MEM)
		ret = prep_slave_config_rd(sc);
	else
		ret = prep_slave_config_wr(sc);
	if (ret) {
		kfree(sc);
		return ERR_PTR(ret);
	}

	sc->cfg.direction = transfer_direction;

	return sc;
}

static void unprep_slave_config(struct dmatest_slave_config *sc)
{
	if (sc->cfg.direction == DMA_DEV_TO_MEM)
		unprep_slave_config_rd(sc);
	else
		unprep_slave_config_wr(sc);
	kfree(sc);
}

static int __init dummy_init(void)
{
	sc = prep_slave_config();
	if (IS_ERR(sc))
		return PTR_ERR(sc);
	return 0;
}
module_init(dummy_init);

static void __exit dummy_exit(void)
{
	unprep_slave_config(sc);
}
module_exit(dummy_exit);

MODULE_AUTHOR("Alexander Gordeev");
MODULE_DESCRIPTION("Dummy module");
MODULE_LICENSE("GPL");
