// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *      zx_i2s.h - Zhaoxin I2S driver header file
 *
 *      Copyright(c) 2021 Shanghai Zhaoxin Corporation. All rights reserved.
 *
*/

#ifndef __SOUND_ZHAOXIN_I2S_PRIV_H
#define __SOUND_ZHAOXIN_I2S_PRIV_H

#define       ZXI2S_DEVS 3
#define       ZXI2S_MC_NAME "zhaoxin_i2s_mc"		/* machine device */
#define       ZXI2S_CPU_NAME "zhaoxin_i2s_cpu"		/* cpu device */
#define       ZXI2S_DMA_NAME "zhaoxin_i2s_dma"		/* dma device */


/*
 * common registers
 */
#define ZXI2S_REG_COMSET		0x00
#define           COMSET_STOP_DAC_BUF		BIT(5) /* 使能(1)之后，执行到STOPBUF(reg:103)时，DMA停止，SRAM继续发到link，发完停止 */
#define           COMSET_STOP_ADC_BUF		BIT(4) /* 同上input,(reg 113) */
#define           COMSET_MCLK_ALWAYS		BIT(3) /* MCLK是(0)随ADC/DAC同时enable还是一直(1)enable */
#define           COMSET_EN_SLV_WS_INT		BIT(2) /* slave mode时，WS 不sync时是否产生中断，设置位0 */
#define           COMSET_SLV_MODE		BIT(1) /* 工作模式：master(0)/slave */
#define           COMSET_SEL_PLLEA		BIT(0) /* 时钟源选择：33.882M(0) or 36.864M */
#define ZXI2S_REG_MODRST		0x02
#define           MODRST_ADC			BIT(1) /* reset ADC module */
#define           MODRST_DAC			BIT(0) /* reset DAC module */
#define ZXI2S_REG_WSLENMST		0x04	/* Master mode时，WS高低电平时的BCLK Unit数量 */
#define ZXI2S_REG_WSLENSLV		0x06	/* Slave mode时，WS高低电平时的BCLK Unit数量 */
#define           WSLENSLV_SLV_WS_STS		BIT(15)/* slave mode时，WS 不sync时是否产生中断，状态值 */ 
#define ZXI2S_REG_INTCTRL		0x08
#define           INTCTRL_OUT			BIT(1) /* 输出中断enable, 总开关 */
#define           INTCTRL_IN			BIT(0) /* 输入中断enable, 总开关 */
#define ZXI2S_REG_DPLBASE		0x10	/* DMA Position Lower Base Address */
#define           DPLBASE_EN			BIT(0) /* DPL使能位 */
#define ZXI2S_REG_DPUBASE		0x14	/* DMA Position Upper Base Address */
/* DAC */
#define ZXI2S_REG_DACIFCFG		0x40
#define           DACIFCFG_START		BIT(22)/* DAC 启动发送 */
#define           DACIFCFG_POSTIVE_EDGE		BIT(21)/* WS/data在下降沿(0)变化，即上升沿采样 */
#define           DACIFCFG_LEFT_IN_HIGN		BIT(20)/* 左声道(0)数据在低电平 */
#define           DACIFCFG_MDIV			GENMASK(19,16)/* DAC分频系数1 */
#define           DACIFCFG_BDIV			GENMASK(14,12)/* DAC分频系数2 */
#define           DACIFCFG_LRDIV		GENMASK(9,8)/* DAC采样精度，分频系数3 */
#define           DACIFCFG_SDPAD		GENMASK(4,0)/* 补零的个数 */
#define ZXI2S_REG_DACMASK		0x44	/* TODO */
#define ZXI2S_REG_DACPD0S		0x48	/* 通道0的采样数据play */
#define ZXI2S_REG_DACPD1S		0x4C	/* 通道1的采样数据play */
#define ZXI2S_REG_DACFIFOCFG		0x50
#define           DACFIFOCFG_ENDIAN_EXCHANGE	BIT(7) /* 大小端转换 */
#define           DACFIFOCFG_SIGN_EXCHANGE	BIT(6) /* 有无符号*/
#define           DACFIFOCFG_POP_LEN		GENMASK(5,4)/* 每次从FIFO中取出多少bit */
#define           DACFIFOCFG_CHN_NUM		GENMASK(1,0)/* 激活的通道数 */
#define ZXI2S_REG_DACROUTER		0x51
#define           DACROUTER_CHN1_RIGHT		BIT(4) /* 通道1的数据放右声道 */
#define           DACROUTER_CHN1_LEFT		BIT(0) /* TODO: 和bit 4 互斥？ */
/* ADC */
#define ZXI2S_REG_ADCIFCFG		0x80
#define           ADCIFCFG_NEGATIVE_EDGE	BIT(7) /* 数据在上升沿变化，即在下降沿采样 */
#define           ADCIFCFG_LEFT_IN_LOW		BIT(6) /* 左声道放低电平 */
#define           ADCIFCFG_SDPAD		GENMASK(4,0)/* 补零的个数 */
#define ZXI2S_REG_ADCPD0S		0x84	/* 通道0采样数据capture */
#define ZXI2S_REG_ADCPD1S		0x88	/* 通道1采样数据capture */
#define ZXI2S_REG_ADCFIFOCFG		0x8C
#define           ADCFIFOCFG_START		BIT(2) /* ADC 启动接收 */
#define           ADCFIFOCFG_POP_LEN		BIT(0) /* 每次往FIFO写多少bit */
/*
 * input registers
 */
#define ZXI2S_REG_ISDINTE		0x100
#define           ISDINTE_ABORT			BIT(2) /* DMA 异常 */
#define           ISDINTE_XRUN			BIT(1) /* overrun/underrun */
#define           ISDINTE_IOC			BIT(0) /* 正常完成中断 */
#define           ISDINTE_ALL			GENMASK(2,0)
#define ZXI2S_REG_ISDINTS		0x101
#define           ISDINTS_ABORT			BIT(2)
#define           ISDINTS_XRUN			BIT(1)
#define           ISDINTS_IOC			BIT(0)
#define           ISDINTS_ALL			GENMASK(2,0)
#define ZXI2S_REG_ISDLVI		0x102	/* 最后有效的buf index*/
#define ZXI2S_REG_ISDSTOPBUF		0x103	/* 停止buf index，取完该buf之后，DMA可以选择停止，由0x00的bit 4控制 */
#define ZXI2S_REG_ISDFIFOSIZE		0x104	/* FIFO 大小 */
#define ZXI2S_REG_ISDCURBUF		0x106	/* 当前buf的 index */
#define ZXI2S_REG_IBDLLBASE		0x108	/* BDL的基值 */
#define ZXI2S_REG_IBDLUBASE		0x10C
/*
 * output registers
 */
#define ZXI2S_REG_OSDINTE		0x110
#define           OSDINTE_ABORT			BIT(2)
#define           OSDINTE_XRUN			BIT(1)
#define           OSDINTE_IOC			BIT(0)
#define           OSDINTE_ALL			GENMASK(2,0)
#define ZXI2S_REG_OSDINTS		0x111
#define           OSDINTS_ABORT			BIT(2)
#define           OSDINTS_XRUN			BIT(1)
#define           OSDINTS_IOC			BIT(0)
#define           OSDINTS_ALL			GENMASK(2,0)
#define ZXI2S_REG_OSDLVI		0x112
#define ZXI2S_REG_OSDSTOPBUF		0x113
#define ZXI2S_REG_OSDFIFOSIZE		0x114
#define ZXI2S_REG_OSDCURBUF		0x116
#define ZXI2S_REG_OBDLLBASE		0x118
#define ZXI2S_REG_OBDLUBASE		0x11C

/************zhuangzhuang add 2021/11/17************/
struct zx_platform_data {
	#define ZX_I2S_PLAY	(1 << 0)
	#define ZX_I2S_RECORD	(1 << 1)
	#define ZX_I2S_SLAVE	(1 << 2)
	#define ZX_I2S_MASTER	(1 << 3)

	unsigned int cap;
	int channel;
};
/**************************************************/


/*
 * macros for easy use
 */
/* read/write a register, pass without ZXI2S_REG_ prefix */
#define zxi2s_reg_writel(chip, reg, value) \
	writel(value, (chip)->regs + reg)
#define zxi2s_reg_writew(chip, reg, value) \
	writew(value, (chip)->regs +  reg)
#define zxi2s_reg_writeb(chip, reg, value) \
	writeb(value, (chip)->regs +  reg)
#define zxi2s_reg_readl(chip, reg) \
	readl((chip)->regs, reg)
#define zxi2s_reg_readw(chip, reg) \
	readw((chip)->regs,  reg)
#define zxi2s_reg_readb(chip, reg) \
	readb((chip)->regs,  reg)
/* update a register, pass without ZXI2S_REG_ prefix */
#define zxi2s_reg_updatel(chip, reg, mask, value) \
	zxi2s_reg_writel(chip, reg, \
			(zxi2s_reg_readl(chip, reg) & ~(mask)) | (val))
#define zxi2s_reg_updatew(chip, reg, mask, value) \
	zxi2s_reg_writew(chip, reg, \
			(zxi2s_reg_readw(chip, reg) & ~(mask)) | (val))
#define zxi2s_reg_updateb(zxi2s, reg, mask, value) \
	zxi2s_reg_writeb(chip, reg, \
			(zxi2s_reg_readb(chip, reg) & ~(mask)) | (val))




#endif /* __SOUND_ZHAOXIN_I2S_PRIV_H */
#define DRIVER_VERSION  "debug-2021-10-22-113328"
