// SPDX-License-Identifier: GPL-2.0+ or ISC
/*
 * Copyright (c) 2018 Mark Kettenis <kettenis@openbsd.org>
 * Copyright (c) 2019 Patrick Wildt <patrick@blueri.se>
 */

#include <common.h>
#include <dm.h>
#include <pci.h>
#include <generic-phy.h>
#include <power-domain.h>
#include <regmap.h>
#include <reset.h>
#include <syscon.h>
#include <asm/io.h>
#include <asm-generic/gpio.h>
#include <asm/arch-rockchip/clock.h>

DECLARE_GLOBAL_DATA_PTR;

#define PCIE_CLIENT_BASIC_STRAP_CONF	0x0000
#define  PCIE_CLIENT_PCIE_GEN_SEL_1	(((1 << 7) << 16) | (0 << 7))
#define  PCIE_CLIENT_PCIE_GEN_SEL_2	(((1 << 7) << 16) | (1 << 7))
#define  PCIE_CLIENT_MODE_SELECT_RC	(((1 << 6) << 16) | (1 << 6))
#define  PCIE_CLIENT_LINK_TRAIN_EN	(((1 << 1) << 16) | (1 << 1))
#define  PCIE_CLIENT_CONF_EN		(((1 << 0) << 16) | (1 << 0))
#define PCIE_CLIENT_BASIC_STATUS1	0x0048
#define  PCIE_CLIENT_LINK_ST		(0x3 << 20)
#define  PCIE_CLIENT_LINK_ST_UP		(0x3 << 20)
#define PCIE_CLIENT_INT_MASK		0x004c
#define  PCIE_CLIENT_INTD_MASK		(((1 << 8) << 16) | (1 << 8))
#define  PCIE_CLIENT_INTD_UNMASK	(((1 << 8) << 16) | (0 << 8))
#define  PCIE_CLIENT_INTC_MASK		(((1 << 7) << 16) | (1 << 7))
#define  PCIE_CLIENT_INTC_UNMASK	(((1 << 7) << 16) | (0 << 7))
#define  PCIE_CLIENT_INTB_MASK		(((1 << 6) << 16) | (1 << 6))
#define  PCIE_CLIENT_INTB_UNMASK	(((1 << 6) << 16) | (0 << 6))
#define  PCIE_CLIENT_INTA_MASK		(((1 << 5) << 16) | (1 << 5))
#define  PCIE_CLIENT_INTA_UNMASK	(((1 << 5) << 16) | (0 << 5))

#define PCIE_RC_NORMAL_BASE		0x800000

#define PCIE_LM_BASE			0x900000
#define PCIE_LM_VENDOR_ID		(PCIE_LM_BASE + 0x44)
#define  PCIE_LM_VENDOR_ROCKCHIP	0x1d87
#define PCIE_LM_RCBAR			(PCIE_LM_BASE + 0x300)
#define  PCIE_LM_RCBARPIE		(1 << 19)
#define  PCIE_LM_RCBARPIS		(1 << 20)

#define PCIE_RC_BASE			0xa00000
#define PCIE_RC_PCIE_LCAP		(PCIE_RC_BASE + 0x0cc)
#define  PCIE_RC_PCIE_LCAP_APMS_L0S	(1 << 10)

#define PCIE_ATR_BASE			0xc00000
#define PCIE_ATR_OB_ADDR0(i)		(PCIE_ATR_BASE + 0x000 + (i) * 0x20)
#define PCIE_ATR_OB_ADDR1(i)		(PCIE_ATR_BASE + 0x004 + (i) * 0x20)
#define PCIE_ATR_OB_DESC0(i)		(PCIE_ATR_BASE + 0x008 + (i) * 0x20)
#define PCIE_ATR_OB_DESC1(i)		(PCIE_ATR_BASE + 0x00c + (i) * 0x20)
#define PCIE_ATR_IB_ADDR0(i)		(PCIE_ATR_BASE + 0x800 + (i) * 0x8)
#define PCIE_ATR_IB_ADDR1(i)		(PCIE_ATR_BASE + 0x804 + (i) * 0x8)
#define  PCIE_ATR_HDR_MEM		0x2
#define  PCIE_ATR_HDR_IO		0x6
#define  PCIE_ATR_HDR_CFG_TYPE0		0xa
#define  PCIE_ATR_HDR_CFG_TYPE1		0xb
#define  PCIE_ATR_HDR_RID		(1 << 23)

#define PCIE_ATR_OB_REGION0_SIZE	(32 * 1024 * 1024)
#define PCIE_ATR_OB_REGION_SIZE		(1 * 1024 * 1024)

#define RK3399_GRF_SOC_CON5_PCIE	0xe214
#define  RK3399_TX_ELEC_IDLE_OFF_MASK	((1 << 3) << 16)
#define  RK3399_TX_ELEC_IDLE_OFF	(1 << 3)
#define RK3399_GRF_SOC_CON8		0xe220
#define  RK3399_PCIE_TEST_DATA_MASK	((0xf << 7) << 16)
#define  RK3399_PCIE_TEST_DATA_SHIFT	7
#define  RK3399_PCIE_TEST_ADDR_MASK	((0x3f << 1) << 16)
#define  RK3399_PCIE_TEST_ADDR_SHIFT	1
#define  RK3399_PCIE_TEST_WRITE_ENABLE	(((1 << 0) << 16) | (1 << 0))
#define  RK3399_PCIE_TEST_WRITE_DISABLE	(((1 << 0) << 16) | (0 << 0))
#define RK3399_GRF_SOC_STATUS1		0xe2a4
#define  RK3399_PCIE_PHY_PLL_LOCKED	(1 << 9)
#define  RK3399_PCIE_PHY_PLL_OUTPUT	(1 << 10)

#define RK3399_PCIE_PHY_CFG_PLL_LOCK	0x10
#define RK3399_PCIE_PHY_CFG_CLK_TEST	0x10
#define  RK3399_PCIE_PHY_CFG_SEPE_RATE	(1 << 3)
#define RK3399_PCIE_PHY_CFG_CLK_SCC	0x12
#define  RK3399_PCIE_PHY_CFG_PLL_100M	(1 << 3)

/**
 * struct pcie_rockchip - Rockchip PCIe controller state
 *
 * @api_base: The base address of apb register space
 * @axi_base: The base address of axi register space
 * @first_busno: This driver supports multiple PCIe controllers.
 *               first_busno stores the bus number of the PCIe root-port
 *               number which may vary depending on the PCIe setup
 *               (PEX switches etc).
 */
struct pcie_rockchip {
	void *apb_base;
	void *axi_base;
	fdt_size_t apb_size;
	fdt_size_t axi_size;
	int first_busno;
	struct udevice *dev;

	/* Resets */
	struct reset_ctl aclk_ctl;
	struct reset_ctl core_ctl;
	struct reset_ctl mgmt_ctl;
	struct reset_ctl mgmt_sticky_ctl;
	struct reset_ctl pclk_ctl;
	struct reset_ctl pipe_ctl;
	struct reset_ctl pm_ctl;
	struct reset_ctl phy_ctl;

	/* GPIO */
	struct gpio_desc ep_gpio;

	/* PHY */
	ofnode phy_node;
	uintptr_t phy_regs;
};

/**
 * pcie_rockchip_read_config() - Read from configuration space
 *
 * @bus: Pointer to the PCI bus
 * @bdf: Identifies the PCIe device to access
 * @offset: The offset into the device's configuration space
 * @valuep: A pointer at which to store the read value
 * @size: Indicates the size of access to perform
 *
 * Read a value of size @size from offset @offset within the configuration
 * space of the device identified by the bus, device & function numbers in @bdf
 * on the PCI bus @bus.
 *
 * Return: 0 on success
 */
static int pcie_rockchip_read_config(struct udevice *bus, pci_dev_t bdf,
				  uint offset, ulong *valuep,
				  enum pci_size_t size)
{
	struct pcie_rockchip *pcie = dev_get_priv(bus);
	ulong value;
	u32 off;

	off = (PCI_BUS(bdf) << 20) | (PCI_DEV(bdf) << 15) |
	    (PCI_FUNC(bdf) << 12) | (offset & ~0x3);

	if ((PCI_BUS(bdf) == pcie->first_busno) && (PCI_DEV(bdf) == 0)) {
		value = readl(pcie->apb_base + PCIE_RC_NORMAL_BASE + off);
		*valuep = pci_conv_32_to_size(value, offset, size);
		return 0;
	}
	if ((PCI_BUS(bdf) == pcie->first_busno + 1) && (PCI_DEV(bdf) == 0)) {
		value = readl(pcie->axi_base + off);
		*valuep = pci_conv_32_to_size(value, offset, size);
		return 0;
	}

	*valuep = pci_get_ff(size);
	return 0;
}

/**
 * pcie_rockchip_write_config() - Write to configuration space
 *
 * @bus: Pointer to the PCI bus
 * @bdf: Identifies the PCIe device to access
 * @offset: The offset into the device's configuration space
 * @value: The value to write
 * @size: Indicates the size of access to perform
 *
 * Write the value @value of size @size from offset @offset within the
 * configuration space of the device identified by the bus, device & function
 * numbers in @bdf on the PCI bus @bus.
 *
 * Return: 0 on success
 */
static int pcie_rockchip_write_config(struct udevice *bus, pci_dev_t bdf,
				   uint offset, ulong value,
				   enum pci_size_t size)
{
	struct pcie_rockchip *pcie = dev_get_priv(bus);
	ulong old;
	u32 off;

	off = (PCI_BUS(bdf) << 20) | (PCI_DEV(bdf) << 15) |
	    (PCI_FUNC(bdf) << 12) | (offset & ~0x3);

	if ((PCI_BUS(bdf) == pcie->first_busno) && (PCI_DEV(bdf) == 0)) {
		old = readl(pcie->apb_base + PCIE_RC_NORMAL_BASE + off);
		value = pci_conv_size_to_32(old, value, offset, size);
		writel(value, pcie->apb_base + PCIE_RC_NORMAL_BASE + off);
		return 0;
	}
	if ((PCI_BUS(bdf) == pcie->first_busno + 1) && (PCI_DEV(bdf) == 0)) {
		old = readl(pcie->axi_base + off);
		value = pci_conv_size_to_32(old, value, offset, size);
		writel(value, pcie->axi_base + off);
		return 0;
	}

	return 0;
}

static int pcie_rockchip_phy_init(struct pcie_rockchip *pci)
{
	if (reset_get_by_index_nodev(pci->phy_node, 0, &pci->phy_ctl))
		return -EINVAL;

	/* XXX clock enable refclk */
	reset_assert(&pci->phy_ctl);
	return 0;
}

static void pcie_rockchip_phy_write_conf(struct pcie_rockchip *pci,
					 uint8_t addr, uint8_t data)
{
	writel(RK3399_PCIE_TEST_ADDR_MASK |
	    (addr << RK3399_PCIE_TEST_ADDR_SHIFT) |
	    RK3399_PCIE_TEST_DATA_MASK |
	    (data << RK3399_PCIE_TEST_DATA_SHIFT) |
	    RK3399_PCIE_TEST_WRITE_DISABLE,
	    pci->phy_regs + RK3399_GRF_SOC_CON8);
	udelay(1);
	writel(RK3399_PCIE_TEST_WRITE_ENABLE,
	    pci->phy_regs + RK3399_GRF_SOC_CON8);
	udelay(1);
	writel(RK3399_PCIE_TEST_WRITE_DISABLE,
	    pci->phy_regs + RK3399_GRF_SOC_CON8);
}

static int pcie_rockchip_phy_poweron(struct pcie_rockchip *pci)
{
	int timo, lane = 0;
	u32 status;

	reset_deassert(&pci->phy_ctl);

	pci->phy_regs = (u64)syscon_get_first_range(ROCKCHIP_SYSCON_GRF);

	writel(RK3399_PCIE_TEST_ADDR_MASK |
	    RK3399_PCIE_PHY_CFG_PLL_LOCK << RK3399_PCIE_TEST_ADDR_SHIFT,
	    pci->phy_regs + RK3399_GRF_SOC_CON8);
	writel(RK3399_TX_ELEC_IDLE_OFF_MASK << lane | 0,
	    pci->phy_regs + RK3399_GRF_SOC_CON5_PCIE);

	for (timo = 50; timo > 0; timo--) {
		status = readl(pci->phy_regs + RK3399_GRF_SOC_STATUS1);
		if (status & RK3399_PCIE_PHY_PLL_LOCKED)
			break;
		udelay(20000);
	}
	if (timo == 0)
		return -ENXIO;

	pcie_rockchip_phy_write_conf(pci, RK3399_PCIE_PHY_CFG_CLK_TEST,
	    RK3399_PCIE_PHY_CFG_SEPE_RATE);
	pcie_rockchip_phy_write_conf(pci, RK3399_PCIE_PHY_CFG_CLK_SCC,
	    RK3399_PCIE_PHY_CFG_PLL_100M);

	for (timo = 50; timo > 0; timo--) {
		status = readl(pci->phy_regs + RK3399_GRF_SOC_STATUS1);
		if ((status & RK3399_PCIE_PHY_PLL_OUTPUT) == 0)
			break;
		udelay(20000);
	}
	if (timo == 0)
		return -ENXIO;

	writel(RK3399_PCIE_TEST_ADDR_MASK |
	    RK3399_PCIE_PHY_CFG_PLL_LOCK << RK3399_PCIE_TEST_ADDR_SHIFT,
	    pci->phy_regs + RK3399_GRF_SOC_CON8);

	for (timo = 50; timo > 0; timo--) {
		status = readl(pci->phy_regs + RK3399_GRF_SOC_STATUS1);
		if (status & RK3399_PCIE_PHY_PLL_LOCKED)
			break;
		udelay(20000);
	}
	if (timo == 0)
		return -ENXIO;
	return 0;
}

static int pcie_rockchip_atr_init(struct pcie_rockchip *pci)
{
	struct udevice *ctlr = pci_get_controller(pci->dev);
	struct pci_controller *hose = dev_get_uclass_priv(ctlr);
	u64 addr, size, offset;
	u32 type;
	int i, region;

	/* Use region 0 to map PCI configuration space. */
	writel(25 - 1, pci->apb_base + PCIE_ATR_OB_ADDR0(0));
	writel(0, pci->apb_base + PCIE_ATR_OB_ADDR1(0));
	writel(PCIE_ATR_HDR_CFG_TYPE0 | PCIE_ATR_HDR_RID,
	    pci->apb_base + PCIE_ATR_OB_DESC0(0));
	writel(0, pci->apb_base + PCIE_ATR_OB_DESC1(0));

	for (i = 0; i < hose->region_count; i++) {
		if (hose->regions[i].flags == PCI_REGION_SYS_MEMORY)
			continue;

		if (hose->regions[i].flags == PCI_REGION_IO)
			type = PCIE_ATR_HDR_IO;
		else
			type = PCIE_ATR_HDR_MEM;

		/* Only support identity mappings. */
		if (hose->regions[i].bus_start !=
		    hose->regions[i].phys_start)
			return -EINVAL;

		/* Only support mappings aligned on a region boundary. */
		addr = hose->regions[i].bus_start;
		if (addr & (PCIE_ATR_OB_REGION_SIZE - 1))
			return -EINVAL;

		/* Mappings should lie between AXI and APB regions. */
		size = hose->regions[i].size;
		if (addr < (u64)pci->axi_base + PCIE_ATR_OB_REGION0_SIZE)
			return -EINVAL;
		if (addr + size > (u64)pci->apb_base)
			return -EINVAL;

		offset = addr - (u64)pci->axi_base - PCIE_ATR_OB_REGION0_SIZE;
		region = 1 + (offset / PCIE_ATR_OB_REGION_SIZE);
		while (size > 0) {
			writel(32 - 1, pci->apb_base + PCIE_ATR_OB_ADDR0(region));
			writel(0, pci->apb_base + PCIE_ATR_OB_ADDR1(region));
			writel(type | PCIE_ATR_HDR_RID,
			    pci->apb_base + PCIE_ATR_OB_DESC0(region));
			writel(0, pci->apb_base + PCIE_ATR_OB_DESC1(region));

			addr += PCIE_ATR_OB_REGION_SIZE;
			size -= PCIE_ATR_OB_REGION_SIZE;
			region++;
		}
	}

	/* Passthrough inbound translations unmodified. */
	writel(32 - 1, pci->apb_base + PCIE_ATR_IB_ADDR0(2));
	writel(0, pci->apb_base + PCIE_ATR_IB_ADDR1(2));

	return 0;
}

/**
 * pcie_rockchip_probe() - Probe the PCIe bus for active link
 *
 * @dev: A pointer to the device being operated on
 *
 * Probe for an active link on the PCIe bus and configure the controller
 * to enable this port.
 *
 * Return: 0 on success, else -ENODEV
 */
static int pcie_rockchip_probe(struct udevice *dev)
{
	struct pcie_rockchip *pci = dev_get_priv(dev);
	struct udevice *ctlr = pci_get_controller(dev);
	struct pci_controller *hose = dev_get_uclass_priv(ctlr);
	int timo;
	u32 val;

	pci->first_busno = dev->seq;
	pci->dev = dev;

	gpio_request_by_name(dev, "ep-gpios", 0, &pci->ep_gpio,
			     GPIOD_IS_OUT);
	if (!dm_gpio_is_valid(&pci->ep_gpio)) {
		dev_err(dev, "failed to get EP gpio\n");
		return -ENODEV;
	}

	if (reset_get_by_name(dev, "aclk", &pci->aclk_ctl) ||
	    reset_get_by_name(dev, "core", &pci->core_ctl) ||
	    reset_get_by_name(dev, "mgmt", &pci->mgmt_ctl) ||
	    reset_get_by_name(dev, "mgmt-sticky", &pci->mgmt_sticky_ctl) ||
	    reset_get_by_name(dev, "pclk", &pci->pclk_ctl) ||
	    reset_get_by_name(dev, "pipe", &pci->pipe_ctl) ||
	    reset_get_by_name(dev, "pm", &pci->pm_ctl)) {
		dev_err(dev, "failed to get resets\n");
		return -ENODEV;
	}

	dm_gpio_set_value(&pci->ep_gpio, 0);

	reset_assert(&pci->aclk_ctl);
	reset_assert(&pci->pclk_ctl);
	reset_assert(&pci->pm_ctl);

	if (pcie_rockchip_phy_init(pci)) {
		printf("PCIE-%d: Link down\n", dev->seq);
		return -ENODEV;
	}

	reset_assert(&pci->core_ctl);
	reset_assert(&pci->mgmt_ctl);
	reset_assert(&pci->mgmt_sticky_ctl);
	reset_assert(&pci->pipe_ctl);

	udelay(10);

	reset_deassert(&pci->aclk_ctl);
	reset_deassert(&pci->pclk_ctl);
	reset_deassert(&pci->pm_ctl);

	/* Only advertise Gen 1 support for now. */
	writel(PCIE_CLIENT_PCIE_GEN_SEL_1,
	    pci->apb_base + PCIE_CLIENT_BASIC_STRAP_CONF);

	/* Switch into Root Complex mode. */
	writel(PCIE_CLIENT_MODE_SELECT_RC | PCIE_CLIENT_CONF_EN,
	    pci->apb_base + PCIE_CLIENT_BASIC_STRAP_CONF);

	if (pcie_rockchip_phy_poweron(pci)) {
		printf("PCIE-%d: Link down\n", dev->seq);
		return -ENODEV;
	}

	reset_deassert(&pci->core_ctl);
	reset_deassert(&pci->mgmt_ctl);
	reset_deassert(&pci->mgmt_sticky_ctl);
	reset_deassert(&pci->pipe_ctl);

	/* Start link training. */
	writel(PCIE_CLIENT_LINK_TRAIN_EN,
	    pci->apb_base + PCIE_CLIENT_BASIC_STRAP_CONF);

	/* XXX Advertise power limits? */

	dm_gpio_set_value(&pci->ep_gpio, 1);

	for (timo = 500; timo > 0; timo--) {
		val = readl(pci->apb_base + PCIE_CLIENT_BASIC_STATUS1);
		if ((val & PCIE_CLIENT_LINK_ST) == PCIE_CLIENT_LINK_ST)
			break;
		udelay(1000);
	}
	if (timo == 0) {
		printf("PCIE-%d: Link down\n", dev->seq);
		return -ENODEV;
	}

	/* Initialize Root Complex registers. */
	writel(PCIE_LM_VENDOR_ROCKCHIP, pci->apb_base + PCIE_LM_VENDOR_ID);
	writel(PCI_CLASS_BRIDGE_PCI << 16, pci->apb_base +
	    PCIE_RC_BASE + PCI_CLASS_REVISION);
	writel(PCIE_LM_RCBARPIE | PCIE_LM_RCBARPIS,
	    pci->apb_base + PCIE_LM_RCBAR);

	if (dev_read_bool(dev, "aspm-no-l0s")) {
		val = readl(pci->apb_base + PCIE_RC_PCIE_LCAP);
		val &= ~PCIE_RC_PCIE_LCAP_APMS_L0S;
		writel(val, pci->apb_base + PCIE_RC_PCIE_LCAP);
	}

	/* Configure Address Translation. */
	if (pcie_rockchip_atr_init(pci)) {
		printf("PCIE-%d: ATR init failed\n", dev->seq);
		return -ENODEV;
	}

	printf("PCIE-%d: Link up (Bus%d)\n", dev->seq, hose->first_busno);

	return 0;
}

/**
 * pcie_rockchip_ofdata_to_platdata() - Translate from DT to device state
 *
 * @dev: A pointer to the device being operated on
 *
 * Translate relevant data from the device tree pertaining to device @dev into
 * state that the driver will later make use of. This state is stored in the
 * device's private data structure.
 *
 * Return: 0 on success, else -EINVAL
 */
static int pcie_rockchip_ofdata_to_platdata(struct udevice *dev)
{
	struct pcie_rockchip *pcie = dev_get_priv(dev);
	u32 phandle;

	/* Get AXI base address and size */
	pcie->axi_base = (void *)devfdt_get_addr_size_index(dev, 0,
							 &pcie->axi_size);
	if ((fdt_addr_t)pcie->axi_base == FDT_ADDR_T_NONE)
		return -EINVAL;

	/* Get APB base address and size */
	pcie->apb_base = (void *)devfdt_get_addr_size_index(dev, 1,
							 &pcie->apb_size);
	if ((fdt_addr_t)pcie->apb_base == FDT_ADDR_T_NONE)
		return -EINVAL;

	if (ofnode_read_u32(dev_ofnode(dev), "phys", &phandle))
		return -EINVAL;

	pcie->phy_node = ofnode_get_by_phandle(phandle);
	if (!ofnode_valid(pcie->phy_node))
		return -EINVAL;

	return 0;
}

static const struct dm_pci_ops pcie_rockchip_ops = {
	.read_config	= pcie_rockchip_read_config,
	.write_config	= pcie_rockchip_write_config,
};

static const struct udevice_id pcie_rockchip_ids[] = {
	{ .compatible = "rockchip,rk3399-pcie" },
	{ }
};

U_BOOT_DRIVER(pcie_rockchip) = {
	.name			= "pcie_rockchip",
	.id			= UCLASS_PCI,
	.of_match		= pcie_rockchip_ids,
	.ops			= &pcie_rockchip_ops,
	.ofdata_to_platdata	= pcie_rockchip_ofdata_to_platdata,
	.probe			= pcie_rockchip_probe,
	.priv_auto_alloc_size	= sizeof(struct pcie_rockchip),
};
