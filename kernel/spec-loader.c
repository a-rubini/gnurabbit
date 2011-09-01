
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/firmware.h>
#include <asm/unaligned.h>

#include "rawrabbit.h"
#include "compat.h"

#ifndef CONFIG_FW_LOADER
#warning "CONFIG_FW_LOADER is need in kernel configuration."
#warning "Compiling anyways, but won't be able to load firmware"
#endif

/*
 * This is a trivial kernel-space inplementation of the lm32-loader,
 * so a user of the spec can just insmod and be done
 */

/* Default name is what's correct for the spec */
char *spec_program = "wrc.bin";
module_param_named(program, spec_program, charp, 0644);

/*
 * The callback when loading is over, either with or without data.
 * This a context that can sleep, so we can program it taking as
 * much time as we want.
 */
static void spec_loader_complete(const struct firmware *fw, void *context)
{
	struct rr_dev *dev = context;
	void __iomem *bar0 = dev->remap[0];
	int off;

	if (fw) {
		pr_info("%s: got program file, %i (0x%x) bytes\n", __func__,
			fw ? fw->size : 0, fw ? fw->size : 0);
	} else {
		pr_warning("%s: no firmware\n", __func__);
		return;
	}

	/* Reset the LM32 */
	writel(1, bar0 + 0xE2000);

	/* Copy stuff over */
	for (off = 0; off < fw->size; off += 4) {
		uint32_t datum;

		datum = get_unaligned_be32(fw->data + off);
		writel(datum, bar0 + 0x80000 + off);
	}
	/* Unreset the LM32 */
	writel(0, bar0 + 0xE2000);


	/* MSC */
	pr_info("LM32 has been restarted\n");
	return;
}

/* This function is called in work-queue context -- process context */
void spec_ask_program(struct rr_dev *dev)
{
	struct pci_dev *pdev = dev->pdev;
	int err;

	if (!strcmp(spec_program, "none")) {
		printk("%s: not loading program \"none\"\n", __func__);
		return;
	}

	err = request_firmware_nowait(THIS_MODULE, 1, spec_program, &pdev->dev,
				      __RR_GFP_FOR_RFNW(GFP_KERNEL)
				      dev, spec_loader_complete);
	printk("request program \"%s\": %i (%s)\n", spec_program, err,
	       err ? "Error" : "Success");
}





