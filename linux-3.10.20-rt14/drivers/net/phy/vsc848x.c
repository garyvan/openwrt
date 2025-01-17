/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2012 Cavium, Inc.
 */

#include <linux/platform_device.h>
#include <linux/of_mdio.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/phy.h>
#include <linux/memory.h>
#include <linux/mutex.h>
#include <linux/of_memory_accessor.h>
#include <linux/delay.h>
#include <linux/ctype.h>

#define PMD_RX_SIGNAL_DETECT		(MII_ADDR_C45 | 0x01000a)
#define PMA_TXOUTCTRL2			(MII_ADDR_C45 | 0x018014)
#define EDC_EYE_QUALITY			(MII_ADDR_C45 | 0x018034)
/* EDC Firmware State Machine Status and Lib Force
 * 15: library force enable
 * 14:8 - Library number
 * 7:5  - N/A
 * 4    - State force
 * 3:0  - FW state (1=reset, 2=wait for alarm to clear, 3 = convergence,
 *	  4 = tracking, 5 = freeze)
 */
#define EDC_FW_SM_STATUS		(MII_ADDR_C45 | 0x018036)

#define BASER_PCS_STATUS		(MII_ADDR_C45 | 0x030020)
#define XGXS_LANE_STATUS		(MII_ADDR_C45 | 0x040018)
#define EWIS_INTR_PEND1			(MII_ADDR_C45 | 0x02EE00)
#define EWIS_INTR_MASKA_1		(MII_ADDR_C45 | 0x02EE01)
#define EWIS_INTR_MASKB_1		(MII_ADDR_C45 | 0x02EE02)
#define EWIS_INTR_STAT2			(MII_ADDR_C45 | 0x02EE03)
#define EWIS_INTR_PEND2			(MII_ADDR_C45 | 0x02EE04)
#define EWIS_INTR_MASKA_2		(MII_ADDR_C45 | 0x02EE05)
#define EWIS_INTR_MASKB_2		(MII_ADDR_C45 | 0x02EE06)
#define EWIS_FAULT_MASK			(MII_ADDR_C45 | 0x02EE07)
#define EWIS_INTR_PEND3			(MII_ADDR_C45 | 0x02EE08)
#define EWIS_INTR_MASKA_3		(MII_ADDR_C45 | 0x02EE09)
#define EWIS_INTR_MASKB_3		(MII_ADDR_C45 | 0x02EE0A)

/* Device ID
 * 15:0 - device ID
 */
#define GBL_DEVICE_ID			(MII_ADDR_C45 | 0x1e0000)
/* Device revision
 * 15:04 - reserved
 * 03:00 - revision ID
 */
#define GBL_DEVICE_REVISION		(MII_ADDR_C45 | 0x1e0001)
/* Block Level Software Reset
 * 15:14 - reserved
 * 13:   - software reset EDC 1 (1 = reset, autoclears)
 * 12:   - software reset EDC 0 (1 = reset, autoclears)
 * 11:10 - reserved
 * 09:   - Software reset channel 1 (1 = reset, autoclears)
 * 08:   - Software reset channel 0 (1 = reset, autoclears)
 * 07:   - Microprocessor reset (0 = normal operation, 1 = reset)
 * 06:   - Software reset BIU (1 = reset, autoclears)
 * 05:   - Software reset TWS slave (1 = reset, autoclears)
 * 04:   - Software reset TWS master (1 = reset, autoclears)
 * 03:   - Software reset MDIO (1 = reset, autoclears)
 * 02:   - Software reset UART (1 = reset, autoclears)
 * 01:   - Global register reset (1 = reset, autoclears)
 * 00:   - Software reset chip (1 = reset, autoclears)
 */
#define GBL_BLOCK_LVL_SW_RESET		(MII_ADDR_C45 | 0x1e0002)
#define GBL_GPIO_0_CONFIG1_STATUS	(MII_ADDR_C45 | 0x1e0100)
#define GBL_GPIO_0_CONFIG2		(MII_ADDR_C45 | 0x1e0101)
#define GBL_DEVICE_ID			(MII_ADDR_C45 | 0x1e0000)
#define GBL_FW_CHECKSUM			(MII_ADDR_C45 | 0x1e7fe0)
#define GBL_FW_WATCHDOG			(MII_ADDR_C45 | 0x1e7fe1)
#define GBL_FW_VERSION			(MII_ADDR_C45 | 0x1e7fe2)
#define GBL_FW_VAR_ACC_CTRL		(MII_ADDR_C45 | 0x1e7fe3)
#define GBL_FW_VAR_ACC_DATA		(MII_ADDR_C45 | 0x1e7fe4)

/* The Vitesse VSC848X series are 10G PHYs.
 *
 * Some of these devices contain multiple PHYs in a single package and
 * some features are controlled by a global set of registers shared between
 * all of the PHY devices.  Because of this a nexus is used to handle all
 * of the PHYs on the same device.
 *
 * Unlike some PHY devices, it is up to the driver to read the SFP module
 * serial EEPROM in order to put the PHY into the right mode.  The VSC848X
 * does not provide an I2C interface so the PHY driver relies on the
 * external AT24 I2C EEPROM driver to read the module whenever it is inserted.
 *
 */

/* Enable LOPC detection (see 0x5B for target state)
 * 15:12 - channel 3
 * 11:08 - channel 2
 * 07:04 - channel 1
 * 03:00 - channel 0
 * 1 = enable (default), 0 = disable
 */
#define FW_VAR_ENABLE_LOPC		0x58
/* While in tracking mode, go to this state in response to LOPC assertion
 * 1 = reset, 2 = wait (default), 3 = converging, 4 = tracking, 5 = freeze
 */
#define FW_VAR_LOPC_ASSERT_MODE		0x5B
/* While in freeze mode, enable state transition upon deassertion of LOPC (see
 * 0x61 for target state)
 * 1 - reset, 2 = wait, 3 = converging, 4 = tracking (default), 5 = freeze
 */
#define FW_VAR_FREEZE_DEASSERT_MODE	0x61
/* Current functional mode
 * See VITESSE_FUNC_MODE_XXX below for values
 * NOTE: When the firmware is done servicing the mode change request, bit 4
 * will be set to 1.
 */
#define FW_VAR_FUNCTIONAL_MODE		0x94
/* Current state of graded SPSA process
 * 3: channel 3
 * 2: channel 2
 * 1: channel 1
 * 0: channel 0
 * 1 = busy, 2 = done
 */
#define FW_VAR_GRADED_SPSA_STATE	0x95
/* BerScore at start of SPSA cycle */
#define FW_VAR_BERSCORE_START		0x96
/* BerScore at end of SPSA cycle */
#define FW_VAR_BERSCORE_END		0x97
/* Enable/Disable aggressive track phase on entering tracking state
 * 15:12 - channel 3
 * 11:08 - channel 2
 * 07:04 - channel 1
 * 03:00 - channel 0
 * 0 = disable, 1 = enable (default)
 */
#define FW_VAR_AGG_TRACKING		0xAF

/* Modes for the PHY firmware */
#define VITESSE_FUNC_MODE_LIMITING	2	/* Optical */
#define VITESSE_FUNC_MODE_COPPER	3	/* Copper */
#define VITESSE_FUNC_MODE_LINEAR	4
#define VITESSE_FUNC_MODE_KR		5
#define VITESSE_FUNC_MODE_ZR		7
#define VITESSE_FUNC_MODE_1G		8


struct vsc848x_nexus_mdiobus {
	struct mii_bus *mii_bus;
	struct mii_bus *parent_mii_bus;
	int reg_offset;
	struct mutex lock;	/* Lock used for global register sequences */
	int phy_irq[PHY_MAX_ADDR];
};

struct vsc848x_phy_info {
	int sfp_conn;		/* Module connected? */
	int tx_en_gpio;		/* GPIO that enables transmit */
	int mod_abs_gpio;	/* Module Absent GPIO line */
	int tx_fault_gpio;	/* TX Fault GPIO line */
	int inta_gpio, intb_gpio;	/* Interrupt GPIO line (output) */
	uint8_t mode;		/* Mode for module */
	uint8_t channel;	/* channel in multi-phy devices */
	struct device_node *sfp_node;	/* EEPROM NODE for SFP */
	struct memory_accessor *macc;	/* memory access routines for EEPROM */
	struct vsc848x_nexus_mdiobus *nexus;	/* Nexus for lock */
};

/**
 * Maps GPIO lines to the global GPIO config registers.
 *
 * Please see the data sheet since the configuration for each GPIO line is
 * different.
 */
static const struct {
	uint32_t config1_status_reg;
	uint32_t config2_reg;
} vcs848x_gpio_to_reg[12] = {
	{ (MII_ADDR_C45 | 0x1e0100), (MII_ADDR_C45 | 0x1e0101) },	/* 0 */
	{ (MII_ADDR_C45 | 0x1e0102), (MII_ADDR_C45 | 0x1e0103) },	/* 1 */
	{ (MII_ADDR_C45 | 0x1e0104), (MII_ADDR_C45 | 0x1e0105) },	/* 2 */
	{ (MII_ADDR_C45 | 0x1e0106), (MII_ADDR_C45 | 0x1e0107) },	/* 3 */
	{ (MII_ADDR_C45 | 0x1e0108), (MII_ADDR_C45 | 0x1e0109) },	/* 4 */
	{ (MII_ADDR_C45 | 0x1e010A), (MII_ADDR_C45 | 0x1e010B) },	/* 5 */
	{ (MII_ADDR_C45 | 0x1e0124), (MII_ADDR_C45 | 0x1e0125) },	/* 6 */
	{ (MII_ADDR_C45 | 0x1e0126), (MII_ADDR_C45 | 0x1e0127) },	/* 7 */
	{ (MII_ADDR_C45 | 0x1e0128), (MII_ADDR_C45 | 0x1e0129) },	/* 8 */
	{ (MII_ADDR_C45 | 0x1e012a), (MII_ADDR_C45 | 0x1e012b) },	/* 9 */
	{ (MII_ADDR_C45 | 0x1e012c), (MII_ADDR_C45 | 0x1e012d) },	/* 10 */
	{ (MII_ADDR_C45 | 0x1e012e), (MII_ADDR_C45 | 0x1e012f) },	/* 11 */
};

static int vsc848x_probe(struct phy_device *phydev)
{
	struct vsc848x_phy_info *dev_info;
	int ret;

	dev_info = devm_kzalloc(&phydev->dev, sizeof(*dev_info), GFP_KERNEL);
	if (dev_info == NULL)
		return -ENOMEM;

	phydev->priv = dev_info;
	dev_info->mode = VITESSE_FUNC_MODE_LIMITING;	/* Default to optical */
	phydev->priv = dev_info;
	dev_info->nexus = phydev->bus->priv;

	ret = of_property_read_u32(phydev->dev.of_node, "mod_abs",
				   &dev_info->mod_abs_gpio);
	if (ret) {
		dev_err(&phydev->dev, "%s has invalid mod_abs address\n",
			phydev->dev.of_node->full_name);
		return ret;
	}

	ret = of_property_read_u32(phydev->dev.of_node, "tx_fault",
				   &dev_info->tx_fault_gpio);
	if (ret) {
		dev_err(&phydev->dev, "%s has invalid tx_fault address\n",
			phydev->dev.of_node->full_name);
		return ret;
	}

	ret = of_property_read_u32(phydev->dev.of_node, "inta",
				   &dev_info->inta_gpio);
	if (ret)
		dev_info->inta_gpio = -1;

	ret = of_property_read_u32(phydev->dev.of_node, "intb",
				   &dev_info->intb_gpio);
	if (ret)
		dev_info->intb_gpio = -1;

	ret = of_property_read_u32(phydev->dev.of_node, "txon",
				   &dev_info->tx_en_gpio);
	if (ret) {
		dev_err(&phydev->dev, "%s has invalid txon gpio address\n",
			phydev->dev.of_node->full_name);
		return -ENXIO;
	}

	dev_info->sfp_node = of_parse_phandle(phydev->dev.of_node,
					      "sfp-eeprom", 0);
	if (!dev_info->sfp_node) {
		dev_err(&phydev->dev, "%s has invalid sfp-eeprom node\n",
			phydev->dev.of_node->full_name);
		return -ENXIO;
	}

	dev_info->macc = of_memory_accessor_get(dev_info->sfp_node);

	ret = phy_read(phydev, GBL_DEVICE_ID);
	if (ret < 0) {
		dev_err(&phydev->dev, "%s error reading PHY\n",
			phydev->dev.of_node->full_name);
		return ret;
	}

	/* Check how many devices are in the package to figure out the channel
	 * number.
	 */
	switch (ret) {
	case 0x8487:	/* Single */
	case 0x8486:
		dev_info->channel = 0;
		break;
	case 0x8488:	/* Dual */
		dev_info->channel = phydev->addr & 1;
		break;
	case 0x8484:	/* Quad */
		dev_info->channel = phydev->addr & 3;
		break;
	default:
		dev_err(&phydev->dev, "%s Unknown Vitesse PHY model %04x\n",
			phydev->dev.of_node->full_name, ret);
		return -EINVAL;
	}

	return 0;
}

static void vsc848x_remove(struct phy_device *phydev)
{
	struct vsc848x_phy_info *dev_info = phydev->priv;

	dev_info(&phydev->dev, "%s Exiting\n", phydev->dev.of_node->full_name);

	of_memory_accessor_put(dev_info->sfp_node);

	kfree(dev_info);
}

static int vsc848x_config_init(struct phy_device *phydev)
{
	phydev->supported = SUPPORTED_10000baseR_FEC;
	phydev->advertising = ADVERTISED_10000baseR_FEC;
	phydev->state = PHY_NOLINK;

	return 0;
}

static int vsc848x_config_aneg(struct phy_device *phydev)
{
	return -EINVAL;
}

static int vsc848x_write_global_var(struct phy_device *phydev, uint8_t channel,
				    uint8_t addr, uint16_t value)
{
	struct vsc848x_phy_info *dev_info = phydev->priv;
	int timeout = 1000;
	int ret = 0;

	mutex_lock(&(dev_info->nexus->lock));

	/* Wait for firmware download to complete */
	timeout = 100000;
	do {
		ret = phy_read(phydev, MII_ADDR_C45 | 0x1e7fe0);
		if (ret < 0)
			goto error;
		if (ret == 3)
			break;
		udelay(100);
	} while (timeout-- > 0);
	if (timeout <= 0) {
		dev_err(&phydev->dev, "%s Timeout waiting for PHY firmware to load\n",
			phydev->dev.of_node->full_name);
		ret = -EIO;
		goto error;
	}

	do {
		ret = phy_read(phydev, (MII_ADDR_C45 | 0x1e7fe3));
		if (ret < 0)
			return ret;
		if (ret == 0)
			break;
		mdelay(1);
	} while (timeout-- > 0);
	if (timeout <= 0) {
		dev_err(&phydev->dev, "%s timed out waiting to write global\n",
			phydev->dev.of_node->full_name);
		ret = -EIO;
		goto error;
	}
	ret = phy_write(phydev, (MII_ADDR_C45 | 0x1e7fe4), value);
	if (ret < 0)
		goto error;

	ret = phy_write(phydev, (MII_ADDR_C45 | 0x1e7fe3),
			0x8000 | ((channel & 3) << 8) | addr);
	if (ret < 0)
		goto error;

	/* Wait for value to be written */
	do {
		ret = phy_read(phydev, (MII_ADDR_C45 | 0x1e7fe3));
		if (ret < 0)
			return ret;
		if (ret == 0)
			break;
		mdelay(1);
	} while (timeout-- > 0);
	if (timeout <= 0) {
		dev_err(&phydev->dev, "%s timed out waiting to write global\n",
			phydev->dev.of_node->full_name);
		ret = -EIO;
		goto error;
	}
	ret = 0;

error:
	mutex_unlock(&(dev_info->nexus->lock));

	return ret;
}

/**
 * Dumps out the contents of the SFP EEPROM when errors are detected
 *
 * @param eeprom - contents of SFP+ EEPROM
 */
static void dump_sfp_eeprom(const uint8_t eeprom[64])
{
	int addr = 0;
	int i;
	char line[17];
	line[16] = '\0';

	pr_info("SFP+ EEPROM contents:\n");
	while (addr < 64) {
		pr_info("  %02x:  ", addr);
		for (i = 0; i < 16; i++)
			pr_cont("%02x ", eeprom[addr + i]);
		for (i = 0; i < 16; i++) {
			if (!isprint(eeprom[addr + i]) ||
			    eeprom[addr + i] >= 0x80)
				line[i] = '.';
			else
				line[i] = eeprom[addr + i];
		}
		pr_cont("    %s\n", line);
		addr += 16;
	}
	pr_info("\n");
}

/**
 * Read the SFP+ module EEPROM and program the Vitesse PHY accordingly.
 *
 * @param phydev - Phy device
 *
 * @returns 0 for success, error otherwise.
 */
static int vsc848x_read_sfp(struct phy_device *phydev)
{
	struct vsc848x_phy_info *dev_info = phydev->priv;
	uint8_t sfp_buffer[64];
	ssize_t size;
	uint8_t csum;
	uint8_t mode = VITESSE_FUNC_MODE_LIMITING;
	const char *mode_str = "Unknown";
	int i;
	int ret = 0;

	/* For details on the SFP+ EEPROM contents see the SFF-8472
	 * Diagnostic Monitoring Interface for Optical Transceivers.
	 *
	 * This is based on revision 11.1, October 26, 2012.
	 */
	if (!dev_info->macc) {
		dev_info->macc = of_memory_accessor_get(dev_info->sfp_node);
		if (!dev_info->macc) {
			dev_info(&phydev->dev,
				 "Could not connect to sfp memory accessor %s\n",
				 dev_info->sfp_node->full_name);
			return -ENODEV;
		}
	}
	size = dev_info->macc->read(dev_info->macc,
				    (char *)sfp_buffer, 0, sizeof(sfp_buffer));
	if (size != sizeof(sfp_buffer)) {
		dev_err(&phydev->dev, "%s cannot read SFP module EEPROM\n",
			phydev->dev.of_node->full_name);
		return -ENODEV;
	}

	/* Validate SFP checksum */
	csum = 0;
	for (i = 0; i < 63; i++)
		csum += sfp_buffer[i];
	if (csum != sfp_buffer[63]) {
		dev_err(&phydev->dev, "%s SFP EEPROM checksum bad, calculated 0x%02x, should be 0x%02x\n",
			phydev->dev.of_node->full_name, csum, sfp_buffer[63]);
		dump_sfp_eeprom(sfp_buffer);
		return -ENXIO;
	}

	/* Make sure it's a SFP or SFP+ module */
	if (sfp_buffer[0] != 3) {
		dev_err(&phydev->dev, "%s module is not SFP or SFP+\n",
			phydev->dev.of_node->full_name);
		dump_sfp_eeprom(sfp_buffer);
		return -ENXIO;
	}

	/* Check connector type */
	switch (sfp_buffer[2]) {
	case 0x01:	/* SC */
		mode = VITESSE_FUNC_MODE_LIMITING;
		break;
	case 0x07:	/* LC */
		mode = VITESSE_FUNC_MODE_LIMITING;
		break;
	case 0x0B:	/* Optical pigtail */
		mode = VITESSE_FUNC_MODE_LIMITING;
		break;
	case 0x21:	/* Copper pigtail */
	case 0x22:	/* RJ45 */
		mode = VITESSE_FUNC_MODE_COPPER;
		break;
	default:
		dev_err(&phydev->dev, "%s Unknown Connector Type 0x%x\n",
			phydev->dev.of_node->full_name, sfp_buffer[2]);
		dump_sfp_eeprom(sfp_buffer);
		return -EINVAL;
	}

	if (mode == VITESSE_FUNC_MODE_LIMITING) {
		if (mode_str[3] & 0x10)
			mode_str = "10GBase-SR";
		else if (mode_str[3] & 0x20)
			mode_str = "10GBase-LR";
		else if (mode_str[3] & 0x40)
			mode_str = "10GBase-LRM";
		else if (mode_str[3] & 0x80)
			mode_str = "10GBase-ER";
		else
			dev_err(&phydev->dev, "%s unknown SFP compatibility\n"
				"type ID: 0x%02x, extended ID: 0x%02x, Connector type code: 0x%02x\n"
				"Transceiver compatibility code: (%02x) %02x %02x %02x %02x %02x %02x %02x %02x\n",
				phydev->dev.of_node->full_name, sfp_buffer[0],
				sfp_buffer[1], sfp_buffer[2], sfp_buffer[36],
				sfp_buffer[3], sfp_buffer[4], sfp_buffer[5],
				sfp_buffer[6], sfp_buffer[7], sfp_buffer[8],
				sfp_buffer[9], sfp_buffer[10]);
	} else if (mode == VITESSE_FUNC_MODE_COPPER) {
		if (sfp_buffer[8] & 0x4) {
			mode_str = "10G Passive Copper";
		} else if (sfp_buffer[8] & 0x8) {
			mode_str = "10G Active Copper";
			mode = VITESSE_FUNC_MODE_LIMITING;
		} else {
			dev_err(&phydev->dev, "%s Unknown SFP+ copper cable capability 0x%02x\n"
				"Transceiver compatibility code: (%02x) %02x %02x %02x %02x %02x %02x %02x %02x\n",
				phydev->dev.of_node->full_name, sfp_buffer[8],
				sfp_buffer[36], sfp_buffer[3], sfp_buffer[4],
				sfp_buffer[5], sfp_buffer[6], sfp_buffer[7],
				sfp_buffer[8], sfp_buffer[9], sfp_buffer[10]);
			return -EINVAL;
		}
	} else {
		dev_err(&phydev->dev, "%s Unsupported phy mode %d\n",
			phydev->dev.of_node->full_name, mode);
		dump_sfp_eeprom(sfp_buffer);
	}

	vsc848x_write_global_var(phydev, dev_info->channel, 0x94, mode);

	/* Adjust PMA_TXOUTCTRL2 based on cable length.  Vitesse recommends
	 * 0x1606 for copper cable lengths 5M and longer.
	 *
	 * The default value is 0x1300.
	 */
	if (mode == VITESSE_FUNC_MODE_COPPER) {
		if (sfp_buffer[18] >= 5)
			ret = phy_write(phydev, PMA_TXOUTCTRL2, 0x1606);
		else
			ret = phy_write(phydev, PMA_TXOUTCTRL2, 0x1300);
		if (ret)
			return ret;
	}

	/* Reset the state machine */
	ret = phy_write(phydev, MII_ADDR_C45 | 0x18034, 0x11);

	dev_info(&phydev->dev, "%s configured for %s\n",
		phydev->dev.of_node->full_name, mode_str);

	return ret;
}

static int vsc848x_read_status(struct phy_device *phydev)
{
	struct vsc848x_phy_info *dev_info = phydev->priv;
	int rx_signal_detect;
	int pcs_status;
	int xgxs_lane_status;
	int value;
	int sfp_conn;
	int ret;

	/* Check if a module is plugged in */
	value = phy_read(phydev, vcs848x_gpio_to_reg[dev_info->mod_abs_gpio]
							.config1_status_reg);
	if (value < 0)
		return value;

	sfp_conn = !(value & 0x400);

	if (sfp_conn != dev_info->sfp_conn) {
		/* We detect a module being plugged in */
		if (sfp_conn) {
			ret = vsc848x_read_sfp(phydev);
			if (ret < 0)
				goto no_link;
			dev_info->sfp_conn = sfp_conn;
		} else {
			dev_info(&phydev->dev, "%s module unplugged\n",
				 phydev->dev.of_node->full_name);
			dev_info->sfp_conn = sfp_conn;
			goto no_link;
		}
	}

	rx_signal_detect = phy_read(phydev, PMD_RX_SIGNAL_DETECT);
	if (rx_signal_detect < 0)
		return rx_signal_detect;

	if ((rx_signal_detect & 1) == 0)
		goto no_link;

	pcs_status = phy_read(phydev, BASER_PCS_STATUS);
	if (pcs_status < 0)
		return pcs_status;

	if ((pcs_status & 1) == 0)
		goto no_link;

	xgxs_lane_status = phy_read(phydev, XGXS_LANE_STATUS);
	if (xgxs_lane_status < 0)
		return xgxs_lane_status;

	if ((xgxs_lane_status & 0x1000) == 0)
		goto no_link;

	phydev->speed = 10000;
	phydev->link = 1;
	phydev->duplex = 1;
	return 0;
no_link:
	phydev->link = 0;
	return 0;
}

static struct of_device_id vsc848x_match[] = {
	{
		.compatible = "vitesse,vsc8488",
	},
	{
		.compatible = "vitesse,vsc8486",
	},
	{
		.compatible = "vitesse,vsc8484",
	},
	{},
};
MODULE_DEVICE_TABLE(of, vsc848x_match);

static struct phy_driver vsc848x_phy_driver = {
	.phy_id		= 0x00070400,
	.phy_id_mask	= 0xfffffff0,
	.name		= "Vitesse VSC848X",
	.config_init	= vsc848x_config_init,
	.probe		= vsc848x_probe,
	.remove		= vsc848x_remove,
	.config_aneg	= vsc848x_config_aneg,
	.read_status	= vsc848x_read_status,
	.driver		= {
		.owner = THIS_MODULE,
		.of_match_table = vsc848x_match,
	},
};

/* Phy nexus support below. */

static int vsc848x_nexus_read(struct mii_bus *bus, int phy_id, int regnum)
{
	struct vsc848x_nexus_mdiobus *p = bus->priv;
	return p->parent_mii_bus->read(p->parent_mii_bus,
				       phy_id + p->reg_offset,
				       regnum);
}

static int vsc848x_nexus_write(struct mii_bus *bus, int phy_id,
			       int regnum, u16 val)
{
	struct vsc848x_nexus_mdiobus *p = bus->priv;
	return p->parent_mii_bus->write(p->parent_mii_bus,
					phy_id + p->reg_offset,
					regnum, val);
}

static int vsc848x_nexus_probe(struct platform_device *pdev)
{
	struct vsc848x_nexus_mdiobus *bus;
	const char *bus_id;
	int len;
	int err = 0;

	bus = devm_kzalloc(&pdev->dev, sizeof(*bus), GFP_KERNEL);
	if (!bus)
		return -ENOMEM;

	bus->parent_mii_bus = container_of(pdev->dev.parent,
					   struct mii_bus, dev);

	/* The PHY nexux  must have a reg property in the range [0-31] */
	err = of_property_read_u32(pdev->dev.of_node, "reg", &bus->reg_offset);
	if (err) {
		dev_err(&pdev->dev, "%s has invalid PHY address\n",
			pdev->dev.of_node->full_name);
		return err;
	}

	bus->mii_bus = mdiobus_alloc();
	if (!bus->mii_bus)
		return -ENOMEM;

	bus->mii_bus->priv = bus;
	bus->mii_bus->irq = bus->phy_irq;
	bus->mii_bus->name = "vsc848x_nexus";
	bus_id = bus->parent_mii_bus->id;
	len = strlen(bus_id);
	if (len > MII_BUS_ID_SIZE - 4)
		bus_id += len - (MII_BUS_ID_SIZE - 4);
	snprintf(bus->mii_bus->id, MII_BUS_ID_SIZE, "%s:%02x",
		 bus_id, bus->reg_offset);
	bus->mii_bus->parent = &pdev->dev;

	bus->mii_bus->read = vsc848x_nexus_read;
	bus->mii_bus->write = vsc848x_nexus_write;
	mutex_init(&bus->lock);

	dev_set_drvdata(&pdev->dev, bus);

	err = of_mdiobus_register(bus->mii_bus, pdev->dev.of_node);
	if (err) {
		dev_err(&pdev->dev, "Error registering with device tree\n");
		goto fail_register;
	}

	return 0;

fail_register:
	dev_err(&pdev->dev, "Failed to register\n");
	mdiobus_free(bus->mii_bus);
	return err;
}

static int vsc848x_nexus_remove(struct platform_device *pdev)
{
	return 0;
}

static struct of_device_id vsc848x_nexus_match[] = {
	{
		.compatible = "vitesse,vsc8488-nexus",
	},
	{
		.compatible = "vitesse,vsc8486-nexus",
	},
	{
		.compatible = "vitesse,vsc8484-nexus",
	},
	{},
};
MODULE_DEVICE_TABLE(of, vsc848x_nexus_match);

static struct platform_driver mdio_mux_gpio_driver = {
	.driver = {
		.name		= "vsc848x-nexus",
		.owner		= THIS_MODULE,
		.of_match_table = vsc848x_nexus_match,
	},
	.probe		= vsc848x_nexus_probe,
	.remove		= vsc848x_nexus_remove,
};

static int __init vsc848x_mod_init(void)
{
	int rv;

	rv = platform_driver_register(&mdio_mux_gpio_driver);
	if (rv)
		return rv;

	rv = phy_driver_register(&vsc848x_phy_driver);

	return rv;
}
module_init(vsc848x_mod_init);

static void __exit vsc848x_mod_exit(void)
{
	phy_driver_unregister(&vsc848x_phy_driver);
	platform_driver_unregister(&mdio_mux_gpio_driver);
}
module_exit(vsc848x_mod_exit);

MODULE_DESCRIPTION("Driver for Vitesse VSC848X PHY");
MODULE_AUTHOR("David Daney and Aaron Williams");
MODULE_LICENSE("GPL");
