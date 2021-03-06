/*
 * (C) Copyright 2010
 * Dirk Eibach,  Guntermann & Drunck GmbH, eibach@gdsys.de
 *
 * SPDX-License-Identifier:	GPL-2.0+
 */

#include <common.h>
#include <command.h>
#include <errno.h>
#include <asm/processor.h>
#include <asm/io.h>
#include <asm/ppc4xx-gpio.h>

#include "405ep.h"
#include <gdsys_fpga.h>

#include "../common/osd.h"
#include "../common/mclink.h"

#include <i2c.h>
#include <pca953x.h>
#include <pca9698.h>

#include <miiphy.h>

DECLARE_GLOBAL_DATA_PTR;

#define LATCH0_BASE (CONFIG_SYS_LATCH_BASE)
#define LATCH1_BASE (CONFIG_SYS_LATCH_BASE + 0x100)
#define LATCH2_BASE (CONFIG_SYS_LATCH_BASE + 0x200)

#define MAX_MUX_CHANNELS 2

enum {
	UNITTYPE_MAIN_SERVER = 0,
	UNITTYPE_MAIN_USER = 1,
	UNITTYPE_VIDEO_SERVER = 2,
	UNITTYPE_VIDEO_USER = 3,
};

enum {
	HWVER_100 = 0,
	HWVER_104 = 1,
	HWVER_110 = 2,
	HWVER_120 = 3,
	HWVER_200 = 4,
	HWVER_210 = 5,
	HWVER_220 = 6,
	HWVER_230 = 7,
};

enum {
	FPGA_HWVER_200 = 0,
	FPGA_HWVER_210 = 1,
};

enum {
	COMPRESSION_NONE = 0,
	COMPRESSION_TYPE1_DELTA = 1,
	COMPRESSION_TYPE1_TYPE2_DELTA = 3,
};

enum {
	AUDIO_NONE = 0,
	AUDIO_TX = 1,
	AUDIO_RX = 2,
	AUDIO_RXTX = 3,
};

enum {
	SYSCLK_147456 = 0,
};

enum {
	RAM_DDR2_32 = 0,
	RAM_DDR3_32 = 1,
};

enum {
	CARRIER_SPEED_1G = 0,
	CARRIER_SPEED_2_5G = 1,
};

enum {
	MCFPGA_DONE = 1 << 0,
	MCFPGA_INIT_N = 1 << 1,
	MCFPGA_PROGRAM_N = 1 << 2,
	MCFPGA_UPDATE_ENABLE_N = 1 << 3,
	MCFPGA_RESET_N = 1 << 4,
};

enum {
	GPIO_MDC = 1 << 14,
	GPIO_MDIO = 1 << 15,
};

unsigned int mclink_fpgacount;
struct ihs_fpga *fpga_ptr[] = CONFIG_SYS_FPGA_PTR;

static int setup_88e1518(const char *bus, unsigned char addr);

int fpga_set_reg(u32 fpga, u16 *reg, off_t regoff, u16 data)
{
	int res;

	switch (fpga) {
	case 0:
		out_le16(reg, data);
		break;
	default:
		res = mclink_send(fpga - 1, regoff, data);
		if (res < 0) {
			printf("mclink_send reg %02lx data %04x returned %d\n",
			       regoff, data, res);
			return res;
		}
		break;
	}

	return 0;
}

int fpga_get_reg(u32 fpga, u16 *reg, off_t regoff, u16 *data)
{
	int res;

	switch (fpga) {
	case 0:
		*data = in_le16(reg);
		break;
	default:
		if (fpga > mclink_fpgacount)
			return -EINVAL;
		res = mclink_receive(fpga - 1, regoff, data);
		if (res < 0) {
			printf("mclink_receive reg %02lx returned %d\n",
			       regoff, res);
			return res;
		}
	}

	return 0;
}

/*
 * Check Board Identity:
 */
int checkboard(void)
{
	char *s = getenv("serial#");

	puts("Board: ");

	puts("IoCon");

	if (s != NULL) {
		puts(", serial# ");
		puts(s);
	}

	puts("\n");

	return 0;
}

static void print_fpga_info(unsigned int fpga, bool rgmii2_present)
{
	u16 versions;
	u16 fpga_version;
	u16 fpga_features;
	unsigned unit_type;
	unsigned hardware_version;
	unsigned feature_compression;
	unsigned feature_osd;
	unsigned feature_audio;
	unsigned feature_sysclock;
	unsigned feature_ramconfig;
	unsigned feature_carrier_speed;
	unsigned feature_carriers;
	unsigned feature_video_channels;

	int legacy = get_fpga_state(0) & FPGA_STATE_PLATFORM;

	FPGA_GET_REG(0, versions, &versions);
	FPGA_GET_REG(0, fpga_version, &fpga_version);
	FPGA_GET_REG(0, fpga_features, &fpga_features);

	unit_type = (versions & 0xf000) >> 12;
	feature_compression = (fpga_features & 0xe000) >> 13;
	feature_osd = fpga_features & (1<<11);
	feature_audio = (fpga_features & 0x0600) >> 9;
	feature_sysclock = (fpga_features & 0x0180) >> 7;
	feature_ramconfig = (fpga_features & 0x0060) >> 5;
	feature_carrier_speed = fpga_features & (1<<4);
	feature_carriers = (fpga_features & 0x000c) >> 2;
	feature_video_channels = fpga_features & 0x0003;

	if (legacy)
		printf("legacy ");

	switch (unit_type) {
	case UNITTYPE_MAIN_USER:
		printf("Mainchannel");
		break;

	case UNITTYPE_VIDEO_USER:
		printf("Videochannel");
		break;

	default:
		printf("UnitType %d(not supported)", unit_type);
		break;
	}

	if (unit_type == UNITTYPE_MAIN_USER) {
		if (legacy)
			hardware_version =
				(in_le16((void *)LATCH2_BASE)>>8) & 0x0f;
		else
			hardware_version =
				  (!!pca9698_get_value(0x20, 24) << 0)
				| (!!pca9698_get_value(0x20, 25) << 1)
				| (!!pca9698_get_value(0x20, 26) << 2)
				| (!!pca9698_get_value(0x20, 27) << 3);
	switch (hardware_version) {
	case HWVER_100:
			printf(" HW-Ver 1.00,");
		break;

	case HWVER_104:
			printf(" HW-Ver 1.04,");
		break;

	case HWVER_110:
			printf(" HW-Ver 1.10,");
			break;

		case HWVER_120:
			printf(" HW-Ver 1.20-1.21,");
			break;

		case HWVER_200:
			printf(" HW-Ver 2.00,");
			break;

		case HWVER_210:
			printf(" HW-Ver 2.10,");
			break;

		case HWVER_220:
			printf(" HW-Ver 2.20,");
			break;

		case HWVER_230:
			printf(" HW-Ver 2.30,");
		break;

	default:
			printf(" HW-Ver %d(not supported),",
		       hardware_version);
		break;
	}
		if (rgmii2_present)
			printf(" RGMII2,");
	}

	if (unit_type == UNITTYPE_VIDEO_USER) {
		hardware_version = versions & 0x000f;
		switch (hardware_version) {
		case FPGA_HWVER_200:
			printf(" HW-Ver 2.00,");
			break;

		case FPGA_HWVER_210:
			printf(" HW-Ver 2.10,");
			break;

		default:
			printf(" HW-Ver %d(not supported),",
			       hardware_version);
			break;
		}
	}

	printf(" FPGA V %d.%02d\n       features:",
		fpga_version / 100, fpga_version % 100);


	switch (feature_compression) {
	case COMPRESSION_NONE:
		printf(" no compression");
		break;

	case COMPRESSION_TYPE1_DELTA:
		printf(" type1-deltacompression");
		break;

	case COMPRESSION_TYPE1_TYPE2_DELTA:
		printf(" type1-deltacompression, type2-inlinecompression");
		break;

	default:
		printf(" compression %d(not supported)", feature_compression);
		break;
	}

	printf(", %sosd", feature_osd ? "" : "no ");

	switch (feature_audio) {
	case AUDIO_NONE:
		printf(", no audio");
		break;

	case AUDIO_TX:
		printf(", audio tx");
		break;

	case AUDIO_RX:
		printf(", audio rx");
		break;

	case AUDIO_RXTX:
		printf(", audio rx+tx");
		break;

	default:
		printf(", audio %d(not supported)", feature_audio);
		break;
	}

	puts(",\n       ");

	switch (feature_sysclock) {
	case SYSCLK_147456:
		printf("clock 147.456 MHz");
		break;

	default:
		printf("clock %d(not supported)", feature_sysclock);
		break;
	}

	switch (feature_ramconfig) {
	case RAM_DDR2_32:
		printf(", RAM 32 bit DDR2");
		break;

	case RAM_DDR3_32:
		printf(", RAM 32 bit DDR3");
		break;

	default:
		printf(", RAM %d(not supported)", feature_ramconfig);
		break;
	}

	printf(", %d carrier(s) %s", feature_carriers,
	       feature_carrier_speed ? "2.5Gbit/s" : "1Gbit/s");

	printf(", %d video channel(s)\n", feature_video_channels);
}

int last_stage_init(void)
{
	int slaves;
	unsigned int k;
	unsigned int mux_ch;
	unsigned char mclink_controllers[] = { 0x24, 0x25, 0x26 };
	int legacy = get_fpga_state(0) & FPGA_STATE_PLATFORM;
	u16 fpga_features;
	int feature_carrier_speed = fpga_features & (1<<4);
	bool ch0_rgmii2_present = false;

	FPGA_GET_REG(0, fpga_features, &fpga_features);

	if (!legacy)
		ch0_rgmii2_present = !pca9698_get_value(0x20, 30);

	print_fpga_info(0, ch0_rgmii2_present);
	osd_probe(0);

	/* wait for FPGA done */
	for (k = 0; k < ARRAY_SIZE(mclink_controllers); ++k) {
		unsigned int ctr = 0;

		if (i2c_probe(mclink_controllers[k]))
			continue;

		while (!(pca953x_get_val(mclink_controllers[k])
		       & MCFPGA_DONE)) {
			udelay(100000);
			if (ctr++ > 5) {
				printf("no done for mclink_controller %d\n", k);
				break;
			}
		}
	}

	if (!legacy && (feature_carrier_speed == CARRIER_SPEED_1G)) {
		miiphy_register(bb_miiphy_buses[0].name, bb_miiphy_read,
				bb_miiphy_write);
		for (mux_ch = 0; mux_ch < MAX_MUX_CHANNELS; ++mux_ch) {
			if ((mux_ch == 1) && !ch0_rgmii2_present)
				continue;

			setup_88e1518(bb_miiphy_buses[0].name, mux_ch);
		}
	}

	/* wait for slave-PLLs to be up and running */
	udelay(500000);

	mclink_fpgacount = CONFIG_SYS_MCLINK_MAX;
	slaves = mclink_probe();
	mclink_fpgacount = 0;

	if (slaves <= 0)
		return 0;

	mclink_fpgacount = slaves;

	for (k = 1; k <= slaves; ++k) {
		FPGA_GET_REG(k, fpga_features, &fpga_features);
		feature_carrier_speed = fpga_features & (1<<4);

		print_fpga_info(k, false);
		osd_probe(k);
		if (feature_carrier_speed == CARRIER_SPEED_1G) {
			miiphy_register(bb_miiphy_buses[k].name,
					bb_miiphy_read, bb_miiphy_write);
			setup_88e1518(bb_miiphy_buses[k].name, 0);
		}
	}

	return 0;
}

/*
 * provide access to fpga gpios (for I2C bitbang)
 * (these may look all too simple but make iocon.h much more readable)
 */
void fpga_gpio_set(unsigned int bus, int pin)
{
	FPGA_SET_REG(bus, gpio.set, pin);
}

void fpga_gpio_clear(unsigned int bus, int pin)
{
	FPGA_SET_REG(bus, gpio.clear, pin);
}

int fpga_gpio_get(unsigned int bus, int pin)
{
	u16 val;

	FPGA_GET_REG(bus, gpio.read, &val);

	return val & pin;
}

void gd405ep_init(void)
{
	unsigned int k;

	if (i2c_probe(0x20)) { /* i2c_probe returns 0 on success */
		for (k = 0; k < CONFIG_SYS_FPGA_COUNT; ++k)
			gd->arch.fpga_state[k] |= FPGA_STATE_PLATFORM;
	} else {
		pca9698_direction_output(0x20, 4, 1);
	}
}

void gd405ep_set_fpga_reset(unsigned state)
{
	int legacy = get_fpga_state(0) & FPGA_STATE_PLATFORM;

	if (legacy) {
	if (state) {
		out_le16((void *)LATCH0_BASE, CONFIG_SYS_LATCH0_RESET);
		out_le16((void *)LATCH1_BASE, CONFIG_SYS_LATCH1_RESET);
	} else {
		out_le16((void *)LATCH0_BASE, CONFIG_SYS_LATCH0_BOOT);
		out_le16((void *)LATCH1_BASE, CONFIG_SYS_LATCH1_BOOT);
	}
	} else {
		pca9698_set_value(0x20, 4, state ? 0 : 1);
	}
}

void gd405ep_setup_hw(void)
{
	/*
	 * set "startup-finished"-gpios
	 */
	gpio_write_bit(21, 0);
	gpio_write_bit(22, 1);
}

int gd405ep_get_fpga_done(unsigned fpga)
{
	int legacy = get_fpga_state(0) & FPGA_STATE_PLATFORM;

	if (legacy)
		return in_le16((void *)LATCH2_BASE)
		       & CONFIG_SYS_FPGA_DONE(fpga);
	else
		return pca9698_get_value(0x20, 20);
}

/*
 * FPGA MII bitbang implementation
 */

struct fpga_mii {
	unsigned fpga;
	int mdio;
} fpga_mii[] = {
	{ 0, 1},
	{ 1, 1},
	{ 2, 1},
	{ 3, 1},
};

static int mii_dummy_init(struct bb_miiphy_bus *bus)
{
	return 0;
}

static int mii_mdio_active(struct bb_miiphy_bus *bus)
{
	struct fpga_mii *fpga_mii = bus->priv;

	if (fpga_mii->mdio)
		FPGA_SET_REG(fpga_mii->fpga, gpio.set, GPIO_MDIO);
	else
		FPGA_SET_REG(fpga_mii->fpga, gpio.clear, GPIO_MDIO);

	return 0;
}

static int mii_mdio_tristate(struct bb_miiphy_bus *bus)
{
	struct fpga_mii *fpga_mii = bus->priv;

	FPGA_SET_REG(fpga_mii->fpga, gpio.set, GPIO_MDIO);

	return 0;
}

static int mii_set_mdio(struct bb_miiphy_bus *bus, int v)
{
	struct fpga_mii *fpga_mii = bus->priv;

	if (v)
		FPGA_SET_REG(fpga_mii->fpga, gpio.set, GPIO_MDIO);
	else
		FPGA_SET_REG(fpga_mii->fpga, gpio.clear, GPIO_MDIO);

	fpga_mii->mdio = v;

	return 0;
}

static int mii_get_mdio(struct bb_miiphy_bus *bus, int *v)
{
	u16 gpio;
	struct fpga_mii *fpga_mii = bus->priv;

	FPGA_GET_REG(fpga_mii->fpga, gpio.read, &gpio);

	*v = ((gpio & GPIO_MDIO) != 0);

	return 0;
}

static int mii_set_mdc(struct bb_miiphy_bus *bus, int v)
{
	struct fpga_mii *fpga_mii = bus->priv;

	if (v)
		FPGA_SET_REG(fpga_mii->fpga, gpio.set, GPIO_MDC);
	else
		FPGA_SET_REG(fpga_mii->fpga, gpio.clear, GPIO_MDC);

	return 0;
}

static int mii_delay(struct bb_miiphy_bus *bus)
{
	udelay(1);

	return 0;
}

struct bb_miiphy_bus bb_miiphy_buses[] = {
	{
		.name = "board0",
		.init = mii_dummy_init,
		.mdio_active = mii_mdio_active,
		.mdio_tristate = mii_mdio_tristate,
		.set_mdio = mii_set_mdio,
		.get_mdio = mii_get_mdio,
		.set_mdc = mii_set_mdc,
		.delay = mii_delay,
		.priv = &fpga_mii[0],
	},
	{
		.name = "board1",
		.init = mii_dummy_init,
		.mdio_active = mii_mdio_active,
		.mdio_tristate = mii_mdio_tristate,
		.set_mdio = mii_set_mdio,
		.get_mdio = mii_get_mdio,
		.set_mdc = mii_set_mdc,
		.delay = mii_delay,
		.priv = &fpga_mii[1],
	},
	{
		.name = "board2",
		.init = mii_dummy_init,
		.mdio_active = mii_mdio_active,
		.mdio_tristate = mii_mdio_tristate,
		.set_mdio = mii_set_mdio,
		.get_mdio = mii_get_mdio,
		.set_mdc = mii_set_mdc,
		.delay = mii_delay,
		.priv = &fpga_mii[2],
	},
	{
		.name = "board3",
		.init = mii_dummy_init,
		.mdio_active = mii_mdio_active,
		.mdio_tristate = mii_mdio_tristate,
		.set_mdio = mii_set_mdio,
		.get_mdio = mii_get_mdio,
		.set_mdc = mii_set_mdc,
		.delay = mii_delay,
		.priv = &fpga_mii[3],
	},
};

int bb_miiphy_buses_num = sizeof(bb_miiphy_buses) /
			  sizeof(bb_miiphy_buses[0]);

enum {
	MIICMD_SET,
	MIICMD_MODIFY,
	MIICMD_VERIFY_VALUE,
	MIICMD_WAIT_FOR_VALUE,
};

struct mii_setupcmd {
	u8 token;
	u8 reg;
	u16 data;
	u16 mask;
	u32 timeout;
};

/*
 * verify we are talking to a 88e1518
 */
struct mii_setupcmd verify_88e1518[] = {
	{ MIICMD_SET, 22, 0x0000 },
	{ MIICMD_VERIFY_VALUE, 2, 0x0141, 0xffff },
	{ MIICMD_VERIFY_VALUE, 3, 0x0dd0, 0xfff0 },
};

/*
 * workaround for erratum mentioned in 88E1518 release notes
 */
struct mii_setupcmd fixup_88e1518[] = {
	{ MIICMD_SET, 22, 0x00ff },
	{ MIICMD_SET, 17, 0x214b },
	{ MIICMD_SET, 16, 0x2144 },
	{ MIICMD_SET, 17, 0x0c28 },
	{ MIICMD_SET, 16, 0x2146 },
	{ MIICMD_SET, 17, 0xb233 },
	{ MIICMD_SET, 16, 0x214d },
	{ MIICMD_SET, 17, 0xcc0c },
	{ MIICMD_SET, 16, 0x2159 },
	{ MIICMD_SET, 22, 0x00fb },
	{ MIICMD_SET,  7, 0xc00d },
	{ MIICMD_SET, 22, 0x0000 },
};

/*
 * default initialization:
 * - set RGMII receive timing to "receive clock transition when data stable"
 * - set RGMII transmit timing to "transmit clock internally delayed"
 * - set RGMII output impedance target to 78,8 Ohm
 * - run output impedance calibration
 * - set autonegotiation advertise to 1000FD only
 */
struct mii_setupcmd default_88e1518[] = {
	{ MIICMD_SET, 22, 0x0002 },
	{ MIICMD_MODIFY, 21, 0x0030, 0x0030 },
	{ MIICMD_MODIFY, 25, 0x0000, 0x0003 },
	{ MIICMD_MODIFY, 24, 0x8000, 0x8000 },
	{ MIICMD_WAIT_FOR_VALUE, 24, 0x4000, 0x4000, 2000 },
	{ MIICMD_SET, 22, 0x0000 },
	{ MIICMD_MODIFY, 4, 0x0000, 0x01e0 },
	{ MIICMD_MODIFY, 9, 0x0200, 0x0300 },
};

/*
 * turn off CLK125 for PHY daughterboard
 */
struct mii_setupcmd ch1fix_88e1518[] = {
	{ MIICMD_SET, 22, 0x0002 },
	{ MIICMD_MODIFY, 16, 0x0006, 0x0006 },
	{ MIICMD_SET, 22, 0x0000 },
};

/*
 * perform copper software reset
 */
struct mii_setupcmd swreset_88e1518[] = {
	{ MIICMD_SET, 22, 0x0000 },
	{ MIICMD_MODIFY, 0, 0x8000, 0x8000 },
	{ MIICMD_WAIT_FOR_VALUE, 0, 0x0000, 0x8000, 2000 },
};

static int process_setupcmd(const char *bus, unsigned char addr,
			    struct mii_setupcmd *setupcmd)
{
	int res;
	u8 reg = setupcmd->reg;
	u16 data = setupcmd->data;
	u16 mask = setupcmd->mask;
	u32 timeout = setupcmd->timeout;
	u16 orig_data;
	unsigned long start;

	debug("mii %s:%u reg %2u ", bus, addr, reg);

	switch (setupcmd->token) {
	case MIICMD_MODIFY:
		res = miiphy_read(bus, addr, reg, &orig_data);
		if (res)
			break;
		debug("is %04x. (value %04x mask %04x) ", orig_data, data,
		      mask);
		data = (orig_data & ~mask) | (data & mask);
	case MIICMD_SET:
		debug("=> %04x\n", data);
		res = miiphy_write(bus, addr, reg, data);
		break;
	case MIICMD_VERIFY_VALUE:
		res = miiphy_read(bus, addr, reg, &orig_data);
		if (res)
			break;
		if ((orig_data & mask) != (data & mask))
			res = -1;
		debug("(value %04x mask %04x) == %04x? %s\n", data, mask,
		      orig_data, res ? "FAIL" : "PASS");
		break;
	case MIICMD_WAIT_FOR_VALUE:
		res = -1;
		start = get_timer(0);
		while ((res != 0) && (get_timer(start) < timeout)) {
			res = miiphy_read(bus, addr, reg, &orig_data);
			if (res)
				continue;
			if ((orig_data & mask) != (data & mask))
				res = -1;
		}
		debug("(value %04x mask %04x) == %04x? %s after %lu ms\n", data,
		      mask, orig_data, res ? "FAIL" : "PASS",
		      get_timer(start));
		break;
	default:
		res = -1;
		break;
	}

	return res;
}

static int process_setup(const char *bus, unsigned char addr,
			    struct mii_setupcmd *setupcmd, unsigned int count)
{
	int res = 0;
	unsigned int k;

	for (k = 0; k < count; ++k) {
		res = process_setupcmd(bus, addr, &setupcmd[k]);
		if (res) {
			printf("mii cmd %u on bus %s addr %u failed, aborting setup",
			       setupcmd[k].token, bus, addr);
			break;
		}
	}

	return res;
}

static int setup_88e1518(const char *bus, unsigned char addr)
{
	int res;

	res = process_setup(bus, addr,
			    verify_88e1518, ARRAY_SIZE(verify_88e1518));
	if (res)
		return res;

	res = process_setup(bus, addr,
			    fixup_88e1518, ARRAY_SIZE(fixup_88e1518));
	if (res)
		return res;

	res = process_setup(bus, addr,
			    default_88e1518, ARRAY_SIZE(default_88e1518));
	if (res)
		return res;

	if (addr) {
		res = process_setup(bus, addr,
				    ch1fix_88e1518, ARRAY_SIZE(ch1fix_88e1518));
		if (res)
			return res;
	}

	res = process_setup(bus, addr,
			    swreset_88e1518, ARRAY_SIZE(swreset_88e1518));
	if (res)
		return res;

	return 0;
}
