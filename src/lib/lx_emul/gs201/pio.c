/*
 * \brief  GS201 GPIO banks for Linux drivers, backed by gs201_pio sessions
 * \author Ivan Loskutov
 * \date   2026-09-30
 *
 * The GPIO controllers are owned by the pin driver gs201_pio. This file
 * stands in for Linux' pinctrl-samsung driver: it binds
 * to the "google,gs201-pinctrl" nodes of the device tree and registers one
 * gpio_chip per bank node (gpa7, gpp0, ...) so that DT references like
 * 'reset-gpios = <&gpa7 1 0>' resolve. Pin accesses are forwarded to the
 * Pin_control / Pin_state sessions labeled "<bank>-<index>" (e.g. "gpa7-1"),
 * which gs201_pio's policies assign to its configured pins. Pin direction,
 * function and pull are gs201_pio configuration, not changed from here.
 *
 * The pinctrl consumer API is emulated with handles that carry no state:
 * pin-mux states (e.g. the DSIM's hw_te_on) are static gs201_pio config.
 *
 * Pin interrupts (gpio_to_irq) exist for the pins of wakeup banks (ALIVE,
 * FAR_ALIVE), which have one GIC interrupt per pin (the bank node's
 * 'interrupts'). The interrupt itself is owned by gs201_pio: unmasking a pin
 * IRQ opens gs201_pio's IRQ session labeled with the pin's GIC INTID (e.g.
 * "84" for gpa7-0). Its signal raises the same INTID in lx_emul's GIC
 * domain, where a flow handler (never unmasked here) forwards it to the
 * bank's own irq domain.
 *
 * A pin interrupt is reached in two ways: through gpio_to_irq or through a
 * device-tree 'interrupts' property that names the bank as interrupt
 * parent; the domain's
 * 'map' operation installs chip and flow handler for both. A consumer that
 * requests a level trigger (IRQF_TRIGGER_HIGH/LOW) gets the level flow: the
 * pin interrupt is acknowledged at gs201_pio only when Linux unmasks it
 * again, i.e. after the (threaded) handler has cleared the source. Edge
 * consumers keep the edge flow with the acknowledgement up front.
 */

/*
 * SPDX-License-Identifier: MIT
 */

#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/gpio/driver.h>
#include <linux/irq.h>
#include <linux/irqdomain.h>
#include <linux/of_irq.h>
#include <linux/pinctrl/consumer.h>

#include <lx_emul/pin.h>

enum { PINS_PER_BANK = 8 };

struct gs201_bank
{
	struct gpio_chip   chip;
	char               name[8];   /* bank node name, e.g. "gpa7" */
	struct device_node *np;
	struct irq_domain  *domain;   /* pin interrupts, wakeup banks only */
	unsigned           gic_irq[PINS_PER_BANK]; /* GIC INTID per pin, 0 = none */
	bool               level[PINS_PER_BANK];       /* level-triggered consumer */
	bool               ack_pending[PINS_PER_BANK]; /* level: ack at unmask */
	bool               driven[PINS_PER_BANK];      /* output set by the driver */
	bool               value[PINS_PER_BANK];       /* level it was set to */
};


static void gs201_pin_name(struct gpio_chip *chip, unsigned offset,
                           char *buf, size_t len)
{
	struct gs201_bank *bank = gpiochip_get_data(chip);

	snprintf(buf, len, "%s-%u", bank->name, offset);
}


static void gs201_gpio_set(struct gpio_chip *chip, unsigned offset, int value)
{
	struct gs201_bank *bank = gpiochip_get_data(chip);
	char name[16];
	gs201_pin_name(chip, offset, name, sizeof(name));

	lx_emul_pin_control(name, value);

	bank->driven[offset] = true;
	bank->value[offset]  = !!value;
}


/*
 * An output that the driver drives reads back the level it was set to:
 * sensing it would yield its Pin_control session and leave the pin
 * floating. The WiFi driver reads its reset lines right after setting them.
 */
static int gs201_gpio_get(struct gpio_chip *chip, unsigned offset)
{
	struct gs201_bank *bank = gpiochip_get_data(chip);
	char name[16];

	if (bank->driven[offset])
		return bank->value[offset];

	gs201_pin_name(chip, offset, name, sizeof(name));

	return lx_emul_pin_sense(name);
}


static int gs201_gpio_direction_output(struct gpio_chip *chip,
                                       unsigned offset, int value)
{
	gs201_gpio_set(chip, offset, value);
	return 0;
}


static int gs201_gpio_direction_input(struct gpio_chip *chip, unsigned offset)
{
	struct gs201_bank *bank = gpiochip_get_data(chip);

	/* the next read senses the pin */
	bank->driven[offset] = false;
	return 0;
}


/*
 * Pin interrupts
 */

static void gs201_eint_unmask(struct irq_data *d)
{
	struct gs201_bank *bank = irq_data_get_irq_chip_data(d);
	unsigned const gic = bank->gic_irq[d->hwirq];
	char name[16];

	gs201_pin_name(&bank->chip, d->hwirq, name, sizeof(name));

	/*
	 * lx_emul_pin_irq_unmask expects the GIC interrupt as SPI number and
	 * adds 32 itself, while 'gic' is the INTID (hwirq of lx_emul's GIC
	 * domain = SPI + 32). The pin IRQ session keeps the INTID as label
	 * (gs201_pio policy "<client> -> <INTID>").
	 */
	lx_emul_pin_irq_unmask(gic - 32, gic, name);

	/*
	 * Level flow: the interrupt was left unacknowledged (and thereby masked
	 * at gs201_pio) while it was handled. Re-arm it now that the source is
	 * serviced; a level that is still asserted triggers again.
	 */
	if (bank->level[d->hwirq] && bank->ack_pending[d->hwirq]) {
		bank->ack_pending[d->hwirq] = false;
		lx_emul_pin_irq_ack(gic);
	}
}


static void gs201_eint_ack(struct irq_data *d)
{
	struct gs201_bank *bank = irq_data_get_irq_chip_data(d);

	/* level flow: acknowledged in gs201_eint_unmask */
	if (bank->level[d->hwirq]) {
		bank->ack_pending[d->hwirq] = true;
		return;
	}

	lx_emul_pin_irq_ack(bank->gic_irq[d->hwirq]);
}


static void gs201_eint_mask(struct irq_data *d) { }


/*
 * The trigger itself is gs201_pio configuration (irq="falling", "high",
 * ...). A level type selects the level flow for the pin; edge types keep
 * the edge flow installed at mapping time.
 */
static int gs201_eint_set_type(struct irq_data *d, unsigned int type)
{
	struct gs201_bank *bank = irq_data_get_irq_chip_data(d);

	if (!bank || d->hwirq >= PINS_PER_BANK)
		return 0;

	if (type & IRQ_TYPE_LEVEL_MASK) {
		bank->level[d->hwirq] = true;
		irq_set_handler_locked(d, handle_level_irq);

	} else if (bank->level[d->hwirq]) {

		/*
		 * A pin that had a level consumer is requested as edge: back to
		 * the edge flow, and a deferred acknowledgement is issued now (the
		 * edge flow never unmasks)
		 */
		bank->level[d->hwirq] = false;
		irq_set_handler_locked(d, handle_edge_irq);

		if (bank->ack_pending[d->hwirq]) {
			bank->ack_pending[d->hwirq] = false;
			lx_emul_pin_irq_ack(bank->gic_irq[d->hwirq]);
		}
	}
	return 0;
}


static struct irq_chip gs201_eint_chip = {
	.name         = "gs201_eint",
	.irq_unmask   = gs201_eint_unmask,
	.irq_mask     = gs201_eint_mask,
	.irq_ack      = gs201_eint_ack,
	.irq_set_type = gs201_eint_set_type,
};


/* flow handler of the pin's INTID in lx_emul's GIC domain */
static void gs201_gic_irq_handler(struct irq_desc *desc)
{
	struct gs201_bank *bank = irq_desc_get_handler_data(desc);
	unsigned const gic = lx_emul_pin_last_irq();
	unsigned pin;

	for (pin = 0; pin < PINS_PER_BANK; pin++)
		if (bank->gic_irq[pin] == gic)
			generic_handle_domain_irq(bank->domain, pin);
}


static int gs201_gpio_to_irq(struct gpio_chip *chip, unsigned offset)
{
	struct gs201_bank *bank = gpiochip_get_data(chip);
	int irq;

	if (!bank->domain || offset >= PINS_PER_BANK || !bank->gic_irq[offset])
		return -ENXIO;

	irq = irq_create_mapping(bank->domain, offset);
	if (irq <= 0)
		return -ENXIO;

	/* a pin in level flow (set by a level-triggered request) keeps it */
	if (!bank->level[offset]) {
		irq_set_chip_and_handler(irq, &gs201_eint_chip, handle_edge_irq);
		irq_set_chip_data(irq, bank);
	}
	return irq;
}


/*
 * Mapping of a pin interrupt, for gpio_to_irq as well as for interrupts
 * taken from a device-tree 'interrupts' property (of_irq), which never pass
 * gs201_gpio_to_irq
 */
static int gs201_eint_map(struct irq_domain *domain, unsigned int virq,
                          irq_hw_number_t hwirq)
{
	struct gs201_bank *bank = domain->host_data;

	/* a pin without GIC interrupt stays unusable */
	if (hwirq >= PINS_PER_BANK || !bank->gic_irq[hwirq])
		return 0;

	irq_set_chip_and_handler(virq, &gs201_eint_chip, handle_edge_irq);
	irq_set_chip_data(virq, bank);
	return 0;
}


static const struct irq_domain_ops gs201_eint_domain_ops = {
	.map   = gs201_eint_map,
	.xlate = irq_domain_xlate_twocell,
};


static void gs201_bank_add_irqs(struct gs201_bank *bank)
{
	unsigned pin;

	/* only wakeup banks have per-pin GIC interrupts */
	if (!of_find_property(bank->np, "interrupts", NULL))
		return;

	bank->domain = irq_domain_add_linear(bank->np, PINS_PER_BANK,
	                                     &gs201_eint_domain_ops, bank);
	if (!bank->domain)
		return;

	for (pin = 0; pin < PINS_PER_BANK; pin++) {
		int const virq = irq_of_parse_and_map(bank->np, pin);

		if (virq <= 0)
			break;

		bank->gic_irq[pin] = irqd_to_hwirq(irq_get_irq_data(virq));

		/* flow handler only: the INTID is unmasked by gs201_pio, not here */
		irq_set_handler_data(virq, bank);
		irq_set_handler(virq, gs201_gic_irq_handler);
	}
}


static int gs201_bank_add(struct device *dev, struct device_node *np)
{
	struct gs201_bank *bank = devm_kzalloc(dev, sizeof(*bank), GFP_KERNEL);

	if (!bank)
		return -ENOMEM;

	strscpy(bank->name, np->name, sizeof(bank->name));
	bank->np = np;

	bank->chip.label            = bank->name;
	bank->chip.parent           = dev;
	bank->chip.fwnode           = of_fwnode_handle(np);
	bank->chip.owner            = THIS_MODULE;
	bank->chip.base             = -1;
	bank->chip.ngpio            = PINS_PER_BANK;
	bank->chip.of_gpio_n_cells  = 2;
	bank->chip.set              = gs201_gpio_set;
	bank->chip.get              = gs201_gpio_get;
	bank->chip.direction_output = gs201_gpio_direction_output;
	bank->chip.direction_input  = gs201_gpio_direction_input;
	bank->chip.to_irq           = gs201_gpio_to_irq;

	gs201_bank_add_irqs(bank);

	return devm_gpiochip_add_data(dev, &bank->chip, bank);
}


static int gs201_pinctrl_probe(struct platform_device *pdev)
{
	struct device_node *np;

	for_each_child_of_node(pdev->dev.of_node, np) {
		int err;

		if (!of_property_read_bool(np, "gpio-controller"))
			continue;

		err = gs201_bank_add(&pdev->dev, np);
		if (err) {
			of_node_put(np);
			return err;
		}
	}
	return 0;
}


static const struct of_device_id gs201_pinctrl_ids[] = {
	{ .compatible = "google,gs201-pinctrl" },
	{ }
};


static struct platform_driver gs201_pinctrl_driver = {
	.driver = {
		.name           = "gs201-pinctrl",
		.of_match_table = gs201_pinctrl_ids,
	},
	.probe = gs201_pinctrl_probe,
};


/* as pinctrl-samsung: banks exist before the devices that reference them */
static int __init gs201_pinctrl_drv_register(void)
{
	return platform_driver_register(&gs201_pinctrl_driver);
}
postcore_initcall(gs201_pinctrl_drv_register);


/*
 * Pinctrl consumer API
 */

struct pinctrl       { int unused; };
struct pinctrl_state { int unused; };

static struct pinctrl       gs201_pinctrl;
static struct pinctrl_state gs201_pinctrl_state;


struct pinctrl *devm_pinctrl_get(struct device *dev)
{
	return &gs201_pinctrl;
}


void devm_pinctrl_put(struct pinctrl *p) { }


struct pinctrl_state *pinctrl_lookup_state(struct pinctrl *p, const char *name)
{
	return &gs201_pinctrl_state;
}


int pinctrl_select_state(struct pinctrl *p, struct pinctrl_state *s)
{
	return 0;
}
