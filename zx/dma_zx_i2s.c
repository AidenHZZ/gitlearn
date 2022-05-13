// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *      zx_i2s_dma.c - Zhaoxin I2S dma driver
 *
 *      Copyright(c) 2021 Shanghai Zhaoxin Corporation. All rights reserved.
 *
*/
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/interrupt.h>
#include <sound/soc.h>
#include "zx_i2s.h"


static const struct snd_pcm_hardware zxi2s_pcm_hardware_playback = {
		.info			= SNDRV_PCM_INFO_MMAP |
					  SNDRV_PCM_INFO_MMAP_VALID |
					  SNDRV_PCM_INFO_INTERLEAVED |//数据的排列方式（左右左右左右还是左左左右右右）
					  SNDRV_PCM_INFO_PAUSE |
					  SNDRV_PCM_INFO_RESUME,
		.formats		= SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S24_LE | SNDRV_PCM_FMTBIT_S32_LE,
		.rate_min		= 8000,
		.rate_max		= 96000,
		.period_bytes_min	= 16 * 1024,
		.period_bytes_max	= 1024,
		.periods_min		= 1,
		.periods_max		= 16,
		.channels_min		= 2,
		.channels_max		= 2,
		.buffer_bytes_max	= 64 * 1024,
		.fifo_size		= 32,
	};


	static const struct snd_pcm_hardware zxi2s_pcm_hardware_capture = {
			.info			= SNDRV_PCM_INFO_MMAP |
						  SNDRV_PCM_INFO_MMAP_VALID |
						  SNDRV_PCM_INFO_INTERLEAVED |//数据的排列方式（左右左右左右还是左左左右右右）
						  SNDRV_PCM_INFO_PAUSE |
						  SNDRV_PCM_INFO_RESUME,
			.formats		= SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S24_LE | SNDRV_PCM_FMTBIT_S32_LE,
			.rate_min		= 8000,
			.rate_max		= 96000,
			.period_bytes_min	= 16 * 1024,
			.period_bytes_max	= 1024,
			.periods_min		= 1,
			.periods_max		= 16,
			.channels_min		= 2,
			.channels_max		= 2,
			.buffer_bytes_max	= 64 * 1024,
			.fifo_size		= 32,
		};


struct zxi2s_dma {

	/* controller resources information */    
	struct device    *dev;
	void __iomem *regs;
	u8 irq;

	struct zxi2s_stream_data *stream_data;
		/* hdac_stream linked list */
	struct list_head stream_list;
	
	struct snd_dma_buffer posbuf;		/* position buffer pointer */

	spinlock_t zxi2s_reg_lock;
};



struct zxi2s_stream_data { 

		/* pcm support */

		int direction;		/* playback / capture (SNDRV_PCM_STREAM_*) */

		unsigned int bufsize;	/* size of the play buffer in bytes */
		unsigned int period_bytes; /* size of the period in bytes */
		unsigned int frags;	/* number for period in the play buffer */
		//unsigned int fifo_size;	/* FIFO size */
		unsigned int format_val;

		u32 sd_int_sta_mask;	/* stream int status mask */

		void __iomem *regs;
		struct snd_pcm_substream *substream;	/* assigned substream,set in PCM open*/
		struct snd_dma_buffer bdl; /* BDL buffer */
		__le32 *posbuf;		/* position buffer pointer */

		struct list_head list;	
		bool running;

};

/*
 * set up a BDL entry
 */
static int setup_bdle(
		      struct snd_dma_buffer *dmab,
		      struct zxi2s_stream_data *priv_data, __le32 **bdlp,
		      int ofs, int size, int with_ioc)
{
	__le32 *bdl = *bdlp;

	while (size > 0) {
		dma_addr_t addr;
		int chunk;

		if (priv_data->frags >= 256)
			return -EINVAL;

		addr = snd_sgbuf_get_addr(dmab, ofs);
		/* program the address field of the BDL entry */
		bdl[0] = cpu_to_le32((u32)addr);
		bdl[1] = cpu_to_le32(upper_32_bits(addr));
		/* program the size field of the BDL entry */
		chunk = snd_sgbuf_get_chunk_size(dmab, ofs, size);
		
		bdl[2] = cpu_to_le32(chunk);
		/* program the IOC to enable interrupt
		 * only when the whole fragment is processed
		 */
		size -= chunk;
		bdl[3] = (size || !with_ioc) ? 0 : cpu_to_le32(0x01);
		bdl += 4;
		priv_data->frags++;
		ofs += chunk;
	}
	*bdlp = bdl;
	return ofs;
}

/**
 * snd_hdac_stream_setup_periods - set up BDL entries
 * @azx_dev: HD-audio core stream to set up
 *
 * Set up the buffer descriptor table of the given stream based on the
 * period and buffer sizes of the assigned PCM substream.
 */
int snd_i2s_stream_setup_periods(struct zxi2s_stream_data *priv_data)
{
	
	struct snd_pcm_substream *substream = priv_data->substream;
	struct snd_pcm_runtime *runtime = substream->runtime;
	__le32 *bdl;
	int i, ofs, periods, period_bytes;


	period_bytes = priv_data->period_bytes;
	periods = priv_data->bufsize / period_bytes;

	/* program the initial BDL entries */
	bdl = (__le32 *)priv_data->bdl.area;//虚拟地址
	ofs = 0;
	priv_data->frags = 0;


	for (i = 0; i < periods; i++) {
		ofs = setup_bdle(snd_pcm_get_dma_buf(substream),priv_data, &bdl, ofs,period_bytes, 1);

		if (ofs < 0)
			goto error;
	}
	return 0;

 error:
	printk("Too many BDL entries: buffer=%d, period=%d\n",
		priv_data->bufsize, period_bytes);
	return -EINVAL;
}

void zxi2s_dma_start(struct zxi2s_stream_data *priv_data)
{
	
	if(priv_data->direction){

	priv_data->running = true;
			
	/* start a transfer---output*/
	zxi2s_reg_writeb(priv_data,ZXI2S_REG_MODRST, ZXI2S_REG_MODRST & (~MODRST_DAC));
	zxi2s_reg_writeb(priv_data,ZXI2S_REG_MODRST, ZXI2S_REG_MODRST | MODRST_DAC);

	/* Output Stream DMA Stop Buffer Function Control*/
	zxi2s_reg_writeb(priv_data,ZXI2S_REG_COMSET, ZXI2S_REG_COMSET & (~COMSET_STOP_DAC_BUF));

	/*no exchange little/big endian in FIFO*/
	zxi2s_reg_writeb(priv_data,ZXI2S_REG_DACFIFOCFG, ZXI2S_REG_DACFIFOCFG & (~DACFIFOCFG_ENDIAN_EXCHANGE));

	/*no exchange unsigned/signed format*/
	zxi2s_reg_writeb(priv_data,ZXI2S_REG_DACFIFOCFG, ZXI2S_REG_DACFIFOCFG & (~DACFIFOCFG_SIGN_EXCHANGE));

	/*sample quantization=32*/
	zxi2s_reg_writeb(priv_data,ZXI2S_REG_DACFIFOCFG,ZXI2S_REG_DACFIFOCFG |(DACFIFOCFG_POP_LEN & 0X10 ));

	/*2 CHANNEL*/
	zxi2s_reg_writeb(priv_data,ZXI2S_REG_DACFIFOCFG,ZXI2S_REG_DACFIFOCFG | (DACFIFOCFG_CHN_NUM & 0X10 ));


	/*I2S output data/WS change on negative edge of serial clock*/
	zxi2s_reg_writeb(priv_data,ZXI2S_REG_DACIFCFG,ZXI2S_REG_DACIFCFG & (~DACIFCFG_POSTIVE_EDGE ));

	/*output data left/right channel transmitted on low-level of WS*/
	zxi2s_reg_writeb(priv_data,ZXI2S_REG_DACIFCFG,ZXI2S_REG_DACIFCFG & (~DACIFCFG_POSTIVE_EDGE ));

	/*mclk/bclk/ws setting on cpu_dai hwparams func*/

	/*padding=8BITS*/
	zxi2s_reg_writeb(priv_data,ZXI2S_REG_DACIFCFG,ZXI2S_REG_DACIFCFG | (DACIFCFG_SDPAD & 0X1000));

	/* start output transfer*/
	zxi2s_reg_writeb(priv_data,ZXI2S_REG_DACIFCFG,ZXI2S_REG_DACIFCFG |DACIFCFG_START);
	}

	else{
	
	priv_data->running = true;
	/* start a transfer---input*/
	zxi2s_reg_writeb(priv_data,ZXI2S_REG_MODRST, ZXI2S_REG_MODRST & (~MODRST_ADC));
	zxi2s_reg_writeb(priv_data,ZXI2S_REG_MODRST,  ZXI2S_REG_MODRST | MODRST_ADC);

	/*I2S input data/WS change on negative edge of serial clock*/
	zxi2s_reg_writeb(priv_data,ZXI2S_REG_DACIFCFG,ZXI2S_REG_DACIFCFG & (~DACIFCFG_POSTIVE_EDGE ));

	/*I2S input data/WS change on negative edge of serial clock*/
	zxi2s_reg_writeb(priv_data,ZXI2S_REG_ADCIFCFG, ZXI2S_REG_ADCIFCFG | ADCIFCFG_NEGATIVE_EDGE );

	/*left/right channel data sampled on low-level/high-level of WS*/
	zxi2s_reg_writeb(priv_data,ZXI2S_REG_ADCIFCFG,ZXI2S_REG_ADCIFCFG | ADCIFCFG_LEFT_IN_LOW );

	/*16-bit mode*/
	zxi2s_reg_writeb(priv_data,ZXI2S_REG_ADCFIFOCFG,ZXI2S_REG_ADCFIFOCFG | ADCFIFOCFG_POP_LEN );

	/* start input transfer*/
	zxi2s_reg_writeb(priv_data,ZXI2S_REG_ADCFIFOCFG,ZXI2S_REG_ADCFIFOCFG | ADCFIFOCFG_START);
	
	}
	

}
void zxi2s_dma_stop(struct zxi2s_stream_data *priv_data)
{
	if(priv_data->direction){

	priv_data->running = 0;

	/* stop output transfer*/
	zxi2s_reg_writeb(priv_data,ZXI2S_REG_DACIFCFG,ZXI2S_REG_DACIFCFG & (~DACIFCFG_START));

	/* reset output func*/
	zxi2s_reg_writeb(priv_data,ZXI2S_REG_MODRST,ZXI2S_REG_MODRST | MODRST_DAC);
	}
	else{

	priv_data->running = 0;
	/* stop input transfer*/
	zxi2s_reg_writeb(priv_data,ZXI2S_REG_ADCFIFOCFG,ZXI2S_REG_ADCFIFOCFG & (~ADCFIFOCFG_START));

	/* reset input func*/
	zxi2s_reg_writeb(priv_data,ZXI2S_REG_MODRST, ZXI2S_REG_MODRST | MODRST_ADC);

	}
}

static int zxi2s_dma_open(struct snd_soc_component *component ,struct snd_pcm_substream *substream)
{
	int err,res;
	struct zxi2s_stream_data *priv_data；
	struct zxi2s_stream_data *res=null；
	
	struct snd_pcm_runtime *runtime = substream->runtime;
	
//	struct snd_soc_pcm_runtime *prtd = substream->private_data;
//	component = snd_soc_rtdcom_lookup(prtd,ZXI2S_DMA_NAME);

	struct zxi2s_dma *i2sdma =  dev_get_drvdata(component->dev);
	
	
	list_for_each_entry(priv_data, &i2sdma->stream_list, list) {
		if (priv_data->direction != substream->stream)
			continue;
		else res = priv_data；
			break;
		}

	res->substream = substream;// irq handler中elapsed需要从这里获取

	//先通过参数，系统分配runtime hw的值，然后最终还是要和app传下来的值做比较，以及我自己特定的hw值，
	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		runtime->hw = zxi2s_pcm_hardware_playback;
		}
	else{
		runtime->hw = zxi2s_pcm_hardware_capture;
		}

	runtime->private_data = res;


	//enable BDL position buffer
	zxi2s_reg_writeb(res,ZXI2S_REG_DPLBASE, ZXI2S_REG_DPLBASE | DPLBASE_EN);


	snd_pcm_hw_constraint_integer(runtime, SNDRV_PCM_HW_PARAM_PERIODS);
	return 0;
}

static int zxi2s_dma_close(struct snd_soc_component *component, struct snd_pcm_substream *substream)
{

	//amd 主要在这里要关中断

	return 0;
}




static int zxi2s_dma_trigger(struct snd_soc_component *component,struct snd_pcm_substream *substream,int cmd)
{
	int ret;
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct zxi2s_stream_data *priv_data = runtime->private_data;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
	case SNDRV_PCM_TRIGGER_RESUME:
		zxi2s_dma_start(priv_data);
		break;

	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
	case SNDRV_PCM_TRIGGER_SUSPEND:
		zxi2s_dma_stop(priv_data);
		break;

	default:
		ret = -EINVAL;
	}
	return ret;
}

static snd_pcm_uframes_t zxi2s_dma_pointer(struct snd_soc_component *component ,struct snd_pcm_substream *substream)
{
	unsigned int pos;

	struct snd_pcm_runtime *runtime = substream->runtime;
	struct zxi2s_stream_data *priv_data = runtime->private_data;

	/* use the position buffer 
	在snd_hdac_bus_alloc_stream_pages()函数里面，在分配完posbuf后，
	将area赋值给了stream的le32* posbuf,这里就直接用，因为就一个stream
	*/
	pos = (__le32 *)(priv_data->posbuf.area);
	
	return bytes_to_frames(substream->runtime,pos);
}

static int zxi2s_dma_hw_free(struct snd_soc_component *component,struct snd_pcm_substream *substream)
{
	return snd_pcm_lib_free_pages(substream);

}


static int zxi2s_dma_hw_params(struct snd_soc_component *component ,struct snd_pcm_substream *substream,
			      struct snd_pcm_hw_params *params)
{
	return snd_pcm_lib_malloc_pages(substream, params_buffer_bytes(params));

}				  

/*
*在az的pcm_prepare函数里面，先在snd_hdac_stream_set_params这个函数里面确定buffersize
*那几个东西，然后调用setup_period函数，这里面就用到了bdl.area,说明在这之前就申请好了
*
*然后在snd_hdac_stream_setup这个函数里面写LVI的值
*/
static int zxi2s_dma_prepare(struct snd_soc_component *component ,struct snd_pcm_substream *substream)
{
	int err;
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct zxi2s_stream_data *priv_data = runtime->private_data;

	priv_data->bufsize = snd_pcm_lib_buffer_bytes(substream);//alsa会根据min/max自己计算
	priv_data->period_bytes = snd_pcm_lib_period_bytes(substream);
	priv_data->frags = 4;

	err = snd_i2s_stream_setup_periods(priv_data);
	if (err < 0)
		return err;
	
	//然后根据这些从app传到runtime再拉下来的数据，计算分频等波特率

	/* program the position buffer */
	zxi2s_reg_writel(priv_data,ZXI2S_REG_DPLBASE, (u32)priv_data->posbuf.addr | DPLBASE_EN);
	zxi2s_reg_writel(priv_data,ZXI2S_REG_DPUBASE, ZXI2S_REG_DPUBASE | upper_32_bits(priv_data->posbuf.addr));

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
	{
		/* program the stream LVI (last valid index) of the BDL */
		zxi2s_reg_writel(priv_data, ZXI2S_REG_OSDLVI, priv_data->frags-1);
		/* program the BDL address */
		zxi2s_reg_writel(priv_data,ZXI2S_REG_OBDLUBASE, upper_32_bits(priv_data->bdl.addr));
		zxi2s_reg_writel(priv_data,ZXI2S_REG_OBDLLBASE, ZXI2S_REG_OBDLLBASE | priv_data->bdl.addr);

	}
	else{
		/* program the stream LVI (last valid index) of the BDL */
		zxi2s_reg_writel(priv_data, ZXI2S_REG_ISDLVI, priv_data->frags-1);
		/* program the BDL address */
		zxi2s_reg_writel(priv_data,ZXI2S_REG_IBDLUBASE, upper_32_bits(priv_data->bdl.addr));
		zxi2s_reg_writel(priv_data,ZXI2S_REG_IBDLLBASE,  ZXI2S_REG_IBDLLBASE | priv_data->bdl.addr);
	}

	

	return 0;
}

static int zxi2s_dma_mmap(struct snd_soc_component *component ,struct snd_pcm_substream *substream,struct vm_area_struct *area)
{

	return snd_pcm_lib_default_mmap(substream, area);
}



static int zxi2s_dma_new(struct snd_soc_component *component ,struct snd_soc_pcm_runtime *rtd)
{
	int i,err;
	component = snd_soc_rtdcom_lookup(rtd,ZXI2S_DMA_NAME);
	struct zxi2s_dma *i2sdma =  dev_get_drvdata(component->dev);
	
	for (i = 0; i<2; i++) {
		struct zxi2s_stream_data *priv_data = kzalloc(sizeof(struct zxi2s_stream_data), GFP_KERNEL);
		if (!priv_data)
			return -ENOMEM;

		priv_data->direction=i;
		priv_data->regs = i2sdma->regs;

		list_add_tail(&priv_data->list, &i2sdma->stream_list);
	}

	/* allocate memory for the BDL for each stream */
	list_for_each_entry(prvi_data, &i2sdma->stream_list, list) {
		err = snd_dma_alloc_pages(SNDRV_DMA_TYPE_DEV, i2sdma->dev,4096, &priv_data->bdl);
		if (err < 0)
			return -ENOMEM;
	}

	/* allocate memory for the position buffer */
	err = snd_dma_alloc_pages(SNDRV_DMA_TYPE_DEV, i2sdma->dev,2*8, &priv_data->posbuf);

	list_for_each_entry(priv_data, &i2sdma->stream_list, list)
		priv_data->posbuf = (__le32 *)(i2sdma->posbuf.area + ~(priv_data->direction)* 8);


	snd_pcm_lib_preallocate_pages_for_all(rtd->pcm,
						  SNDRV_DMA_TYPE_DEV,
						  component->dev,  //这里是否要用parent?
						  64*1024,	
						  32*1024*1024);


	return 0;
}



/* TODO: */
static irqreturn_t zxi2s_dma_irq_handle(int irq, void *dev_id)
{
	int err,res;
	u8 sd_status;
	struct zxi2s_stream_data *priv_data；

	//首先要打开Rx08两个中断总控制bit
	
	struct zxi2s_dma *i2sdma = (struct zxi2s_dma *)dev_id;


	list_for_each_entry(priv_data, &i2sdma->stream_list, list) {

		if(!(priv_data->direction)){	//playback
			sd_status = zxi2s_reg_readb(priv_data,ZXI2S_REG_OSDINTS);
			zxi2s_reg_updatew(priv_data,ZXI2S_REG_OSDINTS,OSDINTS_IOC,0);
			if(priv_data->running | (sd_status & OSDINTS_IOC){
				spin_unlock(&i2sdma->zxi2s_reg_lock);
				snd_pcm_period_elapsed(priv_data->substream);
				spin_lock(&i2sdma->zxi2s_reg_lock);
				}
			}
		else {
			sd_status = zxi2s_reg_readb(priv_data,ZXI2S_REG_ISDINTS);
			zxi2s_reg_updatew(priv_data,ZXI2S_REG_ISDINTS,OSDINTS_IOC,0);
			if(priv_data->running | (sd_status & ISDINTS_IOC){
				spin_unlock(&i2sdma->zxi2s_reg_lock);
				snd_pcm_period_elapsed(priv_data->substream);
				spin_lock(&i2sdma->zxi2s_reg_lock);
				}
			}
		}
	
	
	//如果发生Descriptor Error & FIFO Error就直接停止
	if(zxi2s_reg_readl(priv_data, ZXI2S_REG_ISDINTS)& ISDINTS_ABORT || 
		zxi2s_reg_readl(priv_data, ZXI2S_REG_ISDINTS)& ISDINTS_XRUN ||
		zxi2s_reg_readl(priv_data, ZXI2S_REG_OSDINTS)& ISDINTS_ABORT ||
		zxi2s_reg_readl(priv_data, ZXI2S_REG_OSDINTS)& ISDINTS_XRUN)
			dev_info(i2sdma->dev, "%s +%d :%s err happend \n",__FILE__, __LINE__, __func__);

 /`      
	dev_info(i2sdma->dev, "%s +%d :%s \n",__FILE__, __LINE__, __func__);
	return IRQ_HANDLED;
}


//kernel version different big,use construct for pcm_new
static const struct snd_soc_component_driver zxi2s_dma_drv = {
                .name           = ZXI2S_DMA_NAME,
        .open           = zxi2s_dma_open,
        .close          = zxi2s_dma_close,
        .hw_params      = zxi2s_dma_hw_params,
        .hw_free        = zxi2s_dma_hw_free,
        .prepare        = zxi2s_dma_prepare,
        .trigger        = zxi2s_dma_trigger,
        .pointer        = zxi2s_dma_pointer,
        .mmap           = zxi2s_dma_mmap,
                .pcm_construct = zxi2s_dma_new,
};







static int zxi2s_dma_probe(struct platform_device *pdev)
{
	int error = 0;
	struct resource *res;
	struct zxi2s_dma *i2sdma;

	i2sdma = devm_kzalloc(&pdev->dev, sizeof(*i2sdma), GFP_KERNEL);
	if (IS_ERR(i2sdma)) {
		dev_err(&pdev->dev, "devm_kzalloc FAILED\n");
		return -ENOMEM;
	}

	/* get mmio */
	i2sdma->regs = devm_platform_get_and_ioremap_resource(pdev, 0, &res);
	if (IS_ERR(i2sdma->regs)) {
		dev_err(&pdev->dev, "get regs base failed\n");
		return -ENOMEM;
	}

	/* get irq */
	i2sdma->irq = platform_get_irq(pdev, 0);
	if (i2sdma->irq < 0) {
		dev_err(&pdev->dev, "get irq failed\n");
		return i2sdma->irq;
	}

	i2sdma->dev = &pdev->dev;
	platform_set_drvdata(pdev, (void *)i2sdma);
	dev_set_drvdata(&pdev->dev, i2sdma);

	/* request irq */
	if (devm_request_irq(&pdev->dev, i2sdma->irq, zxi2s_dma_irq_handle,
				IRQF_SHARED, pdev->name, i2sdma)) {
		dev_err(i2sdma->dev, "i2sdma IRQ%d allocate failed.\n",i2sdma->irq);
		return -ENODEV;
	}

	error = devm_snd_soc_register_component(&pdev->dev,
						 &zxi2s_dma_drv, NULL, 0);
	if (error) {
		dev_err(&pdev->dev, "Fail to register ALSA platform device\n");
		return error;
	}
'/'
	pm_runtime_set_autosuspend_delay(&pdev->dev, 10000);
	pm_runtime_use_autosuspend(&pdev->dev);
	pm_runtime_enable(&pdev->dev);

	dev_info(&pdev->dev, "driver registered.\n");
	return 0;
}

static int zxi2s_dma_remove(struct platform_device *pdev)
{
	struct zxi2s_dma *i2sdma = platform_get_drvdata(pdev);

	devm_free_irq(&pdev->dev, i2sdma->irq, i2sdma);
	platform_set_drvdata(pdev, NULL);
	devm_kfree(&pdev->dev, i2sdma);

	dev_info(&pdev->dev, "driver removed.\n");
	return 0;
}


/* Initialize and bring ACP hardware to default state. */
static int zxi2s_dma_init(void __iomem *zxi2s_mmio)+
{
	//通过T/P spec流程来
	return 0;
}




static int zxi2s_pcm_resume(struct device *dev)
{
	int status;
	struct zxi2s_stream_data *priv_data;
	struct zxi2s_dma *i2sdma = dev_get_drvdata(dev);

	status = zxi2s_dma_init(i2sdma->regs);

	if (status) {
		dev_err(dev, "zxi2s Init failed status:%d\n", status);
	}

	return status;

}

static int zxi2s_pcm_runtime_suspend(struct device *dev)
{
	int status;
	//struct zxi2s_dma *i2sdma = dev_get_drvdata(dev);


	//disable INTR
	
	return 0;		
}


static int zxi2s_pcm_runtime_resume(struct device *dev)	
{
	int status;
	struct zxi2s_dma *i2sdma = dev_get_drvdata(dev);
	status = zxi2s_dma_init(i2sdma->regs);
	if (status) {
		dev_err(dev, "zxi2s Init failed status:%d\n", status);
		return status;
	}

	//enbale INTR
	
	return 0;
}


static const struct dev_pm_ops zxi2s_pm_ops = {
	.resume = zxi2s_pcm_resume,
	.runtime_suspend = zxi2s_pcm_runtime_suspend,
	.runtime_resume = zxi2s_pcm_runtime_resume,
};



static struct platform_driver zxi2s_dma_driver = {
	.probe  = zxi2s_dma_probe,
	.remove = zxi2s_dma_remove,
	.driver = {
		.name = ZXI2S_DMA_NAME,
		.owner = THIS_MODULE,
		.pm = &zxi2s_pm_ops,
	},
};

module_platform_driver(zxi2s_dma_driver);
MODULE_AUTHOR("hanshu@zhaoxin.com");
MODULE_DESCRIPTION("ZHAOXIN I2S dma driver");
MODULE_VERSION(DRIVER_VERSION);
MODULE_LICENSE("GPL v2");
