// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *      zx_i2s_rt5645.c - Zhaoxin I2S machine driver
 *		主要做下面几件事：
 *		1，分配注册一个名为zx-rt5650的平台设备
 *		2，该平台设备有个私有数据结构：snd_soc_card,其中snd_soc_dai_link结构被用来决定ASOC各部分的驱动
 *
 *      Copyright(c) 2021 Shanghai Zhaoxin Corporation. All rights reserved.
 *
*/
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/acpi.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <sound/core.h>
#include <sound/jack.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/soc-acpi.h>
#include "zx_i2s.h"
#include "5645.h"

static struct snd_soc_jack headset_jack;

/* machine's private date, not used yet */
struct zxi2s_mc_private {
	struct snd_soc_jack jack;
	char codec_name[SND_ACPI_I2C_ID_LEN];
	struct clk *mclk;
};


/*
codec_dai 硬件参数设置，根据上层声道、采样率、数据格式，来配置 codec_dai 寄存器。
其中 snd_soc_dai_set_fmt() 实际上会调用 cpu_dai 或 codec_dai 的 set_fmt() 回调， 
snd_soc_dai_set_pll()、snd_soc_dai_set_sysclk() 也类似。

*/
static int zx_aif1_hw_params(struct snd_pcm_substream *substream,
			     struct snd_pcm_hw_params *params)
{
	int ret = 0;
	struct snd_soc_pcm_runtime *rtd = asoc_substream_to_rtd(substream);
	struct snd_soc_dai *codec_dai = asoc_rtd_to_codec(rtd,0);
	//struct snd_soc_dai *cpu_dai = asoc_rtd_to_cpu(rtd,0);

	int mclk;
	int zx_pll_1=36864000;
	int zx_pll_2=33868800;
	
/*仿照rockchip_rt5645,没有对codec pll和sysclk的设置*/
	switch (params_rate(params)) {
	case 8000:
		mclk = zx_pll_1/12;
		break;
	case 24000:
		mclk = zx_pll_1/4;
		break;
	case 32000:
		mclk = zx_pll_1/3;
		break;		
	case 48000:
		mclk = zx_pll_1/2;
		break;			
	case 96000:
		mclk = zx_pll_1/2;
		break;	
	case 192000:
		mclk = zx_pll_1;
		break;		
	case 44100:
		mclk = zx_pll_2/2;
		break;
	default:
		return -EINVAL;
	}


	//here we set pll sorce as MCLK,pll in(MCLK?)=mclk,pllout=512fs
	ret = snd_soc_dai_set_pll(codec_dai, 0, RT5645_PLL1_S_MCLK,
				  mclk, params_rate(params) * 512);
	if (ret < 0) {
		dev_err(rtd->dev, "can't set codec pll: %d\n", ret);
		return ret;
	}
	
	//根据codec spec，sysclk can select from MCLK/pll,here we
	//choose pll as src(clk_id),and set sysclk output to 512fs
	ret = snd_soc_dai_set_sysclk(codec_dai, RT5645_SCLK_S_PLL1,
				params_rate(params) * 512, 0);
	if (ret < 0) {
		dev_err(rtd->dev, "can't set codec sysclk: %d\n", ret);
		return ret;
	}



/*
    //在codec spec里面，在slavemode里面也会设置PLL（例程也是=521fs）
	 	
	
	在soc-pcm.c中的soc_pcm_hw_params中，当app设置了hw参数，
	就会先调用这个dailink->hw_params函数，
	然后去配置cpu和codec dai_driver->ops->hw_params
	并在这个时候把rate/samplebits/channles 交给dai双方
	snd_soc_dai_set_fmt()实际上会调用cpu_dai或codec_dai的set_fmt回调，
	所以这些要cpu_dai和codec_dai设一致
	
	
	ret = snd_soc_dai_set_fmt(cpu_dai, SND_SOC_DAIFMT_I2S |  
            SND_SOC_DAIFMT_NB_NF | SND_SOC_DAIFMT_CBS_CFS);  
    if (ret < 0)  
        return ret;  
	ret = snd_soc_dai_set_fmt(codec_dai, SND_SOC_DAIFMT_I2S |  
            SND_SOC_DAIFMT_NB_NF | SND_SOC_DAIFMT_CBS_CFS);  
    if (ret < 0)  
        return ret;  

	//here we set pll sorce as MCLK,pll in(MCLK?)=24M,pllout=512fs
	ret = snd_soc_dai_set_pll(codec_dai, 0, RT5645_PLL1_S_MCLK,
				  24000000, params_rate(params) * 512);
	if (ret < 0) {
		dev_err(rtd->dev, "can't set codec pll: %d\n", ret);
		return ret;
	}
	
	//根据codec spec，sysclk can select from MCLK/pll,here we
	//choose pll as src(clk_id),and set sysclk output to 512fs
	ret = snd_soc_dai_set_sysclk(codec_dai, RT5645_SCLK_S_PLL1,
				params_rate(params) * 512, 0);
	if (ret < 0) {
		dev_err(rtd->dev, "can't set codec sysclk: %d\n", ret);
		return ret;
	}

*/

	return ret;
}

static int zx_init(struct snd_soc_pcm_runtime *runtime)
{
	struct snd_soc_card *card = runtime->card;
	int ret;

	/* Enable Headset and 4 Buttons Jack detection */
	ret = snd_soc_card_jack_new(card, "Headset Jack",
			SND_JACK_HEADPHONE | SND_JACK_MICROPHONE |
			SND_JACK_BTN_0 | SND_JACK_BTN_1 |
			SND_JACK_BTN_2 | SND_JACK_BTN_3,
			&headset_jack, NULL, 0);
	if (ret) {
		dev_err(card->dev, "New Headset Jack failed! (%d)\n", ret);
		return ret;
	}
	return rt5645_set_jack_detect(asoc_rtd_to_codec(runtime, 0)->component,
			&headset_jack,
			&headset_jack,
			&headset_jack);
}

// Widget 来描述一个声卡的功能部件 
//Mic Jack，代表麦克风
//Headphone Jack，代表 3.5 mm 耳机座
static const struct snd_soc_dapm_widget zx_dapm_widgets[] = {
	SND_SOC_DAPM_HP("Headphones", NULL),
	SND_SOC_DAPM_SPK("Speakers", NULL),
	SND_SOC_DAPM_MIC("Headset Mic", NULL),
	SND_SOC_DAPM_MIC("Int Mic", NULL),
};

/*
将 CPU DAI 和 Codec DAI 连接起来后，还需要设置 Codec 的 input 和 output
路径,对应的术语就是 Routing。这里的 Route 有点类似网络中的路由表，路由表中
的每一项定义了一段路径。将多个路由器里的某个路径都连接在一起后，就形成一个
完整的音频播放 / 录制路径。

    第1个参数是目的地;
    第2个参数是会用到的 kcontrol，可以为 NULL;
    第3个成员是来源;
*/
static const struct snd_soc_dapm_route zx_audio_map[] = {
	/* Input Lines */
	{"DMIC L2", NULL, "Int Mic"},
	{"DMIC R2", NULL, "Int Mic"},
	{"RECMIXL", NULL, "Headset Mic"},
	{"RECMIXR", NULL, "Headset Mic"},

	/* Output Lines */
	{"Headphones", NULL, "HPOR"},
	{"Headphones", NULL, "HPOL"},
	{"Speakers", NULL, "SPOL"},
	{"Speakers", NULL, "SPOR"},
};

static const struct snd_kcontrol_new zx_mc_controls[] = {
	SOC_DAPM_PIN_SWITCH("Headphones"),
	SOC_DAPM_PIN_SWITCH("Speakers"),
	SOC_DAPM_PIN_SWITCH("Headset Mic"),
	SOC_DAPM_PIN_SWITCH("Int Mic"),
};

static int zx_aif1_startup(struct snd_pcm_substream *substream)
{
	return snd_pcm_hw_constraint_single(substream->runtime,
			SNDRV_PCM_HW_PARAM_RATE, 48000);
}
/* TODO: */
static const struct snd_soc_ops zx_aif1_ops = {
	.startup = zx_aif1_startup,
	.hw_params = zx_aif1_hw_params,//a*d 只有这
};


/*
	根据zx-rt5645分别对应三个dai_link和component
*/
SND_SOC_DAILINK_DEFS(
	pcm,
	DAILINK_COMP_ARRAY(COMP_CPU("zhaoxin_i2s_cpu")),/* 这里填入cpu文件的设备名 */
	DAILINK_COMP_ARRAY(COMP_CODEC("i2c-10EC5645:00", "rt5645-aif1")),
	DAILINK_COMP_ARRAY(COMP_PLATFORM("zhaoxin_i2s_dma")));

/*
codec dai在自己的代码里面有自己的dai配置和ops

为什么要分playback/capture
dai_fmt不应该是hw_params的set_fmt函数里面做的事情吗
*/
static struct snd_soc_dai_link zx_dailink[] = {
	{
		.name = "zx-rt5645-play",
		.stream_name = "RT5645_AIF1",
		.dai_fmt = SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF
				| SND_SOC_DAIFMT_CBS_CFS,
		.init = zx_init, /* playback 多了这个 */
		.ops = &zx_aif1_ops,
		SND_SOC_DAILINK_REG(pcm),
	},
	{
		.name = "zx-rt5645-cap",
		.stream_name = "RT5645_AIF1",
		.dai_fmt = SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF
				| SND_SOC_DAIFMT_CBS_CFS,
		.ops = &zx_aif1_ops,
		SND_SOC_DAILINK_REG(pcm),
	}
};

static struct snd_soc_card zxi2s_card = {
	.name = "zxi2s_rt5650",
	.owner = THIS_MODULE,

	.dai_link = zx_dailink,
	.num_links = 2,

	.dapm_widgets = zx_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(zx_dapm_widgets),

	.dapm_routes = zx_audio_map,
	.num_dapm_routes = ARRAY_SIZE(zx_audio_map),

	.controls = zx_mc_controls,
	.num_controls = ARRAY_SIZE(zx_mc_controls),
};

static int zx_probe(struct platform_device *pdev)
{
	int ret;
	struct snd_soc_card *card;
	struct zxi2s_mc_private *drv;

	drv = devm_kzalloc(&pdev->dev, sizeof(*drv), GFP_KERNEL);//申请内存的目标设备
	if (!drv)
		return -ENOMEM;

	card = &zxi2s_card;
	zxi2s_card.dev = &pdev->dev;	
	if (!acpi_dev_found("10EC5650")) {	//这是找codec
		dev_err(&pdev->dev, "No matching Codec found\n");
		return -ENODEV;
	}

	snd_soc_card_set_drvdata(card, drv);//card把drv作为私有的driver数据
										//下面platform把card作为私有数据


	//在pci里面通过platform_device_register_full分配了platform device,
	platform_set_drvdata(pdev, card);
	ret = devm_snd_soc_register_card(&pdev->dev, card);
	if (ret) {
		dev_err(&pdev->dev,
				"devm_snd_soc_register_card(%s) failed: %d\n",
				card->name, ret);
		return ret;
	}
	return 0;
}

/* Machine device 由 BIOS 上报 */
static const struct acpi_device_id zxi2s_acpi_match[] = {
	{ "I2S1D17", 0 },
	{},//在定义 id_table 时必须使用一个空元素{}来作为结束标记。这个用法就类似字符数组必须使用\0字符来作为结束符一样（或者表述为：字符串必须以\0字符结尾），否则程序在处理字符串时将不知何时终止而出现错误。
};
static struct platform_driver zx_mc_driver = {//是不是改成zx_mc_driver好一点
	.driver = {
		.name = "ZXI2S_MC_NAME",//zx-rt5645
		.acpi_match_table = ACPI_PTR(zxi2s_acpi_match),
		.pm = &snd_soc_pm_ops,
	},
	.probe = zx_probe,
};
module_platform_driver(zx_mc_driver);

MODULE_AUTHOR("hanshu@zhaoxin.com");
MODULE_DESCRIPTION("ZHAOXIN I2S machine driver");
MODULE_VERSION(DRIVER_VERSION);
MODULE_LICENSE("GPL v2");
