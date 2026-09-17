/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/drivers/dac.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/byteorder.h>

LOG_MODULE_REGISTER(dac_ad53x4, CONFIG_DAC_LOG_LEVEL);

/* https://www.analog.com/media/en/technical-documentation/data-sheets/ad5304_5314_5324.pdf */

#define DAC_AD53X4_NUM_CHANNELS  4U
#define DAC_AD53X4_CHANNEL_SHIFT 14U
#define DAC_AD53X4_MODE_NORMAL   BIT(13)
#define DAC_AD53X4_DATA_BITS     12U

struct ad53x4_config {
	struct spi_dt_spec bus;
	uint8_t resolution;
};

static int ad53x4_channel_setup(const struct device *dev, const struct dac_channel_cfg *channel_cfg)
{
	const struct ad53x4_config *config = dev->config;

	if (channel_cfg->channel_id >= DAC_AD53X4_NUM_CHANNELS) {
		LOG_ERR("invalid channel %u", channel_cfg->channel_id);
		return -EINVAL;
	}

	if (channel_cfg->resolution != config->resolution) {
		LOG_ERR("invalid resolution %u", channel_cfg->resolution);
		return -ENOTSUP;
	}

	if (channel_cfg->internal) {
		LOG_ERR("internal channels not supported");
		return -ENOTSUP;
	}

	return 0;
}

static int ad53x4_write_value(const struct device *dev, uint8_t channel, uint32_t value)
{
	const struct ad53x4_config *config = dev->config;
	uint8_t buffer_tx[2];
	uint16_t command;
	const struct spi_buf tx_buf = {
		.buf = buffer_tx,
		.len = sizeof(buffer_tx),
	};
	const struct spi_buf_set tx = {
		.buffers = &tx_buf,
		.count = 1,
	};
	int ret;

	if (channel >= DAC_AD53X4_NUM_CHANNELS) {
		LOG_ERR("invalid channel %u", channel);
		return -EINVAL;
	}

	if (value >= BIT(config->resolution)) {
		LOG_ERR("invalid value %u", value);
		return -EINVAL;
	}

	/* PD = 1 selects normal operation; LDAC = 0 updates the selected output. */
	command = ((uint16_t)channel << DAC_AD53X4_CHANNEL_SHIFT) | DAC_AD53X4_MODE_NORMAL |
		  (value << (DAC_AD53X4_DATA_BITS - config->resolution));
	sys_put_be16(command, buffer_tx);

	ret = spi_write_dt(&config->bus, &tx);
	if (ret != 0) {
		LOG_ERR("SPI write failed with error %d", ret);
	}

	return ret;
}

static int ad53x4_init(const struct device *dev)
{
	const struct ad53x4_config *config = dev->config;

	if (!spi_is_ready_dt(&config->bus)) {
		LOG_ERR("SPI bus %s not ready", config->bus.bus->name);
		return -ENODEV;
	}

	return 0;
}

static DEVICE_API(dac, ad53x4_driver_api) = {
	.channel_setup = ad53x4_channel_setup,
	.write_value = ad53x4_write_value,
};

BUILD_ASSERT(CONFIG_DAC_AD53X4_INIT_PRIORITY > CONFIG_SPI_INIT_PRIORITY,
	     "CONFIG_DAC_AD53X4_INIT_PRIORITY must be higher than CONFIG_SPI_INIT_PRIORITY");

#define DAC_AD53X4_DEVICE(node_id, name, res)                                                      \
	BUILD_ASSERT(DT_PROP(node_id, spi_cpha) != DT_PROP(node_id, spi_cpol),                     \
		     "exactly one of spi-cpha and spi-cpol must be set");                          \
	BUILD_ASSERT(!DT_PROP(node_id, spi_lsb_first), "LSB-first operation is not supported");    \
	static const struct ad53x4_config config_##name = {                                        \
		.bus = SPI_DT_SPEC_GET(node_id, SPI_OP_MODE_CONTROLLER | SPI_WORD_SET(8)),         \
		.resolution = res,                                                                 \
	};                                                                                         \
	DEVICE_DT_DEFINE(node_id, ad53x4_init, NULL, NULL, &config_##name, POST_KERNEL,            \
			 CONFIG_DAC_AD53X4_INIT_PRIORITY, &ad53x4_driver_api);

#define DAC_AD53X4_INST_DEFINE(index, compat, name, res)                                           \
	DAC_AD53X4_DEVICE(DT_INST(index, compat), name##_##index, res)

#define DT_DRV_COMPAT adi_ad5304
DT_INST_FOREACH_STATUS_OKAY_VARGS(DAC_AD53X4_INST_DEFINE, DT_DRV_COMPAT, ad5304, 8)
#undef DT_DRV_COMPAT

#define DT_DRV_COMPAT adi_ad5314
DT_INST_FOREACH_STATUS_OKAY_VARGS(DAC_AD53X4_INST_DEFINE, DT_DRV_COMPAT, ad5314, 10)
#undef DT_DRV_COMPAT

#define DT_DRV_COMPAT adi_ad5324
DT_INST_FOREACH_STATUS_OKAY_VARGS(DAC_AD53X4_INST_DEFINE, DT_DRV_COMPAT, ad5324, 12)
#undef DT_DRV_COMPAT
