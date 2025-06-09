// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2022 MediaTek Inc.
 * Author: Sam Shih <sam.shih@mediatek.com>
 */

#include <asm/io.h>
#include <linux/libfdt.h>
#include <common.h>
#include <env.h>
#include <net.h>
#include <mtd.h>
#include <stdio_dev.h>
#include <errno.h>
#include <fdtdec.h> /* For fdt32_to_cpu, also used in ft_system_setup */

#define MAC_ADDR_LEN	6

/*
 * Reads the MAC address from the "Factory" MTD partition and sets
 * the 'ethaddr' environment variable.
 *
 * Returns 0 on success, or a non-zero error code on failure.
 */
static int board_set_ethaddr_from_factory(void)
{
	struct mtd_info *mtd_dev;
	u_char mac_bytes[MAC_ADDR_LEN]; /* Use ETH_ALEN for MAC address length */
	char mac_str[18];             /* XX:XX:XX:XX:XX:XX\0 */
	size_t retlen;
	int err;

	const char *part_label = "Factory"; /* Partition label */
	loff_t mac_offset = 0x2A;           /* Offset of MAC within the "Factory" partition */

	debug("RFB: Attempting to read MAC from MTD partition: %s, Offset: 0x%llx\n",
	      part_label, mac_offset);

	/* Get the MTD device info for the "Factory" partition directly */
	mtd_dev = get_mtd_device_nm(part_label);
	if (IS_ERR(mtd_dev) || !mtd_dev) {
		printf("ERROR: RFB: MTD partition '%s' not found.\n", part_label);
		return -ENODEV;
	}

	/* Check if the offset is within the bounds of the "Factory" partition */
	if (mac_offset + sizeof(mac_bytes) > mtd_dev->size) {
		printf("ERROR: RFB: MAC offset 0x%llx + size %zu exceeds partition '%s' size 0x%llx.\n",
		       mac_offset, sizeof(mac_bytes), part_label, (u64)mtd_dev->size);
		put_mtd_device(mtd_dev);
		return -EINVAL;
	}

	/* Read directly from the "Factory" partition's mtd_info structure */
	err = mtd_read(mtd_dev, mac_offset, sizeof(mac_bytes), &retlen, mac_bytes);
	if (err || retlen != sizeof(mac_bytes)) {
		printf("ERROR: RFB: Failed to read MAC from '%s' (offset 0x%llx). Read %zu bytes, err %d\n",
		       part_label, mac_offset, retlen, err);
		put_mtd_device(mtd_dev);
		return err ? err : -EIO;
	}
	put_mtd_device(mtd_dev); /* Release the MTD device */

	/* Validate the MAC address before attempting to set it */
	if (is_zero_ethaddr(mac_bytes) || is_broadcast_ethaddr(mac_bytes)) {
		printf("WARNING: RFB: MAC from Factory @0x%llx is invalid (zero/broadcast): %02X:%02X:%02X:%02X:%02X:%02X\n",
		       mac_offset,
		       mac_bytes[0], mac_bytes[1], mac_bytes[2],
		       mac_bytes[3], mac_bytes[4], mac_bytes[5]);
		return -EINVAL;
	}

	/* Format the MAC address bytes into a string */
	sprintf(mac_str, "%02X:%02X:%02X:%02X:%02X:%02X",
	        mac_bytes[0], mac_bytes[1], mac_bytes[2],
	        mac_bytes[3], mac_bytes[4], mac_bytes[5]);

	/* Check if the environment variable needs to be updated */
	const char *current_ethaddr = env_get("ethaddr");

	if (!current_ethaddr || strcmp(current_ethaddr, mac_str) != 0) {
		printf("RFB: Read MAC from Factory: %s. Updating 'ethaddr'.\n", mac_str);
		if (env_set("ethaddr", mac_str) < 0) {
			printf("ERROR: RFB: Failed to set 'ethaddr' environment variable.\n");
			return -EIO;
			/* Optionally, you might want to trigger a saveenv here if your board policy
			 * requires immediate persistence of this change and doesn't do it later.
			 * However, typically env_save() is called at a more central place or on demand.
			 * For example: run_command("saveenv", 0);
			 */
		}
	} else {
		printf("RFB: Read MAC from Factory: %s. 'ethaddr' is already correctly set.\n", mac_str);
	}

	return 0;
}

int board_init(void)
{
	return 0;
}

#ifdef CONFIG_BOARD_LATE_INIT
int board_late_init(void)
{
	if (board_set_ethaddr_from_factory() != 0) {
		printf("WARNING: RFB: Could not set MAC address from factory. Network may use random MAC or fail.\n");
		/* U-Boot's default behavior (CONFIG_NET_RANDOM_ETHADDR) might generate a random MAC */
	}
	return 0;
}
#endif

#define	MT7981_BOOT_NOR		0
#define	MT7981_BOOT_SPIM_NAND	1 /* ToDo: fallback to SD */
#define	MT7981_BOOT_EMMC	2
#define	MT7981_BOOT_SNFI_NAND	3 /* ToDo (treated as SD) */

int ft_system_setup(void *blob, struct bd_info *bd)
{
	const u32 *media_handle_p;
	int chosen, len, ret;
	const char *media;
	u32 media_handle;

	switch ((readl(0x11d006f0) & 0xc0) >> 6) {
	case MT7981_BOOT_NOR:
		media = "rootdisk-nor";
		break
		;;
	case MT7981_BOOT_SPIM_NAND:
		media = "rootdisk-spim-nand";
		break
		;;
	case MT7981_BOOT_EMMC:
		media = "rootdisk-emmc";
		break
		;;
	case MT7981_BOOT_SNFI_NAND:
		media = "rootdisk-sd";
		break
		;;
	}

	chosen = fdt_path_offset(blob, "/chosen");
	if (chosen <= 0)
		return 0;

	media_handle_p = fdt_getprop(blob, chosen, media, &len);
	if (media_handle_p <= 0 || len != 4)
		return 0;

	media_handle = *media_handle_p;
	ret = fdt_setprop(blob, chosen, "rootdisk", &media_handle, sizeof(media_handle));
	if (ret) {
		printf("cannot set media phandle %s as rootdisk /chosen node\n", media);
		return ret;
	}

	printf("set /chosen/rootdisk to bootrom media: %s (phandle 0x%08x)\n", media, fdt32_to_cpu(media_handle));

	return 0;
}

