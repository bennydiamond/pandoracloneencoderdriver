// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  Copyright (c) 2023 Benjamin Fiset-Deschenes
 */

/*
 * Pandora Arcade clone uart arcade encoder
 */

#include <linux/kernel.h>
#include <linux/input.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/serio.h>
#include <linux/slab.h>
#include <linux/timer.h>
#include <linux/types.h>
#include <linux/version.h>

#define DRIVER_DESC	"PandoraClone uart encoder driver"

MODULE_AUTHOR("Benjamin Fiset-Deschenes <psyko_chewbacca@hotmail.com>");
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL");

/*
 * Constants.
 */
#define PANDORA_SERIO_ID 0x43
#define PANDORA_MAX_LENGTH_BYTES 6 // Can contains 2 players worth of data + special key data
#define PANDORA_SINGLE_PLAYER_DATA_LENTGH_BYTES 2
#define PANDORA_PLAYER1_PREFIX 0xc
#define PANDORA_PLAYER2_PREFIX 0xd
#define PANDORA_SPECIAL_PREFIX 0xe
#define NORMAL_TIMEOUT_JIFFIES (msecs_to_jiffies(20)) // 16 ms
#define SPECIAL_KEY_TIMEOUT_JIFFIES (msecs_to_jiffies(12)) // 12 ms

/*
 * Per-encoder data.
 */

typedef union __attribute__((packed)) {
    struct {
#if defined(__BIG_ENDIAN)
        u16 button_a :1;
        u16 button_x :1;
        u16 button_y :1;
        u16 button_rt :1;
        u16 button_b :1;
        u16 button_lt :1;
        u16 button_start :1;
        u16 button_select :1;
        u16 pid :4;
        u16 button_dpad_u :1;
        u16 button_dpad_d :1;
        u16 button_dpad_l :1;
        u16 button_dpad_r :1;
#elif defined(__LITTLE_ENDIAN)
        u16 button_dpad_r :1;
        u16 button_dpad_l :1;
        u16 button_dpad_d :1;
        u16 button_dpad_u :1;
        u16 pid :4;
        u16 button_select :1;
        u16 button_start :1;
        u16 button_lt :1;
        u16 button_b :1;
        u16 button_rt :1;
        u16 button_y :1;
        u16 button_x :1;
        u16 button_a :1;
#else
	#error "Endianess of target is unsupported"
#endif
    };
    u8 raw[PANDORA_SINGLE_PLAYER_DATA_LENTGH_BYTES];
}mappedio_t;

struct pandoraclone {
    struct input_dev *p1, *p2;
    mappedio_t p1_prev, p2_prev;
	u8 special_key_state;
    struct timer_list timer;
	struct timer_list special_key_timeout;
    int idx;
    u8 data[PANDORA_MAX_LENGTH_BYTES];
	char phys_p1[32], phys_p2[32];
	struct mutex mutex;
};

/*
 * check_keys_being_pressed() returns true if at least 1 key is being pressed
 */
static bool check_keys_being_pressed (mappedio_t io)
{
	mappedio_t all_zeroes = { .raw[0] = 0, .raw[1] = 0 };
	io.pid = 0;
	return memcmp(&all_zeroes, &io, sizeof(mappedio_t));
}

/*
 * pandora_special_key_handle() handles special key when receiving frame
 * Send input event only if state changes from unpressed to pressed.
 * Sending of depressed event is handle by special timeout timer
 */
static void pandora_special_key_handle(struct pandoraclone *pandora)
{
	if (pandora->special_key_state == 0)
	{
		input_report_key(pandora->p1, BTN_MODE, 1);
		pandora->special_key_state = 1;
		input_sync(pandora->p1);
	}

	// Arm/re-arm timer on data receive as this means the special key is being actively pressed
	mod_timer(&pandora->special_key_timeout, jiffies + SPECIAL_KEY_TIMEOUT_JIFFIES);
}

/*
 * pandora_update_keys() Generates input events on key state changes
 * Does not handle Special key presses
 * Returns true if a key is in "pressed" state
 */
static bool pandora_update_keys(struct input_dev *p, mappedio_t *input, mappedio_t *compare)
{
	if ((compare->button_dpad_u != input->button_dpad_u) || (compare->button_dpad_d != input->button_dpad_d))
	{
		input_report_key(p, BTN_DPAD_UP, input->button_dpad_u);
		input_report_abs(p, ABS_Y, !!(input->button_dpad_d) - !!(input->button_dpad_u));
	}
	if ((compare->button_dpad_l != input->button_dpad_l) || (compare->button_dpad_r != input->button_dpad_r))
	{
		input_report_abs(p, ABS_X, !!(input->button_dpad_r) - !!(input->button_dpad_l));
	}
	
	if (compare->button_a != input->button_a)
	{ 
		input_report_key(p, BTN_A, input->button_a);
	}
	if (compare->button_x != input->button_x)
	{
		input_report_key(p, BTN_X, input->button_x);
	}
	if (compare->button_y != input->button_y)
	{
		input_report_key(p, BTN_Y, input->button_y);
	}
	if (compare->button_rt != input->button_rt)
	{
		input_report_key(p, BTN_TR, input->button_rt);
	}
	if (compare->button_b != input->button_b)
	{
		input_report_key(p, BTN_B, input->button_b);
	}
	if (compare->button_lt != input->button_lt)
	{
		input_report_key(p, BTN_TL, input->button_lt);
	}
	if (compare->button_start != input->button_start)
	{
		input_report_key(p, BTN_START, input->button_start);
	}
	if (compare->button_select != input->button_select)
	{
		input_report_key(p, BTN_SELECT, input->button_select);
	}
	input_sync(p);
	*compare = *input;

	return check_keys_being_pressed(*compare);
}

/*
 * pandora_process_packet() decodes packets the driver receives from the
 * Pandora clone encoder. It updates the data accordingly.
 * Returns true if timer needs to be started, because at least one key (for both players) is being pressed
 */

static void pandora_process_packet(struct pandoraclone *pandora)
{
	struct input_dev *p = NULL;
	u8 *data = pandora->data;
	u8 dataread = 0;
	bool data_valid = false;
	bool keys_held = false;

    // safe since function is only called when idx is a multiple of 2
	while (dataread < pandora->idx)
	{
		mappedio_t *input = (mappedio_t *)data;
		mappedio_t *compare = NULL;
		bool is_special_key = false;

		u8 trailing[2] = { 0xFF, 0xFF };
#if 0
		printk("pandora  mapped input, raw: 0x%u 0x%u , pid:%x", 
			input->raw[0], input->raw[1], input->pid);
		printk("pandora  mapped input up:%u down:%u left:%u right:%u a:%u x:%u y:%u rt:%u, b:%u lt:%u start:%u select:%u", 
			input->button_dpad_u, input->button_dpad_d, input->button_dpad_l, input->button_dpad_r, input->button_a, input->button_x, input->button_y, input->button_rt, input->button_b, input->button_lt, input->button_start, input->button_select);
#endif
		// Ignore garbage trailing data, 2 bytes at 0xFF.
		if (memcmp(&trailing, input->raw, sizeof(trailing)))
		{
			switch (input->pid)
			{
				case PANDORA_SPECIAL_PREFIX:
					is_special_key = true;
					__attribute__((__fallthrough__));
				case PANDORA_PLAYER1_PREFIX:
					p = pandora->p1;
					compare = &pandora->p1_prev;
				break;
				case PANDORA_PLAYER2_PREFIX:
					p = pandora->p2;
					compare = &pandora->p2_prev;
				break;
				default:
				break;
			}

			if (p)
			{
				if (is_special_key)
				{
					pandora_special_key_handle(pandora);
					//keys_held = true;  // TODO not sure if it should be in or not
				}
				else
				{
					data_valid = true;
					keys_held |= pandora_update_keys(p, input, compare);
				}
			}
		}
		else 

		data += PANDORA_SINGLE_PLAYER_DATA_LENTGH_BYTES;
		dataread += PANDORA_SINGLE_PLAYER_DATA_LENTGH_BYTES;
	}

	if (keys_held)
	{
		mod_timer(&pandora->timer, jiffies + NORMAL_TIMEOUT_JIFFIES);
	}
	else if (data_valid)
	{
		del_timer_sync(&pandora->timer);
	}
}

/*
 * pandora_interrupt() is called by the low level driver when characters
 * are ready for us. We then buffer them for further processing, or call the
 * packet processing routine.
 */

static irqreturn_t pandora_interrupt(struct serio *serio,
	unsigned char data, unsigned int flags)
{
	struct pandoraclone *pandora = serio_get_drvdata(serio);

	/* packets have a min length of 2 and max of 6 */
	if (pandora->idx < PANDORA_MAX_LENGTH_BYTES)
		pandora->data[pandora->idx++] = data;

	if ((pandora->idx) && !(pandora->idx % 2)) {
		mutex_lock(&pandora->mutex);
		pandora_process_packet(pandora);
		mutex_unlock(&pandora->mutex);
		pandora->idx = 0;
	}

	return IRQ_HANDLED;
}

/*
 * pandora_idle_timer() timeouts when no message received
 * Set timeout value way longer than transmit period.
 * This is just to ensure no key stays stuck.
 */
#if LINUX_VERSION_CODE < KERNEL_VERSION(4,14,0)
static void pandora_idle_timer(unsigned long data)
{
	struct pandoraclone *pandora = (struct pandoraclone *)data;
#else
static void pandora_idle_timer(struct timer_list *t)
{
	struct pandoraclone *pandora = from_timer(pandora, t, timer);
#endif
	mappedio_t all_zeroes = { .raw[0] = 0, .raw[1] = 0 };
	mutex_lock(&pandora->mutex);

	// Only reset special key state if its timer is not running
	if (timer_pending(&pandora->special_key_timeout) == 0)
	{
		// input_sync will be handled by pandora_update_keys()
		input_report_key(pandora->p1, BTN_MODE, 0);
	}
	pandora_update_keys(pandora->p1, &all_zeroes, &pandora->p1_prev);
	pandora_update_keys(pandora->p2, &all_zeroes, &pandora->p2_prev);
	mutex_unlock(&pandora->mutex);
}

/*
 * pandora_special_timeout_timer() timeouts when no message received
 * This means special key has not been pressed for a while.
 */
#if LINUX_VERSION_CODE < KERNEL_VERSION(4,14,0)
static void pandora_special_timeout_timer(unsigned long data)
{
	struct pandoraclone *pandora = (struct pandoraclone *)data;
#else
static void pandora_special_timeout_timer(struct timer_list *t)
{
	struct pandoraclone *pandora = from_timer(pandora, t, special_key_timeout);
#endif
	mutex_lock(&pandora->mutex);
	input_report_key(pandora->p1, BTN_MODE, 0);
	input_sync(pandora->p1);
	pandora->special_key_state = 0;
	mutex_unlock(&pandora->mutex);
}

/*
 * pandora_disconnect() is the opposite of pandora_connect()
 */

static void pandora_disconnect(struct serio *serio)
{
	struct pandoraclone *pandora = serio_get_drvdata(serio);

	serio_close(serio);
	serio_set_drvdata(serio, NULL);
	input_unregister_device(pandora->p1);
	input_unregister_device(pandora->p2);
	del_timer_sync(&pandora->timer);
	del_timer_sync(&pandora->special_key_timeout);
	kfree(pandora);
}

/*
 * pandora_connect() is the routine that is called when someone adds a
 * new serio device that supports pandora protocol and registers it as
 * an input device.
 */

static int pandora_connect(struct serio *serio, struct serio_driver *drv)
{
	struct pandoraclone *pandora;
	struct input_dev *input_dev, *input_dev2;
	int err = -ENOMEM;

	pandora = kzalloc(sizeof(struct pandoraclone), GFP_KERNEL);
	input_dev = input_allocate_device();
	input_dev2 = input_allocate_device();
	if (!pandora || !input_dev || !input_dev2)
		goto fail1;

	pandora->p1 = input_dev;
	pandora->p2 = input_dev2;
	snprintf(pandora->phys_p1, sizeof(pandora->phys_p1), "%s/serio0", serio->phys);
	snprintf(pandora->phys_p2, sizeof(pandora->phys_p2), "%s/serio1", serio->phys);

	input_dev->name = "PandoraClone Arcade encoder Player 1";
	input_dev->phys = pandora->phys_p1;
	input_dev->id.bustype = BUS_RS232;
	input_dev->id.vendor = SERIO_UNKNOWN;
	input_dev->id.product = 0x0001;
	input_dev->id.version = 0x0100;
	input_dev->dev.parent = &serio->dev;
	
	input_dev->evbit[0] = BIT_MASK(EV_KEY) | BIT_MASK(EV_ABS);
	input_set_abs_params(input_dev, ABS_X, -1, 1, 0, 0);
	input_set_abs_params(input_dev, ABS_Y, -1, 1, 0, 0);
	input_set_capability(input_dev, EV_KEY, BTN_A);
	input_set_capability(input_dev, EV_KEY, BTN_B);
	input_set_capability(input_dev, EV_KEY, BTN_X);
	input_set_capability(input_dev, EV_KEY, BTN_Y);
	input_set_capability(input_dev, EV_KEY, BTN_TL);
	input_set_capability(input_dev, EV_KEY, BTN_TR);
	input_set_capability(input_dev, EV_KEY, BTN_MODE);
	input_set_capability(input_dev, EV_KEY, BTN_START);
	input_set_capability(input_dev, EV_KEY, BTN_SELECT);

	input_dev2->name = "PandoraClone Arcade encoder Player 2";
	input_dev2->phys = pandora->phys_p2;
	input_dev2->id.bustype = BUS_RS232;
	input_dev2->id.vendor = SERIO_UNKNOWN;
	input_dev2->id.product = 0x0001;
	input_dev2->id.version = 0x0100;
	input_dev2->dev.parent = &serio->dev;

	input_dev2->evbit[0] = BIT_MASK(EV_KEY) | BIT_MASK(EV_ABS);
	input_set_abs_params(input_dev2, ABS_X, -1, 1, 0, 0);
	input_set_abs_params(input_dev2, ABS_Y, -1, 1, 0, 0);
	input_set_capability(input_dev2, EV_KEY, BTN_A);
	input_set_capability(input_dev2, EV_KEY, BTN_B);
	input_set_capability(input_dev2, EV_KEY, BTN_X);
	input_set_capability(input_dev2, EV_KEY, BTN_Y);
	input_set_capability(input_dev2, EV_KEY, BTN_TL);
	input_set_capability(input_dev2, EV_KEY, BTN_TR);
	input_set_capability(input_dev2, EV_KEY, BTN_START);
	input_set_capability(input_dev2, EV_KEY, BTN_SELECT);

	serio_set_drvdata(serio, pandora);

	err = serio_open(serio, drv);
	if (err)
		goto fail2;

	err = input_register_device(pandora->p1);
	err |= input_register_device(pandora->p2);
	if (err)
		goto fail3;

#if LINUX_VERSION_CODE < KERNEL_VERSION(4,14,0)
	setup_timer(&pandora->timer, pandora_idle_timer, (unsigned long)pandora);
	setup_timer(&pandora->special_key_timeout, pandora_special_timeout_timer, (unsigned long)pandora);
#else
	timer_setup(&pandora->timer, pandora_idle_timer, 0);
	timer_setup(&pandora->special_key_timeout, pandora_special_timeout_timer, 0);
#endif
	mutex_init(&pandora->mutex);

	return 0;

 fail3:	serio_close(serio);
 fail2:	serio_set_drvdata(serio, NULL);
 fail1:	input_free_device(input_dev); input_free_device(input_dev2);
	kfree(pandora);
	return err;
}

/*
 * The serio driver structure.
 */

static const struct serio_device_id pandora_serio_ids[] = {
	{
		.type	= SERIO_RS232,
		.proto	= PANDORA_SERIO_ID,
		.id	= SERIO_ANY,
		.extra	= SERIO_ANY,
	},
	{ 0 }
};

MODULE_DEVICE_TABLE(serio, pandora_serio_ids);

static struct serio_driver pandora_drv = {
	.driver		= {
		.name	= "pandoraclone",
	},
	.description	= DRIVER_DESC,
	.id_table	= pandora_serio_ids,
	.interrupt	= pandora_interrupt,
	.connect	= pandora_connect,
	.disconnect	= pandora_disconnect,
};

module_serio_driver(pandora_drv);
