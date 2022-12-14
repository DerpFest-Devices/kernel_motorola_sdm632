/*
 * madera.c - Cirrus Logic Madera class codecs common support
 *
 * Copyright 2015-2016 Cirrus Logic
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/delay.h>
#include <linux/gcd.h>
#include <linux/module.h>
#include <linux/pm_runtime.h>
#include <linux/slab.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/tlv.h>

#include <linux/mfd/madera/core.h>
#include <linux/mfd/madera/registers.h>
#include <linux/mfd/madera/pdata.h>
#include <sound/madera-pdata.h>
#include <linux/extcon/extcon-madera-pdata.h>

#include <dt-bindings/sound/madera.h>

#include "madera.h"

#define MADERA_AIF_BCLK_CTRL		0x00
#define MADERA_AIF_TX_PIN_CTRL		0x01
#define MADERA_AIF_RX_PIN_CTRL		0x02
#define MADERA_AIF_RATE_CTRL		0x03
#define MADERA_AIF_FORMAT		0x04
#define MADERA_AIF_TX_BCLK_RATE		0x05
#define MADERA_AIF_RX_BCLK_RATE		0x06
#define MADERA_AIF_FRAME_CTRL_1		0x07
#define MADERA_AIF_FRAME_CTRL_2		0x08
#define MADERA_AIF_FRAME_CTRL_3		0x09
#define MADERA_AIF_FRAME_CTRL_4		0x0A
#define MADERA_AIF_FRAME_CTRL_5		0x0B
#define MADERA_AIF_FRAME_CTRL_6		0x0C
#define MADERA_AIF_FRAME_CTRL_7		0x0D
#define MADERA_AIF_FRAME_CTRL_8		0x0E
#define MADERA_AIF_FRAME_CTRL_9		0x0F
#define MADERA_AIF_FRAME_CTRL_10	0x10
#define MADERA_AIF_FRAME_CTRL_11	0x11
#define MADERA_AIF_FRAME_CTRL_12	0x12
#define MADERA_AIF_FRAME_CTRL_13	0x13
#define MADERA_AIF_FRAME_CTRL_14	0x14
#define MADERA_AIF_FRAME_CTRL_15	0x15
#define MADERA_AIF_FRAME_CTRL_16	0x16
#define MADERA_AIF_FRAME_CTRL_17	0x17
#define MADERA_AIF_FRAME_CTRL_18	0x18
#define MADERA_AIF_TX_ENABLES		0x19
#define MADERA_AIF_RX_ENABLES		0x1A
#define MADERA_AIF_FORCE_WRITE		0x1B

#define MADERA_DSP_CONFIG_1_OFFS	0x00
#define MADERA_DSP_CONFIG_2_OFFS	0x02

#define MADERA_DSP_CLK_SEL_MASK		0x70000
#define MADERA_DSP_CLK_SEL_SHIFT	16

#define MADERA_DSP_RATE_MASK		0x7800
#define MADERA_DSP_RATE_SHIFT		11

#define MADERA_FLL_VCO_CORNER		141900000
#define MADERA_FLL_MAX_FREF		 13500000
#define MADERA_FLL_MAX_N		     1023
#define MADERA_FLL_MIN_FOUT		 90000000
#define MADERA_FLL_MAX_FOUT		100000000
#define MADERA_FLL_MAX_FRATIO		       16
#define MADERA_FLL_MAX_REFDIV			8
#define MADERA_FLL_OUTDIV			3
#define MADERA_FLL_VCO_MULT			3
#define MADERA_FLLAO_MAX_FREF		 12288000
#define MADERA_FLLAO_MIN_N		        4
#define MADERA_FLLAO_MAX_N		     1023
#define MADERA_FLLAO_MAX_FBDIV		      254

#define MADERA_FLL_SYNCHRONISER_OFFS		0x10
#define CS47L35_FLL_SYNCHRONISER_OFFS		0xE

#define MADERA_FLL_CONTROL_1_OFFS		0x1
#define MADERA_FLL_CONTROL_2_OFFS		0x2
#define MADERA_FLL_CONTROL_3_OFFS		0x3
#define MADERA_FLL_CONTROL_4_OFFS		0x4
#define MADERA_FLL_CONTROL_5_OFFS		0x5
#define MADERA_FLL_CONTROL_6_OFFS		0x6
#define MADERA_FLL_LOOP_FILTER_TEST_1_OFFS	0x7
#define MADERA_FLL_NCO_TEST_0_OFFS		0x8
#define MADERA_FLL_CONTROL_7_OFFS		0x9
#define MADERA_FLL_EFS_2_OFFS			0xA
#define MADERA_FLL_SYNCHRONISER_1_OFFS		0x1
#define MADERA_FLL_SYNCHRONISER_2_OFFS		0x2
#define MADERA_FLL_SYNCHRONISER_3_OFFS		0x3
#define MADERA_FLL_SYNCHRONISER_4_OFFS		0x4
#define MADERA_FLL_SYNCHRONISER_5_OFFS		0x5
#define MADERA_FLL_SYNCHRONISER_6_OFFS		0x6
#define MADERA_FLL_SYNCHRONISER_7_OFFS		0x7
#define MADERA_FLL_SPREAD_SPECTRUM_OFFS		0x9
#define MADERA_FLL_GPIO_CLOCK_OFFS		0xA

#define MADERA_FLLAO_CONTROL_1_OFFS		0x1
#define MADERA_FLLAO_CONTROL_2_OFFS		0x2
#define MADERA_FLLAO_CONTROL_3_OFFS		0x3
#define MADERA_FLLAO_CONTROL_4_OFFS		0x4
#define MADERA_FLLAO_CONTROL_5_OFFS		0x5
#define MADERA_FLLAO_CONTROL_6_OFFS		0x6
#define MADERA_FLLAO_CONTROL_7_OFFS		0x8
#define MADERA_FLLAO_CONTROL_8_OFFS		0xA
#define MADERA_FLLAO_CONTROL_9_OFFS		0xB
#define MADERA_FLLAO_CONTROL_10_OFFS		0xC
#define MADERA_FLLAO_CONTROL_11_OFFS		0xD

#define MADERA_FMT_DSP_MODE_A		0
#define MADERA_FMT_DSP_MODE_B		1
#define MADERA_FMT_I2S_MODE		2
#define MADERA_FMT_LEFT_JUSTIFIED_MODE	3

#define madera_fll_err(_fll, fmt, ...) \
	dev_err(_fll->madera->dev, "FLL%d: " fmt, _fll->id, ##__VA_ARGS__)
#define madera_fll_warn(_fll, fmt, ...) \
	dev_warn(_fll->madera->dev, "FLL%d: " fmt, _fll->id, ##__VA_ARGS__)
#define madera_fll_dbg(_fll, fmt, ...) \
	dev_dbg(_fll->madera->dev, "FLL%d: " fmt, _fll->id, ##__VA_ARGS__)

#define madera_aif_err(_dai, fmt, ...) \
	dev_err(_dai->dev, "AIF%d: " fmt, _dai->id, ##__VA_ARGS__)
#define madera_aif_warn(_dai, fmt, ...) \
	dev_warn(_dai->dev, "AIF%d: " fmt, _dai->id, ##__VA_ARGS__)
#define madera_aif_dbg(_dai, fmt, ...) \
	dev_dbg(_dai->dev, "AIF%d: " fmt, _dai->id, ##__VA_ARGS__)

static const int madera_dsp_bus_error_irqs[MADERA_MAX_ADSP] = {
	MADERA_IRQ_DSP1_BUS_ERROR,
	MADERA_IRQ_DSP2_BUS_ERROR,
	MADERA_IRQ_DSP3_BUS_ERROR,
	MADERA_IRQ_DSP4_BUS_ERROR,
	MADERA_IRQ_DSP5_BUS_ERROR,
	MADERA_IRQ_DSP6_BUS_ERROR,
	MADERA_IRQ_DSP7_BUS_ERROR,
};

static const unsigned int madera_aif1_inputs[32] = {
	MADERA_AIF1TX1MIX_INPUT_1_SOURCE,
	MADERA_AIF1TX1MIX_INPUT_2_SOURCE,
	MADERA_AIF1TX1MIX_INPUT_3_SOURCE,
	MADERA_AIF1TX1MIX_INPUT_4_SOURCE,
	MADERA_AIF1TX2MIX_INPUT_1_SOURCE,
	MADERA_AIF1TX2MIX_INPUT_2_SOURCE,
	MADERA_AIF1TX2MIX_INPUT_3_SOURCE,
	MADERA_AIF1TX2MIX_INPUT_4_SOURCE,
	MADERA_AIF1TX3MIX_INPUT_1_SOURCE,
	MADERA_AIF1TX3MIX_INPUT_2_SOURCE,
	MADERA_AIF1TX3MIX_INPUT_3_SOURCE,
	MADERA_AIF1TX3MIX_INPUT_4_SOURCE,
	MADERA_AIF1TX4MIX_INPUT_1_SOURCE,
	MADERA_AIF1TX4MIX_INPUT_2_SOURCE,
	MADERA_AIF1TX4MIX_INPUT_3_SOURCE,
	MADERA_AIF1TX4MIX_INPUT_4_SOURCE,
	MADERA_AIF1TX5MIX_INPUT_1_SOURCE,
	MADERA_AIF1TX5MIX_INPUT_2_SOURCE,
	MADERA_AIF1TX5MIX_INPUT_3_SOURCE,
	MADERA_AIF1TX5MIX_INPUT_4_SOURCE,
	MADERA_AIF1TX6MIX_INPUT_1_SOURCE,
	MADERA_AIF1TX6MIX_INPUT_2_SOURCE,
	MADERA_AIF1TX6MIX_INPUT_3_SOURCE,
	MADERA_AIF1TX6MIX_INPUT_4_SOURCE,
	MADERA_AIF1TX7MIX_INPUT_1_SOURCE,
	MADERA_AIF1TX7MIX_INPUT_2_SOURCE,
	MADERA_AIF1TX7MIX_INPUT_3_SOURCE,
	MADERA_AIF1TX7MIX_INPUT_4_SOURCE,
	MADERA_AIF1TX8MIX_INPUT_1_SOURCE,
	MADERA_AIF1TX8MIX_INPUT_2_SOURCE,
	MADERA_AIF1TX8MIX_INPUT_3_SOURCE,
	MADERA_AIF1TX8MIX_INPUT_4_SOURCE,
};

static const unsigned int madera_aif2_inputs[32] = {
	MADERA_AIF2TX1MIX_INPUT_1_SOURCE,
	MADERA_AIF2TX1MIX_INPUT_2_SOURCE,
	MADERA_AIF2TX1MIX_INPUT_3_SOURCE,
	MADERA_AIF2TX1MIX_INPUT_4_SOURCE,
	MADERA_AIF2TX2MIX_INPUT_1_SOURCE,
	MADERA_AIF2TX2MIX_INPUT_2_SOURCE,
	MADERA_AIF2TX2MIX_INPUT_3_SOURCE,
	MADERA_AIF2TX2MIX_INPUT_4_SOURCE,
	MADERA_AIF2TX3MIX_INPUT_1_SOURCE,
	MADERA_AIF2TX3MIX_INPUT_2_SOURCE,
	MADERA_AIF2TX3MIX_INPUT_3_SOURCE,
	MADERA_AIF2TX3MIX_INPUT_4_SOURCE,
	MADERA_AIF2TX4MIX_INPUT_1_SOURCE,
	MADERA_AIF2TX4MIX_INPUT_2_SOURCE,
	MADERA_AIF2TX4MIX_INPUT_3_SOURCE,
	MADERA_AIF2TX4MIX_INPUT_4_SOURCE,
	MADERA_AIF2TX5MIX_INPUT_1_SOURCE,
	MADERA_AIF2TX5MIX_INPUT_2_SOURCE,
	MADERA_AIF2TX5MIX_INPUT_3_SOURCE,
	MADERA_AIF2TX5MIX_INPUT_4_SOURCE,
	MADERA_AIF2TX6MIX_INPUT_1_SOURCE,
	MADERA_AIF2TX6MIX_INPUT_2_SOURCE,
	MADERA_AIF2TX6MIX_INPUT_3_SOURCE,
	MADERA_AIF2TX6MIX_INPUT_4_SOURCE,
	MADERA_AIF2TX7MIX_INPUT_1_SOURCE,
	MADERA_AIF2TX7MIX_INPUT_2_SOURCE,
	MADERA_AIF2TX7MIX_INPUT_3_SOURCE,
	MADERA_AIF2TX7MIX_INPUT_4_SOURCE,
	MADERA_AIF2TX8MIX_INPUT_1_SOURCE,
	MADERA_AIF2TX8MIX_INPUT_2_SOURCE,
	MADERA_AIF2TX8MIX_INPUT_3_SOURCE,
	MADERA_AIF2TX8MIX_INPUT_4_SOURCE,
};

static const unsigned int madera_aif3_inputs[8] = {
	MADERA_AIF3TX1MIX_INPUT_1_SOURCE,
	MADERA_AIF3TX1MIX_INPUT_2_SOURCE,
	MADERA_AIF3TX1MIX_INPUT_3_SOURCE,
	MADERA_AIF3TX1MIX_INPUT_4_SOURCE,
	MADERA_AIF3TX2MIX_INPUT_1_SOURCE,
	MADERA_AIF3TX2MIX_INPUT_2_SOURCE,
	MADERA_AIF3TX2MIX_INPUT_3_SOURCE,
	MADERA_AIF3TX2MIX_INPUT_4_SOURCE,
};

static const unsigned int madera_aif4_inputs[8] = {
	MADERA_AIF4TX1MIX_INPUT_1_SOURCE,
	MADERA_AIF4TX1MIX_INPUT_2_SOURCE,
	MADERA_AIF4TX1MIX_INPUT_3_SOURCE,
	MADERA_AIF4TX1MIX_INPUT_4_SOURCE,
	MADERA_AIF4TX2MIX_INPUT_1_SOURCE,
	MADERA_AIF4TX2MIX_INPUT_2_SOURCE,
	MADERA_AIF4TX2MIX_INPUT_3_SOURCE,
	MADERA_AIF4TX2MIX_INPUT_4_SOURCE,
};

static int madera_get_sources(struct snd_soc_dai *dai,
			      const unsigned int **sources, int *lim)
{
	int ret = 0;

	*lim = dai->driver->playback.channels_max * 4;

	switch (dai->driver->base) {
	case MADERA_AIF1_BCLK_CTRL:
		*sources = madera_aif1_inputs;
		break;
	case MADERA_AIF2_BCLK_CTRL:
		*sources = madera_aif2_inputs;
		break;
	case MADERA_AIF3_BCLK_CTRL:
		*sources = madera_aif3_inputs;
		break;
	case MADERA_AIF4_BCLK_CTRL:
		*sources = madera_aif4_inputs;
		break;
	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

int madera_cache_and_clear_sources(struct madera_priv *priv,
				   const unsigned int *sources,
				   unsigned int *cache, int lim)
{
	struct madera *madera = priv->madera;
	int ret = 0;
	int i;

	memset(cache, 0, lim * sizeof(unsigned int));

	for (i = 0; i < lim; i++) {
		ret = regmap_read(madera->regmap, sources[i], &cache[i]);

		dev_dbg(madera->dev,
			"%s addr: 0x%04x value: 0x%04x\n",
			__func__, sources[i], cache[i]);

		if (ret) {
			dev_err(madera->dev,
				"%s Failed to cache AIF:0x%04x inputs: %d\n",
				__func__, sources[i], ret);
			break;
		}

		ret = regmap_write(madera->regmap, sources[i], 0);

		if (ret) {
			dev_err(madera->dev,
				"%s Failed to clear AIF:0x%04x inputs: %d\n",
				__func__, sources[i], ret);
			break;
		}
	}

	return ret;
}
EXPORT_SYMBOL_GPL(madera_cache_and_clear_sources);

void madera_spin_sysclk(struct madera_priv *priv)
{
	struct madera *madera = priv->madera;
	unsigned int val;
	int ret, i;

	/* Skip this if the chip is down */
	if (pm_runtime_suspended(madera->dev))
		return;

	/*
	 * Just read a register a few times to ensure the internal
	 * oscillator sends out a few clocks.
	 */
	for (i = 0; i < 4; i++) {
		ret = regmap_read(madera->regmap, MADERA_SOFTWARE_RESET, &val);
		if (ret)
			dev_err(madera->dev,
				"%s Failed to read register: %d (%d)\n",
				__func__, ret, i);
	}

	udelay(300);
}
EXPORT_SYMBOL_GPL(madera_spin_sysclk);

int madera_restore_sources(struct madera_priv *priv,
			   const unsigned int *sources,
			   unsigned int *cache, int lim)
{
	struct madera *madera = priv->madera;
	int i;
	int ret = 0;

	for (i = 0; i < lim; i++) {
		dev_dbg(madera->dev,
			"%s addr: 0x%04x value: 0x%04x\n",
			__func__, sources[i], cache[i]);

		ret = regmap_write(madera->regmap, sources[i], cache[i]);

		if (ret) {
			dev_err(madera->dev,
				"%s Failed to restore AIF:0x%04x inputs: %d\n",
				__func__, sources[i], ret);
			break;
		}
	}

	return ret;

}
EXPORT_SYMBOL_GPL(madera_restore_sources);

int madera_sysclk_ev(struct snd_soc_dapm_widget *w,
		     struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_codec *codec = snd_soc_dapm_to_codec(w->dapm);
	struct madera_priv *priv = snd_soc_codec_get_drvdata(codec);

	madera_spin_sysclk(priv);

	return 0;
}
EXPORT_SYMBOL_GPL(madera_sysclk_ev);

static int madera_check_speaker_overheat(struct madera *madera,
					 bool *warn, bool *shutdown)
{
	unsigned int val;
	int ret;

	ret = regmap_read(madera->regmap, MADERA_IRQ1_RAW_STATUS_15, &val);
	if (ret) {
		dev_err(madera->dev, "Failed to read thermal status: %d\n",
			ret);
		return ret;
	}

	*warn = val & MADERA_SPK_OVERHEAT_WARN_STS1 ? true : false;
	*shutdown = val & MADERA_SPK_OVERHEAT_STS1 ? true : false;
	return 0;
}

static int madera_spk_ev(struct snd_soc_dapm_widget *w,
			 struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_codec *codec = snd_soc_dapm_to_codec(w->dapm);
	struct madera *madera = dev_get_drvdata(codec->dev->parent);
	bool warn, shutdown;
	int ret;

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		ret = madera_check_speaker_overheat(madera, &warn, &shutdown);
		if (ret)
			return ret;

		if (shutdown) {
			dev_crit(madera->dev,
				 "Speaker not enabled due to temperature\n");
			return -EBUSY;
		}

		regmap_update_bits_async(madera->regmap,
					 MADERA_OUTPUT_ENABLES_1,
					 1 << w->shift, 1 << w->shift);
		break;
	case SND_SOC_DAPM_PRE_PMD:
		regmap_update_bits_async(madera->regmap,
					 MADERA_OUTPUT_ENABLES_1,
					 1 << w->shift, 0);
		break;
	default:
		break;
	}

	return 0;
}

static irqreturn_t madera_thermal_warn(int irq, void *data)
{
	struct madera *madera = data;
	bool warn, shutdown;
	int ret;

	ret = madera_check_speaker_overheat(madera, &warn, &shutdown);
	if ((ret == 0) && warn)
		dev_crit(madera->dev, "Thermal warning\n");

	return IRQ_HANDLED;
}

static irqreturn_t madera_thermal_shutdown(int irq, void *data)
{
	struct madera *madera = data;
	bool warn, shutdown;
	int ret;

	ret = madera_check_speaker_overheat(madera, &warn, &shutdown);
	if ((ret == 0) && shutdown) {
		dev_crit(madera->dev, "Thermal shutdown\n");
		ret = regmap_update_bits(madera->regmap,
					 MADERA_OUTPUT_ENABLES_1,
					 MADERA_OUT4L_ENA |
					 MADERA_OUT4R_ENA, 0);
		if (ret != 0)
			dev_crit(madera->dev,
				 "Failed to disable speaker outputs: %d\n",
				 ret);
	}

	return IRQ_HANDLED;
}

static const struct snd_soc_dapm_widget madera_spk[2] = {
	SND_SOC_DAPM_PGA_E("OUT4L", SND_SOC_NOPM,
			   MADERA_OUT4L_ENA_SHIFT, 0, NULL, 0, madera_spk_ev,
			   SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_PRE_PMD),
	SND_SOC_DAPM_PGA_E("OUT4R", SND_SOC_NOPM,
			   MADERA_OUT4R_ENA_SHIFT, 0, NULL, 0, madera_spk_ev,
			   SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_PRE_PMD),
};

int madera_init_spk(struct snd_soc_codec *codec, int n_channels)
{
	struct snd_soc_dapm_context *dapm = snd_soc_codec_get_dapm(codec);
	struct madera_priv *priv = snd_soc_codec_get_drvdata(codec);
	struct madera *madera = priv->madera;
	int ret;

	ret = snd_soc_dapm_new_controls(dapm, madera_spk, n_channels);
	if (ret)
		return ret;

	ret = madera_request_irq(madera, MADERA_IRQ_SPK_OVERHEAT_WARN,
				 "Thermal warning", madera_thermal_warn,
				 madera);
	if (ret)
		dev_warn(madera->dev,
			"Failed to get thermal warning IRQ: %d\n", ret);

	ret = madera_request_irq(madera, MADERA_IRQ_SPK_OVERHEAT,
				 "Thermal shutdown", madera_thermal_shutdown,
				 madera);
	if (ret)
		dev_warn(madera->dev,
			"Failed to get thermal shutdown IRQ: %d\n", ret);

	return 0;
}
EXPORT_SYMBOL_GPL(madera_init_spk);

int madera_free_spk(struct snd_soc_codec *codec)
{
	struct madera_priv *priv = snd_soc_codec_get_drvdata(codec);
	struct madera *madera = priv->madera;

	madera_free_irq(madera, MADERA_IRQ_SPK_OVERHEAT_WARN, madera);
	madera_free_irq(madera, MADERA_IRQ_SPK_OVERHEAT, madera);

	return 0;
}
EXPORT_SYMBOL_GPL(madera_free_spk);

static void madera_get_inmode_from_of(struct madera *madera)
{
	struct device_node *np = madera->dev->of_node;
	struct property *tempprop;
	const __be32 *cur;
	u32 val;
	int in_n = 0, ch_n = 0;

	BUILD_BUG_ON(ARRAY_SIZE(madera->pdata.codec.inmode) !=
		MADERA_MAX_INPUT);
	BUILD_BUG_ON(ARRAY_SIZE(madera->pdata.codec.inmode[0]) !=
		     MADERA_MAX_MUXED_CHANNELS);

	of_property_for_each_u32(np, "cirrus,inmode", tempprop, cur, val) {
		madera->pdata.codec.inmode[in_n][ch_n] = val;

		if (++ch_n == MADERA_MAX_MUXED_CHANNELS) {
			ch_n = 0;
			if (++in_n == MADERA_MAX_INPUT)
				break;
		}
	}

	if (ch_n != 0)
		dev_warn(madera->dev, "%s not a multiple of %d entries\n",
			 "cirrus,inmode", MADERA_MAX_MUXED_CHANNELS);
}

static void madera_get_pdata_from_of(struct madera *madera)
{
	struct madera_codec_pdata *pdata = &madera->pdata.codec;
	unsigned int out_mono[ARRAY_SIZE(pdata->out_mono)];
	int i;

	memset(&out_mono, 0, sizeof(out_mono));

	madera_of_read_uint_array(madera, "cirrus,max-channels-clocked", false,
				 pdata->max_channels_clocked,
				0, ARRAY_SIZE(pdata->max_channels_clocked));

	madera_get_inmode_from_of(madera);

	madera_of_read_uint_array(madera, "cirrus,out-mono", false,
				 out_mono,
				 ARRAY_SIZE(out_mono), ARRAY_SIZE(out_mono));
	for (i = 0; i < ARRAY_SIZE(out_mono); ++i)
		pdata->out_mono[i] = !!out_mono[i];

	madera_of_read_uint_array(madera, "cirrus,pdm-fmt", false,
				 pdata->pdm_fmt,
				 ARRAY_SIZE(pdata->pdm_fmt),
				 ARRAY_SIZE(pdata->pdm_fmt));

	madera_of_read_uint_array(madera, "cirrus,pdm-mute", false,
				 pdata->pdm_mute,
				 ARRAY_SIZE(pdata->pdm_mute),
				 ARRAY_SIZE(pdata->pdm_mute));

	madera_of_read_uint_array(madera, "cirrus,dmic-ref", false,
				pdata->dmic_ref,
				0, ARRAY_SIZE(pdata->dmic_ref));

	madera_of_read_uint_array(madera, "cirrus,dmic-clksrc", false,
				pdata->dmic_clksrc,
				0, ARRAY_SIZE(pdata->dmic_clksrc));
}

int madera_core_init(struct madera_priv *priv)
{
	BUILD_BUG_ON(ARRAY_SIZE(madera_mixer_texts) != MADERA_NUM_MIXER_INPUTS);
	BUILD_BUG_ON(ARRAY_SIZE(madera_mixer_values) !=
		MADERA_NUM_MIXER_INPUTS);
	BUILD_BUG_ON(ARRAY_SIZE(priv->aif_sources_cache) <
				ARRAY_SIZE(madera_aif1_inputs));
	BUILD_BUG_ON(madera_sample_rate_text[MADERA_SAMPLE_RATE_ENUM_SIZE - 1]
		     == NULL);
	BUILD_BUG_ON(madera_sample_rate_val[MADERA_SAMPLE_RATE_ENUM_SIZE - 1]
		     == 0);

	if (IS_ENABLED(CONFIG_OF))
		if (!dev_get_platdata(priv->madera->dev))
			madera_get_pdata_from_of(priv->madera);

	mutex_init(&priv->adsp_rate_lock);
	mutex_init(&priv->rate_lock);
	mutex_init(&priv->adsp_fw_lock);

	return 0;
}
EXPORT_SYMBOL_GPL(madera_core_init);

int madera_core_destroy(struct madera_priv *priv)
{
	mutex_destroy(&priv->adsp_rate_lock);
	mutex_destroy(&priv->rate_lock);
	mutex_destroy(&priv->adsp_fw_lock);

	return 0;
}
EXPORT_SYMBOL_GPL(madera_core_destroy);

static bool madera_is_hp_shorted(const struct madera *madera,
				 unsigned int index)
{
	if (index >= MADERA_MAX_ACCESSORY)
		return false;

	return (madera->hp_impedance_x100[index] <=
		madera->pdata.accdet[index].hpdet_short_circuit_imp * 100);
}

int madera_out1_demux_put(struct snd_kcontrol *kcontrol,
			  struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_soc_dapm_kcontrol_codec(kcontrol);
	struct snd_soc_dapm_context *dapm =
		snd_soc_dapm_kcontrol_dapm(kcontrol);
	struct madera *madera = dev_get_drvdata(codec->dev->parent);
	struct soc_enum *e = (struct soc_enum *)kcontrol->private_value;
	unsigned int ep_sel, mux, change;
	unsigned int mask;
	int ret, demux_change_ret;
	bool out_mono, restore_out = true;

	if (ucontrol->value.enumerated.item[0] > e->items - 1)
		return -EINVAL;

	mux = ucontrol->value.enumerated.item[0];
	ep_sel = mux << e->shift_l;
	mask = e->mask << e->shift_l;

	snd_soc_dapm_mutex_lock(dapm);

	change = snd_soc_test_bits(codec, e->reg, mask, ep_sel);

	/* if no change is required, skip */
	if (!change)
		goto end;

	/* EP_SEL should not be modified while HP or EP driver is enabled */
	ret = regmap_update_bits(madera->regmap,
				 MADERA_OUTPUT_ENABLES_1,
				 MADERA_OUT1L_ENA |
				 MADERA_OUT1R_ENA, 0);
	if (ret)
		dev_warn(madera->dev, "Failed to disable outputs: %d\n", ret);

	usleep_range(2000, 3000); /* wait for wseq to complete */

	/* if HP detection clamp is applied while switching to HPOUT
	 * OUT1 should remain disabled and EDRE should be set to manual
	 */
	if (!ep_sel &&
	    (madera->hpdet_clamp[0] || madera_is_hp_shorted(madera, 0)))
		restore_out = false;

	if (!ep_sel && madera->hpdet_clamp[0]) {
		ret = regmap_write(madera->regmap, MADERA_EDRE_MANUAL, 0x3);
		if (ret)
			dev_warn(madera->dev,
				 "Failed to set EDRE Manual: %d\n",
				 ret);
	}

	/* change demux setting */
	demux_change_ret = regmap_update_bits(madera->regmap,
					      MADERA_OUTPUT_ENABLES_1,
					      MADERA_EP_SEL, ep_sel);
	if (demux_change_ret) {
		dev_err(madera->dev, "Failed to set OUT1 demux: %d\n",
			demux_change_ret);
	} else {
		/* apply correct setting for mono mode */
		if (!ep_sel && !madera->pdata.codec.out_mono[0])
			out_mono = false; /* stereo HP */
		else
			out_mono = true; /* EP or mono HP */

		ret = madera_set_output_mode(codec, 1, out_mono);
		if (ret)
			dev_warn(madera->dev,
				 "Failed to set output mode: %d\n", ret);
	}

	/* restore output state if allowed */
	if (restore_out) {
		ret = regmap_update_bits(madera->regmap,
					 MADERA_OUTPUT_ENABLES_1,
					 MADERA_OUT1L_ENA |
					 MADERA_OUT1R_ENA,
					 madera->hp_ena);
		if (ret)
			dev_warn(madera->dev,
				 "Failed to restore earpiece outputs: %d\n",
				 ret);
		else if (madera->hp_ena)
			msleep(34); /* wait for enable wseq */
		else
			usleep_range(2000, 3000); /* wait for disable wseq */
	}

	/* if a switch to EPOUT occurred restore EDRE setting */
	if (ep_sel && !demux_change_ret) {
		ret = regmap_write(madera->regmap, MADERA_EDRE_MANUAL, 0);
		if (ret)
			dev_warn(madera->dev,
				 "Failed to restore EDRE Manual: %d\n",
				 ret);
	}

end:
	snd_soc_dapm_mutex_unlock(dapm);

	return snd_soc_dapm_mux_update_power(dapm, kcontrol, mux, e, NULL);
}
EXPORT_SYMBOL_GPL(madera_out1_demux_put);


static int madera_inmux_put(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_soc_dapm_kcontrol_codec(kcontrol);
	struct snd_soc_dapm_context *dapm =
		snd_soc_dapm_kcontrol_dapm(kcontrol);
	struct madera *madera = dev_get_drvdata(codec->dev->parent);
	struct soc_enum *e = (struct soc_enum *) kcontrol->private_value;
	unsigned int mux, src_val, src_mask, gang_reg, dmode_reg, dmode_val;
	unsigned int inmode_a, inmode_gang, inmode;
	bool changed = false;
	int ret;

	mux = ucontrol->value.enumerated.item[0];
	if (mux > 1)
		return -EINVAL;

	src_val = mux << e->shift_l;
	src_mask = e->mask << e->shift_l;

	switch (e->reg) {
	case MADERA_ADC_DIGITAL_VOLUME_1L:
		inmode_a = madera->pdata.codec.inmode[0][0];
		inmode = madera->pdata.codec.inmode[0][2 * mux];
		inmode_gang = madera->pdata.codec.inmode[0][1 + (2 * mux)];
		gang_reg = MADERA_ADC_DIGITAL_VOLUME_1R;
		dmode_reg = MADERA_IN1L_CONTROL;
		break;
	case MADERA_ADC_DIGITAL_VOLUME_1R:
		inmode_a = madera->pdata.codec.inmode[0][0];
		inmode = madera->pdata.codec.inmode[0][1 + (2 * mux)];
		inmode_gang = madera->pdata.codec.inmode[0][2 * mux];
		gang_reg = MADERA_ADC_DIGITAL_VOLUME_1L;
		dmode_reg = MADERA_IN1L_CONTROL;
		break;
	case MADERA_ADC_DIGITAL_VOLUME_2L:
		inmode_a = madera->pdata.codec.inmode[1][0];
		inmode = madera->pdata.codec.inmode[1][2 * mux];
		inmode_gang = madera->pdata.codec.inmode[1][1 + (2 * mux)];
		gang_reg = MADERA_ADC_DIGITAL_VOLUME_2R;
		dmode_reg = MADERA_IN2L_CONTROL;
		break;
	case MADERA_ADC_DIGITAL_VOLUME_2R:
		inmode_a = madera->pdata.codec.inmode[1][0];
		inmode = madera->pdata.codec.inmode[1][1 + (2 * mux)];
		inmode_gang = madera->pdata.codec.inmode[1][2 * mux];
		gang_reg = MADERA_ADC_DIGITAL_VOLUME_2L;
		dmode_reg = MADERA_IN2L_CONTROL;
		break;
	default:
		return -EINVAL;
	}

	/* SE mask and shift is same for all channels */
	src_mask |= MADERA_IN1L_SRC_SE_MASK;
	if (inmode & MADERA_INMODE_SE)
		src_val |= 1 << MADERA_IN1L_SRC_SE_SHIFT;

	dev_dbg(madera->dev,
		"mux=%u reg=0x%x inmode_a=0x%x inmode=0x%x mask=0x%x val=0x%x\n",
		mux, e->reg, inmode_a, inmode, src_mask, src_val);

	ret = snd_soc_component_update_bits(dapm->component,
					    e->reg,
					    src_mask,
					    src_val);
	if (ret < 0)
		return ret;
	else if (ret)
		changed = true;

	/* if the A input is digital we must switch both channels together */
	if (inmode_a == MADERA_INMODE_DMIC) {
		switch (madera->type) {
		case CS47L85:
		case WM1840:
			if (e->reg == MADERA_ADC_DIGITAL_VOLUME_1L)
				goto out;	/* not ganged */
			break;
		case CS47L90:
		case CS47L91:
			if (e->reg == MADERA_ADC_DIGITAL_VOLUME_2L)
				goto out;	/* not ganged */
			break;
		default:
			break;
		}

		/* ganged channels can have different analogue modes */
		if (inmode_gang & MADERA_INMODE_SE)
			src_val |= 1 << MADERA_IN1L_SRC_SE_SHIFT;
		else
			src_val &= ~(1 << MADERA_IN1L_SRC_SE_SHIFT);

		if (mux)
			dmode_val = 0; /* B always analogue */
		else
			dmode_val = 1 << MADERA_IN1_MODE_SHIFT; /* DMIC */

		dev_dbg(madera->dev,
			"gang_reg=0x%x inmode_gang=0x%x gang_val=0x%x dmode_val=0x%x\n",
			gang_reg, inmode_gang, src_val, dmode_val);

		ret = snd_soc_component_update_bits(dapm->component,
						    gang_reg,
						    src_mask,
						    src_val);
		if (ret < 0)
			return ret;
		else if (ret)
			changed |= true;

		ret = snd_soc_component_update_bits(dapm->component,
						    dmode_reg,
						    MADERA_IN1_MODE_MASK,
						    dmode_val);
	}

out:
	if (changed)
		return snd_soc_dapm_mux_update_power(dapm, kcontrol,
						     mux, e, NULL);
	else
		return 0;
}

static const char * const madera_inmux_texts[] = {
	"A",
	"B",
};

static SOC_ENUM_SINGLE_DECL(madera_in1muxl_enum,
			    MADERA_ADC_DIGITAL_VOLUME_1L,
			    MADERA_IN1L_SRC_SHIFT,
			    madera_inmux_texts);

static SOC_ENUM_SINGLE_DECL(madera_in1muxr_enum,
			    MADERA_ADC_DIGITAL_VOLUME_1R,
			    MADERA_IN1R_SRC_SHIFT,
			    madera_inmux_texts);

static SOC_ENUM_SINGLE_DECL(madera_in2muxl_enum,
			    MADERA_ADC_DIGITAL_VOLUME_2L,
			    MADERA_IN2L_SRC_SHIFT,
			    madera_inmux_texts);

static SOC_ENUM_SINGLE_DECL(madera_in2muxr_enum,
			    MADERA_ADC_DIGITAL_VOLUME_2R,
			    MADERA_IN2R_SRC_SHIFT,
			    madera_inmux_texts);

const struct snd_kcontrol_new madera_inmux[] = {
	SOC_DAPM_ENUM_EXT("IN1L Mux", madera_in1muxl_enum,
			  snd_soc_dapm_get_enum_double, madera_inmux_put),
	SOC_DAPM_ENUM_EXT("IN1R Mux", madera_in1muxr_enum,
			  snd_soc_dapm_get_enum_double, madera_inmux_put),
	SOC_DAPM_ENUM_EXT("IN2L Mux", madera_in2muxl_enum,
			  snd_soc_dapm_get_enum_double, madera_inmux_put),
	SOC_DAPM_ENUM_EXT("IN2R Mux", madera_in2muxr_enum,
			  snd_soc_dapm_get_enum_double, madera_inmux_put),
};
EXPORT_SYMBOL_GPL(madera_inmux);

int madera_adsp_rate_get(struct snd_kcontrol *kcontrol,
			 struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_soc_kcontrol_codec(kcontrol);
	struct madera_priv *priv = snd_soc_codec_get_drvdata(codec);
	struct soc_enum *e = (struct soc_enum *)kcontrol->private_value;
	unsigned int cached_rate, item;
	const int adsp_num = e->shift_l;
	int ret = -EINVAL;

	mutex_lock(&priv->adsp_rate_lock);

	cached_rate = priv->adsp_rate_cache[adsp_num];

	for (item = 0; item < e->items; item++) {
		if (e->values[item] == cached_rate) {
			ucontrol->value.enumerated.item[0] = item;
			ret = 0;
			break;
		}
	}

	mutex_unlock(&priv->adsp_rate_lock);
	return ret;
}
EXPORT_SYMBOL_GPL(madera_adsp_rate_get);

int madera_adsp_rate_put(struct snd_kcontrol *kcontrol,
			 struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_soc_kcontrol_codec(kcontrol);
	struct madera_priv *priv = snd_soc_codec_get_drvdata(codec);
	struct soc_enum *e = (struct soc_enum *)kcontrol->private_value;
	const int adsp_num = e->shift_l;
	const unsigned int item = ucontrol->value.enumerated.item[0];

	if (item >= e->items)
		return -EINVAL;

	mutex_lock(&priv->adsp_rate_lock);

	priv->adsp_rate_cache[adsp_num] = e->values[item];

	mutex_unlock(&priv->adsp_rate_lock);
	return 0;
}
EXPORT_SYMBOL_GPL(madera_adsp_rate_put);

static const struct soc_enum madera_adsp_rate_enum[] = {
	SOC_VALUE_ENUM_SINGLE(SND_SOC_NOPM, 0, 0xf, MADERA_RATE_ENUM_SIZE,
			      madera_rate_text, madera_rate_val),
	SOC_VALUE_ENUM_SINGLE(SND_SOC_NOPM, 1, 0xf, MADERA_RATE_ENUM_SIZE,
			      madera_rate_text, madera_rate_val),
	SOC_VALUE_ENUM_SINGLE(SND_SOC_NOPM, 2, 0xf, MADERA_RATE_ENUM_SIZE,
			      madera_rate_text, madera_rate_val),
	SOC_VALUE_ENUM_SINGLE(SND_SOC_NOPM, 3, 0xf, MADERA_RATE_ENUM_SIZE,
			      madera_rate_text, madera_rate_val),
	SOC_VALUE_ENUM_SINGLE(SND_SOC_NOPM, 4, 0xf, MADERA_RATE_ENUM_SIZE,
			      madera_rate_text, madera_rate_val),
	SOC_VALUE_ENUM_SINGLE(SND_SOC_NOPM, 5, 0xf, MADERA_RATE_ENUM_SIZE,
			      madera_rate_text, madera_rate_val),
	SOC_VALUE_ENUM_SINGLE(SND_SOC_NOPM, 6, 0xf, MADERA_RATE_ENUM_SIZE,
			      madera_rate_text, madera_rate_val),
};

const struct snd_kcontrol_new madera_adsp_rate_controls[] = {
	SOC_ENUM_EXT("DSP1 Rate", madera_adsp_rate_enum[0],
		     madera_adsp_rate_get, madera_adsp_rate_put),
	SOC_ENUM_EXT("DSP2 Rate", madera_adsp_rate_enum[1],
		     madera_adsp_rate_get, madera_adsp_rate_put),
	SOC_ENUM_EXT("DSP3 Rate", madera_adsp_rate_enum[2],
		     madera_adsp_rate_get, madera_adsp_rate_put),
	SOC_ENUM_EXT("DSP4 Rate", madera_adsp_rate_enum[3],
		     madera_adsp_rate_get, madera_adsp_rate_put),
	SOC_ENUM_EXT("DSP5 Rate", madera_adsp_rate_enum[4],
		     madera_adsp_rate_get, madera_adsp_rate_put),
	SOC_ENUM_EXT("DSP6 Rate", madera_adsp_rate_enum[5],
		     madera_adsp_rate_get, madera_adsp_rate_put),
	SOC_ENUM_EXT("DSP7 Rate", madera_adsp_rate_enum[6],
		     madera_adsp_rate_get, madera_adsp_rate_put),
};
EXPORT_SYMBOL_GPL(madera_adsp_rate_controls);

static int madera_write_adsp_clk_setting(struct wm_adsp *dsp, unsigned int freq)
{
	struct madera_priv *priv = snd_soc_codec_get_drvdata(dsp->codec);
	unsigned int mask, val;
	int ret;

	mask = MADERA_DSP_RATE_MASK;
	val = priv->adsp_rate_cache[dsp->num - 1] << MADERA_DSP_RATE_SHIFT;

	switch (priv->madera->type) {
	case CS47L35:
	case CS47L85:
	case WM1840:
		/* use legacy frequency registers */
		mask |= MADERA_DSP_CLK_SEL_MASK;
		val |= (freq << MADERA_DSP_CLK_SEL_SHIFT);
		break;
	default:
		/* Configure exact dsp frequency */
		dev_dbg(priv->madera->dev, "Set DSP frequency to 0x%x\n", freq);

		ret = regmap_write(dsp->regmap,
				   dsp->base + MADERA_DSP_CONFIG_2_OFFS, freq);
		if (ret)
			goto err;
		break;
	}

	ret = regmap_update_bits(dsp->regmap,
				 dsp->base + MADERA_DSP_CONFIG_1_OFFS,
				 mask, val);
	dev_dbg(priv->madera->dev, "Set DSP clocking to 0x%x\n", val);

	return 0;

err:
	dev_err(dsp->dev, "Failed to set DSP%d clock: %d\n", dsp->num, ret);

	return ret;
}

int madera_set_adsp_clk(struct wm_adsp *dsp, unsigned int freq)
{
	struct madera_priv *priv = snd_soc_codec_get_drvdata(dsp->codec);
	struct madera *madera = priv->madera;
	const unsigned int *cur_sources;
	unsigned int cur, new;
	int lim, ret, err;

	ret = regmap_read(dsp->regmap,  dsp->base, &cur);
	if (ret) {
		dev_err(madera->dev,
			"Failed to read current DSP rate: %d\n", ret);
		return ret;
	}

	cur &= MADERA_DSP_RATE_MASK;
	new = priv->adsp_rate_cache[dsp->num - 1] << MADERA_DSP_RATE_SHIFT;

	if (new == cur) {
		dev_dbg(madera->dev, "DSP rate not changed\n");
		return madera_write_adsp_clk_setting(dsp, freq);
	}

	ret = priv->get_sources(dsp->base, &cur_sources, &lim);
	if (ret) {
		dev_err(madera->dev,
			"Failed to get sources for DSP: %d\n", ret);
		return ret;
	}

	mutex_lock(&priv->rate_lock);

	ret = madera_cache_and_clear_sources(priv, cur_sources,
					     priv->mixer_sources_cache, lim);
	if (ret) {
		dev_err(madera->dev,
			"failed to cache and clear DSP sources %d\n", ret);
		goto out;
	}

	madera_spin_sysclk(priv);
	ret = madera_write_adsp_clk_setting(dsp, freq);
	madera_spin_sysclk(priv);

out:
	err = madera_restore_sources(priv, cur_sources,
				     priv->mixer_sources_cache, lim);

	if (err) {
		dev_err(madera->dev,
			"failed to restore DSP sources %d\n", err);
	}

	mutex_unlock(&priv->rate_lock);
	return ret;
}
EXPORT_SYMBOL_GPL(madera_set_adsp_clk);

int madera_rate_put(struct snd_kcontrol *kcontrol,
		    struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_soc_kcontrol_codec(kcontrol);
	struct madera_priv *priv = snd_soc_codec_get_drvdata(codec);
	struct madera *madera = priv->madera;
	struct soc_enum *e = (struct soc_enum *)kcontrol->private_value;
	const unsigned int *cur_sources;
	unsigned int mask, val, cur;
	int lim, ret, err;

	if (ucontrol->value.enumerated.item[0] > e->items - 1)
		return -EINVAL;

	val = e->values[ucontrol->value.enumerated.item[0]] << e->shift_l;
	mask = e->mask << e->shift_l;

	ret = regmap_read(madera->regmap, e->reg, &cur);
	if (ret) {
		dev_err(madera->dev, "Failed to read current reg: %d\n", ret);
		return ret;
	}

	if ((cur & mask) == (val & mask))
		return 0;

	ret = priv->get_sources(e->reg, &cur_sources, &lim);
	if (ret) {
		dev_err(madera->dev, "Failed to get sources for 0x%08x: %d\n",
			e->reg, ret);
		return ret;
	}

	mutex_lock(&priv->rate_lock);

	ret = madera_cache_and_clear_sources(priv, cur_sources,
					     priv->mixer_sources_cache, lim);
	if (ret) {
		dev_err(madera->dev,
			"%s Failed to cache and clear sources %d\n",
			__func__, ret);
		goto out;
	}

	/* Apply the rate through the original callback */
	madera_spin_sysclk(priv);
	ret = snd_soc_update_bits(codec, e->reg, mask, val);
	if (ret > 0)
		ret = 0; /* snd_soc_update_bits returns 1 if bits changed ok */
	madera_spin_sysclk(priv);

out:
	err = madera_restore_sources(priv, cur_sources,
				     priv->mixer_sources_cache, lim);
	if (err) {
		dev_err(madera->dev,
			"%s Failed to restore sources %d\n",
			__func__, err);
	}

	mutex_unlock(&priv->rate_lock);
	return ret;
}
EXPORT_SYMBOL_GPL(madera_rate_put);

static const struct snd_soc_dapm_route madera_mono_routes[] = {
	{ "OUT1R", NULL, "OUT1L" },
	{ "OUT2R", NULL, "OUT2L" },
	{ "OUT3R", NULL, "OUT3L" },
	{ "OUT4R", NULL, "OUT4L" },
	{ "OUT5R", NULL, "OUT5L" },
	{ "OUT6R", NULL, "OUT6L" },
};

static void madera_configure_input_mode(struct madera *madera)
{
	unsigned int val, dig_mode, ana_mode_l, ana_mode_r;
	int max_analogue_inputs, num_dmic_clksrc, i;

	switch (madera->type) {
	case CS47L35:
		max_analogue_inputs = 2;
		num_dmic_clksrc = 0;
		break;
	case CS47L85:
	case WM1840:
		max_analogue_inputs = 3;
		num_dmic_clksrc = 0;
		break;
	default:
		max_analogue_inputs = 2;
		num_dmic_clksrc = 5;
		break;
	}

	for (i = 0; i < num_dmic_clksrc; i++) {
		val = madera->pdata.codec.dmic_clksrc[i] <<
			MADERA_IN1_DMICCLK_SRC_SHIFT;
		regmap_update_bits(madera->regmap,
				   MADERA_IN1R_CONTROL + (i * 8),
				   MADERA_IN1_DMICCLK_SRC_MASK, val);
		dev_dbg(madera->dev, "IN%d DMICCLK_SRC=0x%x\n", i + 1, val);
	}

	/* Initialize input modes from the A settings. For muxed inputs the
	 * B settings will be applied if the mux is changed
	 */
	for (i = 0; i < max_analogue_inputs; i++) {
		dev_dbg(madera->dev, "IN%d mode %d:%d:%d:%d\n", i + 1,
			madera->pdata.codec.inmode[i][0],
			madera->pdata.codec.inmode[i][1],
			madera->pdata.codec.inmode[i][2],
			madera->pdata.codec.inmode[i][3]);

		dig_mode = madera->pdata.codec.dmic_ref[i] <<
			   MADERA_IN1_DMIC_SUP_SHIFT;

		switch (madera->pdata.codec.inmode[i][0]) {
		case MADERA_INMODE_DIFF:
			ana_mode_l = 0;
			break;
		case MADERA_INMODE_SE:
			ana_mode_l = 1 << MADERA_IN1L_SRC_SE_SHIFT;
			break;
		case MADERA_INMODE_DMIC:
			ana_mode_l = 0;
			dig_mode |= 1 << MADERA_IN1_MODE_SHIFT;
			break;
		default:
			dev_warn(madera->dev,
				 "IN%dAL Illegal inmode %d ignored\n",
				 i + 1, madera->pdata.codec.inmode[i][0]);
			continue;
		}

		switch (madera->pdata.codec.inmode[i][1]) {
		case MADERA_INMODE_DIFF:
		case MADERA_INMODE_DMIC:
			ana_mode_r = 0;
			break;
		case MADERA_INMODE_SE:
			ana_mode_r = 1 << MADERA_IN1R_SRC_SE_SHIFT;
			break;
		default:
			dev_warn(madera->dev,
				 "IN%dAR Illegal inmode %d ignored\n",
				 i + 1, madera->pdata.codec.inmode[i][1]);
			continue;
		}

		dev_dbg(madera->dev,
			"IN%dA DMIC mode=0x%x Analogue mode=0x%x,0x%x\n",
			i + 1, dig_mode, ana_mode_l, ana_mode_r);

		regmap_update_bits(madera->regmap,
				   MADERA_IN1L_CONTROL + (i * 8),
				   MADERA_IN1_DMIC_SUP_MASK |
				   MADERA_IN1_MODE_MASK,
				   dig_mode);

		regmap_update_bits(madera->regmap,
				   MADERA_ADC_DIGITAL_VOLUME_1L + (i * 8),
				   MADERA_IN1L_SRC_SE_MASK, ana_mode_l);

		regmap_update_bits(madera->regmap,
				   MADERA_ADC_DIGITAL_VOLUME_1R + (i * 8),
				   MADERA_IN1R_SRC_SE_MASK, ana_mode_r);
	}
}

int madera_init_inputs(struct snd_soc_codec *codec,
		       const char * const *dmic_inputs, int n_dmic_inputs,
		       const char * const *dmic_refs, int n_dmic_refs)
{
	struct snd_soc_dapm_context *dapm = snd_soc_codec_get_dapm(codec);
	struct madera_priv *priv = snd_soc_codec_get_drvdata(codec);
	struct madera *madera = priv->madera;
	unsigned int ref;
	int i, ret;
	struct snd_soc_dapm_route routes[2];

	memset(&routes, 0, sizeof(routes));

	madera_configure_input_mode(madera);

	for (i = 0; i < n_dmic_inputs / 2; ++i) {
		ref = madera->pdata.codec.dmic_ref[i];
		if (ref >= n_dmic_refs) {
			dev_err(madera->dev,
				"Illegal DMIC ref %u for IN%d\n", ref, i);
			return -EINVAL;
		}

		routes[0].source = dmic_refs[ref];
		routes[1].source = dmic_refs[ref];
		routes[0].sink = dmic_inputs[i * 2];
		routes[1].sink = dmic_inputs[(i * 2) + 1];

		ret = snd_soc_dapm_add_routes(dapm, routes, 2);
		if (ret < 0) {
			dev_err(madera->dev,
				"Failed to add routes for DMIC refs: %d\n",
				ret);
			return ret;
		}
	}

	return 0;
}
EXPORT_SYMBOL_GPL(madera_init_inputs);

int madera_init_outputs(struct snd_soc_codec *codec)
{
	struct snd_soc_dapm_context *dapm = snd_soc_codec_get_dapm(codec);
	struct madera_priv *priv = snd_soc_codec_get_drvdata(codec);
	struct madera *madera = priv->madera;
	const struct madera_codec_pdata *pdata = &madera->pdata.codec;
	unsigned int val;
	int i;

	for (i = 0; i < ARRAY_SIZE(pdata->out_mono); i++) {
		/* Default is 0 so noop with defaults */
		if (pdata->out_mono[i]) {
			val = MADERA_OUT1_MONO;
			snd_soc_dapm_add_routes(dapm,
						&madera_mono_routes[i], 1);
		} else {
			val = 0;
		}

		regmap_update_bits(madera->regmap,
				   MADERA_OUTPUT_PATH_CONFIG_1L + (i * 8),
				   MADERA_OUT1_MONO, val);

		dev_dbg(madera->dev, "OUT%d mono=0x%x\n", i + 1, val);
	}

	for (i = 0; i < MADERA_MAX_PDM_SPK; i++) {
		dev_dbg(madera->dev, "PDM%d fmt=0x%x mute=0x%x\n", i + 1,
			pdata->pdm_fmt[i], pdata->pdm_mute[i]);

		if (pdata->pdm_mute[i])
			regmap_update_bits(madera->regmap,
					   MADERA_PDM_SPK1_CTRL_1 + (i * 2),
					   MADERA_SPK1_MUTE_ENDIAN_MASK |
					   MADERA_SPK1_MUTE_SEQ1_MASK,
					   pdata->pdm_mute[i]);

		if (pdata->pdm_fmt[i])
			regmap_update_bits(madera->regmap,
					   MADERA_PDM_SPK1_CTRL_2 + (i * 2),
					   MADERA_SPK1_FMT_MASK,
					   pdata->pdm_fmt[i]);
	}

	return 0;
}
EXPORT_SYMBOL_GPL(madera_init_outputs);

int madera_init_drc(struct snd_soc_codec *codec)
{
	struct snd_soc_dapm_context *dapm = snd_soc_codec_get_dapm(codec);
	struct madera_priv *priv = snd_soc_codec_get_drvdata(codec);
	struct madera *madera = priv->madera;
	bool enable_drc1 = false, enable_drc2 = false;
	int i;

	for (i = 0; i < ARRAY_SIZE(madera->pdata.gpio_defaults); i++) {
		switch (madera->pdata.gpio_defaults[i] & MADERA_GP1_FN_MASK) {
		case MADERA_GP_FN_DRC1_SIGNAL_DETECT:
			enable_drc1 = true;
			break;
		case MADERA_GP_FN_DRC2_SIGNAL_DETECT:
			enable_drc2 = true;
			break;
		default:
			break;
		}
	}

	if (enable_drc1)
		snd_soc_dapm_enable_pin(dapm, "DRC1 Signal Activity");
	else
		snd_soc_dapm_disable_pin(dapm, "DRC1 Signal Activity");

	if (enable_drc2)
		snd_soc_dapm_enable_pin(dapm, "DRC2 Signal Activity");
	else
		snd_soc_dapm_disable_pin(dapm, "DRC2 Signal Activity");

	return 0;
}
EXPORT_SYMBOL_GPL(madera_init_drc);

int madera_init_bus_error_irq(struct snd_soc_codec *codec, int dsp_num,
			      irq_handler_t handler)
{
	struct madera_priv *priv = snd_soc_codec_get_drvdata(codec);
	struct madera *madera = priv->madera;
	int ret;

	ret = madera_request_irq(madera,
				 madera_dsp_bus_error_irqs[dsp_num],
				 "ADSP2 bus error",
				 handler,
				 &priv->adsp[dsp_num]);
	if (ret)
		dev_err(madera->dev,
			"Failed to request DSP Lock region IRQ: %d\n", ret);

	return ret;
}
EXPORT_SYMBOL_GPL(madera_init_bus_error_irq);

void madera_destroy_bus_error_irq(struct snd_soc_codec *codec, int dsp_num)
{
	struct madera_priv *priv = snd_soc_codec_get_drvdata(codec);
	struct madera *madera = priv->madera;

	madera_free_irq(madera,
			madera_dsp_bus_error_irqs[dsp_num],
			&priv->adsp[dsp_num]);
}
EXPORT_SYMBOL_GPL(madera_destroy_bus_error_irq);

const char * const madera_mixer_texts[] = {
	"None",
	"Tone Generator 1",
	"Tone Generator 2",
	"Haptics",
	"AEC1",
	"AEC2",
	"Mic Mute Mixer",
	"Noise Generator",
	"IN1L",
	"IN1R",
	"IN2L",
	"IN2R",
	"IN3L",
	"IN3R",
	"IN4L",
	"IN4R",
	"IN5L",
	"IN5R",
	"IN6L",
	"IN6R",
	"AIF1RX1",
	"AIF1RX2",
	"AIF1RX3",
	"AIF1RX4",
	"AIF1RX5",
	"AIF1RX6",
	"AIF1RX7",
	"AIF1RX8",
	"AIF2RX1",
	"AIF2RX2",
	"AIF2RX3",
	"AIF2RX4",
	"AIF2RX5",
	"AIF2RX6",
	"AIF2RX7",
	"AIF2RX8",
	"AIF3RX1",
	"AIF3RX2",
	"AIF4RX1",
	"AIF4RX2",
	"SLIMRX1",
	"SLIMRX2",
	"SLIMRX3",
	"SLIMRX4",
	"SLIMRX5",
	"SLIMRX6",
	"SLIMRX7",
	"SLIMRX8",
	"EQ1",
	"EQ2",
	"EQ3",
	"EQ4",
	"DRC1L",
	"DRC1R",
	"DRC2L",
	"DRC2R",
	"LHPF1",
	"LHPF2",
	"LHPF3",
	"LHPF4",
	"DSP1.1",
	"DSP1.2",
	"DSP1.3",
	"DSP1.4",
	"DSP1.5",
	"DSP1.6",
	"DSP2.1",
	"DSP2.2",
	"DSP2.3",
	"DSP2.4",
	"DSP2.5",
	"DSP2.6",
	"DSP3.1",
	"DSP3.2",
	"DSP3.3",
	"DSP3.4",
	"DSP3.5",
	"DSP3.6",
	"DSP4.1",
	"DSP4.2",
	"DSP4.3",
	"DSP4.4",
	"DSP4.5",
	"DSP4.6",
	"DSP5.1",
	"DSP5.2",
	"DSP5.3",
	"DSP5.4",
	"DSP5.5",
	"DSP5.6",
	"DSP6.1",
	"DSP6.2",
	"DSP6.3",
	"DSP6.4",
	"DSP6.5",
	"DSP6.6",
	"DSP7.1",
	"DSP7.2",
	"DSP7.3",
	"DSP7.4",
	"DSP7.5",
	"DSP7.6",
	"ASRC1IN1L",
	"ASRC1IN1R",
	"ASRC1IN2L",
	"ASRC1IN2R",
	"ASRC2IN1L",
	"ASRC2IN1R",
	"ASRC2IN2L",
	"ASRC2IN2R",
	"ISRC1INT1",
	"ISRC1INT2",
	"ISRC1INT3",
	"ISRC1INT4",
	"ISRC1DEC1",
	"ISRC1DEC2",
	"ISRC1DEC3",
	"ISRC1DEC4",
	"ISRC2INT1",
	"ISRC2INT2",
	"ISRC2INT3",
	"ISRC2INT4",
	"ISRC2DEC1",
	"ISRC2DEC2",
	"ISRC2DEC3",
	"ISRC2DEC4",
	"ISRC3INT1",
	"ISRC3INT2",
	"ISRC3INT3",
	"ISRC3INT4",
	"ISRC3DEC1",
	"ISRC3DEC2",
	"ISRC3DEC3",
	"ISRC3DEC4",
	"ISRC4INT1",
	"ISRC4INT2",
	"ISRC4DEC1",
	"ISRC4DEC2",
	"DFC1",
	"DFC2",
	"DFC3",
	"DFC4",
	"DFC5",
	"DFC6",
	"DFC7",
	"DFC8",
};
EXPORT_SYMBOL_GPL(madera_mixer_texts);

unsigned int madera_mixer_values[] = {
	0x00,	/* None */
	0x04,	/* Tone Generator 1 */
	0x05,	/* Tone Generator 2 */
	0x06,	/* Haptics */
	0x08,	/* AEC */
	0x09,	/* AEC2 */
	0x0c,	/* Noise mixer */
	0x0d,	/* Comfort noise */
	0x10,	/* IN1L */
	0x11,
	0x12,
	0x13,
	0x14,
	0x15,
	0x16,
	0x17,
	0x18,
	0x19,
	0x1A,
	0x1B,
	0x20,	/* AIF1RX1 */
	0x21,
	0x22,
	0x23,
	0x24,
	0x25,
	0x26,
	0x27,
	0x28,	/* AIF2RX1 */
	0x29,
	0x2a,
	0x2b,
	0x2c,
	0x2d,
	0x2e,
	0x2f,
	0x30,	/* AIF3RX1 */
	0x31,
	0x34,	/* AIF4RX1 */
	0x35,
	0x38,	/* SLIMRX1 */
	0x39,
	0x3a,
	0x3b,
	0x3c,
	0x3d,
	0x3e,
	0x3f,
	0x50,	/* EQ1 */
	0x51,
	0x52,
	0x53,
	0x58,	/* DRC1L */
	0x59,
	0x5a,
	0x5b,
	0x60,	/* LHPF1 */
	0x61,
	0x62,
	0x63,
	0x68,	/* DSP1.1 */
	0x69,
	0x6a,
	0x6b,
	0x6c,
	0x6d,
	0x70,	/* DSP2.1 */
	0x71,
	0x72,
	0x73,
	0x74,
	0x75,
	0x78,	/* DSP3.1 */
	0x79,
	0x7a,
	0x7b,
	0x7c,
	0x7d,
	0x80,	/* DSP4.1 */
	0x81,
	0x82,
	0x83,
	0x84,
	0x85,
	0x88,	/* DSP5.1 */
	0x89,
	0x8a,
	0x8b,
	0x8c,
	0x8d,
	0xc0,	/* DSP6.1 */
	0xc1,
	0xc2,
	0xc3,
	0xc4,
	0xc5,
	0xc8,	/* DSP7.1 */
	0xc9,
	0xca,
	0xcb,
	0xcc,
	0xcd,
	0x90,	/* ASRC1IN1L */
	0x91,
	0x92,
	0x93,
	0x94,	/* ASRC2IN1L */
	0x95,
	0x96,
	0x97,
	0xa0,	/* ISRC1INT1 */
	0xa1,
	0xa2,
	0xa3,
	0xa4,	/* ISRC1DEC1 */
	0xa5,
	0xa6,
	0xa7,
	0xa8,	/* ISRC2DEC1 */
	0xa9,
	0xaa,
	0xab,
	0xac,	/* ISRC2INT1 */
	0xad,
	0xae,
	0xaf,
	0xb0,	/* ISRC3DEC1 */
	0xb1,
	0xb2,
	0xb3,
	0xb4,	/* ISRC3INT1 */
	0xb5,
	0xb6,
	0xb7,
	0xb8,	/* ISRC4INT1 */
	0xb9,
	0xbc,	/* ISRC4DEC1 */
	0xbd,
	0xf8,	/* DFC1 */
	0xf9,
	0xfa,
	0xfb,
	0xfc,
	0xfd,
	0xfe,
	0xff,	/* DFC8 */
};
EXPORT_SYMBOL_GPL(madera_mixer_values);

const DECLARE_TLV_DB_SCALE(madera_ana_tlv, 0, 100, 0);
EXPORT_SYMBOL_GPL(madera_ana_tlv);

const DECLARE_TLV_DB_SCALE(madera_eq_tlv, -1200, 100, 0);
EXPORT_SYMBOL_GPL(madera_eq_tlv);

const DECLARE_TLV_DB_SCALE(madera_digital_tlv, -6400, 50, 0);
EXPORT_SYMBOL_GPL(madera_digital_tlv);

const DECLARE_TLV_DB_SCALE(madera_noise_tlv, -13200, 600, 0);
EXPORT_SYMBOL_GPL(madera_noise_tlv);

const DECLARE_TLV_DB_SCALE(madera_ng_tlv, -12000, 600, 0);
EXPORT_SYMBOL_GPL(madera_ng_tlv);

const DECLARE_TLV_DB_SCALE(madera_mixer_tlv, -3200, 100, 0);
EXPORT_SYMBOL_GPL(madera_mixer_tlv);

const char * const madera_sample_rate_text[MADERA_SAMPLE_RATE_ENUM_SIZE] = {
	"12kHz", "24kHz", "48kHz", "96kHz", "192kHz", "384kHz",
	"11.025kHz", "22.05kHz", "44.1kHz", "88.2kHz", "176.4kHz", "352.8kHz",
	"4kHz", "8kHz", "16kHz", "32kHz",
};
EXPORT_SYMBOL_GPL(madera_sample_rate_text);

const unsigned int madera_sample_rate_val[MADERA_SAMPLE_RATE_ENUM_SIZE] = {
	0x01, 0x02, 0x03, 0x04, 0x05, 0x06,
	0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E,
	0x10, 0x11, 0x12, 0x13,
};
EXPORT_SYMBOL_GPL(madera_sample_rate_val);

const char *madera_sample_rate_val_to_name(unsigned int rate_val)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(madera_sample_rate_val); ++i) {
		if (madera_sample_rate_val[i] == rate_val)
			return madera_sample_rate_text[i];
	}

	return "Illegal";
}
EXPORT_SYMBOL_GPL(madera_sample_rate_val_to_name);

const struct soc_enum madera_sample_rate[] = {
	SOC_VALUE_ENUM_SINGLE(MADERA_SAMPLE_RATE_2,
			      MADERA_SAMPLE_RATE_2_SHIFT, 0x1f,
			      MADERA_SAMPLE_RATE_ENUM_SIZE,
			      madera_sample_rate_text,
			      madera_sample_rate_val),
	SOC_VALUE_ENUM_SINGLE(MADERA_SAMPLE_RATE_3,
			      MADERA_SAMPLE_RATE_3_SHIFT, 0x1f,
			      MADERA_SAMPLE_RATE_ENUM_SIZE,
			      madera_sample_rate_text,
			      madera_sample_rate_val),
	SOC_VALUE_ENUM_SINGLE(MADERA_ASYNC_SAMPLE_RATE_2,
			      MADERA_ASYNC_SAMPLE_RATE_2_SHIFT, 0x1f,
			      MADERA_SAMPLE_RATE_ENUM_SIZE,
			      madera_sample_rate_text,
			      madera_sample_rate_val),

};
EXPORT_SYMBOL_GPL(madera_sample_rate);

const char * const madera_rate_text[MADERA_RATE_ENUM_SIZE] = {
	"SYNCCLK rate 1", "SYNCCLK rate 2", "SYNCCLK rate 3",
	"ASYNCCLK rate 1", "ASYNCCLK rate 2",
};
EXPORT_SYMBOL_GPL(madera_rate_text);

const unsigned int madera_rate_val[MADERA_RATE_ENUM_SIZE] = {
	0x0, 0x1, 0x2, 0x8, 0x9,
};
EXPORT_SYMBOL_GPL(madera_rate_val);

const struct soc_enum madera_output_rate =
	SOC_VALUE_ENUM_SINGLE(MADERA_OUTPUT_RATE_1,
			      MADERA_OUT_RATE_SHIFT,
			      0x0f,
			      MADERA_SYNC_RATE_ENUM_SIZE,
			      madera_rate_text,
			      madera_rate_val);
EXPORT_SYMBOL_GPL(madera_output_rate);

const struct soc_enum madera_input_rate[] = {
	SOC_VALUE_ENUM_SINGLE(MADERA_IN1L_RATE_CONTROL,
			      MADERA_IN1L_RATE_SHIFT,
			      MADERA_IN1L_RATE_MASK >> MADERA_IN1L_RATE_SHIFT,
			      MADERA_SYNC_RATE_ENUM_SIZE,
			      madera_rate_text,
			      madera_rate_val),
	SOC_VALUE_ENUM_SINGLE(MADERA_IN1R_RATE_CONTROL,
			      MADERA_IN1R_RATE_SHIFT,
			      MADERA_IN1R_RATE_MASK >> MADERA_IN1R_RATE_SHIFT,
			      MADERA_SYNC_RATE_ENUM_SIZE,
			      madera_rate_text,
			      madera_rate_val),
	SOC_VALUE_ENUM_SINGLE(MADERA_IN2L_RATE_CONTROL,
			      MADERA_IN2L_RATE_SHIFT,
			      MADERA_IN2L_RATE_MASK >> MADERA_IN2L_RATE_SHIFT,
			      MADERA_SYNC_RATE_ENUM_SIZE,
			      madera_rate_text,
			      madera_rate_val),
	SOC_VALUE_ENUM_SINGLE(MADERA_IN2R_RATE_CONTROL,
			      MADERA_IN2R_RATE_SHIFT,
			      MADERA_IN2R_RATE_MASK >> MADERA_IN2R_RATE_SHIFT,
			      MADERA_SYNC_RATE_ENUM_SIZE,
			      madera_rate_text,
			      madera_rate_val),
	SOC_VALUE_ENUM_SINGLE(MADERA_IN3L_RATE_CONTROL,
			      MADERA_IN3L_RATE_SHIFT,
			      MADERA_IN3L_RATE_MASK >> MADERA_IN3L_RATE_SHIFT,
			      MADERA_SYNC_RATE_ENUM_SIZE,
			      madera_rate_text,
			      madera_rate_val),
	SOC_VALUE_ENUM_SINGLE(MADERA_IN3R_RATE_CONTROL,
			      MADERA_IN3R_RATE_SHIFT,
			      MADERA_IN3R_RATE_MASK >> MADERA_IN3R_RATE_SHIFT,
			      MADERA_SYNC_RATE_ENUM_SIZE,
			      madera_rate_text,
			      madera_rate_val),
	SOC_VALUE_ENUM_SINGLE(MADERA_IN4L_RATE_CONTROL,
			      MADERA_IN4L_RATE_SHIFT,
			      MADERA_IN4L_RATE_MASK >> MADERA_IN4L_RATE_SHIFT,
			      MADERA_SYNC_RATE_ENUM_SIZE,
			      madera_rate_text,
			      madera_rate_val),
	SOC_VALUE_ENUM_SINGLE(MADERA_IN4R_RATE_CONTROL,
			      MADERA_IN4R_RATE_SHIFT,
			      MADERA_IN4R_RATE_MASK >> MADERA_IN4R_RATE_SHIFT,
			      MADERA_SYNC_RATE_ENUM_SIZE,
			      madera_rate_text,
			      madera_rate_val),
	SOC_VALUE_ENUM_SINGLE(MADERA_IN5L_RATE_CONTROL,
			      MADERA_IN5L_RATE_SHIFT,
			      MADERA_IN5L_RATE_MASK >> MADERA_IN5L_RATE_SHIFT,
			      MADERA_SYNC_RATE_ENUM_SIZE,
			      madera_rate_text,
			      madera_rate_val),
	SOC_VALUE_ENUM_SINGLE(MADERA_IN5R_RATE_CONTROL,
			      MADERA_IN5R_RATE_SHIFT,
			      MADERA_IN5R_RATE_MASK >> MADERA_IN5R_RATE_SHIFT,
			      MADERA_SYNC_RATE_ENUM_SIZE,
			      madera_rate_text,
			      madera_rate_val),
};
EXPORT_SYMBOL_GPL(madera_input_rate);

const char * const madera_dfc_width_text[MADERA_DFC_WIDTH_ENUM_SIZE] = {
	"8bit", "16bit", "20bit", "24bit", "32bit",
};
EXPORT_SYMBOL_GPL(madera_dfc_width_text);

const unsigned int madera_dfc_width_val[MADERA_DFC_WIDTH_ENUM_SIZE] = {
	7, 15, 19, 23, 31,
};
EXPORT_SYMBOL_GPL(madera_dfc_width_val);

const char * const madera_dfc_type_text[MADERA_DFC_TYPE_ENUM_SIZE] = {
	"Fixed", "Unsigned Fixed", "Single Precision Floating",
	"Half Precision Floating", "Arm Alternative Floating",
};
EXPORT_SYMBOL_GPL(madera_dfc_type_text);

const unsigned int madera_dfc_type_val[MADERA_DFC_TYPE_ENUM_SIZE] = {
	0, 1, 2, 4, 5,
};
EXPORT_SYMBOL_GPL(madera_dfc_type_val);

const struct soc_enum madera_dfc_width[] = {
	SOC_VALUE_ENUM_SINGLE(MADERA_DFC1_RX,
			      MADERA_DFC1_RX_DATA_WIDTH_SHIFT,
			      MADERA_DFC1_RX_DATA_WIDTH_MASK >>
			      MADERA_DFC1_RX_DATA_WIDTH_SHIFT,
			      ARRAY_SIZE(madera_dfc_width_text),
			      madera_dfc_width_text,
			      madera_dfc_width_val),
	SOC_VALUE_ENUM_SINGLE(MADERA_DFC1_TX,
			      MADERA_DFC1_TX_DATA_WIDTH_SHIFT,
			      MADERA_DFC1_TX_DATA_WIDTH_MASK >>
			      MADERA_DFC1_TX_DATA_WIDTH_SHIFT,
			      ARRAY_SIZE(madera_dfc_width_text),
			      madera_dfc_width_text,
			      madera_dfc_width_val),
	SOC_VALUE_ENUM_SINGLE(MADERA_DFC2_RX,
			      MADERA_DFC1_RX_DATA_WIDTH_SHIFT,
			      MADERA_DFC1_RX_DATA_WIDTH_MASK >>
			      MADERA_DFC1_RX_DATA_WIDTH_SHIFT,
			      ARRAY_SIZE(madera_dfc_width_text),
			      madera_dfc_width_text,
			      madera_dfc_width_val),
	SOC_VALUE_ENUM_SINGLE(MADERA_DFC2_TX,
			      MADERA_DFC1_TX_DATA_WIDTH_SHIFT,
			      MADERA_DFC1_TX_DATA_WIDTH_MASK >>
			      MADERA_DFC1_TX_DATA_WIDTH_SHIFT,
			      ARRAY_SIZE(madera_dfc_width_text),
			      madera_dfc_width_text,
			      madera_dfc_width_val),
	SOC_VALUE_ENUM_SINGLE(MADERA_DFC3_RX,
			      MADERA_DFC1_RX_DATA_WIDTH_SHIFT,
			      MADERA_DFC1_RX_DATA_WIDTH_MASK >>
			      MADERA_DFC1_RX_DATA_WIDTH_SHIFT,
			      ARRAY_SIZE(madera_dfc_width_text),
			      madera_dfc_width_text,
			      madera_dfc_width_val),
	SOC_VALUE_ENUM_SINGLE(MADERA_DFC3_TX,
			      MADERA_DFC1_TX_DATA_WIDTH_SHIFT,
			      MADERA_DFC1_TX_DATA_WIDTH_MASK >>
			      MADERA_DFC1_TX_DATA_WIDTH_SHIFT,
			      ARRAY_SIZE(madera_dfc_width_text),
			      madera_dfc_width_text,
			      madera_dfc_width_val),
	SOC_VALUE_ENUM_SINGLE(MADERA_DFC4_RX,
			      MADERA_DFC1_RX_DATA_WIDTH_SHIFT,
			      MADERA_DFC1_RX_DATA_WIDTH_MASK >>
			      MADERA_DFC1_RX_DATA_WIDTH_SHIFT,
			      ARRAY_SIZE(madera_dfc_width_text),
			      madera_dfc_width_text,
			      madera_dfc_width_val),
	SOC_VALUE_ENUM_SINGLE(MADERA_DFC4_TX,
			      MADERA_DFC1_TX_DATA_WIDTH_SHIFT,
			      MADERA_DFC1_TX_DATA_WIDTH_MASK >>
			      MADERA_DFC1_TX_DATA_WIDTH_SHIFT,
			      ARRAY_SIZE(madera_dfc_width_text),
			      madera_dfc_width_text,
			      madera_dfc_width_val),
	SOC_VALUE_ENUM_SINGLE(MADERA_DFC5_RX,
			      MADERA_DFC1_RX_DATA_WIDTH_SHIFT,
			      MADERA_DFC1_RX_DATA_WIDTH_MASK >>
			      MADERA_DFC1_RX_DATA_WIDTH_SHIFT,
			      ARRAY_SIZE(madera_dfc_width_text),
			      madera_dfc_width_text,
			      madera_dfc_width_val),
	SOC_VALUE_ENUM_SINGLE(MADERA_DFC5_TX,
			      MADERA_DFC1_TX_DATA_WIDTH_SHIFT,
			      MADERA_DFC1_TX_DATA_WIDTH_MASK >>
			      MADERA_DFC1_TX_DATA_WIDTH_SHIFT,
			      ARRAY_SIZE(madera_dfc_width_text),
			      madera_dfc_width_text,
			      madera_dfc_width_val),
	SOC_VALUE_ENUM_SINGLE(MADERA_DFC6_RX,
			      MADERA_DFC1_RX_DATA_WIDTH_SHIFT,
			      MADERA_DFC1_RX_DATA_WIDTH_MASK >>
			      MADERA_DFC1_RX_DATA_WIDTH_SHIFT,
			      ARRAY_SIZE(madera_dfc_width_text),
			      madera_dfc_width_text,
			      madera_dfc_width_val),
	SOC_VALUE_ENUM_SINGLE(MADERA_DFC6_TX,
			      MADERA_DFC1_TX_DATA_WIDTH_SHIFT,
			      MADERA_DFC1_TX_DATA_WIDTH_MASK >>
			      MADERA_DFC1_TX_DATA_WIDTH_SHIFT,
			      ARRAY_SIZE(madera_dfc_width_text),
			      madera_dfc_width_text,
			      madera_dfc_width_val),
	SOC_VALUE_ENUM_SINGLE(MADERA_DFC7_RX,
			      MADERA_DFC1_RX_DATA_WIDTH_SHIFT,
			      MADERA_DFC1_RX_DATA_WIDTH_MASK >>
			      MADERA_DFC1_RX_DATA_WIDTH_SHIFT,
			      ARRAY_SIZE(madera_dfc_width_text),
			      madera_dfc_width_text,
			      madera_dfc_width_val),
	SOC_VALUE_ENUM_SINGLE(MADERA_DFC7_TX,
			      MADERA_DFC1_TX_DATA_WIDTH_SHIFT,
			      MADERA_DFC1_TX_DATA_WIDTH_MASK >>
			      MADERA_DFC1_TX_DATA_WIDTH_SHIFT,
			      ARRAY_SIZE(madera_dfc_width_text),
			      madera_dfc_width_text,
			      madera_dfc_width_val),
	SOC_VALUE_ENUM_SINGLE(MADERA_DFC8_RX,
			      MADERA_DFC1_RX_DATA_WIDTH_SHIFT,
			      MADERA_DFC1_RX_DATA_WIDTH_MASK >>
			      MADERA_DFC1_RX_DATA_WIDTH_SHIFT,
			      ARRAY_SIZE(madera_dfc_width_text),
			      madera_dfc_width_text,
			      madera_dfc_width_val),
	SOC_VALUE_ENUM_SINGLE(MADERA_DFC8_TX,
			      MADERA_DFC1_TX_DATA_WIDTH_SHIFT,
			      MADERA_DFC1_TX_DATA_WIDTH_MASK >>
			      MADERA_DFC1_TX_DATA_WIDTH_SHIFT,
			      ARRAY_SIZE(madera_dfc_width_text),
			      madera_dfc_width_text,
			      madera_dfc_width_val),
};
EXPORT_SYMBOL_GPL(madera_dfc_width);

const struct soc_enum madera_dfc_type[] = {
	SOC_VALUE_ENUM_SINGLE(MADERA_DFC1_RX,
			      MADERA_DFC1_RX_DATA_TYPE_SHIFT,
			      MADERA_DFC1_RX_DATA_TYPE_MASK >>
			      MADERA_DFC1_RX_DATA_TYPE_SHIFT,
			      ARRAY_SIZE(madera_dfc_type_text),
			      madera_dfc_type_text,
			      madera_dfc_type_val),
	SOC_VALUE_ENUM_SINGLE(MADERA_DFC1_TX,
			      MADERA_DFC1_TX_DATA_TYPE_SHIFT,
			      MADERA_DFC1_TX_DATA_TYPE_MASK >>
			      MADERA_DFC1_TX_DATA_TYPE_SHIFT,
			      ARRAY_SIZE(madera_dfc_type_text),
			      madera_dfc_type_text,
			      madera_dfc_type_val),
	SOC_VALUE_ENUM_SINGLE(MADERA_DFC2_RX,
			      MADERA_DFC1_RX_DATA_TYPE_SHIFT,
			      MADERA_DFC1_RX_DATA_TYPE_MASK >>
			      MADERA_DFC1_RX_DATA_TYPE_SHIFT,
			      ARRAY_SIZE(madera_dfc_type_text),
			      madera_dfc_type_text,
			      madera_dfc_type_val),
	SOC_VALUE_ENUM_SINGLE(MADERA_DFC2_TX,
			      MADERA_DFC1_TX_DATA_TYPE_SHIFT,
			      MADERA_DFC1_TX_DATA_TYPE_MASK >>
			      MADERA_DFC1_TX_DATA_TYPE_SHIFT,
			      ARRAY_SIZE(madera_dfc_type_text),
			      madera_dfc_type_text,
			      madera_dfc_type_val),
	SOC_VALUE_ENUM_SINGLE(MADERA_DFC3_RX,
			      MADERA_DFC1_RX_DATA_TYPE_SHIFT,
			      MADERA_DFC1_RX_DATA_TYPE_MASK >>
			      MADERA_DFC1_RX_DATA_TYPE_SHIFT,
			      ARRAY_SIZE(madera_dfc_type_text),
			      madera_dfc_type_text,
			      madera_dfc_type_val),
	SOC_VALUE_ENUM_SINGLE(MADERA_DFC3_TX,
			      MADERA_DFC1_TX_DATA_TYPE_SHIFT,
			      MADERA_DFC1_TX_DATA_TYPE_MASK >>
			      MADERA_DFC1_TX_DATA_TYPE_SHIFT,
			      ARRAY_SIZE(madera_dfc_type_text),
			      madera_dfc_type_text,
			      madera_dfc_type_val),
	SOC_VALUE_ENUM_SINGLE(MADERA_DFC4_RX,
			      MADERA_DFC1_RX_DATA_TYPE_SHIFT,
			      MADERA_DFC1_RX_DATA_TYPE_MASK >>
			      MADERA_DFC1_RX_DATA_TYPE_SHIFT,
			      ARRAY_SIZE(madera_dfc_type_text),
			      madera_dfc_type_text,
			      madera_dfc_type_val),
	SOC_VALUE_ENUM_SINGLE(MADERA_DFC4_TX,
			      MADERA_DFC1_TX_DATA_TYPE_SHIFT,
			      MADERA_DFC1_TX_DATA_TYPE_MASK >>
			      MADERA_DFC1_TX_DATA_TYPE_SHIFT,
			      ARRAY_SIZE(madera_dfc_type_text),
			      madera_dfc_type_text,
			      madera_dfc_type_val),
	SOC_VALUE_ENUM_SINGLE(MADERA_DFC5_RX,
			      MADERA_DFC1_RX_DATA_TYPE_SHIFT,
			      MADERA_DFC1_RX_DATA_TYPE_MASK >>
			      MADERA_DFC1_RX_DATA_TYPE_SHIFT,
			      ARRAY_SIZE(madera_dfc_type_text),
			      madera_dfc_type_text,
			      madera_dfc_type_val),
	SOC_VALUE_ENUM_SINGLE(MADERA_DFC5_TX,
			      MADERA_DFC1_TX_DATA_TYPE_SHIFT,
			      MADERA_DFC1_TX_DATA_TYPE_MASK >>
			      MADERA_DFC1_TX_DATA_TYPE_SHIFT,
			      ARRAY_SIZE(madera_dfc_type_text),
			      madera_dfc_type_text,
			      madera_dfc_type_val),
	SOC_VALUE_ENUM_SINGLE(MADERA_DFC6_RX,
			      MADERA_DFC1_RX_DATA_TYPE_SHIFT,
			      MADERA_DFC1_RX_DATA_TYPE_MASK >>
			      MADERA_DFC1_RX_DATA_TYPE_SHIFT,
			      ARRAY_SIZE(madera_dfc_type_text),
			      madera_dfc_type_text,
			      madera_dfc_type_val),
	SOC_VALUE_ENUM_SINGLE(MADERA_DFC6_TX,
			      MADERA_DFC1_TX_DATA_TYPE_SHIFT,
			      MADERA_DFC1_TX_DATA_TYPE_MASK >>
			      MADERA_DFC1_TX_DATA_TYPE_SHIFT,
			      ARRAY_SIZE(madera_dfc_type_text),
			      madera_dfc_type_text,
			      madera_dfc_type_val),
	SOC_VALUE_ENUM_SINGLE(MADERA_DFC7_RX,
			      MADERA_DFC1_RX_DATA_TYPE_SHIFT,
			      MADERA_DFC1_RX_DATA_TYPE_MASK >>
			      MADERA_DFC1_RX_DATA_TYPE_SHIFT,
			      ARRAY_SIZE(madera_dfc_type_text),
			      madera_dfc_type_text,
			      madera_dfc_type_val),
	SOC_VALUE_ENUM_SINGLE(MADERA_DFC7_TX,
			      MADERA_DFC1_TX_DATA_TYPE_SHIFT,
			      MADERA_DFC1_TX_DATA_TYPE_MASK >>
			      MADERA_DFC1_TX_DATA_TYPE_SHIFT,
			      ARRAY_SIZE(madera_dfc_type_text),
			      madera_dfc_type_text,
			      madera_dfc_type_val),
	SOC_VALUE_ENUM_SINGLE(MADERA_DFC8_RX,
			      MADERA_DFC1_RX_DATA_TYPE_SHIFT,
			      MADERA_DFC1_RX_DATA_TYPE_MASK >>
			      MADERA_DFC1_RX_DATA_TYPE_SHIFT,
			      ARRAY_SIZE(madera_dfc_type_text),
			      madera_dfc_type_text,
			      madera_dfc_type_val),
	SOC_VALUE_ENUM_SINGLE(MADERA_DFC8_TX,
			      MADERA_DFC1_TX_DATA_TYPE_SHIFT,
			      MADERA_DFC1_TX_DATA_TYPE_MASK >>
			      MADERA_DFC1_TX_DATA_TYPE_SHIFT,
			      ARRAY_SIZE(madera_dfc_type_text),
			      madera_dfc_type_text,
			      madera_dfc_type_val),
};
EXPORT_SYMBOL_GPL(madera_dfc_type);

const struct soc_enum madera_fx_rate =
	SOC_VALUE_ENUM_SINGLE(MADERA_FX_CTRL1,
			      MADERA_FX_RATE_SHIFT, 0xf,
			      MADERA_RATE_ENUM_SIZE,
			      madera_rate_text, madera_rate_val);
EXPORT_SYMBOL_GPL(madera_fx_rate);

const struct soc_enum madera_spdif_rate =
	SOC_VALUE_ENUM_SINGLE(MADERA_SPD1_TX_CONTROL,
			      MADERA_SPD1_RATE_SHIFT,
			      0x0f,
			      MADERA_SYNC_RATE_ENUM_SIZE,
			      madera_rate_text,
			      madera_rate_val);
EXPORT_SYMBOL_GPL(madera_spdif_rate);

const struct soc_enum madera_isrc_fsh[] = {
	SOC_VALUE_ENUM_SINGLE(MADERA_ISRC_1_CTRL_1,
			      MADERA_ISRC1_FSH_SHIFT, 0xf,
			      MADERA_RATE_ENUM_SIZE,
			      madera_rate_text, madera_rate_val),
	SOC_VALUE_ENUM_SINGLE(MADERA_ISRC_2_CTRL_1,
			      MADERA_ISRC2_FSH_SHIFT, 0xf,
			      MADERA_RATE_ENUM_SIZE,
			      madera_rate_text, madera_rate_val),
	SOC_VALUE_ENUM_SINGLE(MADERA_ISRC_3_CTRL_1,
			      MADERA_ISRC3_FSH_SHIFT, 0xf,
			      MADERA_RATE_ENUM_SIZE,
			      madera_rate_text, madera_rate_val),
	SOC_VALUE_ENUM_SINGLE(MADERA_ISRC_4_CTRL_1,
			      MADERA_ISRC4_FSH_SHIFT, 0xf,
			      MADERA_RATE_ENUM_SIZE,
			      madera_rate_text, madera_rate_val),

};
EXPORT_SYMBOL_GPL(madera_isrc_fsh);

const struct soc_enum madera_isrc_fsl[] = {
	SOC_VALUE_ENUM_SINGLE(MADERA_ISRC_1_CTRL_2,
			      MADERA_ISRC1_FSL_SHIFT, 0xf,
			      MADERA_RATE_ENUM_SIZE,
			      madera_rate_text, madera_rate_val),
	SOC_VALUE_ENUM_SINGLE(MADERA_ISRC_2_CTRL_2,
			      MADERA_ISRC2_FSL_SHIFT, 0xf,
			      MADERA_RATE_ENUM_SIZE,
			      madera_rate_text, madera_rate_val),
	SOC_VALUE_ENUM_SINGLE(MADERA_ISRC_3_CTRL_2,
			      MADERA_ISRC3_FSL_SHIFT, 0xf,
			      MADERA_RATE_ENUM_SIZE,
			      madera_rate_text, madera_rate_val),
	SOC_VALUE_ENUM_SINGLE(MADERA_ISRC_4_CTRL_2,
			      MADERA_ISRC4_FSL_SHIFT, 0xf,
			      MADERA_RATE_ENUM_SIZE,
			      madera_rate_text, madera_rate_val),

};
EXPORT_SYMBOL_GPL(madera_isrc_fsl);

const struct soc_enum madera_asrc1_rate[] = {
	SOC_VALUE_ENUM_SINGLE(MADERA_ASRC1_RATE1,
			      MADERA_ASRC1_RATE1_SHIFT, 0xf,
			      MADERA_SYNC_RATE_ENUM_SIZE,
			      madera_rate_text, madera_rate_val),
	SOC_VALUE_ENUM_SINGLE(MADERA_ASRC1_RATE2,
			      MADERA_ASRC1_RATE1_SHIFT, 0xf,
			      MADERA_ASYNC_RATE_ENUM_SIZE,
			      madera_rate_text + MADERA_SYNC_RATE_ENUM_SIZE,
			      madera_rate_val + MADERA_SYNC_RATE_ENUM_SIZE),

};
EXPORT_SYMBOL_GPL(madera_asrc1_rate);

const struct soc_enum madera_asrc2_rate[] = {
	SOC_VALUE_ENUM_SINGLE(MADERA_ASRC2_RATE1,
			      MADERA_ASRC2_RATE1_SHIFT, 0xf,
			      MADERA_SYNC_RATE_ENUM_SIZE,
			      madera_rate_text, madera_rate_val),
	SOC_VALUE_ENUM_SINGLE(MADERA_ASRC2_RATE2,
			      MADERA_ASRC2_RATE2_SHIFT, 0xf,
			      MADERA_ASYNC_RATE_ENUM_SIZE,
			      madera_rate_text + MADERA_SYNC_RATE_ENUM_SIZE,
			      madera_rate_val + MADERA_SYNC_RATE_ENUM_SIZE),

};
EXPORT_SYMBOL_GPL(madera_asrc2_rate);

static const char * const madera_vol_ramp_text[] = {
	"0ms/6dB", "0.5ms/6dB", "1ms/6dB", "2ms/6dB", "4ms/6dB", "8ms/6dB",
	"15ms/6dB", "30ms/6dB",
};

SOC_ENUM_SINGLE_DECL(madera_in_vd_ramp,
		     MADERA_INPUT_VOLUME_RAMP,
		     MADERA_IN_VD_RAMP_SHIFT,
		     madera_vol_ramp_text);
EXPORT_SYMBOL_GPL(madera_in_vd_ramp);

SOC_ENUM_SINGLE_DECL(madera_in_vi_ramp,
		     MADERA_INPUT_VOLUME_RAMP,
		     MADERA_IN_VI_RAMP_SHIFT,
		     madera_vol_ramp_text);
EXPORT_SYMBOL_GPL(madera_in_vi_ramp);

SOC_ENUM_SINGLE_DECL(madera_out_vd_ramp,
		     MADERA_OUTPUT_VOLUME_RAMP,
		     MADERA_OUT_VD_RAMP_SHIFT,
		     madera_vol_ramp_text);
EXPORT_SYMBOL_GPL(madera_out_vd_ramp);

SOC_ENUM_SINGLE_DECL(madera_out_vi_ramp,
		     MADERA_OUTPUT_VOLUME_RAMP,
		     MADERA_OUT_VI_RAMP_SHIFT,
		     madera_vol_ramp_text);
EXPORT_SYMBOL_GPL(madera_out_vi_ramp);

static const char * const madera_lhpf_mode_text[] = {
	"Low-pass", "High-pass"
};

SOC_ENUM_SINGLE_DECL(madera_lhpf1_mode,
		     MADERA_HPLPF1_1,
		     MADERA_LHPF1_MODE_SHIFT,
		     madera_lhpf_mode_text);
EXPORT_SYMBOL_GPL(madera_lhpf1_mode);

SOC_ENUM_SINGLE_DECL(madera_lhpf2_mode,
		     MADERA_HPLPF2_1,
		     MADERA_LHPF2_MODE_SHIFT,
		     madera_lhpf_mode_text);
EXPORT_SYMBOL_GPL(madera_lhpf2_mode);

SOC_ENUM_SINGLE_DECL(madera_lhpf3_mode,
		     MADERA_HPLPF3_1,
		     MADERA_LHPF3_MODE_SHIFT,
		     madera_lhpf_mode_text);
EXPORT_SYMBOL_GPL(madera_lhpf3_mode);

SOC_ENUM_SINGLE_DECL(madera_lhpf4_mode,
		     MADERA_HPLPF4_1,
		     MADERA_LHPF4_MODE_SHIFT,
		     madera_lhpf_mode_text);
EXPORT_SYMBOL_GPL(madera_lhpf4_mode);

static const char * const madera_ng_hold_text[] = {
	"30ms", "120ms", "250ms", "500ms",
};

SOC_ENUM_SINGLE_DECL(madera_ng_hold,
		     MADERA_NOISE_GATE_CONTROL,
		     MADERA_NGATE_HOLD_SHIFT,
		     madera_ng_hold_text);
EXPORT_SYMBOL_GPL(madera_ng_hold);

static const char * const madera_in_hpf_cut_text[] = {
	"2.5Hz", "5Hz", "10Hz", "20Hz", "40Hz"
};

SOC_ENUM_SINGLE_DECL(madera_in_hpf_cut_enum,
		     MADERA_HPF_CONTROL,
		     MADERA_IN_HPF_CUT_SHIFT,
		     madera_in_hpf_cut_text);
EXPORT_SYMBOL_GPL(madera_in_hpf_cut_enum);

static const char * const madera_in_dmic_osr_text[MADERA_OSR_ENUM_SIZE] = {
	"384kHz", "768kHz", "1.536MHz", "3.072MHz", "6.144MHz",
};

static const unsigned int madera_in_dmic_osr_val[MADERA_OSR_ENUM_SIZE] = {
	2, 3, 4, 5, 6,
};

const struct soc_enum madera_in_dmic_osr[] = {
	SOC_VALUE_ENUM_SINGLE(MADERA_DMIC1L_CONTROL, MADERA_IN1_OSR_SHIFT,
			      0x7, MADERA_OSR_ENUM_SIZE,
			      madera_in_dmic_osr_text, madera_in_dmic_osr_val),
	SOC_VALUE_ENUM_SINGLE(MADERA_DMIC2L_CONTROL, MADERA_IN2_OSR_SHIFT,
			      0x7, MADERA_OSR_ENUM_SIZE,
			      madera_in_dmic_osr_text, madera_in_dmic_osr_val),
	SOC_VALUE_ENUM_SINGLE(MADERA_DMIC3L_CONTROL, MADERA_IN3_OSR_SHIFT,
			      0x7, MADERA_OSR_ENUM_SIZE,
			      madera_in_dmic_osr_text, madera_in_dmic_osr_val),
	SOC_VALUE_ENUM_SINGLE(MADERA_DMIC4L_CONTROL, MADERA_IN4_OSR_SHIFT,
			      0x7, MADERA_OSR_ENUM_SIZE,
			      madera_in_dmic_osr_text, madera_in_dmic_osr_val),
	SOC_VALUE_ENUM_SINGLE(MADERA_DMIC5L_CONTROL, MADERA_IN5_OSR_SHIFT,
			      0x7, MADERA_OSR_ENUM_SIZE,
			      madera_in_dmic_osr_text, madera_in_dmic_osr_val),
	SOC_VALUE_ENUM_SINGLE(MADERA_DMIC6L_CONTROL, MADERA_IN6_OSR_SHIFT,
			      0x7, MADERA_OSR_ENUM_SIZE,
			      madera_in_dmic_osr_text, madera_in_dmic_osr_val),
};
EXPORT_SYMBOL_GPL(madera_in_dmic_osr);

static const char * const madera_anc_input_src_text[] = {
	"None", "IN1", "IN2", "IN3", "IN4", "IN5", "IN6",
};

static const char * const madera_anc_channel_src_text[] = {
	"None", "Left", "Right", "Combine",
};

const struct soc_enum madera_anc_input_src[] = {
	SOC_ENUM_SINGLE(MADERA_ANC_SRC,
			MADERA_IN_RXANCL_SEL_SHIFT,
			ARRAY_SIZE(madera_anc_input_src_text),
			madera_anc_input_src_text),
	SOC_ENUM_SINGLE(MADERA_FCL_ADC_REFORMATTER_CONTROL,
			MADERA_FCL_MIC_MODE_SEL,
			ARRAY_SIZE(madera_anc_channel_src_text),
			madera_anc_channel_src_text),
	SOC_ENUM_SINGLE(MADERA_ANC_SRC,
			MADERA_IN_RXANCR_SEL_SHIFT,
			ARRAY_SIZE(madera_anc_input_src_text),
			madera_anc_input_src_text),
	SOC_ENUM_SINGLE(MADERA_FCR_ADC_REFORMATTER_CONTROL,
			MADERA_FCR_MIC_MODE_SEL,
			ARRAY_SIZE(madera_anc_channel_src_text),
			madera_anc_channel_src_text),
};
EXPORT_SYMBOL_GPL(madera_anc_input_src);

static const char * const madera_anc_ng_texts[] = {
	"None",	"Internal", "External",
};

SOC_ENUM_SINGLE_DECL(madera_anc_ng_enum, SND_SOC_NOPM, 0, madera_anc_ng_texts);
EXPORT_SYMBOL_GPL(madera_anc_ng_enum);

static const char * const madera_out_anc_src_text[] = {
	"None", "RXANCL", "RXANCR",
};

const struct soc_enum madera_output_anc_src[] = {
	SOC_ENUM_SINGLE(MADERA_OUTPUT_PATH_CONFIG_1L,
			MADERA_OUT1L_ANC_SRC_SHIFT,
			ARRAY_SIZE(madera_out_anc_src_text),
			madera_out_anc_src_text),
	SOC_ENUM_SINGLE(MADERA_OUTPUT_PATH_CONFIG_1R,
			MADERA_OUT1R_ANC_SRC_SHIFT,
			ARRAY_SIZE(madera_out_anc_src_text),
			madera_out_anc_src_text),
	SOC_ENUM_SINGLE(MADERA_OUTPUT_PATH_CONFIG_2L,
			MADERA_OUT2L_ANC_SRC_SHIFT,
			ARRAY_SIZE(madera_out_anc_src_text),
			madera_out_anc_src_text),
	SOC_ENUM_SINGLE(MADERA_OUTPUT_PATH_CONFIG_2R,
			MADERA_OUT2R_ANC_SRC_SHIFT,
			ARRAY_SIZE(madera_out_anc_src_text),
			madera_out_anc_src_text),
	SOC_ENUM_SINGLE(MADERA_OUTPUT_PATH_CONFIG_3L,
			MADERA_OUT3L_ANC_SRC_SHIFT,
			ARRAY_SIZE(madera_out_anc_src_text),
			madera_out_anc_src_text),
	SOC_ENUM_SINGLE(MADERA_OUTPUT_PATH_CONFIG_3R,
			MADERA_OUT3R_ANC_SRC_SHIFT,
			ARRAY_SIZE(madera_out_anc_src_text),
			madera_out_anc_src_text),
	SOC_ENUM_SINGLE(MADERA_OUTPUT_PATH_CONFIG_4L,
			MADERA_OUT4L_ANC_SRC_SHIFT,
			ARRAY_SIZE(madera_out_anc_src_text),
			madera_out_anc_src_text),
	SOC_ENUM_SINGLE(MADERA_OUTPUT_PATH_CONFIG_4R,
			MADERA_OUT4R_ANC_SRC_SHIFT,
			ARRAY_SIZE(madera_out_anc_src_text),
			madera_out_anc_src_text),
	SOC_ENUM_SINGLE(MADERA_OUTPUT_PATH_CONFIG_5L,
			MADERA_OUT5L_ANC_SRC_SHIFT,
			ARRAY_SIZE(madera_out_anc_src_text),
			madera_out_anc_src_text),
	SOC_ENUM_SINGLE(MADERA_OUTPUT_PATH_CONFIG_5R,
			MADERA_OUT5R_ANC_SRC_SHIFT,
			ARRAY_SIZE(madera_out_anc_src_text),
			madera_out_anc_src_text),
	SOC_ENUM_SINGLE(MADERA_OUTPUT_PATH_CONFIG_6L,
			MADERA_OUT6L_ANC_SRC_SHIFT,
			ARRAY_SIZE(madera_out_anc_src_text),
			madera_out_anc_src_text),
	SOC_ENUM_SINGLE(MADERA_OUTPUT_PATH_CONFIG_6R,
			MADERA_OUT6R_ANC_SRC_SHIFT,
			ARRAY_SIZE(madera_out_anc_src_text),
			madera_out_anc_src_text),
};
EXPORT_SYMBOL_GPL(madera_output_anc_src);

static const char * const madera_ip_mode_text[2] = {
	"Analog", "Digital",
};

const struct soc_enum madera_ip_mode[] = {
	SOC_ENUM_SINGLE(MADERA_IN1L_CONTROL, MADERA_IN1_MODE_SHIFT,
		ARRAY_SIZE(madera_ip_mode_text), madera_ip_mode_text),
	SOC_ENUM_SINGLE(MADERA_IN2L_CONTROL, MADERA_IN2_MODE_SHIFT,
		ARRAY_SIZE(madera_ip_mode_text), madera_ip_mode_text),
	SOC_ENUM_SINGLE(MADERA_IN3L_CONTROL, MADERA_IN3_MODE_SHIFT,
		ARRAY_SIZE(madera_ip_mode_text), madera_ip_mode_text),
};
EXPORT_SYMBOL_GPL(madera_ip_mode);

int madera_ip_mode_put(struct snd_kcontrol *kcontrol,
		       struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_soc_kcontrol_codec(kcontrol);
	struct snd_soc_dapm_context *dapm = snd_soc_codec_get_dapm(codec);
	struct soc_enum *e = (struct soc_enum *)kcontrol->private_value;
	unsigned int reg, ret = 0;

	snd_soc_dapm_mutex_lock(dapm);

	/* Cannot change input mode on an active input*/
	reg = snd_soc_read(codec, MADERA_INPUT_ENABLES);

	switch (e->reg) {
	case MADERA_IN1L_CONTROL:
		if (reg & (MADERA_IN1L_ENA_MASK | MADERA_IN1R_ENA_MASK)) {
			ret = -EBUSY;
			goto exit;
		}
		break;
	case MADERA_IN2L_CONTROL:
		if (reg & (MADERA_IN2L_ENA_MASK | MADERA_IN2R_ENA_MASK)) {
			ret = -EBUSY;
			goto exit;
		}
		break;
	case MADERA_IN3L_CONTROL:
		if (reg & (MADERA_IN3L_ENA_MASK | MADERA_IN3R_ENA_MASK)) {
			ret = -EBUSY;
			goto exit;
		}
		break;
	default:
		ret = -EINVAL;
		goto exit;
	}

	ret = snd_soc_put_enum_double(kcontrol, ucontrol);
exit:
	snd_soc_dapm_mutex_unlock(dapm);
	return ret;
}
EXPORT_SYMBOL_GPL(madera_ip_mode_put);

int madera_in_rate_put(struct snd_kcontrol *kcontrol,
		       struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_soc_kcontrol_codec(kcontrol);
	struct snd_soc_dapm_context *dapm = snd_soc_codec_get_dapm(codec);
	struct soc_enum *e = (struct soc_enum *)kcontrol->private_value;
	unsigned int reg, mask;
	int ret = 0;

	snd_soc_dapm_mutex_lock(dapm);

	/* Cannot change rate on an active input */
	reg = snd_soc_read(codec, MADERA_INPUT_ENABLES);
	mask = (e->reg - MADERA_IN1L_CONTROL) / 4;
	mask ^= 0x1; /* Flip bottom bit for channel order */

	if ((reg) & (1 << mask)) {
		ret = -EBUSY;
		goto exit;
	}

	ret = snd_soc_put_enum_double(kcontrol, ucontrol);
exit:
	snd_soc_dapm_mutex_unlock(dapm);
	return ret;
}
EXPORT_SYMBOL_GPL(madera_in_rate_put);

int madera_dfc_put(struct snd_kcontrol *kcontrol,
		   struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_soc_kcontrol_codec(kcontrol);
	struct snd_soc_dapm_context *dapm = snd_soc_codec_get_dapm(codec);
	struct soc_enum *e = (struct soc_enum *)kcontrol->private_value;
	unsigned int reg = e->reg;
	unsigned int val;
	int ret = 0;

	reg = ((reg / 6) * 6) - 2;

	snd_soc_dapm_mutex_lock(dapm);

	/* Cannot change dfc settings when its on */
	val = snd_soc_read(codec, reg);
	if (val & MADERA_DFC1_ENA) {
		ret = -EBUSY;
		goto exit;
	}

	ret = snd_soc_put_enum_double(kcontrol, ucontrol);
exit:
	snd_soc_dapm_mutex_unlock(dapm);
	return ret;
}
EXPORT_SYMBOL_GPL(madera_dfc_put);

int madera_lp_mode_put(struct snd_kcontrol *kcontrol,
		       struct snd_ctl_elem_value *ucontrol)
{
	struct soc_mixer_control *mc =
		(struct soc_mixer_control *)kcontrol->private_value;
	struct snd_soc_codec *codec = snd_soc_kcontrol_codec(kcontrol);
	struct snd_soc_dapm_context *dapm = snd_soc_codec_get_dapm(codec);
	unsigned int reg, mask;
	int ret;

	snd_soc_dapm_mutex_lock(dapm);

	/* Cannot change lp mode on an active input */
	reg = snd_soc_read(codec, MADERA_INPUT_ENABLES);
	mask = (mc->reg - MADERA_ADC_DIGITAL_VOLUME_1L) / 4;
	mask ^= 0x1; /* Flip bottom bit for channel order */

	if ((reg) & (1 << mask)) {
		ret = -EBUSY;
		dev_err(codec->dev,
			"Can't change lp mode on an active input\n");
		goto exit;
	}

	ret = snd_soc_put_volsw(kcontrol, ucontrol);

exit:
	snd_soc_dapm_mutex_unlock(dapm);
	return ret;
}
EXPORT_SYMBOL_GPL(madera_lp_mode_put);

const struct snd_kcontrol_new madera_dsp_trigger_output_mux[] = {
	SOC_DAPM_SINGLE("Switch", SND_SOC_NOPM, 0, 1, 0),
	SOC_DAPM_SINGLE("Switch", SND_SOC_NOPM, 0, 1, 0),
	SOC_DAPM_SINGLE("Switch", SND_SOC_NOPM, 0, 1, 0),
	SOC_DAPM_SINGLE("Switch", SND_SOC_NOPM, 0, 1, 0),
	SOC_DAPM_SINGLE("Switch", SND_SOC_NOPM, 0, 1, 0),
	SOC_DAPM_SINGLE("Switch", SND_SOC_NOPM, 0, 1, 0),
	SOC_DAPM_SINGLE("Switch", SND_SOC_NOPM, 0, 1, 0),
};
EXPORT_SYMBOL_GPL(madera_dsp_trigger_output_mux);

const struct snd_kcontrol_new madera_drc_activity_output_mux[] = {
	SOC_DAPM_SINGLE("Switch", SND_SOC_NOPM, 0, 1, 0),
	SOC_DAPM_SINGLE("Switch", SND_SOC_NOPM, 0, 1, 0),
};
EXPORT_SYMBOL_GPL(madera_drc_activity_output_mux);

static void madera_in_set_vu(struct madera_priv *priv, bool enable)
{
	unsigned int val;
	int i, ret;

	if (enable)
		val = MADERA_IN_VU;
	else
		val = 0;

	for (i = 0; i < priv->num_inputs; i++) {
		ret = regmap_update_bits(priv->madera->regmap,
				    MADERA_ADC_DIGITAL_VOLUME_1L + (i * 4),
				    MADERA_IN_VU, val);
		if (ret)
			dev_warn(priv->madera->dev,
				 "Failed to modify VU bits: %d\n", ret);
	}
}

int madera_in_ev(struct snd_soc_dapm_widget *w, struct snd_kcontrol *kcontrol,
		 int event)
{
	struct snd_soc_codec *codec = snd_soc_dapm_to_codec(w->dapm);
	struct madera_priv *priv = snd_soc_codec_get_drvdata(codec);
	unsigned int reg;

	if (w->shift % 2)
		reg = MADERA_ADC_DIGITAL_VOLUME_1L + ((w->shift / 2) * 8);
	else
		reg = MADERA_ADC_DIGITAL_VOLUME_1R + ((w->shift / 2) * 8);

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		priv->in_pending++;
		break;
	case SND_SOC_DAPM_POST_PMU:
		priv->in_pending--;
		msleep(30);
		snd_soc_update_bits(codec, reg, MADERA_IN1L_MUTE, 0);

		/* If this is the last input pending then allow VU */
		if (priv->in_pending == 0) {
			usleep_range(1000, 1010);
			madera_in_set_vu(priv, true);
		}
		break;
	case SND_SOC_DAPM_PRE_PMD:
		snd_soc_update_bits(codec, reg,
				    MADERA_IN1L_MUTE | MADERA_IN_VU,
				    MADERA_IN1L_MUTE | MADERA_IN_VU);
		break;
	case SND_SOC_DAPM_POST_PMD:
		/* Disable volume updates if no inputs are enabled */
		reg = snd_soc_read(codec, MADERA_INPUT_ENABLES);
		if (reg == 0)
			madera_in_set_vu(priv, false);
		break;
	default:
		break;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(madera_in_ev);

int madera_put_dre(struct snd_kcontrol *kcontrol,
		   struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_soc_kcontrol_codec(kcontrol);
	struct snd_soc_dapm_context *dapm = snd_soc_codec_get_dapm(codec);
	int ret;

	snd_soc_dapm_mutex_lock(dapm);

	ret = snd_soc_put_volsw(kcontrol, ucontrol);

	snd_soc_dapm_mutex_unlock(dapm);

	return ret;
}
EXPORT_SYMBOL_GPL(madera_put_dre);

int madera_out_ev(struct snd_soc_dapm_widget *w,
		  struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_codec *codec = snd_soc_dapm_to_codec(w->dapm);
	struct madera_priv *priv = snd_soc_codec_get_drvdata(codec);
	struct madera *madera = priv->madera;
	int out_up_delay;

	switch (madera->type) {
	case CS47L90:
	case CS47L91:
		out_up_delay = 6;
		break;
	default:
		out_up_delay = 17;
		break;
	}

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		switch (w->shift) {
		case MADERA_OUT1L_ENA_SHIFT:
		case MADERA_OUT1R_ENA_SHIFT:
		case MADERA_OUT2L_ENA_SHIFT:
		case MADERA_OUT2R_ENA_SHIFT:
		case MADERA_OUT3L_ENA_SHIFT:
		case MADERA_OUT3R_ENA_SHIFT:
			priv->out_up_pending++;
			priv->out_up_delay += out_up_delay;
			break;
		default:
			break;
		}
		break;

	case SND_SOC_DAPM_POST_PMU:
		switch (w->shift) {
		case MADERA_OUT1L_ENA_SHIFT:
		case MADERA_OUT1R_ENA_SHIFT:
		case MADERA_OUT2L_ENA_SHIFT:
		case MADERA_OUT2R_ENA_SHIFT:
		case MADERA_OUT3L_ENA_SHIFT:
		case MADERA_OUT3R_ENA_SHIFT:
			priv->out_up_pending--;
			if (!priv->out_up_pending) {
				msleep(priv->out_up_delay);
				priv->out_up_delay = 0;
			}
			break;

		default:
			break;
		}
		break;

	case SND_SOC_DAPM_PRE_PMD:
		switch (w->shift) {
		case MADERA_OUT1L_ENA_SHIFT:
		case MADERA_OUT1R_ENA_SHIFT:
		case MADERA_OUT2L_ENA_SHIFT:
		case MADERA_OUT2R_ENA_SHIFT:
		case MADERA_OUT3L_ENA_SHIFT:
		case MADERA_OUT3R_ENA_SHIFT:
			priv->out_down_pending++;
			priv->out_down_delay++;
			break;
		default:
			break;
		}
		break;

	case SND_SOC_DAPM_POST_PMD:
		switch (w->shift) {
		case MADERA_OUT1L_ENA_SHIFT:
		case MADERA_OUT1R_ENA_SHIFT:
		case MADERA_OUT2L_ENA_SHIFT:
		case MADERA_OUT2R_ENA_SHIFT:
		case MADERA_OUT3L_ENA_SHIFT:
		case MADERA_OUT3R_ENA_SHIFT:
			priv->out_down_pending--;
			if (!priv->out_down_pending) {
				msleep(priv->out_down_delay);
				priv->out_down_delay = 0;
			}
			break;
		default:
			break;
		}
		break;
	default:
		break;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(madera_out_ev);

int madera_hp_ev(struct snd_soc_dapm_widget *w,
		 struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_codec *codec = snd_soc_dapm_to_codec(w->dapm);
	struct madera_priv *priv = snd_soc_codec_get_drvdata(codec);
	struct madera *madera = priv->madera;
	unsigned int mask = 1 << w->shift;
	unsigned int out_num = w->shift / 2;
	unsigned int val;
	unsigned int ep_sel = 0;

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		val = mask;
		break;
	case SND_SOC_DAPM_PRE_PMD:
		val = 0;
		break;
	case SND_SOC_DAPM_PRE_PMU:
	case SND_SOC_DAPM_POST_PMD:
		return madera_out_ev(w, kcontrol, event);
	default:
		return 0;
	}

	/* Store the desired state for the HP outputs */
	priv->madera->hp_ena &= ~mask;
	priv->madera->hp_ena |= val;

	/* if OUT1 is routed to EPOUT, ignore HP clamp and impedance */
	regmap_read(priv->madera->regmap, MADERA_OUTPUT_ENABLES_1, &ep_sel);
	ep_sel &= MADERA_EP_SEL_MASK;

	/* Force off if HPDET clamp is active for this output */
	if ((priv->madera->hpdet_clamp[out_num] ||
	    madera_is_hp_shorted(madera, out_num)) && !ep_sel)
		val = 0;

	regmap_update_bits_async(madera->regmap, MADERA_OUTPUT_ENABLES_1,
				 mask, val);

	return madera_out_ev(w, kcontrol, event);
}
EXPORT_SYMBOL_GPL(madera_hp_ev);

int madera_anc_ev(struct snd_soc_dapm_widget *w, struct snd_kcontrol *kcontrol,
		  int event)
{
	struct snd_soc_codec *codec = snd_soc_dapm_to_codec(w->dapm);
	unsigned int val;

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		val = 1 << w->shift;
		break;
	case SND_SOC_DAPM_PRE_PMD:
		val = 1 << (w->shift + 1);
		break;
	default:
		return 0;
	}

	snd_soc_write(codec, MADERA_CLOCK_CONTROL, val);

	return 0;
}
EXPORT_SYMBOL_GPL(madera_anc_ev);

static const unsigned int madera_opclk_ref_48k_rates[] = {
	6144000,
	12288000,
	24576000,
	49152000,
};

static const unsigned int madera_opclk_ref_44k1_rates[] = {
	5644800,
	11289600,
	22579200,
	45158400,
};

static int madera_set_opclk(struct snd_soc_codec *codec, unsigned int clk,
			    unsigned int freq)
{
	struct madera_priv *priv = snd_soc_codec_get_drvdata(codec);
	unsigned int reg;
	const unsigned int *rates;
	int ref, div, refclk;

	BUILD_BUG_ON(ARRAY_SIZE(madera_opclk_ref_48k_rates) !=
		     ARRAY_SIZE(madera_opclk_ref_44k1_rates));

	switch (clk) {
	case MADERA_CLK_OPCLK:
		reg = MADERA_OUTPUT_SYSTEM_CLOCK;
		refclk = priv->sysclk;
		break;
	case MADERA_CLK_ASYNC_OPCLK:
		reg = MADERA_OUTPUT_ASYNC_CLOCK;
		refclk = priv->asyncclk;
		break;
	default:
		return -EINVAL;
	}

	if (refclk % 4000)
		rates = madera_opclk_ref_44k1_rates;
	else
		rates = madera_opclk_ref_48k_rates;

	for (ref = 0; ref < ARRAY_SIZE(madera_opclk_ref_48k_rates); ++ref) {
		if (rates[ref] > refclk)
			continue;

		div = 2;
		while ((rates[ref] / div >= freq) && (div <= 30)) {
			if (rates[ref] / div == freq) {
				dev_dbg(codec->dev, "Configured %dHz OPCLK\n",
					freq);
				snd_soc_update_bits(codec, reg,
						    MADERA_OPCLK_DIV_MASK |
						    MADERA_OPCLK_SEL_MASK,
						    (div <<
						     MADERA_OPCLK_DIV_SHIFT) |
						    ref);
				return 0;
			}
			div += 2;
		}
	}

	dev_err(codec->dev, "Unable to generate %dHz OPCLK\n", freq);
	return -EINVAL;
}

static int madera_get_sysclk_setting(unsigned int freq)
{
	switch (freq) {
	case 0:
	case 5644800:
	case 6144000:
		return 0;
	case 11289600:
	case 12288000:
		return MADERA_CLK_12MHZ << MADERA_SYSCLK_FREQ_SHIFT;
	case 22579200:
	case 24576000:
		return MADERA_CLK_24MHZ << MADERA_SYSCLK_FREQ_SHIFT;
	case 45158400:
	case 49152000:
		return MADERA_CLK_49MHZ << MADERA_SYSCLK_FREQ_SHIFT;
	case 90316800:
	case 98304000:
		return MADERA_CLK_98MHZ << MADERA_SYSCLK_FREQ_SHIFT;
	default:
		return -EINVAL;
	}
}

int madera_get_legacy_dspclk_setting(struct madera *madera, unsigned int freq)
{
	switch (freq) {
	case 0:
		return 0;
	case 45158400:
	case 49152000:
		switch (madera->type) {
		case CS47L85:
		case WM1840:
			if (madera->rev < 3)
				return -EINVAL;
			else
				return MADERA_CLK_49MHZ <<
				       MADERA_SYSCLK_FREQ_SHIFT;
		default:
			return -EINVAL;
		}
	case 135475200:
	case 147456000:
		return MADERA_DSP_CLK_147MHZ << MADERA_SYSCLK_FREQ_SHIFT;
	default:
		return -EINVAL;
	}
}
EXPORT_SYMBOL_GPL(madera_get_legacy_dspclk_setting);

static int madera_get_dspclk_setting(struct madera *madera,
				     unsigned int freq,
				     unsigned int *clock_2_val)
{
	switch (madera->type) {
	case CS47L35:
	case CS47L85:
	case WM1840:
		*clock_2_val = 0; /* don't use MADERA_DSP_CLOCK_2 */
		return madera_get_legacy_dspclk_setting(madera, freq);
	default:
		if (freq > 150000000)
			return -EINVAL;

		/* Use new exact frequency control */
		*clock_2_val = freq / 15625; /* freq * (2^6) / (10^6) */
		return 0;
	}
}

int madera_set_sysclk(struct snd_soc_codec *codec, int clk_id,
		      int source, unsigned int freq, int dir)
{
	struct madera_priv *priv = snd_soc_codec_get_drvdata(codec);
	struct madera *madera = priv->madera;
	char *name;
	unsigned int reg, clock_2_val = 0;
	unsigned int mask = MADERA_SYSCLK_FREQ_MASK | MADERA_SYSCLK_SRC_MASK;
	unsigned int val = source << MADERA_SYSCLK_SRC_SHIFT;
	int clk_freq_sel, *clk;
	int ret = 0;

	switch (clk_id) {
	case MADERA_CLK_SYSCLK:
		name = "SYSCLK";
		reg = MADERA_SYSTEM_CLOCK_1;
		clk = &priv->sysclk;
		clk_freq_sel = madera_get_sysclk_setting(freq);
		mask |= MADERA_SYSCLK_FRAC;
		break;
	case MADERA_CLK_ASYNCCLK:
		name = "ASYNCCLK";
		reg = MADERA_ASYNC_CLOCK_1;
		clk = &priv->asyncclk;
		clk_freq_sel = madera_get_sysclk_setting(freq);
		break;
	case MADERA_CLK_OPCLK:
	case MADERA_CLK_ASYNC_OPCLK:
		return madera_set_opclk(codec, clk_id, freq);
	case MADERA_CLK_DSPCLK:
		name = "DSPCLK";
		reg = MADERA_DSP_CLOCK_1;
		clk = &priv->dspclk;
		clk_freq_sel = madera_get_dspclk_setting(madera, freq,
							 &clock_2_val);
		break;
	default:
		return -EINVAL;
	}

	if (clk_freq_sel < 0) {
		dev_err(madera->dev,
			"Failed to get clk setting for %dHZ\n", freq);
		return ret;
	}

	*clk = freq;

	if (freq == 0) {
		dev_dbg(madera->dev, "%s cleared\n", name);
		return 0;
	}

	val |= clk_freq_sel;

	if (clock_2_val) {
		ret = regmap_write(madera->regmap, MADERA_DSP_CLOCK_2,
				   clock_2_val);
		if (ret) {
			dev_err(madera->dev,
				"Failed to write DSP_CONFIG2: %d\n", ret);
			return ret;
		}

		/* We're using the frequency setting in MADERA_DSP_CLOCK_2 so
		 * don't change the frequency select bits in MADERA_DSP_CLOCK_1
		 */
		mask = MADERA_SYSCLK_SRC_MASK;
	}

	if (freq % 6144000)
		val |= MADERA_SYSCLK_FRAC;

	dev_dbg(madera->dev, "%s set to %uHz", name, freq);

	return regmap_update_bits(madera->regmap, reg, mask, val);
}
EXPORT_SYMBOL_GPL(madera_set_sysclk);

static int madera_set_fmt(struct snd_soc_dai *dai, unsigned int fmt)
{
	struct snd_soc_codec *codec = dai->codec;
	struct madera_priv *priv = snd_soc_codec_get_drvdata(codec);
	struct madera *madera = priv->madera;
	int lrclk, bclk, mode, base;

	base = dai->driver->base;

	lrclk = 0;
	bclk = 0;

	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_DSP_A:
		mode = MADERA_FMT_DSP_MODE_A;
		break;
	case SND_SOC_DAIFMT_DSP_B:
		if ((fmt & SND_SOC_DAIFMT_MASTER_MASK)
				!= SND_SOC_DAIFMT_CBM_CFM) {
			madera_aif_err(dai, "DSP_B not valid in slave mode\n");
			return -EINVAL;
		}
		mode = MADERA_FMT_DSP_MODE_B;
		break;
	case SND_SOC_DAIFMT_I2S:
		mode = MADERA_FMT_I2S_MODE;
		break;
	case SND_SOC_DAIFMT_LEFT_J:
		if ((fmt & SND_SOC_DAIFMT_MASTER_MASK)
				!= SND_SOC_DAIFMT_CBM_CFM) {
			madera_aif_err(dai, "LEFT_J not valid in slave mode\n");
			return -EINVAL;
		}
		mode = MADERA_FMT_LEFT_JUSTIFIED_MODE;
		break;
	default:
		madera_aif_err(dai, "Unsupported DAI format %d\n",
				fmt & SND_SOC_DAIFMT_FORMAT_MASK);
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBS_CFS:
		break;
	case SND_SOC_DAIFMT_CBS_CFM:
		lrclk |= MADERA_AIF1TX_LRCLK_MSTR;
		break;
	case SND_SOC_DAIFMT_CBM_CFS:
		bclk |= MADERA_AIF1_BCLK_MSTR;
		break;
	case SND_SOC_DAIFMT_CBM_CFM:
		bclk |= MADERA_AIF1_BCLK_MSTR;
		lrclk |= MADERA_AIF1TX_LRCLK_MSTR;
		break;
	default:
		madera_aif_err(dai, "Unsupported master mode %d\n",
				fmt & SND_SOC_DAIFMT_MASTER_MASK);
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_NB_NF:
		break;
	case SND_SOC_DAIFMT_IB_IF:
		bclk |= MADERA_AIF1_BCLK_INV;
		lrclk |= MADERA_AIF1TX_LRCLK_INV;
		break;
	case SND_SOC_DAIFMT_IB_NF:
		bclk |= MADERA_AIF1_BCLK_INV;
		break;
	case SND_SOC_DAIFMT_NB_IF:
		lrclk |= MADERA_AIF1TX_LRCLK_INV;
		break;
	default:
		return -EINVAL;
	}

	regmap_update_bits_async(madera->regmap, base + MADERA_AIF_BCLK_CTRL,
				 MADERA_AIF1_BCLK_INV |
				 MADERA_AIF1_BCLK_MSTR,
				 bclk);
	regmap_update_bits_async(madera->regmap, base + MADERA_AIF_TX_PIN_CTRL,
				 MADERA_AIF1TX_LRCLK_INV |
				 MADERA_AIF1TX_LRCLK_MSTR, lrclk);
	regmap_update_bits_async(madera->regmap,
				 base + MADERA_AIF_RX_PIN_CTRL,
				 MADERA_AIF1RX_LRCLK_INV |
				 MADERA_AIF1RX_LRCLK_MSTR, lrclk);
	regmap_update_bits(madera->regmap, base + MADERA_AIF_FORMAT,
			   MADERA_AIF1_FMT_MASK, mode);

	return 0;
}

static const int madera_48k_bclk_rates[] = {
	-1,
	48000,
	64000,
	96000,
	128000,
	192000,
	256000,
	384000,
	512000,
	768000,
	1024000,
	1536000,
	2048000,
	3072000,
	4096000,
	6144000,
	8192000,
	12288000,
	24576000,
};

static const int madera_44k1_bclk_rates[] = {
	-1,
	44100,
	58800,
	88200,
	117600,
	177640,
	235200,
	352800,
	470400,
	705600,
	940800,
	1411200,
	1881600,
	2822400,
	3763200,
	5644800,
	7526400,
	11289600,
	22579200,
};

static const unsigned int madera_sr_vals[] = {
	0,
	12000,
	24000,
	48000,
	96000,
	192000,
	384000,
	768000,
	0,
	11025,
	22050,
	44100,
	88200,
	176400,
	352800,
	705600,
	4000,
	8000,
	16000,
	32000,
	64000,
	128000,
	256000,
	512000,
};

#define MADERA_48K_RATE_MASK	0x0F003E
#define MADERA_44K1_RATE_MASK	0x003E00
#define MADERA_RATE_MASK	(MADERA_48K_RATE_MASK | MADERA_44K1_RATE_MASK)

static const struct snd_pcm_hw_constraint_list madera_constraint = {
	.count	= ARRAY_SIZE(madera_sr_vals),
	.list	= madera_sr_vals,
};

static int madera_startup(struct snd_pcm_substream *substream,
			  struct snd_soc_dai *dai)
{
	struct snd_soc_codec *codec = dai->codec;
	struct madera_priv *priv = snd_soc_codec_get_drvdata(codec);
	struct madera_dai_priv *dai_priv = &priv->dai[dai->id - 1];
	unsigned int base_rate;

	if (!substream->runtime)
		return 0;

	switch (dai_priv->clk) {
	case MADERA_CLK_SYSCLK:
	case MADERA_CLK_SYSCLK_2:
	case MADERA_CLK_SYSCLK_3:
		base_rate = priv->sysclk;
		break;
	case MADERA_CLK_ASYNCCLK:
	case MADERA_CLK_ASYNCCLK_2:
		base_rate = priv->asyncclk;
		break;
	default:
		return 0;
	}

	if (base_rate == 0)
		dai_priv->constraint.mask = MADERA_RATE_MASK;
	else if (base_rate % 4000)
		dai_priv->constraint.mask = MADERA_44K1_RATE_MASK;
	else
		dai_priv->constraint.mask = MADERA_48K_RATE_MASK;

	return snd_pcm_hw_constraint_list(substream->runtime, 0,
					  SNDRV_PCM_HW_PARAM_RATE,
					  &dai_priv->constraint);
}

static int madera_hw_params_rate(struct snd_pcm_substream *substream,
				 struct snd_pcm_hw_params *params,
				 struct snd_soc_dai *dai)
{
	struct snd_soc_codec *codec = dai->codec;
	struct madera_priv *priv = snd_soc_codec_get_drvdata(codec);
	struct madera_dai_priv *dai_priv = &priv->dai[dai->id - 1];
	int base = dai->driver->base;
	int ret = 0, err;
	int i, sr_val, lim = 0;
	const unsigned int *sources = NULL;
	unsigned int cur, tar;
	bool change_rate = false;
	u32 rx_sampleszbits, rx_samplerate;

	rx_sampleszbits = snd_pcm_format_width(params_format(params));
	if (rx_sampleszbits < 16)
		rx_sampleszbits = 16;

	/* currently we use a single sample rate for SYSCLK */
	for (i = 0; i < ARRAY_SIZE(madera_sr_vals); i++)
		if (madera_sr_vals[i] == params_rate(params)) {
			rx_samplerate = params_rate(params);
			break;
		}

	if (i == ARRAY_SIZE(madera_sr_vals)) {
		madera_aif_err(dai, "Unsupported sample rate %dHz\n",
				params_rate(params));
		return -EINVAL;
	}
	sr_val = i;

	switch (dai->id) {
	case 4: /* cs47l35-slim1 */
		priv->rx1_sampleszbits = rx_sampleszbits;
		priv->rx1_samplerate = rx_samplerate;
		break;
	case 5: /* cs47l35-slim2 */
		priv->rx2_sampleszbits = rx_sampleszbits;
		priv->rx2_samplerate = rx_samplerate;
		break;
	case 6:
	default:
		break;
	}

	if (base) {
		switch (dai_priv->clk) {
		case MADERA_CLK_SYSCLK:
			tar = 0 << MADERA_AIF1_RATE_SHIFT;
			break;
		case MADERA_CLK_SYSCLK_2:
			tar = 1 << MADERA_AIF1_RATE_SHIFT;
			break;
		case MADERA_CLK_SYSCLK_3:
			tar = 2 << MADERA_AIF1_RATE_SHIFT;
			break;
		case MADERA_CLK_ASYNCCLK:
			tar = 8 << MADERA_AIF1_RATE_SHIFT;
			break;
		case MADERA_CLK_ASYNCCLK_2:
			tar = 9 << MADERA_AIF1_RATE_SHIFT;
			break;
		default:
			return -EINVAL;
		}

		ret = regmap_read(priv->madera->regmap,
				  base + MADERA_AIF_RATE_CTRL, &cur);
		if (ret != 0) {
			madera_aif_err(dai, "Failed to check rate: %d\n", ret);
			return ret;
		}

		if ((cur & MADERA_AIF1_RATE_MASK) !=
		    (tar & MADERA_AIF1_RATE_MASK))
			change_rate = true;

		if (change_rate) {
			unsigned int *cache = priv->aif_sources_cache;

			ret = madera_get_sources(dai, &sources, &lim);
			if (ret != 0) {
				madera_aif_err(dai, "Failed to get aif sources %d\n",
					       ret);
				return ret;
			}

			mutex_lock(&priv->rate_lock);

			ret = madera_cache_and_clear_sources(priv, sources,
							     cache, lim);
			if (ret != 0) {
				madera_aif_err(dai, "Failed to cache and clear aif sources: %d\n",
					       ret);
				goto out;
			}

			madera_spin_sysclk(priv);
		}
	}

	switch (dai_priv->clk) {
	case MADERA_CLK_SYSCLK:
		snd_soc_update_bits(codec, MADERA_SAMPLE_RATE_1,
				    MADERA_SAMPLE_RATE_1_MASK, sr_val);
		if (base)
			snd_soc_update_bits(codec, base + MADERA_AIF_RATE_CTRL,
					    MADERA_AIF1_RATE_MASK,
					    0 << MADERA_AIF1_RATE_SHIFT);
		break;
	case MADERA_CLK_SYSCLK_2:
		snd_soc_update_bits(codec, MADERA_SAMPLE_RATE_2,
				    MADERA_SAMPLE_RATE_2_MASK, sr_val);
		if (base)
			snd_soc_update_bits(codec, base + MADERA_AIF_RATE_CTRL,
					    MADERA_AIF1_RATE_MASK,
					    1 << MADERA_AIF1_RATE_SHIFT);
		break;
	case MADERA_CLK_SYSCLK_3:
		snd_soc_update_bits(codec, MADERA_SAMPLE_RATE_3,
				    MADERA_SAMPLE_RATE_3_MASK, sr_val);
		if (base)
			snd_soc_update_bits(codec, base + MADERA_AIF_RATE_CTRL,
					    MADERA_AIF1_RATE_MASK,
					    2 << MADERA_AIF1_RATE_SHIFT);
		break;
	case MADERA_CLK_ASYNCCLK:
		snd_soc_update_bits(codec, MADERA_ASYNC_SAMPLE_RATE_1,
				    MADERA_ASYNC_SAMPLE_RATE_1_MASK, sr_val);
		if (base)
			snd_soc_update_bits(codec, base + MADERA_AIF_RATE_CTRL,
					    MADERA_AIF1_RATE_MASK,
					    8 << MADERA_AIF1_RATE_SHIFT);
		break;
	case MADERA_CLK_ASYNCCLK_2:
		snd_soc_update_bits(codec, MADERA_ASYNC_SAMPLE_RATE_2,
				    MADERA_ASYNC_SAMPLE_RATE_2_MASK, sr_val);
		if (base)
			snd_soc_update_bits(codec, base + MADERA_AIF_RATE_CTRL,
					    MADERA_AIF1_RATE_MASK,
					    9 << MADERA_AIF1_RATE_SHIFT);
		break;
	default:
		madera_aif_err(dai, "Invalid clock %d\n", dai_priv->clk);
		ret = -EINVAL;
	}

	if (change_rate)
		madera_spin_sysclk(priv);

out:
	if (change_rate) {
		err = madera_restore_sources(priv, sources,
					     priv->aif_sources_cache, lim);
		if (err != 0) {
			madera_aif_err(dai,
					"Failed to restore sources: %d\n", err);
		}

		mutex_unlock(&priv->rate_lock);
	}
	return ret;
}

static bool madera_aif_cfg_changed(struct snd_soc_codec *codec,
				   int base, int bclk, int lrclk, int frame)
{
	int val;

	val = snd_soc_read(codec, base + MADERA_AIF_BCLK_CTRL);
	if (bclk != (val & MADERA_AIF1_BCLK_FREQ_MASK))
		return true;

	val = snd_soc_read(codec, base + MADERA_AIF_TX_BCLK_RATE);
	if (lrclk != (val & MADERA_AIF1TX_BCPF_MASK))
		return true;

	val = snd_soc_read(codec, base + MADERA_AIF_FRAME_CTRL_1);
	if (frame != (val & (MADERA_AIF1TX_WL_MASK |
			     MADERA_AIF1TX_SLOT_LEN_MASK)))
		return true;

	return false;
}

static int madera_hw_params(struct snd_pcm_substream *substream,
			    struct snd_pcm_hw_params *params,
			    struct snd_soc_dai *dai)
{
	struct snd_soc_codec *codec = dai->codec;
	struct madera_priv *priv = snd_soc_codec_get_drvdata(codec);
	struct madera *madera = priv->madera;
	int base = dai->driver->base;
	const int *rates;
	int i, ret, val;
	int channels = params_channels(params);
	int chan_limit = madera->pdata.codec.max_channels_clocked[dai->id - 1];
	int tdm_width = priv->tdm_width[dai->id - 1];
	int tdm_slots = priv->tdm_slots[dai->id - 1];
	int bclk, lrclk, wl, frame, bclk_target;
	bool reconfig;
	unsigned int aif_tx_state = 0, aif_rx_state = 0;

	if (params_rate(params) % 4000)
		rates = &madera_44k1_bclk_rates[0];
	else
		rates = &madera_48k_bclk_rates[0];

	wl = snd_pcm_format_width(params_format(params));

	if (tdm_slots) {
		madera_aif_dbg(dai, "Configuring for %d %d bit TDM slots\n",
				tdm_slots, tdm_width);
		bclk_target = tdm_slots * tdm_width * params_rate(params);
		channels = tdm_slots;
	} else {
		bclk_target = snd_soc_params_to_bclk(params);
		tdm_width = wl;
	}

	/* Force width to be 16 bit if params pass 8 bit */
	if (wl == 8) {
		wl *= 2;
		bclk_target *= 2;
		tdm_width = wl;
	}

	if (chan_limit && chan_limit < channels) {
		madera_aif_dbg(dai, "Limiting to %d channels\n", chan_limit);
		bclk_target /= channels;
		bclk_target *= chan_limit;
	}

	/* Force multiple of 2 channels for I2S mode */
	val = snd_soc_read(codec, base + MADERA_AIF_FORMAT);
	val &= MADERA_AIF1_FMT_MASK;
	if ((channels & 1) && (val == MADERA_FMT_I2S_MODE)) {
		madera_aif_dbg(dai, "Forcing stereo mode\n");
		bclk_target /= channels;
		bclk_target *= channels + 1;
	}

	for (i = 0; i < ARRAY_SIZE(madera_44k1_bclk_rates); i++) {
		if (rates[i] >= bclk_target &&
		    rates[i] % params_rate(params) == 0) {
			bclk = i;
			break;
		}
	}
	if (i == ARRAY_SIZE(madera_44k1_bclk_rates)) {
		madera_aif_err(dai, "Unsupported sample rate %dHz\n",
				params_rate(params));
		return -EINVAL;
	}

	lrclk = rates[bclk] / params_rate(params);

	madera_aif_dbg(dai, "BCLK %dHz LRCLK %dHz\n",
			rates[bclk], rates[bclk] / lrclk);

	frame = wl << MADERA_AIF1TX_WL_SHIFT | tdm_width;

	reconfig = madera_aif_cfg_changed(codec, base, bclk, lrclk, frame);

	if (reconfig) {
		/* Save AIF TX/RX state */
		aif_tx_state = snd_soc_read(codec,
					    base + MADERA_AIF_TX_ENABLES);
		aif_rx_state = snd_soc_read(codec,
					    base + MADERA_AIF_RX_ENABLES);
		/* Disable AIF TX/RX before reconfiguring it */
		regmap_update_bits_async(madera->regmap,
				    base + MADERA_AIF_TX_ENABLES, 0xff, 0x0);
		regmap_update_bits(madera->regmap,
				    base + MADERA_AIF_RX_ENABLES, 0xff, 0x0);
	}

	ret = madera_hw_params_rate(substream, params, dai);
	if (ret != 0)
		goto restore_aif;

	if (reconfig) {
		regmap_update_bits_async(madera->regmap,
					 base + MADERA_AIF_BCLK_CTRL,
					 MADERA_AIF1_BCLK_FREQ_MASK, bclk);
		regmap_update_bits_async(madera->regmap,
					 base + MADERA_AIF_TX_BCLK_RATE,
					 MADERA_AIF1TX_BCPF_MASK, lrclk);
		regmap_update_bits_async(madera->regmap,
					 base + MADERA_AIF_RX_BCLK_RATE,
					 MADERA_AIF1RX_BCPF_MASK, lrclk);
		regmap_update_bits_async(madera->regmap,
					 base + MADERA_AIF_FRAME_CTRL_1,
					 MADERA_AIF1TX_WL_MASK |
					 MADERA_AIF1TX_SLOT_LEN_MASK, frame);
		regmap_update_bits(madera->regmap,
				   base + MADERA_AIF_FRAME_CTRL_2,
				   MADERA_AIF1RX_WL_MASK |
				   MADERA_AIF1RX_SLOT_LEN_MASK, frame);
	}

restore_aif:
	if (reconfig) {
		/* Restore AIF TX/RX state */
		regmap_update_bits_async(madera->regmap,
					 base + MADERA_AIF_TX_ENABLES,
					 0xff, aif_tx_state);
		regmap_update_bits(madera->regmap,
				   base + MADERA_AIF_RX_ENABLES,
				   0xff, aif_rx_state);
	}
	return ret;
}

static const char * const madera_dai_clk_str(int clk_id)
{
	switch (clk_id) {
	case MADERA_CLK_SYSCLK:
	case MADERA_CLK_SYSCLK_2:
	case MADERA_CLK_SYSCLK_3:
		return "SYSCLK";
	case MADERA_CLK_ASYNCCLK:
	case MADERA_CLK_ASYNCCLK_2:
		return "ASYNCCLK";
	default:
		return "Unknown clock";
	}
}

static int madera_dai_set_sysclk(struct snd_soc_dai *dai,
				 int clk_id, unsigned int freq, int dir)
{
	struct snd_soc_codec *codec = dai->codec;
	struct snd_soc_dapm_context *dapm = snd_soc_codec_get_dapm(codec);
	struct madera_priv *priv = snd_soc_codec_get_drvdata(codec);
	struct madera_dai_priv *dai_priv = &priv->dai[dai->id - 1];
	struct snd_soc_dapm_route routes[2];

	switch (clk_id) {
	case MADERA_CLK_SYSCLK:
	case MADERA_CLK_SYSCLK_2:
	case MADERA_CLK_SYSCLK_3:
	case MADERA_CLK_ASYNCCLK:
	case MADERA_CLK_ASYNCCLK_2:
		break;
	default:
		return -EINVAL;
	}

	if (clk_id == dai_priv->clk)
		return 0;

	if (dai->active) {
		dev_err(codec->dev, "Can't change clock on active DAI %d\n",
			dai->id);
		return -EBUSY;
	}

	dev_dbg(codec->dev, "Setting AIF%d to %s\n", dai->id + 1,
		madera_dai_clk_str(clk_id));

	memset(&routes, 0, sizeof(routes));
	routes[0].sink = dai->driver->capture.stream_name;
	routes[1].sink = dai->driver->playback.stream_name;

	switch (clk_id) {
	case MADERA_CLK_SYSCLK:
	case MADERA_CLK_SYSCLK_2:
	case MADERA_CLK_SYSCLK_3:
		routes[0].source = madera_dai_clk_str(dai_priv->clk);
		routes[1].source = madera_dai_clk_str(dai_priv->clk);
		snd_soc_dapm_del_routes(dapm, routes, ARRAY_SIZE(routes));
		break;
	default:
		break;
	}

	switch (clk_id) {
	case MADERA_CLK_ASYNCCLK:
	case MADERA_CLK_ASYNCCLK_2:
		routes[0].source = madera_dai_clk_str(clk_id);
		routes[1].source = madera_dai_clk_str(clk_id);
		snd_soc_dapm_add_routes(dapm, routes, ARRAY_SIZE(routes));
		break;
	default:
		break;
	}

	dai_priv->clk = clk_id;

	return snd_soc_dapm_sync(dapm);
}

static int madera_set_tristate(struct snd_soc_dai *dai, int tristate)
{
	struct snd_soc_codec *codec = dai->codec;
	int base = dai->driver->base;
	unsigned int reg;
	int ret;

	if (tristate)
		reg = MADERA_AIF1_TRI;
	else
		reg = 0;

	ret = snd_soc_update_bits(codec, base + MADERA_AIF_RATE_CTRL,
				  MADERA_AIF1_TRI, reg);
	if (ret < 0)
		return ret;
	else
		return 0;
}

static void madera_set_channels_to_mask(struct snd_soc_dai *dai,
					unsigned int base,
					int channels, unsigned int mask)
{
	struct snd_soc_codec *codec = dai->codec;
	struct madera_priv *priv = snd_soc_codec_get_drvdata(codec);
	struct madera *madera = priv->madera;
	int slot, i;

	for (i = 0; i < channels; ++i) {
		slot = ffs(mask) - 1;
		if (slot < 0)
			return;

		regmap_write(madera->regmap, base + i, slot);

		mask &= ~(1 << slot);
	}

	if (mask)
		madera_aif_warn(dai, "Too many channels in TDM mask\n");
}

static int madera_set_tdm_slot(struct snd_soc_dai *dai, unsigned int tx_mask,
				unsigned int rx_mask, int slots, int slot_width)
{
	struct snd_soc_codec *codec = dai->codec;
	struct madera_priv *priv = snd_soc_codec_get_drvdata(codec);
	int base = dai->driver->base;
	int rx_max_chan = dai->driver->playback.channels_max;
	int tx_max_chan = dai->driver->capture.channels_max;

	/* Only support TDM for the physical AIFs */
	if (dai->id > MADERA_MAX_AIF)
		return -ENOTSUPP;

	if (slots == 0) {
		tx_mask = (1 << tx_max_chan) - 1;
		rx_mask = (1 << rx_max_chan) - 1;
	}

	madera_set_channels_to_mask(dai, base + MADERA_AIF_FRAME_CTRL_3,
				    tx_max_chan, tx_mask);
	madera_set_channels_to_mask(dai, base + MADERA_AIF_FRAME_CTRL_11,
				    rx_max_chan, rx_mask);

	priv->tdm_width[dai->id - 1] = slot_width;
	priv->tdm_slots[dai->id - 1] = slots;

	return 0;
}

const struct snd_soc_dai_ops madera_dai_ops = {
	.startup = madera_startup,
	.set_fmt = madera_set_fmt,
	.set_tdm_slot = madera_set_tdm_slot,
	.hw_params = madera_hw_params,
	.set_sysclk = madera_dai_set_sysclk,
	.set_tristate = madera_set_tristate,
};
EXPORT_SYMBOL_GPL(madera_dai_ops);

const struct snd_soc_dai_ops madera_simple_dai_ops = {
	.startup = madera_startup,
	.hw_params = madera_hw_params_rate,
	.set_sysclk = madera_dai_set_sysclk,
	.set_channel_map = madera_set_channel_map,
	.get_channel_map = madera_get_channel_map,
};
EXPORT_SYMBOL_GPL(madera_simple_dai_ops);

const struct snd_soc_dai_ops madera_slim_dai_ops = {
	.hw_params = madera_hw_params_rate,
	.set_sysclk = madera_dai_set_sysclk,
	.set_channel_map = madera_set_channel_map,
	.get_channel_map = madera_get_channel_map,
};
EXPORT_SYMBOL_GPL(madera_slim_dai_ops);

int madera_init_dai(struct madera_priv *priv, int id)
{
	struct madera_dai_priv *dai_priv = &priv->dai[id];

	dai_priv->clk = MADERA_CLK_SYSCLK;
	dai_priv->constraint = madera_constraint;

	return 0;
}
EXPORT_SYMBOL_GPL(madera_init_dai);

static const struct {
	unsigned int min;
	unsigned int max;
	u16 fratio;
	int ratio;
} fll_sync_fratios[] = {
	{       0,    64000, 4, 16 },
	{   64000,   128000, 3,  8 },
	{  128000,   256000, 2,  4 },
	{  256000,  1000000, 1,  2 },
	{ 1000000, 13500000, 0,  1 },
};

static const unsigned int pseudo_fref_max[MADERA_FLL_MAX_FRATIO] = {
	13500000,
	 6144000,
	 6144000,
	 3072000,
	 3072000,
	 2822400,
	 2822400,
	 1536000,
	 1536000,
	 1536000,
	 1536000,
	 1536000,
	 1536000,
	 1536000,
	 1536000,
	  768000,
};

struct madera_fll_gains {
	unsigned int min;
	unsigned int max;
	int gain;		/* main gain */
	int alt_gain;		/* alternate integer gain */
};

static const struct madera_fll_gains madera_fll_sync_gains[] = {
	{       0,   256000, 0, -1 },
	{  256000,  1000000, 2, -1 },
	{ 1000000, 13500000, 4, -1 },
};

static const struct madera_fll_gains madera_fll_main_gains[] = {
	{       0,   100000, 0, 2 },
	{  100000,   375000, 2, 2 },
	{  375000,   768000, 3, 2 },
	{  768001,  1500000, 3, 3 },
	{ 1500000,  6000000, 4, 3 },
	{ 6000000, 13500000, 5, 3 },
};

static int madera_validate_fll(struct madera_fll *fll,
				unsigned int Fref,
				unsigned int Fout)
{
	if (fll->fout && Fout != fll->fout) {
		madera_fll_err(fll, "Can't change output on active FLL\n");
		return -EINVAL;
	}

	if (Fref / MADERA_FLL_MAX_REFDIV > MADERA_FLL_MAX_FREF) {
		madera_fll_err(fll,
				"Can't scale %dMHz in to <=13.5MHz\n", Fref);
		return -EINVAL;
	}

	return 0;
}

static int madera_find_sync_fratio(unsigned int fref, int *fratio)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(fll_sync_fratios); i++) {
		if (fll_sync_fratios[i].min <= fref &&
		    fref <= fll_sync_fratios[i].max) {
			if (fratio)
				*fratio = fll_sync_fratios[i].fratio;

			return fll_sync_fratios[i].ratio;
		}
	}

	return -EINVAL;
}

static int madera_find_main_fratio(unsigned int fref, unsigned int fout,
				   int *fratio)
{
	int ratio = 1;

	while ((fout / (ratio * fref)) > MADERA_FLL_MAX_N)
		ratio++;

	if (fratio)
		*fratio = ratio - 1;

	return ratio;
}

static int madera_find_fratio(struct madera_fll *fll, unsigned int fref,
			      bool sync, int *fratio)
{
	switch (fll->madera->type) {
	case CS47L35:
		switch(fll->madera->rev) {
		case 0:
			/* rev A0 uses sync calculation for both loops */
			return madera_find_sync_fratio(fref, fratio);
		default:
			if (sync)
				return madera_find_sync_fratio(fref, fratio);
			else
				return madera_find_main_fratio(fref,
							       fll->fout,
							       fratio);
		}
		break;
	case CS47L85:
	case WM1840:
		/* these use the same calculation for main and sync loops */
		return madera_find_sync_fratio(fref, fratio);
	default:
		if (sync)
			return madera_find_sync_fratio(fref, fratio);
		else
			return madera_find_main_fratio(fref, fll->fout, fratio);
	}
}

static int madera_calc_fratio(struct madera_fll *fll,
			      struct madera_fll_cfg *cfg,
			      unsigned int Fref, bool sync)
{
	int init_ratio, ratio;
	int refdiv, div;

	/* Fref must be <=13.5MHz, find initial refdiv */
	div = 1;
	cfg->refdiv = 0;
	while (Fref > MADERA_FLL_MAX_FREF) {
		div *= 2;
		Fref /= 2;
		cfg->refdiv++;

		if (div > MADERA_FLL_MAX_REFDIV)
			return -EINVAL;
	}

	/* Find an appropriate FLL_FRATIO */
	init_ratio = madera_find_fratio(fll, Fref, sync, &cfg->fratio);
	if (init_ratio < 0) {
		madera_fll_err(fll, "Unable to find FRATIO for Fref=%uHz\n",
				Fref);
		return init_ratio;
	}

	if (!sync)
		cfg->fratio = init_ratio - 1;

	switch (fll->madera->type) {
	case CS47L35:
		switch (fll->madera->rev) {
		case 0:
			if (sync)
				return init_ratio;
			break;
		default:
			return init_ratio;
		}
		break;
	case CS47L85:
	case WM1840:
		if (sync)
			return init_ratio;
		break;
	default:
		return init_ratio;
	}

	/* For CS47L35 rev A0, CS47L85 and WM1840 adjust FRATIO/refdiv to avoid
	 * integer mode if possible
	 */
	refdiv = cfg->refdiv;

	while (div <= MADERA_FLL_MAX_REFDIV) {
		/* start from init_ratio because this may already give a
		 * fractional N.K
		 */
		for (ratio = init_ratio; ratio > 0; ratio--) {
			if (fll->fout % (ratio * Fref)) {
				cfg->refdiv = refdiv;
				cfg->fratio = ratio - 1;
				return ratio;
			}
		}

		for (ratio = init_ratio + 1; ratio <= MADERA_FLL_MAX_FRATIO;
		     ratio++) {
			if ((MADERA_FLL_VCO_CORNER / 2) /
			    (MADERA_FLL_VCO_MULT * ratio) < Fref)
				break;

			if (Fref > pseudo_fref_max[ratio - 1])
				break;

			if (fll->fout % (ratio * Fref)) {
				cfg->refdiv = refdiv;
				cfg->fratio = ratio - 1;
				return ratio;
			}
		}

		div *= 2;
		Fref /= 2;
		refdiv++;
		init_ratio = madera_find_fratio(fll, Fref, sync, NULL);
	}

	madera_fll_warn(fll, "Falling back to integer mode operation\n");
	return cfg->fratio + 1;
}

static int madera_find_fll_gain(struct madera_fll *fll,
				struct madera_fll_cfg *cfg,
				unsigned int fref,
				const struct madera_fll_gains *gains,
				int n_gains)
{
	int i;

	for (i = 0; i < n_gains; i++) {
		if (gains[i].min <= fref && fref <= gains[i].max) {
			cfg->gain = gains[i].gain;
			cfg->alt_gain = gains[i].alt_gain;
			return 0;
		}
	}

	madera_fll_err(fll, "Unable to find gain for fref=%uHz\n", fref);

	return -EINVAL;
}

static int madera_calc_fll(struct madera_fll *fll,
			   struct madera_fll_cfg *cfg,
			   unsigned int fref, bool sync)
{
	unsigned int gcd_fll;
	const struct madera_fll_gains *gains;
	int n_gains;
	int ratio, ret;

	madera_fll_dbg(fll, "fref=%u Fout=%u fvco=%u\n",
			fref, fll->fout, fll->fout * MADERA_FLL_VCO_MULT);

	/* Find an appropriate FLL_FRATIO and refdiv */
	ratio = madera_calc_fratio(fll, cfg, fref, sync);
	if (ratio < 0)
		return ratio;

	/* Apply the division for our remaining calculations */
	fref = fref / (1 << cfg->refdiv);

	cfg->n = fll->fout / (ratio * fref);

	if (fll->fout % (ratio * fref)) {
		gcd_fll = gcd(fll->fout, ratio * fref);
		madera_fll_dbg(fll, "GCD=%u\n", gcd_fll);

		cfg->theta = (fll->fout - (cfg->n * ratio * fref))
			/ gcd_fll;
		cfg->lambda = (ratio * fref) / gcd_fll;
	} else {
		cfg->theta = 0;
		cfg->lambda = 0;
	}

	/* Round down to 16bit range with cost of accuracy lost.
	 * Denominator must be bigger than numerator so we only
	 * take care of it.
	 */
	while (cfg->lambda >= (1 << 16)) {
		cfg->theta >>= 1;
		cfg->lambda >>= 1;
	}

	switch (fll->madera->type) {
	case CS47L35:
		switch (fll->madera->rev) {
		case 0:
			/* Rev A0 uses the sync gains for both loops */
			gains = madera_fll_sync_gains,
			n_gains = ARRAY_SIZE(madera_fll_sync_gains);
			break;
		default:
			if (sync) {
				gains = madera_fll_sync_gains,
				n_gains = ARRAY_SIZE(madera_fll_sync_gains);
			} else {
				gains = madera_fll_main_gains;
				n_gains = ARRAY_SIZE(madera_fll_main_gains);
			}
			break;
		}
		break;
	case CS47L85:
	case WM1840:
		/* These use the sync gains for both loops */
		gains = madera_fll_sync_gains,
		n_gains = ARRAY_SIZE(madera_fll_sync_gains);
		break;
	default:
		if (sync) {
			gains = madera_fll_sync_gains,
			n_gains = ARRAY_SIZE(madera_fll_sync_gains);
		} else {
			gains = madera_fll_main_gains;
			n_gains = ARRAY_SIZE(madera_fll_main_gains);
		}
		break;
	}

	ret = madera_find_fll_gain(fll, cfg, fref, gains, n_gains);
	if (ret)
		return ret;

	madera_fll_dbg(fll, "N=%d THETA=%d LAMBDA=%d\n",
			cfg->n, cfg->theta, cfg->lambda);
	madera_fll_dbg(fll, "FRATIO=0x%x(%d) REFCLK_DIV=0x%x(%d)\n",
			cfg->fratio, ratio, cfg->refdiv, 1 << cfg->refdiv);
	madera_fll_dbg(fll, "GAIN=0x%x(%d)\n", cfg->gain, 1 << cfg->gain);

	return 0;

}

static bool madera_apply_fll(struct madera *madera, unsigned int base,
			     struct madera_fll_cfg *cfg, int source,
			     bool sync, int gain)
{
	bool change, fll_change;

	fll_change = false;
	regmap_update_bits_check_async(madera->regmap,
				       base + MADERA_FLL_CONTROL_3_OFFS,
				       MADERA_FLL1_THETA_MASK,
				       cfg->theta, &change);
	fll_change |= change;
	regmap_update_bits_check_async(madera->regmap,
				       base + MADERA_FLL_CONTROL_4_OFFS,
				       MADERA_FLL1_LAMBDA_MASK,
				       cfg->lambda, &change);
	fll_change |= change;
	regmap_update_bits_check_async(madera->regmap,
				       base + MADERA_FLL_CONTROL_5_OFFS,
				       MADERA_FLL1_FRATIO_MASK,
				       cfg->fratio << MADERA_FLL1_FRATIO_SHIFT,
				       &change);
	fll_change |= change;
	regmap_update_bits_check_async(madera->regmap,
				base + MADERA_FLL_CONTROL_6_OFFS,
				MADERA_FLL1_REFCLK_DIV_MASK |
				MADERA_FLL1_REFCLK_SRC_MASK,
				cfg->refdiv << MADERA_FLL1_REFCLK_DIV_SHIFT |
				source << MADERA_FLL1_REFCLK_SRC_SHIFT,
				&change);
	fll_change |= change;

	if (sync) {
		regmap_update_bits_check_async(madera->regmap,
				base + MADERA_FLL_SYNCHRONISER_7_OFFS,
				MADERA_FLL1_GAIN_MASK,
				gain << MADERA_FLL1_GAIN_SHIFT, &change);
		fll_change |= change;
	} else {
		regmap_update_bits_check_async(madera->regmap,
				base + MADERA_FLL_CONTROL_7_OFFS,
				MADERA_FLL1_GAIN_MASK,
				gain << MADERA_FLL1_GAIN_SHIFT, &change);
		fll_change |= change;
	}

	regmap_update_bits_check_async(madera->regmap,
				base + MADERA_FLL_CONTROL_2_OFFS,
				MADERA_FLL1_CTRL_UPD | MADERA_FLL1_N_MASK,
				MADERA_FLL1_CTRL_UPD | cfg->n, &change);
	fll_change |= change;

	return fll_change;
}

static int madera_is_enabled_fll(struct madera_fll *fll)
{
	struct madera *madera = fll->madera;
	unsigned int reg;
	int ret;

	ret = regmap_read(madera->regmap,
			  fll->base + MADERA_FLL_CONTROL_1_OFFS, &reg);
	if (ret != 0) {
		madera_fll_err(fll, "Failed to read current state: %d\n", ret);
		return ret;
	}

	return reg & MADERA_FLL1_ENA;
}

static int madera_wait_for_fll(struct madera_fll *fll, bool requested)
{
	struct madera *madera = fll->madera;
	unsigned int val = 0;
	bool status;
	int i;

	madera_fll_dbg(fll, "Waiting for FLL...\n");

	for (i = 0; i < 25; i++) {
		regmap_read(madera->regmap, MADERA_IRQ1_RAW_STATUS_2, &val);
		status = val & (MADERA_FLL1_LOCK_STS1 << (fll->id - 1));
		if (status == requested)
			return 0;
		usleep_range(10000, 10001);
	}

	madera_fll_warn(fll, "Timed out waiting for lock\n");

	return -ETIMEDOUT;
}

static bool madera_set_fll_phase_integrator(struct madera_fll *fll,
					    struct madera_fll_cfg *ref_cfg,
					    bool sync)
{
	unsigned int val;
	bool reg_change;

	if (!sync && (ref_cfg->theta == 0))
		val = (1 << MADERA_FLL1_PHASE_ENA_SHIFT) |
			(2 << MADERA_FLL1_PHASE_GAIN_SHIFT);
	else
		val = 2 << MADERA_FLL1_PHASE_GAIN_SHIFT;

	regmap_update_bits_check(fll->madera->regmap,
				 fll->base + MADERA_FLL_EFS_2_OFFS,
				 MADERA_FLL1_PHASE_ENA_MASK |
				 MADERA_FLL1_PHASE_GAIN_MASK,
				 val,
				 &reg_change);

	return reg_change;
}

static int madera_enable_fll(struct madera_fll *fll)
{
	struct madera *madera = fll->madera;
	bool have_sync = false;
	int already_enabled = madera_is_enabled_fll(fll);
	struct madera_fll_cfg cfg;
	unsigned int sync_reg_base;
	int gain;
	bool fll_change = false;

	if (already_enabled < 0)
		return already_enabled;	/* error getting current state */

	if ((fll->ref_src < 0) || (fll->ref_freq == 0)) {
		madera_fll_err(fll, "No REFCLK\n");
		return -EINVAL;
	}

	madera_fll_dbg(fll, "Enabling FLL, initially %s\n",
			already_enabled ? "enabled" : "disabled");

	switch (madera->type) {
	case CS47L35:
		sync_reg_base = fll->base + CS47L35_FLL_SYNCHRONISER_OFFS;
		break;
	default:
		sync_reg_base = fll->base + MADERA_FLL_SYNCHRONISER_OFFS;
		break;
	}

	if (already_enabled) {
		/* Facilitate smooth refclk across the transition */
		regmap_update_bits_async(fll->madera->regmap,
					 fll->base + MADERA_FLL_CONTROL_7_OFFS,
					 MADERA_FLL1_GAIN_MASK, 0);
		regmap_update_bits(fll->madera->regmap,
				   fll->base + MADERA_FLL_CONTROL_1_OFFS,
				   MADERA_FLL1_FREERUN,
				   MADERA_FLL1_FREERUN);
		udelay(32);
	}

	/* Apply SYNCCLK setting */
	if (fll->sync_src >= 0) {
		madera_calc_fll(fll, &cfg, fll->sync_freq, true);

		fll_change |= madera_apply_fll(madera, sync_reg_base,
						&cfg, fll->sync_src,
						true, cfg.gain);
		have_sync = true;
	}

	/* Apply REFCLK setting */
	madera_calc_fll(fll, &cfg, fll->ref_freq, false);

	switch (fll->madera->type) {
	case CS47L35:
		switch (fll->madera->rev) {
		case 0:
			break;
		default:
			fll_change |=
				madera_set_fll_phase_integrator(fll, &cfg,
								have_sync);
			break;
		}
		gain = cfg.gain;
		break;
	case CS47L85:
	case WM1840:
		gain = cfg.gain;
		break;
	default:
		fll_change |= madera_set_fll_phase_integrator(fll, &cfg,
							      have_sync);
		if (!have_sync && (cfg.theta == 0))
			gain = cfg.alt_gain;
		else
			gain = cfg.gain;
		break;
	}

	fll_change |= madera_apply_fll(madera, fll->base,
				      &cfg, fll->ref_src,
				      false, gain);

	/*
	 * Increase the bandwidth if we're not using a low frequency
	 * sync source.
	 */
	if (have_sync && fll->sync_freq > 100000)
		regmap_update_bits_async(madera->regmap,
				sync_reg_base + MADERA_FLL_SYNCHRONISER_7_OFFS,
				MADERA_FLL1_SYNC_DFSAT_MASK, 0);
	else
		regmap_update_bits_async(madera->regmap,
				sync_reg_base + MADERA_FLL_SYNCHRONISER_7_OFFS,
				MADERA_FLL1_SYNC_DFSAT_MASK,
				MADERA_FLL1_SYNC_DFSAT);

	if (!already_enabled)
		pm_runtime_get_sync(madera->dev);

	/* Clear any pending completions */
	try_wait_for_completion(&fll->ok);

	regmap_update_bits_async(madera->regmap,
				 fll->base + MADERA_FLL_CONTROL_1_OFFS,
				 MADERA_FLL1_ENA, MADERA_FLL1_ENA);
	if (have_sync)
		regmap_update_bits_async(madera->regmap,
				sync_reg_base + MADERA_FLL_SYNCHRONISER_1_OFFS,
				MADERA_FLL1_SYNC_ENA,
				MADERA_FLL1_SYNC_ENA);

	if (already_enabled)
		regmap_update_bits_async(madera->regmap,
					 fll->base + MADERA_FLL_CONTROL_1_OFFS,
					 MADERA_FLL1_FREERUN, 0);

	if (fll_change || !already_enabled)
		madera_wait_for_fll(fll, true);

	return 0;
}

static void madera_disable_fll(struct madera_fll *fll)
{
	struct madera *madera = fll->madera;
	unsigned int sync_reg_base;
	bool change;

	switch (madera->type) {
	case CS47L35:
		sync_reg_base = fll->base + CS47L35_FLL_SYNCHRONISER_OFFS;
		break;
	default:
		sync_reg_base = fll->base + MADERA_FLL_SYNCHRONISER_OFFS;
		break;
	}

	madera_fll_dbg(fll, "Disabling FLL\n");

	regmap_update_bits_async(madera->regmap,
				 fll->base + MADERA_FLL_CONTROL_1_OFFS,
				 MADERA_FLL1_FREERUN, MADERA_FLL1_FREERUN);
	regmap_update_bits_check(madera->regmap,
				 fll->base + MADERA_FLL_CONTROL_1_OFFS,
				 MADERA_FLL1_ENA, 0, &change);
	regmap_update_bits(madera->regmap,
			   sync_reg_base + MADERA_FLL_SYNCHRONISER_1_OFFS,
			   MADERA_FLL1_SYNC_ENA, 0);
	regmap_update_bits_async(madera->regmap,
				 fll->base + MADERA_FLL_CONTROL_1_OFFS,
				 MADERA_FLL1_FREERUN, 0);

	madera_wait_for_fll(fll, false);

	if (change)
		pm_runtime_put_autosuspend(madera->dev);
}

int madera_set_fll_syncclk(struct madera_fll *fll, int source,
			   unsigned int fref, unsigned int Fout)
{
	int ret = 0;

	if (fll->sync_src == source && fll->sync_freq == fref)
		return 0;

	if (fll->fout && fref > 0) {
		ret = madera_validate_fll(fll, fref, fll->fout);
		if (ret != 0)
			return ret;
	}

	fll->sync_src = source;
	fll->sync_freq = fref;

	if (fll->fout && fref > 0)
		ret = madera_enable_fll(fll);

	return ret;
}
EXPORT_SYMBOL_GPL(madera_set_fll_syncclk);

int madera_set_fll_refclk(struct madera_fll *fll, int source,
			   unsigned int fref, unsigned int fout)
{
	int ret = 0;

	if (fll->ref_src == source &&
	    fll->ref_freq == fref && fll->fout == fout)
		return 0;

	if (fout) {
		if ((fout < MADERA_FLL_MIN_FOUT) ||
		    (fout > MADERA_FLL_MAX_FOUT)) {
			madera_fll_err(fll, "invalid fout %uHz\n", fout);
			return -EINVAL;
		}

		ret = madera_validate_fll(fll, fref, fout);
		if (ret != 0)
			return ret;
	}

	fll->ref_src = source;
	fll->ref_freq = fref;
	fll->fout = fout;

	if (fout)
		ret = madera_enable_fll(fll);
	else
		madera_disable_fll(fll);

	return ret;
}
EXPORT_SYMBOL_GPL(madera_set_fll_refclk);

int madera_init_fll(struct madera *madera, int id, int base,
		    struct madera_fll *fll)
{
	init_completion(&fll->ok);

	fll->id = id;
	fll->base = base;
	fll->madera = madera;
	fll->ref_src = MADERA_FLL_SRC_NONE;
	fll->sync_src = MADERA_FLL_SRC_NONE;

	regmap_update_bits(madera->regmap,
			   fll->base + MADERA_FLL_CONTROL_1_OFFS,
			   MADERA_FLL1_FREERUN, 0);

	return 0;
}
EXPORT_SYMBOL_GPL(madera_init_fll);

static const struct reg_sequence madera_fll_ao_32K_49M_patch[] = {
	{ MADERA_FLLAO_CONTROL_2,  0x02EE },
	{ MADERA_FLLAO_CONTROL_3,  0x0000 },
	{ MADERA_FLLAO_CONTROL_4,  0x0001 },
	{ MADERA_FLLAO_CONTROL_5,  0x0002 },
	{ MADERA_FLLAO_CONTROL_6,  0x8001 },
	{ MADERA_FLLAO_CONTROL_7,  0x0004 },
	{ MADERA_FLLAO_CONTROL_8,  0x0077 },
	{ MADERA_FLLAO_CONTROL_10, 0x06D8 },
	{ MADERA_FLLAO_CONTROL_11, 0x0085 },
	{ MADERA_FLLAO_CONTROL_2,  0x82EE },
};

static const struct reg_sequence madera_fll_ao_32K_45M_patch[] = {
	{ MADERA_FLLAO_CONTROL_2,  0x02B1 },
	{ MADERA_FLLAO_CONTROL_3,  0x0001 },
	{ MADERA_FLLAO_CONTROL_4,  0x0010 },
	{ MADERA_FLLAO_CONTROL_5,  0x0002 },
	{ MADERA_FLLAO_CONTROL_6,  0x8001 },
	{ MADERA_FLLAO_CONTROL_7,  0x0004 },
	{ MADERA_FLLAO_CONTROL_8,  0x0077 },
	{ MADERA_FLLAO_CONTROL_10, 0x06D8 },
	{ MADERA_FLLAO_CONTROL_11, 0x0005 },
	{ MADERA_FLLAO_CONTROL_2,  0x82B1 },
};

struct madera_fllao_patch {
	unsigned int fin;
	unsigned int fout;
	const struct reg_sequence *patch;
	unsigned int patch_size;
};

static const struct madera_fllao_patch madera_fllao_settings[] = {
	{
		.fin = 32768,
		.fout = 49152000,
		.patch = madera_fll_ao_32K_49M_patch,
		.patch_size = ARRAY_SIZE(madera_fll_ao_32K_49M_patch),

	},
	{
		.fin = 32768,
		.fout = 45158400,
		.patch = madera_fll_ao_32K_45M_patch,
		.patch_size = ARRAY_SIZE(madera_fll_ao_32K_45M_patch),
	},
};

static int madera_enable_fll_ao(struct madera_fll *fll,
				const struct reg_sequence *patch,
				unsigned int patch_size)
{
	struct madera *madera = fll->madera;
	int already_enabled = madera_is_enabled_fll(fll);
	unsigned int val;
	int i;

	if (already_enabled < 0)
		return already_enabled;

	if (!already_enabled)
		pm_runtime_get_sync(madera->dev);

	madera_fll_dbg(fll, "Enabling FLL_AO, initially %s\n",
			already_enabled ? "enabled" : "disabled");

	/* FLL_AO_HOLD must be set before configuring any registers */
	regmap_update_bits(fll->madera->regmap,
			   fll->base + MADERA_FLLAO_CONTROL_1_OFFS,
			   MADERA_FLL_AO_HOLD, MADERA_FLL_AO_HOLD);

	for (i = 0; i < patch_size; i++) {
		val = patch[i].def;

		/* modify the patch to apply fll->ref_src is input clock */
		if (patch[i].reg == MADERA_FLLAO_CONTROL_6) {
			val &= ~MADERA_FLL_AO_REFCLK_SRC_MASK;
			val |= (fll->ref_src << MADERA_FLL_AO_REFCLK_SRC_SHIFT)
				& MADERA_FLL_AO_REFCLK_SRC_MASK;
		}

		regmap_write(madera->regmap, patch[i].reg, val);
	}

	regmap_update_bits(madera->regmap,
			   fll->base + MADERA_FLLAO_CONTROL_1_OFFS,
			   MADERA_FLL_AO_ENA, MADERA_FLL_AO_ENA);

	/* Release the hold so that fll_ao locks to external frequency */
	regmap_update_bits(madera->regmap,
			   fll->base + MADERA_FLLAO_CONTROL_1_OFFS,
			   MADERA_FLL_AO_HOLD, 0);

	if (!already_enabled)
		madera_wait_for_fll(fll, true);

	return 0;
}

static int madera_disable_fll_ao(struct madera_fll *fll)
{
	struct madera *madera = fll->madera;
	bool change;

	madera_fll_dbg(fll, "Disabling FLL_AO\n");

	regmap_update_bits(madera->regmap,
			   fll->base + MADERA_FLLAO_CONTROL_1_OFFS,
			   MADERA_FLL_AO_HOLD, MADERA_FLL_AO_HOLD);
	regmap_update_bits_check(madera->regmap,
				 fll->base + MADERA_FLLAO_CONTROL_1_OFFS,
				 MADERA_FLL_AO_ENA, 0, &change);

	madera_wait_for_fll(fll, false);

	/* ctrl_up gates the writes to all fll_ao register, setting it to 0
	 * here ensures that after a runtime suspend/resume cycle when one
	 * enables the fllao then ctrl_up is the last bit that is configured
	 * by the fllao enable code rather than the cache sync operation which
	 * would have updated it much earlier before writing out all fllao
	 * registers
	 */
	regmap_update_bits(madera->regmap,
			   fll->base + MADERA_FLLAO_CONTROL_2_OFFS,
			   MADERA_FLL_AO_CTRL_UPD_MASK, 0);

	if (change)
		pm_runtime_put_autosuspend(madera->dev);

	return 0;
}

int madera_set_fll_ao_refclk(struct madera_fll *fll, int source,
			     unsigned int fin, unsigned int fout)
{
	int ret = 0;
	const struct reg_sequence *patch = NULL;
	int patch_size = 0;
	unsigned int i;

	if (fll->ref_src == source &&
	    fll->ref_freq == fin && fll->fout == fout)
		return 0;

	madera_fll_dbg(fll, "Change FLL_AO refclk to fin=%u fout=%u source=%d\n",
			fin, fout, source);

	if (fout && (fll->ref_freq != fin || fll->fout != fout)) {
		for (i = 0; i < ARRAY_SIZE(madera_fllao_settings); i++) {
			if (madera_fllao_settings[i].fin == fin &&
			    madera_fllao_settings[i].fout == fout)
				break;
		}

		if (i == ARRAY_SIZE(madera_fllao_settings)) {
			madera_fll_err(fll,
					"No matching configuration for FLL_AO\n");
			return -EINVAL;
		}

		patch = madera_fllao_settings[i].patch;
		patch_size = madera_fllao_settings[i].patch_size;
	}

	fll->ref_src = source;
	fll->ref_freq = fin;
	fll->fout = fout;

	if (fout)
		ret = madera_enable_fll_ao(fll, patch, patch_size);
	else
		madera_disable_fll_ao(fll);

	return ret;
}
EXPORT_SYMBOL_GPL(madera_set_fll_ao_refclk);

/**
 * madera_set_output_mode - Set the mode of the specified output
 *
 * @codec: Device to configure
 * @output: Output number
 * @diff: True to set the output to differential mode
 *
 * Some systems use external analogue switches to connect more
 * analogue devices to the CODEC than are supported by the device.  In
 * some systems this requires changing the switched output from single
 * ended to differential mode dynamically at runtime, an operation
 * supported using this function.
 *
 * Most systems have a single static configuration and should use
 * platform data instead.
 */
int madera_set_output_mode(struct snd_soc_codec *codec, int output, bool diff)
{
	unsigned int reg, val;
	int ret;

	if (output < 1 || output > MADERA_MAX_OUTPUT)
		return -EINVAL;

	reg = MADERA_OUTPUT_PATH_CONFIG_1L + (output - 1) * 8;

	if (diff)
		val = MADERA_OUT1_MONO;
	else
		val = 0;

	ret = snd_soc_update_bits(codec, reg, MADERA_OUT1_MONO, val);
	if (ret < 0)
		return ret;
	else
		return 0;
}
EXPORT_SYMBOL_GPL(madera_set_output_mode);

int madera_set_custom_jd(struct snd_soc_codec *codec,
			 const struct madera_jd_state *custom_jd,
			 unsigned int index)
{
	struct madera *madera = dev_get_drvdata(codec->dev->parent);

	if (index >= MADERA_MAX_ACCESSORY)
		return -EINVAL;

	madera->pdata.accdet[index].custom_jd = custom_jd;

	return 0;
}
EXPORT_SYMBOL_GPL(madera_set_custom_jd);

struct madera_extcon_info *
madera_get_extcon_info(struct snd_soc_codec *codec)
{
	struct madera *madera = dev_get_drvdata(codec->dev->parent);

	return madera->extcon_info;
}
EXPORT_SYMBOL_GPL(madera_get_extcon_info);

static int madera_set_force_bypass(struct snd_soc_codec *codec, bool set_bypass)
{
	struct madera *madera = dev_get_drvdata(codec->dev->parent);
	struct madera_micbias *micbias = madera->pdata.micbias;
	unsigned int i, cp_bypass = 0, micbias_bypass = 0;
	unsigned int num_micbiases;

	if (set_bypass) {
		cp_bypass = MADERA_CPMIC_BYPASS;
		micbias_bypass = MADERA_MICB1_BYPASS;
	}

	if (madera->micvdd_regulated) {
		if (set_bypass)
			snd_soc_dapm_disable_pin(madera->dapm, "MICSUPP");
		else
			snd_soc_dapm_force_enable_pin(madera->dapm, "MICSUPP");

		snd_soc_dapm_sync(madera->dapm);

		regmap_update_bits(madera->regmap,
				   MADERA_MIC_CHARGE_PUMP_1,
				   MADERA_CPMIC_BYPASS, cp_bypass);
	}

	madera_get_num_micbias(madera, &num_micbiases, NULL);

	for (i = 0; i < num_micbiases; i++) {
		if ((set_bypass) ||
			(!micbias[i].bypass && micbias[i].mV))
			regmap_update_bits(madera->regmap,
					   MADERA_MIC_BIAS_CTRL_1 + i,
					   MADERA_MICB1_BYPASS,
					   micbias_bypass);
	}

	return 0;
}

int madera_enable_force_bypass(struct snd_soc_codec *codec)
{
	return madera_set_force_bypass(codec, true);
}
EXPORT_SYMBOL_GPL(madera_enable_force_bypass);

int madera_disable_force_bypass(struct snd_soc_codec *codec)
{
	return madera_set_force_bypass(codec, false);
}
EXPORT_SYMBOL_GPL(madera_disable_force_bypass);

int madera_frf_bytes_put(struct snd_kcontrol *kcontrol,
			 struct snd_ctl_elem_value *ucontrol)
{
	struct soc_bytes *params = (void *)kcontrol->private_value;
	struct snd_soc_component *component = snd_kcontrol_chip(kcontrol);
	struct snd_soc_codec *codec = snd_soc_kcontrol_codec(kcontrol);
	struct madera_priv *priv = snd_soc_codec_get_drvdata(codec);
	struct madera *madera = priv->madera;
	int ret, len;
	void *data;

	len = params->num_regs * component->val_bytes;

	data = kmemdup(ucontrol->value.bytes.data, len, GFP_KERNEL | GFP_DMA);
	if (!data)
		return -ENOMEM;

	mutex_lock(&madera->reg_setting_lock);
	regmap_write(madera->regmap, 0x80, 0x3);

	ret = regmap_raw_write(madera->regmap, params->base, data, len);

	regmap_write(madera->regmap, 0x80, 0x0);
	mutex_unlock(&madera->reg_setting_lock);

	kfree(data);
	return ret;
}
EXPORT_SYMBOL_GPL(madera_frf_bytes_put);

static bool madera_eq_filter_unstable(bool mode, __be16 _a, __be16 _b)
{
	s16 a = be16_to_cpu(_a);
	s16 b = be16_to_cpu(_b);

	if (!mode) {
		return abs(a) >= 4096;
	} else {
		if (abs(b) >= 4096)
			return true;

		return (abs((a << 16) / (4096 - b)) >= 4096 << 4);
	}
}

int madera_eq_coeff_put(struct snd_kcontrol *kcontrol,
			struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_soc_kcontrol_codec(kcontrol);
	struct madera *madera = dev_get_drvdata(codec->dev->parent);
	struct soc_bytes *params = (void *)kcontrol->private_value;
	unsigned int val;
	__be16 *data;
	int len;
	int ret;

	len = params->num_regs * regmap_get_val_bytes(madera->regmap);

	data = kmemdup(ucontrol->value.bytes.data, len, GFP_KERNEL | GFP_DMA);
	if (!data)
		return -ENOMEM;

	data[0] &= cpu_to_be16(MADERA_EQ1_B1_MODE);

	if (madera_eq_filter_unstable(!!data[0], data[1], data[2]) ||
	    madera_eq_filter_unstable(true, data[4], data[5]) ||
	    madera_eq_filter_unstable(true, data[8], data[9]) ||
	    madera_eq_filter_unstable(true, data[12], data[13]) ||
	    madera_eq_filter_unstable(false, data[16], data[17])) {
		dev_err(madera->dev, "Rejecting unstable EQ coefficients\n");
		ret = -EINVAL;
		goto out;
	}

	ret = regmap_read(madera->regmap, params->base, &val);
	if (ret != 0)
		goto out;

	val &= ~MADERA_EQ1_B1_MODE;
	data[0] |= cpu_to_be16(val);

	ret = regmap_raw_write(madera->regmap, params->base, data, len);

out:
	kfree(data);
	return ret;
}
EXPORT_SYMBOL_GPL(madera_eq_coeff_put);

int madera_lhpf_coeff_put(struct snd_kcontrol *kcontrol,
			   struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_soc_kcontrol_codec(kcontrol);
	struct madera *madera = dev_get_drvdata(codec->dev->parent);
	__be16 *data = (__be16 *)ucontrol->value.bytes.data;
	s16 val = be16_to_cpu(*data);

	if (abs(val) >= 4096) {
		dev_err(madera->dev, "Rejecting unstable LHPF coefficients\n");
		return -EINVAL;
	}

	return snd_soc_bytes_put(kcontrol, ucontrol);
}
EXPORT_SYMBOL_GPL(madera_lhpf_coeff_put);

int madera_register_notifier(struct snd_soc_codec *codec,
			     struct notifier_block *nb)
{
	struct madera *madera = dev_get_drvdata(codec->dev->parent);

	return blocking_notifier_chain_register(&madera->notifier, nb);
}
EXPORT_SYMBOL_GPL(madera_register_notifier);

int madera_unregister_notifier(struct snd_soc_codec *codec,
			       struct notifier_block *nb)
{
	struct madera *madera = dev_get_drvdata(codec->dev->parent);

	return blocking_notifier_chain_unregister(&madera->notifier, nb);
}
EXPORT_SYMBOL_GPL(madera_unregister_notifier);

MODULE_DESCRIPTION("ASoC Cirrus Logic Madera codec support");
MODULE_AUTHOR("Charles Keepax <ckeepax@opensource.wolfsonmicro.com>");
MODULE_AUTHOR("Richard Fitzgerald <rf@opensource.wolfsonmicro.com>");
MODULE_LICENSE("GPL v2");
