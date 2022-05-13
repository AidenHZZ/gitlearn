// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *      zx_i2s_cpu.c - Zhaoxin I2S cpu driver
 *
 *      Copyright(c) 2021 Shanghai Zhaoxin Corporation. All rights reserved.
 *
*/
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <sound/soc.h>
#include "zx_i2s.h"

struct zxi2s_cpu {
	/* controller resources information */
	struct platform_device *pdev;
    struct pci_dev   *pci;
    struct device    *dev;
    struct mutex lock;
    struct snd_soc_card *soc_card;

	void __iomem *regs;
	int master;//zhuangzhuang add 2021/11/17
	int active;
	int lrck;
	int rate;
	int pad_bits;

};

const static int PLL_TABLE[41][5] = {
    /* 48 bit index = 0, num = 13 */
    {48, 192000, 2 | 0 << 4, 0x1, 1},
    {48,  96000, 2 | 2 << 4, 0x0, 1},
    {48,  48000, 2 | 1 << 4, 0x2, 1},
    {48,  32000, 2 | 5 << 4, 0x2, 1},
    {48,  24000, 2 | 2 << 4, 0x2, 1},
    {48,  16000, 2 | 2 << 4, 0xA, 1},
    {48,   8000, 2 | 2 << 4, 0xB, 1},
    {48,   6000, 2 | 2 << 4, 0x4, 1},
    {48, 176400, 2 | 0 << 4, 0x1, 0},
    {48,  88200, 2 | 2 << 4, 0x0, 0},
    {48,  44100, 2 | 1 << 4, 0x2, 0},
    {48,  22050, 2 | 2 << 4, 0x2, 0},
    {48,  11025, 2 | 2 << 4, 0x3, 0},
    /* 32 bit index = 13, num = 14 */
    {32, 192000, 1 | 4 << 4, 0x1, 1},
    {32, 144000, 1 | 2 << 4, 0x0, 1},
    {32,  96000, 1 | 5 << 4, 0x1, 1},
    {32,  48000, 1 | 5 << 4, 0x2, 1},
    {32,  32000, 1 | 5 << 4, 0xA, 1},
    {32,  24000, 1 | 2 << 4, 0xA, 1},
    {32,  16000, 1 | 5 << 4, 0xB, 1},
    {32,   8000, 1 | 6 << 4, 0xB, 1},
    {32,   6000, 1 | 2 << 4, 0xC, 1},
    {32, 176400, 1 | 4 << 4, 0x1, 0},
    {32,  88200, 1 | 5 << 4, 0x1, 0},
    {32,  44100, 1 | 5 << 4, 0x2, 0},
    {32,  22050, 1 | 2 << 4, 0xA, 0},
    {32,  11025, 1 | 2 << 4, 0xB, 0},
    /* 64 bit index = 27, num = 14 */
    {64, 192000, 0 | 4 << 4, 0x0, 1},
    {64, 144000, 0 | 0 << 4, 0x1, 1},
    {64,  96000, 0 | 4 << 4, 0x1, 1},
    {64,  48000, 0 | 5 << 4, 0x1, 1},
    {64,  32000, 0 | 5 << 4, 0x9, 1},
    {64,  24000, 0 | 5 << 4, 0x2, 1},
    {64,  16000, 0 | 5 << 4, 0xA, 1},
    {64,   8000, 0 | 5 << 4, 0xB, 1},
    {64,   6000, 0 | 2 << 4, 0xB, 1},
    {64, 176400, 0 | 4 << 4, 0x0, 0},
    {64,  88200, 0 | 4 << 4, 0x1, 0},
    {64,  44100, 0 | 5 << 4, 0x1, 0},
    {64,  22050, 0 | 5 << 4, 0x2, 0},
    {64,  11025, 0 | 2 << 4, 0xA, 0}
};

static int zxi2s_setup_pll(struct zxi2s_cpu *dev)
{

    u8 start, end, index;

    /* get proper regs */
    if (dev->lrck == 24) {
        start = 0;
        end = start + 13;
    } else if (dev->lrck == 16) {
        start = 13;
        end = start + 14;
    } else {
        start = 27;
        end = start + 14;
    }

    for (index = start; index < end; index ++) {
        if (dev->rate == PLL_TABLE[index][1]) {
            break;
        }
    }

    if (index >= end) {
        return -ENODEV;
    }

    /* set ws, rate, clock source related regs */
    zxi2s_reg_writeb(dev, ZXI2S_REG_DACIFCF1, PLL_TABLE[index][2]); /* LRDIV | BDIV */
    zxi2s_reg_updateb(dev, ZXI2S_REG_DACIFCF2, MDIV, PLL_TABLE[index][3]); /* MDIV */
    zxi2s_reg_updateb(dev, ZXI2S_REG_COMSET, COMSET_SEL_PLLEA, PLL_TABLE[index][4]); /* PLLEA */

    /* set pad bit reg */
    if (direction == SNDRV_PCM_STREAM_PLAYBACK)
        zxi2s_reg_writeb(dev, ZXI2S_REG_DACIFCF0, dev->pad_bits);
    else
        zxi2s_reg_updateb(dev, ZXI2S_REG_ADCIFCFG, ADCIFCFG_SDPAD, dev->pad_bits);

    return 0;
}




static void zxi2s_start(struct zxi2s_cpu *dev,
		      struct snd_pcm_substream *substream)
{

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK){
		zxi2s_reg_writel(dev,ZXI2S_REG_INTCTRL,INTCTRL_OUT);
		zxi2s_reg_writel(dev, ZXI2S_REG_OSDINTE, OSDINTE_ALL);
		}
	else{
		//i2s_enable_irqs
		zxi2s_reg_writel(dev,ZXI2S_REG_INTCTRL,INTCTRL_IN);
		zxi2s_reg_writel(dev, ZXI2S_REG_ISDINTE, OSDINTE_ALL);
		}

}

static void zxi2s_stop(struct zxi2s_cpu *dev,
		struct snd_pcm_substream *substream)
{
				  
	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK){
		zxi2s_reg_writel(dev,ZXI2S_REG_INTCTRL,~INTCTRL_OUT);
		zxi2s_reg_writel(dev, ZXI2S_REG_OSDINTE, 0);
		}
	 else{
	 	zxi2s_reg_writel(dev,ZXI2S_REG_INTCTRL,~INTCTRL_IN);
		zxi2s_reg_writel(dev, ZXI2S_REG_ISDINTE, 0);
	 	}

}

static int zxi2s_cpu_startup(struct snd_pcm_substream *substream,
		struct snd_soc_dai *cpu_dai)
{
	//设置dai DMA相关信息，我们不需要

	return 0;
}



static int zxi2s_cpu_hw_params(struct snd_pcm_substream *substream,
		struct snd_pcm_hw_params *params, struct snd_soc_dai *cpu_dai)
{
	struct zxi2s_cpu *i2scpu = snd_soc_dai_get_drvdata(cpu_dai);

	
	struct snd_pcm_runtime *runtime = substream->runtime;
	   snd_pcm_format_t format = params_format(params);
	   u8 fifo_cfg = 0;
	   u8 width, channels;
	   u8 packed_bits; /* Samples are packed in cyclic buffer which are 8 bits, 16 bits, or 32 bits wide */
	   int ret;
	
	//P-spec 1.2.6 always output MCLK
	zxi2s_reg_writeb(i2scpu, ZXI2S_REG_COMSET, COMSET_MCLK_ALWAYS);

	/* get lrck: word length */
	   width = snd_pcm_format_width(format);
	   switch (width) {
		   case 8:
			   i2scpu->lrck = 16;
			   packed_bits = 8;
			   break;
		   case 16:
			   i2scpu->lrck = 16;
			   packed_bits = 16;
			   break;
		   case 20:
		   case 24:
			   i2scpu->lrck = 24; /* TODO: debug mode use 32 bit instead */
			   packed_bits = 32;
			   break;
		   case 32:
			   i2scpu->lrck = 32;
			   packed_bits = 32;
			   break;
		   default:
			   dev_err(dai->dev, "unsupported width: %d\n", width);
			   return -EINVAL;
	   }
	
	   /* set pll values to regs */
	   i2scpu->pad_bits = i2scpu->lrck - width;
	   i2scpu->rate = params_rate(params);
	   if(zxi2s_setup_pll(i2scpu)) {
		   dev_err(dai->dev, "unsupported rate: %d\n",
				   i2scpu->rate);
		   return -EINVAL;
	   }
	
	   if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		   /* set sample quantization for playback */
		   if (packed_bits == 16)
			   fifo_cfg |= 1 << 4;
		   else if (packed_bits == 32)
			   fifo_cfg |= 2 << 4;
	
		   /* get signed format */
		   if (snd_pcm_format_signed(runtime->format))
			   fifo_cfg |= DACFIFOCFG_SIGN_EXCHANGE;
		   else
			   fifo_cfg &= ~DACFIFOCFG_SIGN_EXCHANGE;
	
		   /* get big/little endian */
		   if (snd_pcm_format_big_endian(runtime->format))
			   fifo_cfg |= DACFIFOCFG_ENDIAN_EXCHANGE;
		   else
			   fifo_cfg &= ~DACFIFOCFG_ENDIAN_EXCHANGE;
	
		   /* get channel number */
		   channels = params_channels(params);
		   if (channels == 2 || channels == 1) {
			   fifo_cfg |= channels;
		   } else {
			   dev_err(dai->dev, "%d channels not supported\n", channels);
			   return -EINVAL;
		   }
	
		   /* set format to regs */
		   zxi2s_reg_writeb(i2scpu, ZXI2S_REG_DACFIFOCFG, fifo_cfg);
	   } else {
		   /* set pop size for capture or sample quantization for playback */
		   if (packed_bits == 16)
			   zxi2s_reg_writeb(i2scpu, ZXI2S_REG_ADCFIFOCFG, 2);
		   else if (packed_bits == 32)
			   zxi2s_reg_writeb(i2scpu, ZXI2S_REG_ADCFIFOCFG, 0);
		   else ; /* TODO: capture only support 16, 32 */
	   }
	
	   /* set master or slave mode */
	   if (i2scpu->master)
		   zxi2s_reg_updateb(i2scpu, ZXI2S_REG_COMSET, COMSET_SLV_MODE, 0);
	   else
		   zxi2s_reg_updateb(i2scpu, ZXI2S_REG_COMSET, COMSET_SLV_MODE, COMSET_SLV_MODE);
	
	   return 0;

}



static void zxi2s_cpu_shutdown(struct snd_pcm_substream *substream,
		struct snd_soc_dai *cpu_dai)
{
	snd_soc_dai_set_dma_data(cpu_dai, substream, NULL);
}

static int zxi2s_cpu_prepare(struct snd_pcm_substream *substream,
			  struct snd_soc_dai *cpu_dai)
{
	struct zxi2s_cpu *i2scpu = snd_soc_dai_get_drvdata(cpu_dai);

	//if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		//zxi2s_reg_writeb(i2scpu, TXFFR, 1);
	//else
		//zxi2s_reg_writeb(i2scpu, RXFFR, 1);

	return 0;
}

static int zxi2s_cpu_trigger(struct snd_pcm_substream *substream,
		int cmd, struct snd_soc_dai *cpu_dai)
{
	struct zxi2s_cpu *i2scpu = snd_soc_dai_get_drvdata(cpu_dai);
	int ret = 0;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		i2scpu->active++;
		zxi2s_start(i2scpu, substream);
		break;
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		i2scpu->active--;
		zxi2s_stop(i2scpu, substream);
		break;
	default:
		ret = -EINVAL;
		break;
	}
	return ret;
}

//codec_dai格式设置
/*
set_sysclk:codec_dai系统时钟设置，当上层打开pcm设备时，需要回调该接口设置codec的系统时钟,codec才能正常工作；
set_pll:codec FLL设置，codec一般接了一个MCKL输入时钟，回调该接口基于mclk来产生Codec FLL时钟，接着codec_dai的sysclk,bclk,lrclk均可从FLL分频出来(假设codec作为master);
set_fmt:codec_dai格式设置，具体见soc-dai.h;
SND_SOC_DAIFMT_I2S:音频数据是I2S格式，常用于多媒体音频；
SND_SOC_DAIFMT_DSP_A:音频数据是PCM格式，常用于通话语音；
SND_SOC_DAIFMT_CBM_CFM:codec作为master,BCLK和LRCLK由codec提供；
SND_SOC_DAIFMT_CBS_CFS:codec作为slave,BCLK和LRCLK由Soc/CPU提供；
hw_params:codec_dai硬件参数设置，根据上层设定的声道数，采样率，数据格式，来配置codec_dai相关寄存器。
fmt & SND_SOC_DAIFMT_FORMAT_MASK这个不用看吗，肯定是SND_SOC_DAIFMT_I2S

*/
//soc_pcm.c的hw_params先会调用到dai_link的hw_params,然后调用codec的hw_params,然后调用cpu的
static int zxi2s_cpu_set_fmt(struct snd_soc_dai *cpu_dai, unsigned int fmt)
{

	//设置传输的格式和主从模式的选择
	struct zxi2s_cpu *i2scpu = snd_soc_dai_get_drvdata(cpu_dai);
	int ret = 0;

	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBM_CFM:
		i2scpu->master = 1;
		//zxi2s_reg_writeb(i2scpu, ZXI2S_REG_COMSET,ZXI2S_REG_COMSET & (~COMSET_SLV_MODE ));
		break;
	case SND_SOC_DAIFMT_CBS_CFS:
		i2scpu->master = 0;
		//zxi2s_reg_writeb(i2scpu, ZXI2S_REG_COMSET,ZXI2S_REG_COMSET  | COMSET_SLV_MODE);
		break;
	case SND_SOC_DAIFMT_CBM_CFS:
	case SND_SOC_DAIFMT_CBS_CFM:
		ret = -EINVAL;
		break;
	default:
		dev_dbg(i2scpu->dev, "zx : Invalid master/slave format\n");
		ret = -EINVAL;
		break;
	}
	return ret;

}

static const struct snd_soc_dai_ops zxi2s_cpu_dai_ops = {
        .startup        = zxi2s_cpu_startup,
        .shutdown       = zxi2s_cpu_shutdown,
        .hw_params      = zxi2s_cpu_hw_params,//设置硬件参数（必须）
        .prepare        = zxi2s_cpu_prepare,
        .trigger        = zxi2s_cpu_trigger,//触发条件（必须）
        .set_fmt        = zxi2s_cpu_set_fmt,//设置dai的格式
};

static struct snd_soc_dai_driver zxi2s_cpu_dai_drv = {
                .name   = ZXI2S_CPU_NAME,
        .ops = &zxi2s_cpu_dai_ops,

        .playback = {
                .rates = SNDRV_PCM_RATE_8000_96000,
                .formats = SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S8 |
                        SNDRV_PCM_FMTBIT_U8 | SNDRV_PCM_FMTBIT_S24_LE |
                        SNDRV_PCM_FMTBIT_S32_LE,
                .channels_min = 2,
                .channels_max = 2,
                .rate_min = 8000,
                .rate_max = 96000,
        },
        .capture = {
                .rates = SNDRV_PCM_RATE_8000_48000,
                .formats = SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S8 |
                        SNDRV_PCM_FMTBIT_U8 | SNDRV_PCM_FMTBIT_S24_LE |
                        SNDRV_PCM_FMTBIT_S32_LE,
                .channels_min = 2,
                .channels_max = 2,
                .rate_min = 8000,
                .rate_max = 48000,
        },
};

static const struct snd_soc_component_driver zxi2s_cpu_drv = {
        .name           = ZXI2S_CPU_NAME,
#if 0
        .open           = zxi2s_cpu_open,
        .close          = zxi2s_cpu_close,
        .pcm_construct  = zxi2s_cpu_new,
#endif
};


static int zxi2s_cpu_probe(struct platform_device *pdev)
{

	int error = 0;
	struct resource *res;
	struct zxi2s_cpu *i2scpu;

	/***************zhuang add 2021/11/17**************/
//	const struct zx_platform_data *pdata = pdev->dev.platform_data;//定义在zx_i2s.h中
//	i2scpu->capability = pdata->cap;//可判断几种mode，amd在这个probe里利用这个设定master mode下的clk等信息


	i2scpu = devm_kzalloc(&pdev->dev, sizeof(*i2scpu), GFP_KERNEL);
	if (IS_ERR(i2scpu)) {
		dev_err(&pdev->dev, "devm_kzalloc FAILED\n");
		return -ENOMEM;
	}

	/* get mmio */
	i2scpu->regs = devm_platform_get_and_ioremap_resource(pdev, 0, &res);
	if (IS_ERR(i2scpu->regs)) {
		dev_err(&pdev->dev, "get regs base failed\n");
		return -ENOMEM;
	}

	i2scpu->dev = &pdev->dev;

	platform_set_drvdata(pdev, (void *)i2scpu);
	dev_set_drvdata(&pdev->dev, i2scpu);

	//devm是一种资源管理的方式，不用考虑资源释放，内核会内部做好资源回收。
	error = devm_snd_soc_register_component(&pdev->dev, &zxi2s_cpu_drv, 
						 &zxi2s_cpu_dai_drv, 0);
	if (error) {
		dev_err(&pdev->dev, "Fail to register ALSA platform device\n");
		return error;
	}

	//pm_runtime_set_autosuspend_delay(&pdev->dev, 10000);
	//pm_runtime_use_autosuspend(&pdev->dev);
	//pm_runtime_enable(&pdev->dev);

	dev_info(&pdev->dev, "driver registered.\n");
	return 0;
}

static int zxi2s_cpu_remove(struct platform_device *pdev)
{
	struct zxi2s_cpu *i2scpu = platform_get_drvdata(pdev);

	platform_set_drvdata(pdev, NULL);
	devm_kfree(&pdev->dev, i2scpu);

	dev_info(&pdev->dev, "driver removed.\n");
	return 0;
}

static struct platform_driver zxi2s_cpu_driver = {
	.probe  = zxi2s_cpu_probe,
	.remove = zxi2s_cpu_remove,
	.driver = {
		.name = ZXI2S_CPU_NAME,
		.owner = THIS_MODULE,
	},
};

module_platform_driver(zxi2s_cpu_driver);
MODULE_AUTHOR("hanshu@zhaoxin.com");
MODULE_DESCRIPTION("ZHAOXIN I2S cpu driver");
MODULE_VERSION(DRIVER_VERSION);
MODULE_LICENSE("GPL v2");
