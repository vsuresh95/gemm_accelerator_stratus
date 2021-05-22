// Copyright (c) 2011-2021 Columbia University, System Level Design Group
// SPDX-License-Identifier: Apache-2.0
#include <linux/of_device.h>
#include <linux/mm.h>

#include <asm/io.h>

#include <esp_accelerator.h>
#include <esp.h>

#include "gemm_accelerator_stratus.h"

#define DRV_NAME	"gemm_accelerator_stratus"

/* <<--regs-->> */
#define GEMM_ACCELERATOR_GEMM_M_REG 0x48
#define GEMM_ACCELERATOR_GEMM_N_REG 0x44
#define GEMM_ACCELERATOR_GEMM_K_REG 0x40

struct gemm_accelerator_stratus_device {
	struct esp_device esp;
};

static struct esp_driver gemm_accelerator_driver;

static struct of_device_id gemm_accelerator_device_ids[] = {
	{
		.name = "SLD_GEMM_ACCELERATOR_STRATUS",
	},
	{
		.name = "eb_095",
	},
	{
		.compatible = "sld,gemm_accelerator_stratus",
	},
	{ },
};

static int gemm_accelerator_devs;

static inline struct gemm_accelerator_stratus_device *to_gemm_accelerator(struct esp_device *esp)
{
	return container_of(esp, struct gemm_accelerator_stratus_device, esp);
}

static void gemm_accelerator_prep_xfer(struct esp_device *esp, void *arg)
{
	struct gemm_accelerator_stratus_access *a = arg;

	/* <<--regs-config-->> */
	iowrite32be(a->gemm_m, esp->iomem + GEMM_ACCELERATOR_GEMM_M_REG);
	iowrite32be(a->gemm_n, esp->iomem + GEMM_ACCELERATOR_GEMM_N_REG);
	iowrite32be(a->gemm_k, esp->iomem + GEMM_ACCELERATOR_GEMM_K_REG);
	iowrite32be(a->src_offset, esp->iomem + SRC_OFFSET_REG);
	iowrite32be(a->dst_offset, esp->iomem + DST_OFFSET_REG);

}

static bool gemm_accelerator_xfer_input_ok(struct esp_device *esp, void *arg)
{
	/* struct gemm_accelerator_stratus_device *gemm_accelerator = to_gemm_accelerator(esp); */
	/* struct gemm_accelerator_stratus_access *a = arg; */

	return true;
}

static int gemm_accelerator_probe(struct platform_device *pdev)
{
	struct gemm_accelerator_stratus_device *gemm_accelerator;
	struct esp_device *esp;
	int rc;

	gemm_accelerator = kzalloc(sizeof(*gemm_accelerator), GFP_KERNEL);
	if (gemm_accelerator == NULL)
		return -ENOMEM;
	esp = &gemm_accelerator->esp;
	esp->module = THIS_MODULE;
	esp->number = gemm_accelerator_devs;
	esp->driver = &gemm_accelerator_driver;
	rc = esp_device_register(esp, pdev);
	if (rc)
		goto err;

	gemm_accelerator_devs++;
	return 0;
 err:
	kfree(gemm_accelerator);
	return rc;
}

static int __exit gemm_accelerator_remove(struct platform_device *pdev)
{
	struct esp_device *esp = platform_get_drvdata(pdev);
	struct gemm_accelerator_stratus_device *gemm_accelerator = to_gemm_accelerator(esp);

	esp_device_unregister(esp);
	kfree(gemm_accelerator);
	return 0;
}

static struct esp_driver gemm_accelerator_driver = {
	.plat = {
		.probe		= gemm_accelerator_probe,
		.remove		= gemm_accelerator_remove,
		.driver		= {
			.name = DRV_NAME,
			.owner = THIS_MODULE,
			.of_match_table = gemm_accelerator_device_ids,
		},
	},
	.xfer_input_ok	= gemm_accelerator_xfer_input_ok,
	.prep_xfer	= gemm_accelerator_prep_xfer,
	.ioctl_cm	= GEMM_ACCELERATOR_STRATUS_IOC_ACCESS,
	.arg_size	= sizeof(struct gemm_accelerator_stratus_access),
};

static int __init gemm_accelerator_init(void)
{
	return esp_driver_register(&gemm_accelerator_driver);
}

static void __exit gemm_accelerator_exit(void)
{
	esp_driver_unregister(&gemm_accelerator_driver);
}

module_init(gemm_accelerator_init)
module_exit(gemm_accelerator_exit)

MODULE_DEVICE_TABLE(of, gemm_accelerator_device_ids);

MODULE_AUTHOR("Emilio G. Cota <cota@braap.org>");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("gemm_accelerator_stratus driver");
