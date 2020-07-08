/*
 *  Copyright (c) 2017 Tan Huang,Shenzhen Huion
 *
 *  USB HID Graphics Tablet support
 *
 *
 *There are more than two USB interfaces exist in graphics tablet.
 *The first interface is used for driver that running on Windows,macOS and Linux.The second
 *interface is used for windows without driver.
 *The pen coordinate and pressure were transferred in the first USB interface after the tablet get
 *the specified string descriptor,otherwise it transferred in the second USB interface.
 *NO. 200 string descriptor is used for switch USB interface,and the graphics tablet will return
 *19 bytes that include the X Y range and pressure level.
 *Byte 2~4 represents the X range.
 *Byte 5~7 represents the Y range.
 *Byte 8~9 represents the pressure level.
 *The pen coordinate and pressure will send from the first interace,the data length is 12 and
 *the report ID is 0x08;
 *Byte 0 represents report ID
 *Byte 1 represents pen and hot key status.
 *    0xE1 hot key
 *     0x80 pen hover
 *     0x81 pen touched
 *  0x82 first pen button pressed
 *  0x84 the second pen button pressed
 *Byte 2 X low 8 bits,if no hotkey event happend
 *Byte 3 X middle 8 bits,if no hotkey event happend
 *Byte 4 Y low 8 bits,if no hotkey event happend.Otherwise,it represents hotkey,
 *         one key occupy one bit.
 *Byte 5 Y middle 8 bits,if no hotkey event happend.Otherwise,it represents hotkey,
 *         one key occupy one bit.
 *Byte 6 pen pressure low 8 bits
 *Byte 7 pen pressure middle 8 bits
 *Byte 8 X high 8 bits
 *Byte 9 Y high 8 bits
 *Byte 10 reserved
 *Byte 11 reserved
 *The deprecated way to swtich data transfer USB interface is by NO.100 string descriptor
 *Byte 2~4 represents the X range.
 *Byte 5~7 represents the Y range.
 *Byte 8~9 represents the pressure level.
 *The pen coordinate and pressure will send from the first interace,the data length is 8 and
 *the report ID is 0x07;
 *Byte 0 represents report ID
 *Byte 1 represents pen and hot key status.
 *    0xE1 hot key
 *     0x80 pen hover
 *     0x81 pen touched
 *  0x82 first pen button pressed
 *  0x84 the second pen button pressed
 *Byte 2 X low 8 bits,if no hotkey event happend
 *Byte 3 X middle 8 bits,if no hotkey event happend
 *Byte 4 Y low 8 bits,if no hotkey event happend.Otherwise,it represents hotkey,
 *        one key occupy one bit.
 *Byte 5 Y middle 8 bits,if no hotkey event happend.Otherwise,it represents hotkey,
 *        one key occupy one bit.
 *Byte 6 pen pressure low 8 bits
 *Byte 7 pen pressure middle 8 bits
 *
 */

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/usb/input.h>
#include <linux/hid.h>
#include <asm/uaccess.h>
#include <linux/platform_device.h>
#include <linux/cdev.h>
#include <linux/miscdevice.h>
/* for apple IDs */
#ifdef CONFIG_USB_HID_MODULE
#include "./hid-ids.h"
#endif

#define DEVICE_NAME "huiontablet"
/*
 * Version Information
 */
#define DRIVER_VERSION "v3.0"
#define DRIVER_AUTHOR "Tan Huang <tanhuang@huion.cn>"
#define DRIVER_DESC "USB HID Boot Protocol tablet driver"
#define DRIVER_LICENSE "GPL"

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE(DRIVER_LICENSE);

#define USB_HUIONTABLET_MINOR_BASE 133

#define SET_REGION_MAGIC 'L'
#define SET_REGION_MAX_NR 4
#define SET_REGION_LEFT   0
#define SET_REGION_RIGHT  1
#define SET_REGION_TOP    2
#define SET_REGION_BOTTOM 3

#define HUIONTABLET_IOCTL_REGION_LEFT   _IOR(SET_REGION_MAGIC, SET_REGION_LEFT,long)
#define HUIONTABLET_IOCTL_REGION_RIGHT  _IOR(SET_REGION_MAGIC, SET_REGION_RIGHT,long)
#define HUIONTABLET_IOCTL_REGION_TOP    _IOR(SET_REGION_MAGIC, SET_REGION_TOP,long)
#define HUIONTABLET_IOCTL_REGION_BOTTOM _IOR(SET_REGION_MAGIC, SET_REGION_BOTTOM,long)

unsigned long tablet_x_max=40000,tablet_y_max=25000;
unsigned long pos_x=0,pos_y=0,pos_z=0;
unsigned long region_left=0,region_right=65536,region_top=0,region_bottom=65536;

const unsigned char HUION_COMPANY_NAME [70] =
{
  70,3,
  'H',0,
  'U',0,
  'I',0,
  'O',0,
  'N',0,
  ' ',0,
  'A',0,
  'n',0,
  'i',0,
  'm',0,
  'a',0,
  't',0,
  'i',0,
  'o',0,
  'n',0,
  ' ',0,
  'T',0,
  'e',0,
  'c',0,
  'h',0,
  'n',0,
  'o',0,
  'l',0,
  'o',0,
  'g',0,
  'y',0,
  ' ',0,
  'C',0,
  'o',0,
  '.',0,
  ',',0,
  'l',0,
  't',0,
  'd',0
};

struct usb_tablet {
    char name[128];
    char phys[64];
    struct usb_device *usbdev;
    struct input_dev *dev;
    struct urb *irq;
    signed char *data;
    dma_addr_t data_dma;
};

bool compare_company_name(unsigned char * str)
{
    int i;

    for (i=0;i<70;i++) {
        if (*str != HUION_COMPANY_NAME[i]) {
            printk ("vendor name is wrong\n");
            return false;
        }
        str++;
    }
    printk ("vendor name is right\n");
    return true;
}

static long huiontablet_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
    int ioarg = 0;
    int ret = 0;
    static int err = 0;

    printk ("kernel huiontablet_ioctl\n");
    if (_IOC_TYPE(cmd) != SET_REGION_MAGIC) {
        return - EINVAL;
    }
    if (_IOC_NR(cmd) > SET_REGION_MAX_NR) {
        return - EINVAL;
    }
    if (_IOC_DIR(cmd) & _IOC_READ) {
        err = !access_ok(VERIFY_WRITE, (void *)arg, _IOC_SIZE(cmd));
    } else if (_IOC_DIR(cmd) & _IOC_WRITE) {
        err = !access_ok(VERIFY_READ, (void *)arg, _IOC_SIZE(cmd));
    }
    if (err) {
        return -EFAULT;
    }

    switch(cmd) {
    case HUIONTABLET_IOCTL_REGION_LEFT:
        ret = __get_user(ioarg, (long *)arg);
        region_left = ioarg;
        printk ("set region left:%ld \n",region_left);
        break;
    case HUIONTABLET_IOCTL_REGION_RIGHT:
        ret = __get_user(ioarg, (long *)arg);
        region_right = ioarg;
        printk ("set region right:%ld \n",region_right);
        break;
    case HUIONTABLET_IOCTL_REGION_TOP:
        ret = __get_user(ioarg, (long *)arg);
        region_top = ioarg;
        printk ("set region top:%ld \n",region_top);
        break;
    case HUIONTABLET_IOCTL_REGION_BOTTOM:
        ret = __get_user(ioarg, (long *)arg);
        region_bottom = ioarg;
        printk ("set region bottom:%ld \n",region_bottom);
        break;
    default:
        printk ("default branch : cmd:%d \n",cmd);
        return -EINVAL;
    }
    return 0;
}

static int huiontablet_open(struct inode *inode, struct file *filp)
{
    printk ("kernel huiontablet_open\n");
    return 0;
}
static int huiontablet_release(struct inode *inode, struct file *filp)
{
    printk ("kernel huiontablet_release\n");
    return 0;
}

static ssize_t huiontablet_read(struct file *filp, char __user *buf, size_t count, loff_t *f_pos)
{
    printk ("kernel huiontablet_read\n");
    return 1;
}

/* file operation pointers */
static struct file_operations huiontablet_fops = {
    .owner   = THIS_MODULE,
    .read    = huiontablet_read,
    .unlocked_ioctl   = huiontablet_ioctl,
    .open    = huiontablet_open,
    .release = huiontablet_release
};

/* class driver information */
static struct usb_class_driver huiontablet_class = {
    .name = "usbhuiontablet%d",
    .fops = &huiontablet_fops,
    .minor_base = USB_HUIONTABLET_MINOR_BASE,
};

static void usb_tablet_irq(struct urb *urb)
{
    struct usb_tablet *tablet = urb->context;
    unsigned char *data = tablet->data;
    struct input_dev *dev = tablet->dev;
    int status;
    int btn_value;
    unsigned long tablet_pos_x=0,tablet_pos_y=0;

    switch (urb->status) {
    case 0:            /* success */
        break;
    case -ECONNRESET:    /* unlink */
    case -ENOENT:
    case -ESHUTDOWN:
        return;
        /* -EPIPE:  should clear the halt */
    default:        /* error */
        goto resubmit;
    }

    if (data[1] == 0xe0 && data[2] == 0x01 && data[3] == 0x01) {
        btn_value = (data[5] & 0x0FF)<<8;
        btn_value |= (data[4] & 0x0FF);

        switch (btn_value) {
        case 0x0000:
            break;
        case 0x0001:
            input_report_key(dev, KEY_E,1);
            input_report_key(dev, KEY_E,0);
            input_sync(dev);
            break;
        case 0x0002:
            input_report_key(dev, KEY_B,1);
            input_report_key(dev, KEY_B,0);
            input_sync(dev);
            break;
        case 0x0004:
            input_report_key(dev, KEY_LEFTCTRL,1);
            input_report_key(dev, KEY_EQUAL,1);
            input_report_key(dev, KEY_EQUAL,0);
            input_report_key(dev, KEY_LEFTCTRL,0);
            input_sync(dev);
            break;
        case 0x0008:
            input_report_key(dev, KEY_LEFTCTRL,1);
            input_report_key(dev, KEY_MINUS,1);
            input_report_key(dev, KEY_MINUS,0);
            input_report_key(dev, KEY_LEFTCTRL,0);
            input_sync(dev);
            break;
        case 0x0010:
            input_report_key(dev, KEY_LEFTBRACE,1);
            input_report_key(dev, KEY_LEFTBRACE,0);
            input_sync(dev);
            break;
        case 0x0020:
            input_report_key(dev, KEY_RIGHTBRACE,1);
            input_report_key(dev, KEY_RIGHTBRACE,0);
            input_sync(dev);
            break;
        case 0x0040:
            input_report_key(dev, KEY_ESC,1);
            input_report_key(dev, KEY_ESC,0);
            input_sync(dev);
            break;
        case 0x0080:
            input_report_key(dev, KEY_TAB,1);
            input_report_key(dev, KEY_TAB,0);
            input_sync(dev);
            break;
        case 0x0100:
            input_report_key(dev, KEY_ENTER,1);
            input_report_key(dev, KEY_ENTER,0);
            input_sync(dev);
            break;
        case 0x0200:
            input_report_key(dev, KEY_BACKSPACE,1);
            input_report_key(dev, KEY_BACKSPACE,0);
            input_sync(dev);
            break;
        case 0x0400:
            input_report_key(dev, KEY_LEFTCTRL,1);
            input_report_key(dev, KEY_C,1);
            input_report_key(dev, KEY_C,0);
            input_report_key(dev, KEY_LEFTCTRL,0);
            input_sync(dev);
            break;
        case 0x0800:
            input_report_key(dev, KEY_LEFTCTRL,1);
            input_report_key(dev, KEY_V,1);
            input_report_key(dev, KEY_V,0);
            input_report_key(dev, KEY_LEFTCTRL,0);
            input_sync(dev);
            break;
        }
    } else {
        if (data[0] == 0x08) {
            pos_x = ((*(data+8)) <<16) | ((*(data+3)) <<8) | (*(data+2) & 0x00ff) ;
            pos_y = ((*(data+9)) <<16) | ((*(data+5)) <<8) | (*(data+4) & 0x00ff) ;
            pos_z = ((*(data+7)) <<8) | (*(data+6) & 0x00ff) ;
        } else if (data[0] == 0x07) {
            pos_x =(data[3] & 0x0FF)<<8;
            pos_x |=(data[2] & 0x0FF);
            pos_y =(data[5] & 0x0FF)<<8;
            pos_y |=(data[4] & 0x0FF);
            pos_z =(data[7] & 0x0FF)<<8;
            pos_z |=(data[6] & 0x0FF);
        }

        if ((region_right >= region_left) && (region_left>=0) &&
            (region_bottom >= region_top) && (region_top>=0)) {
            tablet_pos_x = pos_x * (region_right - region_left) / tablet_x_max + region_left;
            tablet_pos_y = pos_y * (region_bottom - region_top) / tablet_y_max + region_top;

            input_report_abs(dev, ABS_X, tablet_pos_x);
            input_report_abs(dev, ABS_Y, tablet_pos_y);
            input_report_abs(dev, ABS_PRESSURE,pos_z);
            input_report_key(dev, BTN_TOOL_PEN,1);
            input_report_key(dev, BTN_TOUCH,data[1] & 0x01);
            input_report_key(dev, BTN_LEFT,data[1] & 0x01);
            input_report_key(dev, BTN_MIDDLE,(data[1] & 0x02)?1:0);
            input_report_key(dev, BTN_RIGHT,(data[1] & 0x04)?1:0);
            input_sync(dev);
        }
    }

resubmit:
    status = usb_submit_urb (urb, GFP_ATOMIC);
}

static int usb_tablet_open(struct input_dev *dev)
{
    struct usb_tablet *tablet;

    tablet = input_get_drvdata(dev);
    printk(KERN_NOTICE "enter usb_tablet_open function\n");
    tablet->irq->dev = tablet->usbdev;
    printk(KERN_NOTICE "tablet->usbdev is %p\n",tablet->usbdev);

    if(tablet->usbdev == NULL)
        return -1;

    if (usb_submit_urb(tablet->irq, GFP_KERNEL))
        return -EIO;

    printk(KERN_NOTICE "quit usb_tablet_open function\n");
    return 0;
}

static void usb_tablet_close(struct input_dev *dev)
{
    struct usb_tablet *tablet = input_get_drvdata(dev);

    printk(KERN_NOTICE "enter usb_tablet_close function\n");
    usb_kill_urb(tablet->irq);
    printk(KERN_NOTICE "quit usb_tablet_close function\n");
}

static int usb_tablet_probe(struct usb_interface *intf, const struct usb_device_id *id)
{
    struct usb_device *dev = interface_to_usbdev(intf);
    struct usb_host_interface *interface;
    struct usb_endpoint_descriptor *endpoint;
    struct usb_tablet *tablet;
    struct input_dev *input_dev;
    unsigned char* name;
    int name_size = 128;
    int pipe, maxp;
    int error = -ENOMEM;
    int pen_pressure_level=2047;
    int retval;

    printk(KERN_ERR "enter usb_tablet_probe function\n");
    interface = intf->cur_altsetting;
    printk(KERN_NOTICE "devpath: %s\n",dev->devpath);
    if (interface->desc.bNumEndpoints != 1)
        return -ENODEV;

    endpoint = &interface->endpoint[0].desc;
    if (!usb_endpoint_is_int_in(endpoint))
        return -ENODEV;

    pipe = usb_rcvintpipe(dev, endpoint->bEndpointAddress);
    maxp = usb_maxpacket(dev, pipe, usb_pipeout(pipe));

    tablet = kzalloc(sizeof(struct usb_tablet), GFP_KERNEL);
    input_dev = input_allocate_device();
    if (!tablet || !input_dev)
        goto fail1;

    tablet->data = usb_alloc_coherent(dev, 8, GFP_ATOMIC, &tablet->data_dma);
    if (!tablet->data)
        goto fail1;

    tablet->irq = usb_alloc_urb(0, GFP_KERNEL);
    if (!tablet->irq)
        goto fail2;

    tablet->usbdev = dev;
    tablet->dev = input_dev;

    if (dev->manufacturer)
        strlcpy(tablet->name, dev->manufacturer, sizeof(tablet->name));
    if (dev->product) {
        if (dev->manufacturer)
            strlcat(tablet->name, " ", sizeof(tablet->name));
        strlcat(tablet->name, dev->product, sizeof(tablet->name));
    }

    if (!strlen(tablet->name))
        snprintf(tablet->name, sizeof(tablet->name),
             "huion USB tablet %04x:%04x",
             le16_to_cpu(dev->descriptor.idVendor),
             le16_to_cpu(dev->descriptor.idProduct));

    usb_make_path(dev, tablet->phys, sizeof(tablet->phys));
    strlcat(tablet->phys, "/input0", sizeof(tablet->phys));

    input_dev->name = tablet->name;
    input_dev->phys = tablet->phys;
    usb_to_input_id(dev, &input_dev->id);
    input_dev->dev.parent = &intf->dev;
    set_bit(EV_MSC, input_dev->evbit);
    set_bit(EV_KEY, input_dev->evbit);
    set_bit(EV_ABS, input_dev->evbit);
    set_bit(ABS_X, input_dev->absbit);
    set_bit(ABS_Y, input_dev->absbit);
    set_bit(ABS_PRESSURE, input_dev->absbit);
    set_bit(BTN_TOUCH, input_dev->keybit);
    set_bit(BTN_TOOL_PEN, input_dev->keybit);
    set_bit(BTN_TOOL_BRUSH, input_dev->keybit);
    set_bit(BTN_LEFT, input_dev->keybit);
    set_bit(BTN_MIDDLE, input_dev->keybit);
    set_bit(BTN_RIGHT, input_dev->keybit);
    set_bit(KEY_E, input_dev->keybit);
    set_bit(KEY_B, input_dev->keybit);
    set_bit(KEY_C, input_dev->keybit);
    set_bit(KEY_V, input_dev->keybit);
    set_bit(KEY_MINUS, input_dev->keybit);
    set_bit(KEY_EQUAL, input_dev->keybit);
    set_bit(KEY_LEFTBRACE, input_dev->keybit);
    set_bit(KEY_RIGHTBRACE, input_dev->keybit);
    set_bit(KEY_ESC, input_dev->keybit);
    set_bit(KEY_TAB, input_dev->keybit);
    set_bit(KEY_ENTER, input_dev->keybit);
    set_bit(KEY_BACKSPACE, input_dev->keybit);
    set_bit(KEY_LEFTCTRL, input_dev->keybit);

    name = kmalloc(name_size, GFP_KERNEL);
    if (name == NULL) {
        printk("DEBUG kmalloc fail name is null");
        goto fail2;
    }

    retval = usb_get_descriptor(dev, USB_DT_STRING, 202, name, 128);
    printk("dev is %p, name is %p \n", dev, name);
    if ((retval>0) && compare_company_name(name)) {
        printk(KERN_NOTICE "NO.202 string descriptor return %d bytes \n",retval);
        memset(name , 0, 128);
        printk("usb_get_descriptor  dev: %p, name is: %p\n", dev, name);
        retval = usb_get_descriptor(dev, USB_DT_STRING, 200, name, 128);
        printk(KERN_NOTICE "NO.200 string descriptor return %d bytes \n",retval);
        tablet_x_max = ((*(name + 4)) <<16) | ((*(name + 3)) <<8) | (*(name + 2) & 0x00ff) ;
        tablet_y_max = ((*(name + 7)) <<16) | ((*(name + 6)) <<8) | (*(name + 5) & 0x00ff) ;
        pen_pressure_level = ((*(name + 9)) <<8) | (*(name + 8) & 0x00ff) ;

        memset(name , 0, 128);
        retval = usb_get_descriptor(dev, USB_DT_STRING, 201, name, 128);
        printk(KERN_NOTICE "custormer code:%s\n", name);
        retval = usb_get_descriptor(dev, USB_DT_STRING, 100, name, 128);
    } else {
        memset(name, 0, 128);
        retval = usb_get_descriptor(dev, USB_DT_STRING, 100, name, 128);
        printk(KERN_NOTICE "NO.100 string descriptor return %d bytes \n",retval);
        tablet_x_max = ((*(name + 3)) <<8) | (*(name + 2) & 0x00ff) ;
        tablet_y_max = ((*(name + 5)) <<8) | (*(name + 4) & 0x00ff) ;
        pen_pressure_level = ((*(name + 9)) <<8) | (*(name + 8) & 0x00ff) ;
    }
    input_set_drvdata(input_dev, tablet);
    input_set_abs_params(input_dev, ABS_X, 0, 65536, 0, 0);
    input_set_abs_params(input_dev, ABS_Y, 0, 65536, 0, 0);
    input_set_abs_params(input_dev, ABS_PRESSURE, 0, pen_pressure_level, 0, 0);
    input_dev->open = usb_tablet_open;
    input_dev->close = usb_tablet_close;
    usb_fill_int_urb(tablet->irq, dev, pipe, tablet->data,
             (maxp > 12 ? 12 : maxp), usb_tablet_irq, tablet, endpoint->bInterval);
    tablet->irq->transfer_dma = tablet->data_dma;
    tablet->irq->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;

    error = input_register_device(tablet->dev);
    if (error)
        goto fail3;

    usb_set_intfdata(intf, tablet);
    error = usb_register_dev(intf, &huiontablet_class);
    if (error) {
        /* something prevented us from registering this device */
        printk("Unble to allocate minor number.\n");
        usb_set_intfdata(intf, NULL);
        kfree(tablet);
        return error;
    }
    return 0;

fail3:
    usb_free_urb(tablet->irq);
    kfree(name);
fail2:
    usb_free_coherent(dev, 12, tablet->data, tablet->data_dma);
fail1:
    input_free_device(input_dev);
    kfree(tablet);
    printk(KERN_ERR "quit usb_tablet_probe function\n");
    return error;
}

static void usb_tablet_disconnect(struct usb_interface *intf)
{
    struct usb_tablet *tablet;
    tablet = usb_get_intfdata (intf);

    printk(KERN_NOTICE "enter usb_tablet_disconnect function\n");
    usb_set_intfdata(intf, NULL);
    usb_deregister_dev(intf, &huiontablet_class);
    if (tablet) {
        usb_kill_urb(tablet->irq);
        input_unregister_device(tablet->dev);
        usb_free_urb(tablet->irq);
        usb_free_coherent(interface_to_usbdev(intf), 8, tablet->data, tablet->data_dma);
        kfree(tablet);
    }
    printk(KERN_NOTICE "quit usb_tablet_disconnect function\n");
}

static int usb_tablet_suspend(struct usb_interface *intf, pm_message_t message)
{
    printk(KERN_NOTICE "usb tablet suspend\n");
    return 0;
}

static int usb_tablet_resume(struct usb_interface *intf)
{
    printk(KERN_NOTICE "usb tablet resume\n");
    return 0;
}

static struct usb_device_id usb_tablet_id_table [] = {
    { USB_DEVICE(0x256C, 0x006E) },
    { }    /* Terminating entry */
};

MODULE_DEVICE_TABLE (usb, usb_tablet_id_table);

static struct usb_driver usb_tablet_driver = {
    .name        = "usbhuiontablet",
    .probe        = usb_tablet_probe,
    .disconnect    = usb_tablet_disconnect,
    .id_table    = usb_tablet_id_table,
    .suspend = usb_tablet_suspend,
    .resume = usb_tablet_resume,
    .reset_resume = usb_tablet_resume,
    .supports_autosuspend = 1,
};

static int __init usb_tablet_init(void)
{
    int retval = usb_register(&usb_tablet_driver);
    printk(KERN_NOTICE "usb_tablet_init index 1 retval %d\n",retval);
    if (retval == 0)
        printk(KERN_INFO KBUILD_MODNAME ": " DRIVER_VERSION ":"DRIVER_DESC "\n");
    region_left=0;
    region_right=65536;
    region_top=0;
    region_bottom=65536;
    return retval;
}

static void __exit usb_tablet_exit(void)
{
    usb_deregister(&usb_tablet_driver);
    printk(KERN_NOTICE "usb_tablet_exit end\n");
}

module_init(usb_tablet_init);
module_exit(usb_tablet_exit);