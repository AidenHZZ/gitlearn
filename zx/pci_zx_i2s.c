// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *	zx_i2s_pci.c - Zhaoxin I2S PCI driver
 *
 *	Copyright(c) 2021 Shanghai Zhaoxin Corporation. All rights reserved.
 *
*/

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/pci.h>
#include <linux/kernel.h>
#include <linux/stddef.h>
#include <linux/ioport.h>
#include <linux/init.h>
#include <asm/io.h>
#include <linux/version.h>
#include <linux/platform_device.h>
#include "zx_i2s.h"

struct zxi2s_pdata {
	void __iomem *zxi2s_base;
	struct resource *res;
	struct platform_device *pdev[ZXI2S_DEVS];
};

static int zxi2s_pci_suspend(struct device *dev)
{
	dev_info(dev, "%s\n", __func__);
	return 0;
}

static int zxi2s_pci_resume(struct device *dev)
{
	dev_info(dev, "%s\n", __func__);
	return 0;
}


static const struct dev_pm_ops zxi2s_pci_pm = {
	.runtime_suspend = zxi2s_pci_suspend,
	.runtime_resume =  zxi2s_pci_resume,
//	.suspend = zxi2s_pci_suspend,
//	.resume =  zxi2s_pci_resume,
};

static int zxi2s_pci_probe(struct pci_dev *pci,
		     const struct pci_device_id *pci_id)
{
	struct zxi2s_pdata *pdata;
	struct platform_device_info pdevinfo[ZXI2S_DEVS];
	int ret, i;
	u32 addr;
	unsigned int irqflags;

	if (pci_enable_device(pci)) {
		dev_err(&pci->dev, "pci_enable_device failed\n");
		return -ENODEV;
	}

	ret = pci_request_regions(pci, "Zhaoxin I2S controller");
	if (ret < 0) {
		dev_err(&pci->dev, "pci_request_regions failed\n");
		goto disable_pci;
	}

	//申请一块内存来保存base/res/3个platform_dev(i2s\dma\machine)
	pdata = devm_kzalloc(&pci->dev, sizeof(*pdata), GFP_KERNEL);
	if (IS_ERR(pdata)) {
		ret = -ENOMEM;
		goto release_regions;
	}

	addr = pci_resource_start(pci, 0);//bar[0]物理首地址
	pdata->zxi2s_base = pci_ioremap_bar(pci, 0);//根据bar映射后首地址
	if (IS_ERR(pdata->zxi2s_base)) {
		dev_err(&pci->dev, "ioremap error\n");
		ret = -ENOMEM;
		goto release_regions;
	}

	pci_set_master(pci);
	pci_set_drvdata(pci, pdata);

	/*
	  TODO: do some init. Maybe needn't
	*/

	pdata->res = devm_kzalloc(&pci->dev,
			  sizeof(struct resource) * 4, GFP_KERNEL);
	if(IS_ERR(pdata->res)) {
		dev_err(&pci->dev, "devm_kzalloc error\n");
		ret = -ENOMEM;
		goto release_regions;
	}

	/* 准备各个driver需要的信息, 目前没有区分playback/capture/dmai. TODO 
	https://blog.csdn.net/cjianeng/article/details/122776618 中“何为资源”
	一节讲得比较清楚
	*/
	pdata->res[0].name = "zxi2s_iomem";
	pdata->res[0].flags = IORESOURCE_MEM;	// mem信息
	pdata->res[0].start = addr;
	pdata->res[0].end = addr + 0x1FF;

	pdata->res[1].name = "zxi2s_irq";
	pdata->res[1].flags = IORESOURCE_IRQ;	// irq信息
	pdata->res[1].start = pci->irq;
	pdata->res[1].end = pdata->res[1].start;//也可以不用填

	memset(&pdevinfo, 0, sizeof(pdevinfo));//初始化内存
	irqflags = IRQF_SHARED;

	/* 设置machine driver需要的信息 */
	pdevinfo[0].name = ZXI2S_MC_NAME; // TODO: 暂时没想到啥要传给machine driver的
	pdevinfo[0].parent = &pci->dev;

	/* 设置 I2S DAI driver 需要访问的信息 */
	pdevinfo[1].name = ZXI2S_CPU_NAME;
	pdevinfo[1].parent = &pci->dev;
	pdevinfo[1].res = &pdata->res[0];
	pdevinfo[1].num_res = 1;	// 不需要中断
//	pdevinfo[1].data = &irqflags;
//	pdevinfo[1].size_data = sizeof(irqflags);

	/* 设置 DMA driver需要访问的信息 */
	pdevinfo[2].name = ZXI2S_DMA_NAME;
	pdevinfo[2].parent = &pci->dev;
	pdevinfo[2].res = &pdata->res[0];
	pdevinfo[2].num_res = 2;	// 给DMA全部的资源
	pdevinfo[2].data = &irqflags;
	pdevinfo[2].size_data = sizeof(irqflags);

	/* 依次创建3个platform device */
	for (i = 0; i < ZXI2S_DEVS; i ++) {
		pdata->pdev[i] = platform_device_register_full(&pdevinfo[i]);
		if (IS_ERR(pdata->pdev[i])) {
			dev_err(&pci->dev, "cannot register %s device\n",
				pdevinfo[i].name);
			ret = PTR_ERR(pdata->pdev[i]);
			goto unregister_devs;
		}
		dev_info(&pdata->pdev[i]->dev, "device registered!");
	}
	return 0;

unregister_devs:
	for (i = 0; i < ZXI2S_DEVS; i ++) {
		if (!IS_ERR(pdata->pdev[i]))
			platform_device_unregister(pdata->pdev[i]);
	}
release_regions:
	pci_release_regions(pci);
disable_pci:
	pci_disable_device(pci);
	return ret;
}

void zxi2s_pci_remove(struct pci_dev *pci)
{
	struct zxi2s_pdata *pdata;
	int i;

	dev_info(&pci->dev, "%s\n", __func__);

	pdata = pci_get_drvdata(pci);
	for (i = 0; i < ZXI2S_DEVS; i ++) {
		if (!IS_ERR(pdata->pdev[i]))
			platform_device_unregister(pdata->pdev[i]);
	}

	pci_release_regions(pci);
	pci_disable_device(pci);
	return;
}

/* PCI IDs */
static const struct pci_device_id zxi2s_pci_ids[] = {
	{ PCI_DEVICE(0x1d17, 0x9500) },
	{ 0, }
};

static struct pci_driver zxi2s_pci_driver = {
	.name = KBUILD_MODNAME,
	.id_table = zxi2s_pci_ids,
	.probe = zxi2s_pci_probe,
	.remove = zxi2s_pci_remove,
	.driver = {
		.pm = &zxi2s_pci_pm,
	}
};
module_pci_driver(zxi2s_pci_driver);

MODULE_AUTHOR("hanshu@zhaoxin.com");
MODULE_DESCRIPTION("ZHAOXIN I2S controller create");
MODULE_VERSION(DRIVER_VERSION);
MODULE_LICENSE("GPL v2");
