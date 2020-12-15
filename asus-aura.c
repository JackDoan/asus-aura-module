// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * asus-aura.c
 * Copyright (C) 2020 Jack Doan <me@jackdoan.com>
 */

#include <linux/completion.h>
#include <linux/debugfs.h>
#include <linux/errno.h>
#include <linux/hid.h>
#include <linux/hwmon.h>
#include <linux/hwmon-sysfs.h>
#include <linux/jiffies.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/types.h>


#define DRIVER_NAME		"asus-aura-hid"

#define REPLY_SIZE		65 /* max length of a reply to a single command */
#define CMD_BUFFER_SIZE		65 /* 64 does not work */
#define CMD_TIMEOUT_MS		250
#define SECONDS_PER_HOUR	(60 * 60)
#define SECONDS_PER_DAY		(SECONDS_PER_HOUR * 24)

#define AURA_ADDRESSABLE_CONTROL_MODE_EFFECT	0x3B    /* Effect control mode */
#define AURA_START_FRAME 0xEC
#define AURA_GET_FW_STR 0x82
#define AURA_GET_CONFIG_TABLE 0xb0


struct aura_data {
		struct hid_device *hdev;
		struct device *hwmon_dev;
		struct dentry *debugfs;
		struct completion wait_completion;
		struct mutex lock; /* for locking access to cmd_buffer */
		u8 *cmd_buffer;
};

static int aura_usb_cmd(struct aura_data *priv, u8 *in, int in_len, u8 *reply, int reply_len)
{
	unsigned long time;
	int ret;

	memset(priv->cmd_buffer, 0, CMD_BUFFER_SIZE);
	memcpy(priv->cmd_buffer, in, min(CMD_BUFFER_SIZE, in_len));

	reinit_completion(&priv->wait_completion);

	ret = hid_hw_output_report(priv->hdev, priv->cmd_buffer, CMD_BUFFER_SIZE);
	if (ret < 0)
		return ret;

	time = wait_for_completion_timeout(&priv->wait_completion,msecs_to_jiffies(CMD_TIMEOUT_MS));
	if (!time)
		return -ETIMEDOUT;

	/*
	 * check for obvious errors
	 */
	if (AURA_START_FRAME != priv->cmd_buffer[0])
		return -EOPNOTSUPP;

	if (reply)
		memcpy(reply, priv->cmd_buffer + 2, min(REPLY_SIZE, reply_len));

	return 0;
}

#ifdef CONFIG_DEBUG_FS

//static void print_uptime(struct seq_file *seqf, u8 cmd)
//{
//	struct aura_data *priv = seqf->private;
//	long val;
//	int ret;
//
//	ret = aura_get_value(priv, cmd, 0, &val);
//	if (ret < 0) {
//		seq_puts(seqf, "N/A\n");
//		return;
//	}
//
//	if (val > SECONDS_PER_DAY) {
//		seq_printf(seqf, "%ld day(s), %02ld:%02ld:%02ld\n", val / SECONDS_PER_DAY,
//				   val % SECONDS_PER_DAY / SECONDS_PER_HOUR, val % SECONDS_PER_HOUR / 60,
//				   val % 60);
//		return;
//	}
//
//	seq_printf(seqf, "%02ld:%02ld:%02ld\n", val % SECONDS_PER_DAY / SECONDS_PER_HOUR,
//			   val % SECONDS_PER_HOUR / 60, val % 60);
//}

static int firmware_show(struct seq_file *seqf, void *unused)
{
	struct aura_data *priv = seqf->private;
	char fw_str[16] = {0};
	int ret;
	u8 get_fw_cmd[] = {AURA_START_FRAME, AURA_GET_FW_STR};
	ret = aura_usb_cmd(priv, get_fw_cmd, 2, fw_str, 16);
	if(ret >= 0)
		seq_printf(seqf, "%s\n", fw_str);

	return ret;
}
DEFINE_SHOW_ATTRIBUTE(firmware);


static void aura_debugfs_init(struct aura_data *priv)
{
	char name[32];

	scnprintf(name, sizeof(name), "%s-%s", DRIVER_NAME, dev_name(&priv->hdev->dev));

	priv->debugfs = debugfs_create_dir(name, NULL);
	debugfs_create_file("firmware", 0444, priv->debugfs, priv, &firmware_fops);
}

#else

static void aura_debugfs_init(struct aura_data *priv)
{
}

#endif

static int aura_probe(struct hid_device *hdev, const struct hid_device_id *id)
{
	struct aura_data *priv;
	int ret;

	priv = devm_kzalloc(&hdev->dev, sizeof(struct aura_data), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->cmd_buffer = devm_kmalloc(&hdev->dev, CMD_BUFFER_SIZE, GFP_KERNEL);
	if (!priv->cmd_buffer)
		return -ENOMEM;

	ret = hid_parse(hdev);
	if (ret)
		return ret;

	ret = hid_hw_start(hdev, HID_CONNECT_HIDRAW);
	if (ret)
		return ret;

	ret = hid_hw_open(hdev);
	if (ret)
		goto fail_and_stop;

	priv->hdev = hdev;
	hid_set_drvdata(hdev, priv);
	mutex_init(&priv->lock);
	init_completion(&priv->wait_completion);

	hid_device_io_start(hdev);

//	ret = aura_fwinfo(priv);
//	if (ret < 0) {
//		dev_err(&hdev->dev, "unable to query firmware (%d)\n", ret);
//		goto fail_and_stop;
//	}

	aura_debugfs_init(priv);

	return 0;

fail_and_stop:
	hid_hw_stop(hdev);
	return ret;
}

static void aura_remove(struct hid_device *hdev)
{
	struct aura_data *priv = hid_get_drvdata(hdev);

	debugfs_remove_recursive(priv->debugfs);
	hid_hw_close(hdev);
	hid_hw_stop(hdev);
}

static int aura_raw_event(struct hid_device *hdev, struct hid_report *report, u8 *data, int size)
{
	struct aura_data *priv = hid_get_drvdata(hdev);

	if (completion_done(&priv->wait_completion))
		return 0;

	memcpy(priv->cmd_buffer, data, min(CMD_BUFFER_SIZE, size));
	complete(&priv->wait_completion);

	return 0;
}

static const struct hid_device_id aura_idtable[] = {
	{ HID_USB_DEVICE(0x0b05, 0x1872) }, /* Aura USB controller on Strix TRX-40, fw AULA1-S072-0208 */
	//{ HID_USB_DEVICE(0x0b05, 0x18f3) }, /* Aura USB controller on Strix TRX-40 */
	{ },
};
MODULE_DEVICE_TABLE(hid, aura_idtable);

static struct hid_driver aura_driver = {
	.name		= DRIVER_NAME,
	.id_table	= aura_idtable,
	.probe		= aura_probe,
	.remove		= aura_remove,
	.raw_event	= aura_raw_event,
};
module_hid_driver(aura_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Jack Doan <me@jackdoan.com>");
MODULE_DESCRIPTION("Linux driver for LED control of Asus Aura USB devices");
