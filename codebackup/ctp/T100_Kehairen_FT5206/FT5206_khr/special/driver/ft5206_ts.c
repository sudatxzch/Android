/* 
 * drivers/input/touchscreen/ft5x0x_ts.c
 *
 * FocalTech ft5x0x TouchScreen driver. 
 *
 * Copyright (c) 2010  Focal tech Ltd.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * VERSION      	DATE			AUTHOR
 *    1.0		  2010-01-05			WenFS
 *
 * note: only support mulititouch	Wenfs 2010-10-01
 */

#include <linux/delay.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/input.h>
#include <asm/uaccess.h>
#include <mach/ldo.h>
//#include <asm/arch-sc8810/ldo.h>
#include <linux/gpio.h>
#include <linux/smp_lock.h>
#include <linux/earlysuspend.h>

#include <linux/platform_device.h>

#include "ft5206_ts.h"

struct sprd_i2c_setup_data {
	unsigned i2c_bus;  //the same number as i2c->adap.nr in adapter probe function
	unsigned short i2c_address;
	int irq;
	char type[I2C_NAME_SIZE];
};

/*********************************Bee-0928-TOP****************************************/
#define PIXCIR_DEBUG		1

#define CONFIG_FT5X0X_MULTITOUCH	

#ifdef PIXCIR_DEBUG
#define PIXCIR_DBG(format, ...)	\
		printk(KERN_INFO "PIXCIR_TS " format "\n", ## __VA_ARGS__)
#else
#define PIXCIR_DBG(format, ...)
#endif


#define SLAVE_ADDR		    0x70
#define	BOOTLOADER_ADDR		0x71
#ifndef I2C_MAJOR
#define I2C_MAJOR 		125  //zhucaihua ???
#endif

#define I2C_MINORS 		256

#define	CALIBRATION_FLAG	1
#define	BOOTLOADER		7
#define RESET_TP		9

#define	ENABLE_IRQ		10
#define	DISABLE_IRQ		11
#define	BOOTLOADER_STU		16
#define ATTB_VALUE		17

#define	MAX_FINGER_NUM		5
#define X_OFFSET		30
#define Y_OFFSET		40

static unsigned char status_reg = 0;
int global_irq;

struct i2c_dev
{
	struct list_head list;
	struct i2c_adapter *adap;
	struct device *dev;
};

static struct i2c_driver pixcir_i2c_ts_driver;
static struct class *i2c_dev_class;
static LIST_HEAD( i2c_dev_list);
static DEFINE_SPINLOCK( i2c_dev_list_lock);

#define TOUCH_VIRTUAL_KEYS
extern int sprd_3rdparty_gpio_tp_rst ;
extern int sprd_3rdparty_gpio_tp_irq ;
static struct i2c_client *this_client;
static int pixcir_irq;
static int suspend_flag;
static struct early_suspend	pixcir_early_suspend;

static ssize_t pixcir_set_calibrate(struct device* cd, struct device_attribute *attr,
		       const char* buf, size_t len);
static ssize_t pixcir_show_suspend(struct device* cd,struct device_attribute *attr, char* buf);
static ssize_t pixcir_store_suspend(struct device* cd, struct device_attribute *attr,const char* buf, size_t len);

static void pixcir_reset(void);
static void pixcir_ts_suspend(struct early_suspend *handler);
static void pixcir_ts_resume(struct early_suspend *handler);
static void pixcir_ts_pwron(void);
static void pixcir_ts_pwroff(void);


static int pixcir_i2c_rxdata(char *rxdata, int length)
{
        int ret;
        struct i2c_msg msgs[] = {
                {
                        .addr   = this_client->addr,
                        .flags  = 0,
                        .len    = 1,
                        .buf    = rxdata,
                },
                {
                        .addr   = this_client->addr,
                        .flags  = I2C_M_RD,
                        .len    = length,
                        .buf    = rxdata,
                },
        };

        ret = i2c_transfer(this_client->adapter, msgs,2);
        if (ret < 0)
                pr_err("%s i2c read error: %d\n", __func__, ret);
        
        return ret;
}


static int pixcir_i2c_txdata(char *txdata, int length)
{
		int ret;
		struct i2c_msg msg[] = {
			{
				.addr	= this_client->addr,
				.flags	= 0,
				.len		= length,
				.buf		= txdata,
			},
		};

		ret = i2c_transfer(this_client->adapter, msg, 1);
		if (ret < 0)
			pr_err("%s i2c write error: %d\n", __func__, ret);

		return ret;
}

static int pixcir_i2c_write_data(unsigned char addr, unsigned char data)
{
	unsigned char buf[2];
	buf[0]=addr;
	buf[1]=data;
	return pixcir_i2c_txdata(buf, 2); 
}




static DEVICE_ATTR(calibrate, S_IRUGO | S_IWUSR, NULL, pixcir_set_calibrate);
static DEVICE_ATTR(suspend, S_IRUGO | S_IWUSR, pixcir_show_suspend, pixcir_store_suspend);

static ssize_t pixcir_set_calibrate(struct device* cd, struct device_attribute *attr,
		       const char* buf, size_t len)
{
	unsigned long on_off = simple_strtoul(buf, NULL, 10);
	
	if(on_off==1)
	{
#ifdef PIXCIR_DEBUG
		printk("%s: PIXCIR calibrate\n",__func__);
#endif
		pixcir_i2c_write_data(0x3a , 0x03);
		msleep(5*1000);
	}
	
	return len;
}

static ssize_t pixcir_show_suspend(struct device* cd,
				     struct device_attribute *attr, char* buf)
{
	ssize_t ret = 0;

	if(suspend_flag==1)
		sprintf(buf, "Pixcir Suspend\n");
	else
		sprintf(buf, "Pixcir Resume\n");
	
	ret = strlen(buf) + 1;

	return ret;
}

static ssize_t pixcir_store_suspend(struct device* cd, struct device_attribute *attr,
		       const char* buf, size_t len)
{
	unsigned long on_off = simple_strtoul(buf, NULL, 10);
	suspend_flag = on_off;
	
	if(on_off==1)
	{
#ifdef PIXCIR_DEBUG
		printk("Pixcir Entry Suspend\n");
#endif
		pixcir_ts_suspend(NULL);
	}
	else
	{
#ifdef PIXCIR_DEBUG
		printk("Pixcir Entry Resume\n");
#endif
		pixcir_ts_resume(NULL);
	}
	
	return len;
}


static int pixcir_create_sysfs(struct i2c_client *client)
{
	int err;
	struct device *dev = &(client->dev);

#ifdef PIXCIR_DEBUG
	PIXCIR_DBG("%s\n", __func__);
#endif	
	err = device_create_file(dev, &dev_attr_calibrate);
	err = device_create_file(dev, &dev_attr_suspend);

	return err;
}

static void pixcir_ts_suspend(struct early_suspend *handler)
{
#ifdef PIXCIR_DEBUG
	printk("==%s==\n", __func__);
#endif
   	pixcir_ts_pwroff();
}


static void pixcir_ts_resume(struct early_suspend *handler)
{	
#ifdef PIXCIR_DEBUG
	printk("==%s==\n", __func__);
#endif
	pixcir_ts_pwron();
	pixcir_reset();
}


#ifdef TOUCH_VIRTUAL_KEYS
#define SC8810_KEY_HOME	102
#define SC8810_KEY_MENU	30
#define SC8810_KEY_BACK	17
#define SC8810_KEY_SEARCH  217

static ssize_t virtual_keys_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
#if 0//He_Lin Module
	return sprintf(buf,
	     __stringify(EV_KEY) ":" __stringify(KEY_HOME)   ":208:500:30:100"
	 ":" __stringify(EV_KEY) ":" __stringify(KEY_MENU)   ":240:500:30:100"
	 ":" __stringify(EV_KEY) ":" __stringify(KEY_BACK)   ":272:500:30:100"
	 ":" __stringify(EV_KEY) ":" __stringify(KEY_SEARCH) ":304:500:30:100"
	 "\n");
#endif
#if 1
	return sprintf(buf,
	     __stringify(EV_KEY) ":" __stringify(KEY_MENU)   ":55:500:80:100"
	 ":" __stringify(EV_KEY) ":" __stringify(KEY_BACK)   ":260:500:80:100"
	 "\n");
#endif

}


static struct kobj_attribute virtual_keys_attr = {
    .attr = {
        .name = "virtualkeys.ft5206_ts", //pixcir_ts
        .mode = S_IRUGO,
    },
    .show = &virtual_keys_show,
};

static struct attribute *properties_attrs[] = {
    &virtual_keys_attr.attr,
    NULL
};

static struct attribute_group properties_attr_group = {
    .attrs = properties_attrs,
};

static void pixcir_ts_virtual_keys_init(void)
{
    int ret;
    struct kobject *properties_kobj;
	
    PIXCIR_DBG("%s\n",__func__);
	
    properties_kobj = kobject_create_and_add("board_properties", NULL);
    if (properties_kobj)
        ret = sysfs_create_group(properties_kobj,
                     &properties_attr_group);
    if (!properties_kobj || ret)
        pr_err("failed to create board_properties\n");    
}


#endif

static void pixcir_ts_pwron(void)
{
    PIXCIR_DBG("%s",__func__);

	LDO_SetVoltLevel(LDO_LDO_SIM2, LDO_VOLT_LEVEL0);
	LDO_TurnOnLDO(LDO_LDO_SIM2);
	msleep(20);
}

static void pixcir_ts_pwroff(void)
{
	pixcir_i2c_write_data(0xA5, 0x03);

    PIXCIR_DBG("%s",__func__);
	
	LDO_TurnOffLDO(LDO_LDO_SIM2);
}

static int  pixcir_ts_config_pins(void)
{
	pixcir_ts_pwron();
	gpio_direction_input(sprd_3rdparty_gpio_tp_irq);	
	pixcir_irq=sprd_alloc_gpio_irq(sprd_3rdparty_gpio_tp_irq);
	pixcir_reset();
	return pixcir_irq;
}


static int attb_read_val(void)
{
	return gpio_get_value(sprd_3rdparty_gpio_tp_irq);
}

static void pixcir_reset(void)
{
	PIXCIR_DBG("%s\n",__func__);
	gpio_direction_output(sprd_3rdparty_gpio_tp_rst, 1);
	msleep(3);
	gpio_set_value(sprd_3rdparty_gpio_tp_rst, 0);
	msleep(10);
	gpio_set_value(sprd_3rdparty_gpio_tp_rst,1);
	msleep(10);
}


static int pixcir_config_intmode(void)
{
#if 0
	int error;
	unsigned char buf;

	msleep(60);
	error=pixcir_i2c_write_data(52, 0x0A);
	buf =  0x34;
	error=pixcir_i2c_rxdata(&buf, 1);
	PIXCIR_DBG("%s: buf=0x%x\n",__func__, buf);  
	return error;
#endif
return 0;
}

static int  pixcir_init(void)
{
	int irq;
	PIXCIR_DBG("%s\n",__func__);
	irq = pixcir_ts_config_pins();
	pixcir_config_intmode();
	return irq;
}


static void return_i2c_dev(struct i2c_dev *i2c_dev)
{
	spin_lock(&i2c_dev_list_lock);
	list_del(&i2c_dev->list);
	spin_unlock(&i2c_dev_list_lock);
	kfree(i2c_dev);
}

static struct i2c_dev *i2c_dev_get_by_minor(unsigned index)
{
	struct i2c_dev *i2c_dev;
	i2c_dev = NULL;

	spin_lock(&i2c_dev_list_lock);
	list_for_each_entry(i2c_dev, &i2c_dev_list, list)
	{
		if (i2c_dev->adap->nr == index)
			goto found;
	}
	i2c_dev = NULL;
	found: spin_unlock(&i2c_dev_list_lock);
	return i2c_dev;
}

static struct i2c_dev *get_free_i2c_dev(struct i2c_adapter *adap)
{
	struct i2c_dev *i2c_dev;

	if (adap->nr >= I2C_MINORS) {
#ifdef PIXCIR_DEBUG
		printk(KERN_ERR "i2c-dev: Out of device minors (%d)\n",
				adap->nr);
#endif
		return ERR_PTR(-ENODEV);
	}

	i2c_dev = kzalloc(sizeof(*i2c_dev), GFP_KERNEL);
	if (!i2c_dev)
		return ERR_PTR(-ENOMEM);

	i2c_dev->adap = adap;

	spin_lock(&i2c_dev_list_lock);
	list_add_tail(&i2c_dev->list, &i2c_dev_list);
	spin_unlock(&i2c_dev_list_lock);
	return i2c_dev;
}
/*********************************Bee-0928-bottom**************************************/

struct ts_event {
	u16	x1;
	u16	y1;
	u16	x2;
	u16	y2;
	u16	x3;
	u16	y3;
	u16	x4;
	u16	y4;
	u16	x5;
	u16	y5;
	u16	pressure;
    u8  touch_point;
};

struct pixcir_i2c_ts_data {
	struct i2c_client *client;
	struct input_dev *input;
	struct ts_event		event;
	//const struct pixcir_ts_platform_data *chip;
	bool exiting;
};

struct point_node_t{
	unsigned char 	active ;
	unsigned char	finger_id;
	int	posx;
	int	posy;
};

static struct point_node_t point_slot[MAX_FINGER_NUM*2];

static void pixcir_ts_poscheck(struct pixcir_i2c_ts_data *data)
{
	struct pixcir_i2c_ts_data *tsdata = data;
	
	unsigned char *p;
	unsigned char touch_point, button, pix_id,slot_id;
	unsigned char rdbuf[32]={0};
	int ret, i;
 
	rdbuf[0] = 0; 							     //从寄存器0开始读
	pixcir_i2c_rxdata(rdbuf, 31);    //从第0个寄存器开始, 读前5个有效点
	touch_point = rdbuf[2]&0x07;     //获取实际有效点数

	#if 0
	if(touch_point>5) touch_point=5; //只有前5个点有效
	 ":" __stringify(EV_KEY) ":" __stringify(KEY_SEARCH) ":300:500:80:100"

	switch (touch_point) {
		case 5:
			point_slot[4].active = 1;
			point_slot[4].posx = (s16)(rdbuf[0x1b] & 0x0F)<<8 | (s16)rdbuf[0x1c];
			point_slot[4].posy = (s16)(rdbuf[0x1d] & 0x0F)<<8 | (s16)rdbuf[0x1e];
			PIXCIR_DBG("===x5 = %d,y5 = %d ====",point_slot[5].posx,point_slot[5].posy);
			break;
		case 4:
			point_slot[3].active = 1;
			point_slot[3].posx = (s16)(rdbuf[0x0f] & 0x0F)<<8 | (s16)rdbuf[0x10];
			point_slot[3].posy = (s16)(rdbuf[0x11] & 0x0F)<<8 | (s16)rdbuf[0x12];
			PIXCIR_DBG("===x3 = %d,y3 = %d ====",point_slot[3].posx,point_slot[3].posy);
			break;
		case 3:
			point_slot[2].active = 1;
			point_slot[2].posx = (s16)(rdbuf[9] & 0x0F)<<8 | (s16)rdbuf[10];
			point_slot[2].posy = (s16)(rdbuf[11] & 0x0F)<<8 | (s16)rdbuf[12];
			PIXCIR_DBG("===x2 = %d,y2 = %d ====",point_slot[2].posx,point_slot[2].posy);
			break;
		case 2:
			point_slot[1].active = 1;
			point_slot[1].posx = (s16)(rdbuf[3] & 0x0F)<<8 | (s16)rdbuf[4];
			point_slot[1].posy= (s16)(rdbuf[5] & 0x0F)<<8 | (s16)rdbuf[6];
			PIXCIR_DBG("===x1 = %d,y1 = %d ====",point_slot[1].posx,point_slot[1].posy);
            break;
		case 1:
			point_slot[0].active = 1;
			point_slot[0].posx = (s16)(rdbuf[3] & 0x0F)<<8 | (s16)rdbuf[4];
			point_slot[0].posy= (s16)(rdbuf[5] & 0x0F)<<8 | (s16)rdbuf[6];
			PIXCIR_DBG("===x0 = %d,y0 = %d ====",point_slot[0].posx,point_slot[0].posy);
            break;
		default:
		    return -1;
	}
	#endif
	  
	if (touch_point == 0) 
	{
		PIXCIR_DBG("%s: release\n",__func__);
//		input_report_key(tsdata->input, BTN_TOUCH, 0);
#ifdef CONFIG_FT5X0X_MULTITOUCH	
		input_report_abs(tsdata->input, ABS_MT_TOUCH_MAJOR, 0);
#else
		input_report_abs(tsdata->input, ABS_PRESSURE, 0);  
		input_report_key(tsdata->input, BTN_TOUCH, 0);
#endif
		input_sync(tsdata->input);

		return 1;
	}
	else if(touch_point>5) 
	{
	    touch_point=5; //只有前5个点有效
	}
	p=&rdbuf[2];
	for (i=0; i<touch_point; i++)	
    {
		pix_id = (*(p+3));
		slot_id = (pix_id & 0xf0)>>4;
		point_slot[slot_id].active = 1;
		point_slot[slot_id].finger_id = 1;	
		point_slot[slot_id].posx = (*(p+1) & 0x0F)<<8 | (*(p+2));
		point_slot[slot_id].posy = (*(p+3) & 0x0F)<<8 | (*(p+4));
		p+=6;
	}
  

	if(touch_point) {
		//input_report_key(tsdata->input, BTN_TOUCH, 1);
		//input_report_abs(tsdata->input, ABS_MT_TOUCH_MAJOR, 15);
		for (i=0; i<MAX_FINGER_NUM*2; i++) {
			if (point_slot[i].active == 1) {
				if(point_slot[i].posy<0) {
					//printk("\033[33;1m%s: dirty slot=%d,x%d=%d,y%d=%d\033[m\n", \
						//__func__, i, i/2,point_slot[i].posx, i/2, point_slot[i].posy);
				} else {
					if(point_slot[i].posx<0) {
						//printk("\033[33;1m%s: slot=%d, convert x%d from %d to 0\033[m\n",\
							//__func__, i, i/2,point_slot[i].posx);
						point_slot[i].posx = 0;
					} else if (point_slot[i].posx>=320) {
						//printk("\033[33;1m%s: slot=%d, convert x%d from %d to 480\033[m\n",\
							//__func__, i, i/2,point_slot[i].posx);
						point_slot[i].posx = 319;
					}
					input_report_abs(tsdata->input, ABS_MT_POSITION_X,  point_slot[i].posx);
					input_report_abs(tsdata->input, ABS_MT_POSITION_Y,  point_slot[i].posy);
					input_report_abs(tsdata->input, ABS_MT_TOUCH_MAJOR, 200);
					input_report_abs(tsdata->input, ABS_MT_WIDTH_MAJOR, 1);
					input_mt_sync(tsdata->input);
					PIXCIR_DBG("%s: slot=%d,x%d=%d,y%d=%d\n",__func__, i, i/2,point_slot[i].posx, i/2, point_slot[i].posy);
				}
			}
		}
	}

#if 0
    else 
	{
		PIXCIR_DBG("%s: release\n",__func__);
		input_report_key(tsdata->input, BTN_TOUCH, 0);
		input_report_abs(tsdata->input, ABS_MT_TOUCH_MAJOR, 0);
	}
#endif
	input_sync(tsdata->input); 

	for (i=0; i<MAX_FINGER_NUM*2; i++) {
		if (point_slot[i].active == 0) {
			point_slot[i].posx = 0;
			point_slot[i].posy = 0;
		}
		point_slot[i].active = 0;
	}
    return 0;
}

/***********************************************************************************************
Name	:	 

Input	:	
                     

Output	:	

function	:	

***********************************************************************************************/
static void ft5x0x_ts_release(struct pixcir_i2c_ts_data *data)
{
//	struct pixcir_i2c_ts_data *data = i2c_get_clientdata(this_client);
#ifdef CONFIG_FT5X0X_MULTITOUCH	
	input_report_abs(data->input, ABS_MT_TOUCH_MAJOR, 0);
#else
	input_report_abs(data->input, ABS_PRESSURE, 0);  
	input_report_key(data->input, BTN_TOUCH, 0);
#endif
	input_sync(data->input);
}

static int ft5x0x_read_data(struct pixcir_i2c_ts_data *data)
{
//	struct pixcir_i2c_ts_data *data = i2c_get_clientdata(this_client);
	struct ts_event *event = &data->event;
//	u8 buf[14] = {0};
	u8 buf[32] = {0};
	int ret = -1;
	int touch_point = 0;

	//printk("==read data=\n");	
#ifdef CONFIG_FT5X0X_MULTITOUCH
//	ret = ft5x0x_i2c_rxdata(buf, 13);
	ret = pixcir_i2c_rxdata(buf, 31);
#else
    ret = pixcir_i2c_rxdata(buf, 7);
#endif
    if (ret < 0) {
		PIXCIR_DBG("%s read_data i2c_rxdata failed: %d\n", __func__, ret);
		return ret;
	}

//	event->touch_point = buf[2] & 0x03;// 0000 0011
	touch_point = buf[2] & 0x07;// 000 0111
     
    if (touch_point == 0) {
        ft5x0x_ts_release(data);
        return 1; 
    }
	memset(event, 0, sizeof(struct ts_event));
	event->touch_point = touch_point;
#ifdef CONFIG_FT5X0X_MULTITOUCH
    switch (event->touch_point) {
#ifdef CONFIG_TOUCHSCREEN_FT5206_WVGA
//jixwei_add for Q100 ft5x06 convert from WVGA to HVGA
		case 5:
			event->x5 = (s16)(buf[0x1b] & 0x0F)<<8 | (s16)buf[0x1c];
			event->y5 = (s16)(buf[0x1d] & 0x0F)<<8 | (s16)buf[0x1e];
			event->x5 = event->x5*2/3;
			event->y5 = event->y5*3/5;
		case 4:
			event->x4 = (s16)(buf[0x15] & 0x0F)<<8 | (s16)buf[0x16];
			event->y4 = (s16)(buf[0x17] & 0x0F)<<8 | (s16)buf[0x18];
			event->x4 = event->x4*2/3;
			event->y4 = event->y4*3/5;
		case 3:
			event->x3 = (s16)(buf[0x0f] & 0x0F)<<8 | (s16)buf[0x10];
			event->y3 = (s16)(buf[0x11] & 0x0F)<<8 | (s16)buf[0x12];
			event->x3 = event->x3*2/3;
			event->y3 = event->y3*3/5;
		case 2:
			event->x2 = (s16)(buf[9] & 0x0F)<<8 | (s16)buf[10];
			event->y2 = (s16)(buf[11] & 0x0F)<<8 | (s16)buf[12];
			event->x2 = event->x2*2/3;
			event->y2 = event->y2*3/5;
		case 1:
			event->x1 = (s16)(buf[3] & 0x0F)<<8 | (s16)buf[4];
			event->y1 = (s16)(buf[5] & 0x0F)<<8 | (s16)buf[6];
			event->x1 = event->x1*2/3;
			event->y1 = event->y1*3/5;
#else
		case 5:
			event->x5 = (s16)(buf[0x1b] & 0x0F)<<8 | (s16)buf[0x1c];
			event->y5 = (s16)(buf[0x1d] & 0x0F)<<8 | (s16)buf[0x1e];
		case 4:
			event->x4 = (s16)(buf[0x15] & 0x0F)<<8 | (s16)buf[0x16];
			event->y4 = (s16)(buf[0x17] & 0x0F)<<8 | (s16)buf[0x18];
		case 3:
			event->x3 = (s16)(buf[0x0f] & 0x0F)<<8 | (s16)buf[0x10];
			event->y3 = (s16)(buf[0x11] & 0x0F)<<8 | (s16)buf[0x12];
		case 2:
			event->x2 = (s16)(buf[9] & 0x0F)<<8 | (s16)buf[10];
			event->y2 = (s16)(buf[11] & 0x0F)<<8 | (s16)buf[12];
		case 1:
			event->x1 = (s16)(buf[3] & 0x0F)<<8 | (s16)buf[4];
			event->y1 = (s16)(buf[5] & 0x0F)<<8 | (s16)buf[6];
#endif
			break;
		default:
		    return -1;
	}
#else
    if (event->touch_point == 1) {
    	event->x1 = (s16)(buf[3] & 0x0F)<<8 | (s16)buf[4];
		event->y1 = (s16)(buf[5] & 0x0F)<<8 | (s16)buf[6];
    }
#endif
    event->pressure = 200;

#ifdef PIXCIR_DEBUG
	dev_dbg(&this_client->dev, "%s: 1:%d %d 2:%d %d \n", __func__,
		event->x1, event->y1, event->x2, event->y2);
	PIXCIR_DBG("%d (%d, %d), (%d, %d)\n", event->touch_point, event->x1, event->y1, event->x2, event->y2);
#endif
    return 0;
}
/***********************************************************************************************
Name	:	 

Input	:	
                     

Output	:	

function	:	

***********************************************************************************************/
static void ft5x0x_report_value(struct pixcir_i2c_ts_data *data)
{
//	struct pixcir_i2c_ts_data *data = i2c_get_clientdata(this_client);
	struct ts_event *event = &data->event;
        /*lilonghui delet the log for debug2011-12-26*/
//	printk("==ft5x0x_report_value =\n");
#ifdef CONFIG_FT5X0X_MULTITOUCH
	switch(event->touch_point) {
		case 5:
			input_report_abs(data->input, ABS_MT_TOUCH_MAJOR, event->pressure);
			input_report_abs(data->input, ABS_MT_POSITION_X, event->x5);
			input_report_abs(data->input, ABS_MT_POSITION_Y, event->y5);
			input_report_abs(data->input, ABS_MT_WIDTH_MAJOR, 1);
			input_mt_sync(data->input);
			//printk("===x5 = %d,y5 = %d ====\n",event->x5,event->y5);
		case 4:
			input_report_abs(data->input, ABS_MT_TOUCH_MAJOR, event->pressure);
			input_report_abs(data->input, ABS_MT_POSITION_X, event->x4);
			input_report_abs(data->input, ABS_MT_POSITION_Y, event->y4);
			input_report_abs(data->input, ABS_MT_WIDTH_MAJOR, 1);
			input_mt_sync(data->input);
			//printk("===x4 = %d,y4 = %d ====\n",event->x4,event->y4);
		case 3:
			input_report_abs(data->input, ABS_MT_TOUCH_MAJOR, event->pressure);
			input_report_abs(data->input, ABS_MT_POSITION_X, event->x3);
			input_report_abs(data->input, ABS_MT_POSITION_Y, event->y3);
			input_report_abs(data->input, ABS_MT_WIDTH_MAJOR, 1);
			input_mt_sync(data->input);
			//printk("===x3 = %d,y3 = %d ====\n",event->x3,event->y3);
		case 2:
			input_report_abs(data->input, ABS_MT_TOUCH_MAJOR, event->pressure);
			input_report_abs(data->input, ABS_MT_POSITION_X, event->x2);
			input_report_abs(data->input, ABS_MT_POSITION_Y, event->y2);
			input_report_abs(data->input, ABS_MT_WIDTH_MAJOR, 1);
			input_mt_sync(data->input);
			//printk("===x2 = %d,y2 = %d ====\n",event->x2,event->y2);
		case 1:
			input_report_abs(data->input, ABS_MT_TOUCH_MAJOR, event->pressure);
	        input_report_abs(data->input, ABS_MT_POSITION_X, event->x1);
			input_report_abs(data->input, ABS_MT_POSITION_Y, event->y1);
			input_report_abs(data->input, ABS_MT_WIDTH_MAJOR, 1);
			input_mt_sync(data->input);
			//("===x1 = %d,y1 = %d ====\n",event->x1,event->y1);
		default:
			//printk("==touch_point default =\n");
			break;
	}
    
	input_report_abs(data->input, ABS_PRESSURE, event->pressure);
//	input_report_key(data->input, BTN_TOUCH, 1);
#else	/* CONFIG_FT5X0X_MULTITOUCH*/
	if (event->touch_point == 1) {
		input_report_abs(data->input, ABS_X, event->x1);
		input_report_abs(data->input, ABS_Y, event->y1);
		input_report_abs(data->input, ABS_PRESSURE, event->pressure);
	}
	input_report_key(data->input, BTN_TOUCH, 1);
#endif	/* CONFIG_FT5X0X_MULTITOUCH*/
	input_sync(data->input);

//	dev_dbg(&this_client->dev, "%s: 1:%d %d 2:%d %d \n", __func__,
//		event->x1, event->y1, event->x2, event->y2);
//	printk("%s: 1:%d %d 2:%d %d \n", __func__,
//		event->x1, event->y1, event->x2, event->y2);
}	/*end ft5x0x_report_value*/

static irqreturn_t pixcir_ts_isr(int irq, void *dev_id)
{
	struct pixcir_i2c_ts_data *tsdata = dev_id;
	int ret;
	
	disable_irq_nosync(irq);

#if 0
 	while (!tsdata->exiting) {
		pixcir_ts_poscheck(tsdata);

		if (attb_read_val()) {
			PIXCIR_DBG("%s: release\n",__func__);
			input_report_key(tsdata->input, BTN_TOUCH, 0);
			input_report_abs(tsdata->input, ABS_MT_TOUCH_MAJOR, 0);
			input_sync(tsdata->input);
			break;
		}
		msleep(20);
	}
#endif
//    pixcir_ts_poscheck(tsdata);
 	//while (!tsdata->exiting) 
    {
		ret = ft5x0x_read_data(tsdata);	
		if (ret == 0) {	
			ft5x0x_report_value(tsdata);
		}

		//if (attb_read_val()) {
		//	PIXCIR_DBG("%s: release\n",__func__);
//			input_report_key(tsdata->input, BTN_TOUCH, 0);
//			input_report_abs(tsdata->input, ABS_MT_TOUCH_MAJOR, 0);
//			input_sync(tsdata->input);
		//	break;
		//}
	}
	enable_irq(irq);
	
	return IRQ_HANDLED;
}


#ifdef CONFIG_PM_SLEEP
static int pixcir_i2c_ts_suspend(struct device *dev)
{
#if 0
	struct i2c_client *client = to_i2c_client(dev);
	unsigned char wrbuf[2] = { 0 };
	int ret;

	wrbuf[0] = 0x33;
	wrbuf[1] = 0x03;	//enter into freeze mode;
	/**************************************************************
	wrbuf[1]:	0x00: Active mode
			0x01: Sleep mode
			0xA4: Sleep mode automatically switch
			0x03: Freeze mode
	More details see application note 710 power manangement section
	****************************************************************/
	ret = i2c_master_send(client, wrbuf, 2);
	if(ret!=2) {
		dev_err(&client->dev,
			"%s: i2c_master_send failed(), ret=%d\n",
			__func__, ret);
	}

	if (device_may_wakeup(&client->dev))
		enable_irq_wake(client->irq);
#endif
	return 0;
}

static int pixcir_i2c_ts_resume(struct device *dev)
{
#if 0
	struct i2c_client *client = to_i2c_client(dev);
///if suspend enter into freeze mode please reset TP
	pixcir_reset();
	if (device_may_wakeup(&client->dev))
		disable_irq_wake(client->irq);
#endif
	return 0;
}
#endif
#if 0
/***********************************************************************************************
Name	:	 ft5x0x_read_fw_ver

Input	:	 void
                     

Output	:	 firmware version 	

function	:	 read TP firmware version

***********************************************************************************************/
static unsigned char ft5x0x_read_fw_ver(void)
{
//	unsigned char ver;
//	ft5x0x_read_reg(FT5X0X_REG_FIRMID, &ver);
//	return(ver);
}

/***********************************************************************************************
Name	:	 

Input	:	
                     

Output	:	

function	:	

***********************************************************************************************/
static void ft5x0x_ts_pen_irq_work(struct work_struct *work)
{
#if 1
	int ret = -1;
//	printk("==work 1=\n");
	ret = ft5x0x_read_data();	
	if (ret == 0) {	
		ft5x0x_report_value();
	}
//	else printk("data package read error\n");
//	printk("==work 2=\n");
//    	msleep(1);
#endif
    enable_irq(this_client->irq);

}

/***********************************************************************************************
Name	:	 

Input	:	
                     

Output	:	

function	:	

***********************************************************************************************/
static irqreturn_t ft5x0x_ts_interrupt(int irq, void *dev_id)
{

	struct ft5x0x_ts_data *ft5x0x_ts = (struct ft5x0x_ts_data *)dev_id;

	disable_irq_nosync(this_client->irq);
	if (!work_pending(&ft5x0x_ts->pen_event_work)) {
		queue_work(ft5x0x_ts->ts_workqueue, &ft5x0x_ts->pen_event_work);
	}
	//printk("==int=, 11irq=%d\n", this_client->irq);
	return IRQ_HANDLED;
}

static int ft5x0x_create_sysfs(struct i2c_client *client)
{
	int err;
	struct device *dev = &(client->dev);

	PIXCIR_DBG("%s", __func__);
	
//zhucaihua TBD	err = device_create_file(dev, &dev_attr_suspend);
//	err = device_create_file(dev, &dev_attr_update);
//	err = device_create_file(dev, &dev_attr_debug);
	
	return err;
}
#endif

static SIMPLE_DEV_PM_OPS(pixcir_dev_pm_ops,
			 pixcir_i2c_ts_suspend, pixcir_i2c_ts_resume);

static int __devinit pixcir_i2c_ts_probe(struct i2c_client *client,
					 const struct i2c_device_id *id)
{
	//const struct pixcir_ts_platform_data *pdata = client->dev.platform_data;
	struct pixcir_i2c_ts_data *tsdata;
	struct input_dev *input;
	struct device *dev;
	struct i2c_dev *i2c_dev;
	int i, error;
	
	this_client = client;
	client->irq = pixcir_ts_config_pins(); //reset pin set to 0 or 1 and platform init
	
	for(i=0; i<MAX_FINGER_NUM*2; i++) {
		point_slot[i].active = 0;
	}

	tsdata = kzalloc(sizeof(*tsdata), GFP_KERNEL);
	input = input_allocate_device();
	if (!tsdata || !input) {
		dev_err(&client->dev, "Failed to allocate driver data!\n");
		error = -ENOMEM;
		goto err_free_mem;
	}
#ifdef TOUCH_VIRTUAL_KEYS
	pixcir_ts_virtual_keys_init();
#endif

	tsdata->client = client;
	tsdata->input = input;
	//tsdata->chip = pdata;
	global_irq = client->irq;

	input->name = client->name;
	input->id.bustype = BUS_I2C;
	input->dev.parent = &client->dev;

	__set_bit(EV_KEY, input->evbit);
	__set_bit(EV_ABS, input->evbit);
	__set_bit(EV_SYN, input->evbit);
	__set_bit(BTN_TOUCH, input->keybit);

	__set_bit(ABS_MT_TOUCH_MAJOR, input->absbit);
	__set_bit(ABS_MT_POSITION_X, input->absbit);
	__set_bit(ABS_MT_POSITION_Y, input->absbit);
	__set_bit(ABS_MT_WIDTH_MAJOR, input->absbit);

	__set_bit(KEY_MENU,  input->keybit);
	__set_bit(KEY_BACK,  input->keybit);
	__set_bit(KEY_HOME,  input->keybit);
	__set_bit(KEY_SEARCH,  input->keybit);
	
	input_set_abs_params(input, ABS_MT_TOUCH_MAJOR, 0, 255, 0, 0);
	input_set_abs_params(input, ABS_MT_POSITION_X, 0, X_MAX, 0, 0);
	input_set_abs_params(input, ABS_MT_POSITION_Y, 0, Y_MAX, 0, 0);
	input_set_abs_params(input, ABS_MT_WIDTH_MAJOR, 0, 200, 0, 0);


	input_set_drvdata(input, tsdata);

	error = request_threaded_irq(client->irq, NULL, pixcir_ts_isr,
				     IRQF_TRIGGER_FALLING,
				     client->name, tsdata);
	if (error) {
		dev_err(&client->dev, "Unable to request touchscreen IRQ.\n");
		goto err_free_mem;
	}
	disable_irq_nosync(client->irq);

	error = input_register_device(input);
	if (error)
		goto err_free_irq;

	i2c_set_clientdata(client, tsdata);
	device_init_wakeup(&client->dev, 1);

	/*********************************Bee-0928-TOP****************************************/
	i2c_dev = get_free_i2c_dev(client->adapter);
	if (IS_ERR(i2c_dev)) {
		error = PTR_ERR(i2c_dev);
		return error;
	}

	dev = device_create(i2c_dev_class, &client->adapter->dev, MKDEV(I2C_MAJOR,
			client->adapter->nr), NULL, "ft5206_ts%d", 0);
	if (IS_ERR(dev)) {
		error = PTR_ERR(dev);
		return error;
	}
	/*********************************Bee-0928-BOTTOM****************************************/

	pixcir_early_suspend.level = EARLY_SUSPEND_LEVEL_BLANK_SCREEN + 1;
	pixcir_early_suspend.suspend = pixcir_ts_suspend;
	pixcir_early_suspend.resume	= pixcir_ts_resume;
	register_early_suspend(&pixcir_early_suspend);

	if(pixcir_config_intmode()<0) {
#ifdef PIXCIR_DEBUG
		printk("%s: I2C error\n",__func__);
#endif
		goto err_free_irq;
	}
	pixcir_create_sysfs(client);

#ifdef PIXCIR_DEBUG
	dev_err(&tsdata->client->dev, "insmod successfully!\n");
#endif	
	enable_irq(client->irq);
	return 0;

err_free_irq:
	free_irq(client->irq, tsdata);
	sprd_free_gpio_irq(pixcir_irq);
err_free_mem:
	input_free_device(input);
	kfree(tsdata);
	return error;
}

#if 0
static int __devexit ft5x0x_ts_remove(struct i2c_client *client)
{

	struct ft5x0x_ts_data *ft5x0x_ts = i2c_get_clientdata(client);
	int error;
	struct i2c_dev *i2c_dev;


	printk("==ft5x0x_ts_remove=\n");
	
	unregister_early_suspend(&ft5x0x_ts->early_suspend);
	free_irq(client->irq, ft5x0x_ts);
	sprd_free_gpio_irq(ft5x0x_ts_setup.irq);
	input_unregister_device(ft5x0x_ts->input_dev);
	kfree(ft5x0x_ts);
	cancel_work_sync(&ft5x0x_ts->pen_event_work);
	destroy_workqueue(ft5x0x_ts->ts_workqueue);
	i2c_set_clientdata(client, NULL);

	/*********************************Bee-0928-TOP****************************************/
	i2c_dev = get_free_i2c_dev(client->adapter);
	if (IS_ERR(i2c_dev)) {
		error = PTR_ERR(i2c_dev);
		return error;
	}

	return_i2c_dev(i2c_dev);
	device_destroy(i2c_dev_class, MKDEV(I2C_MAJOR, client->adapter->nr));
	/*********************************Bee-0928-BOTTOM****************************************/
	
	return 0;
}
#endif

static int __devexit pixcir_i2c_ts_remove(struct i2c_client *client)
{
	int error;
	struct i2c_dev *i2c_dev;
	struct pixcir_i2c_ts_data *tsdata = i2c_get_clientdata(client);

	device_init_wakeup(&client->dev, 0);

	tsdata->exiting = true;
	mb();
	free_irq(client->irq, tsdata);

	/*********************************Bee-0928-TOP****************************************/
	i2c_dev = get_free_i2c_dev(client->adapter);
	if (IS_ERR(i2c_dev)) {
		error = PTR_ERR(i2c_dev);
		return error;
	}

	return_i2c_dev(i2c_dev);
	device_destroy(i2c_dev_class, MKDEV(I2C_MAJOR, client->adapter->nr));
	/*********************************Bee-0928-BOTTOM****************************************/
	unregister_early_suspend(&pixcir_early_suspend);
	sprd_free_gpio_irq(pixcir_irq);
	input_unregister_device(tsdata->input);
	kfree(tsdata);

	return 0;
}

/*************************************Bee-0928****************************************/
/*                        	     pixcir_open                                     */
/*************************************Bee-0928****************************************/
static int pixcir_open(struct inode *inode, struct file *file)
{
	int subminor;
	struct i2c_client *client;
	struct i2c_adapter *adapter;
	struct i2c_dev *i2c_dev;
	int ret = 0;
	PIXCIR_DBG("enter pixcir_open function\n");

	subminor = iminor(inode);

	lock_kernel();
	i2c_dev = i2c_dev_get_by_minor(subminor);
	if (!i2c_dev) {
#ifdef PIXCIR_DEBUG
		printk("error i2c_dev\n");
#endif
		return -ENODEV;
	}

	adapter = i2c_get_adapter(i2c_dev->adap->nr);
	if (!adapter) {
		return -ENODEV;
	}
	
	client = kzalloc(sizeof(*client), GFP_KERNEL);
	if (!client) {
		i2c_put_adapter(adapter);
		ret = -ENOMEM;
	}

	snprintf(client->name, I2C_NAME_SIZE, "ft5206_ts%d", adapter->nr);
	client->driver = &pixcir_i2c_ts_driver;
	client->adapter = adapter;
	
	file->private_data = client;

	return 0;
}

/*************************************Bee-0928****************************************/
/*                        	     pixcir_ioctl                                    */
/*************************************Bee-0928****************************************/
static long pixcir_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct i2c_client *client = (struct i2c_client *) file->private_data;

	PIXCIR_DBG("pixcir_ioctl(),cmd = %d,arg = %ld\n", cmd, arg);


	switch (cmd)
	{
	case CALIBRATION_FLAG:	//CALIBRATION_FLAG = 1
		client->addr = SLAVE_ADDR;
		status_reg = CALIBRATION_FLAG;
		break;

	case BOOTLOADER:	//BOOTLOADER = 7
		client->addr = BOOTLOADER_ADDR;
		status_reg = BOOTLOADER;

		pixcir_reset();
		mdelay(5);
		break;

	case RESET_TP:		//RESET_TP = 9
		pixcir_reset();
		break;
		
	case ENABLE_IRQ:	//ENABLE_IRQ = 10
		status_reg = 0;
		enable_irq(global_irq);
		break;
		
	case DISABLE_IRQ:	//DISABLE_IRQ = 11
		disable_irq_nosync(global_irq);
		break;

	case BOOTLOADER_STU:	//BOOTLOADER_STU = 12
		client->addr = BOOTLOADER_ADDR;
		status_reg = BOOTLOADER_STU;

		pixcir_reset();
		mdelay(5);

	case ATTB_VALUE:	//ATTB_VALUE = 13
		client->addr = SLAVE_ADDR;
		status_reg = ATTB_VALUE;
		break;

	default:
		client->addr = SLAVE_ADDR;
		status_reg = 0;
		break;
	}
	return 0;
}

/***********************************Bee-0928****************************************/
/*                        	  pixcir_read                                      */
/***********************************Bee-0928****************************************/
static ssize_t pixcir_read (struct file *file, char __user *buf, size_t count,loff_t *offset)
{
	struct i2c_client *client = (struct i2c_client *)file->private_data;
	unsigned char *tmp, bootloader_stu[4], attb_value[1];
	int ret = 0;

	switch(status_reg)
	{
	case BOOTLOADER_STU:
#if 0
		i2c_master_recv(client, bootloader_stu, sizeof(bootloader_stu));
		if (ret!=sizeof(bootloader_stu)) {
			dev_err(&client->dev,
				"%s: BOOTLOADER_STU: i2c_master_recv() failed, ret=%d\n",
				__func__, ret);
			return -EFAULT;
		}

		ret = copy_to_user(buf, bootloader_stu, sizeof(bootloader_stu));
		if(ret)	{
			dev_err(&client->dev,
				"%s: BOOTLOADER_STU: copy_to_user() failed.\n",	__func__);
			return -EFAULT;
		}else {
			ret = 4;
		}
#endif
		break;

	case ATTB_VALUE:
		attb_value[0] = attb_read_val();
		if(copy_to_user(buf, attb_value, sizeof(attb_value))) {
			dev_err(&client->dev,
				"%s: ATTB_VALUE: copy_to_user() failed.\n", __func__);
			return -EFAULT;
		}else {
			ret = 1;
		}
		break;

	default:
		tmp = kmalloc(count,GFP_KERNEL);
		if (tmp==NULL)
			return -ENOMEM;

		ret = i2c_master_recv(client, tmp, count);
		if (ret != count) {
			dev_err(&client->dev,
				"%s: default: i2c_master_recv() failed, ret=%d\n",
				__func__, ret);
			return -EFAULT;
		}

		if(copy_to_user(buf, tmp, count)) {
			dev_err(&client->dev,
				"%s: default: copy_to_user() failed.\n", __func__);
			kfree(tmp);
			return -EFAULT;
		}
		kfree(tmp);
		break;
	}
	return ret;
}

/***********************************Bee-0928****************************************/
/*                        	  pixcir_write                                     */
/***********************************Bee-0928****************************************/
static ssize_t pixcir_write(struct file *file,const char __user *buf,size_t count, loff_t *ppos)
{
	struct i2c_client *client;
	unsigned char *tmp, bootload_data[143];
	int ret=0, i=0;

	client = file->private_data;

	switch(status_reg)
	{
	case CALIBRATION_FLAG:	//CALIBRATION_FLAG=1
#if 0
		tmp = kmalloc(count,GFP_KERNEL);
		if (tmp==NULL)
			return -ENOMEM;

		if (copy_from_user(tmp,buf,count)) { 	
			dev_err(&client->dev,
				"%s: CALIBRATION_FLAG: copy_from_user() failed.\n", __func__);
			kfree(tmp);
			return -EFAULT;
		}

		ret = i2c_master_send(client,tmp,count);
		if (ret!=count ) {
			dev_err(&client->dev,
				"%s: CALIBRATION: i2c_master_send() failed, ret=%d\n",
				__func__, ret);
			kfree(tmp);
			return -EFAULT;
		}

		while(!attb_read_val()) {
			msleep(100);
			i++;
			if(i>99)
				break;  //10s no high aatb break
		}	//waiting to finish the calibration.(pixcir application_note_710_v3 p43)

		kfree(tmp);
#endif
		break;

	case BOOTLOADER:
#if 0
		memset(bootload_data, 0, sizeof(bootload_data));

		if (copy_from_user(bootload_data, buf, count)) {
			dev_err(&client->dev,
				"%s: BOOTLOADER: copy_from_user() failed.\n", __func__);
			return -EFAULT;
		}

		ret = i2c_master_send(client, bootload_data, count);
		if(ret!=count) {
			dev_err(&client->dev,
				"%s: BOOTLOADER: i2c_master_send() failed, ret = %d\n",
				__func__, ret);
			return -EFAULT;
		}
#endif
		break;

	default:
		tmp = kmalloc(count,GFP_KERNEL);
		if (tmp==NULL)
			return -ENOMEM;

		if (copy_from_user(tmp,buf,count)) { 	
			dev_err(&client->dev,
				"%s: default: copy_from_user() failed.\n", __func__);
			kfree(tmp);
			return -EFAULT;
		}
		
		ret = i2c_master_send(client,tmp,count);
		if (ret!=count ) {
			dev_err(&client->dev,
				"%s: default: i2c_master_send() failed, ret=%d\n",
				__func__, ret);
			kfree(tmp);
			return -EFAULT;
		}
		kfree(tmp);
		break;
	}
	return ret;
}

/***********************************Bee-0928****************************************/
/*                        	  pixcir_release                                   */
/***********************************Bee-0928****************************************/
static int pixcir_release(struct inode *inode, struct file *file)
{
	struct i2c_client *client = file->private_data;

	PIXCIR_DBG("enter pixcir_release funtion\n");

	i2c_put_adapter(client->adapter);
	kfree(client);
	file->private_data = NULL;

	return 0;
}

/*********************************Bee-0928-TOP****************************************/
static const struct file_operations pixcir_i2c_ts_fops =
{	.owner		= THIS_MODULE,
	.read		= pixcir_read,
	.write		= pixcir_write,
	.open		= pixcir_open,
	.unlocked_ioctl = pixcir_ioctl,
	.release	= pixcir_release,
};
/*********************************Bee-0928-BOTTOM****************************************/


static const struct i2c_device_id pixcir_i2c_ts_id[] = {
	{ "ft5206_ts", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, pixcir_i2c_ts_id);

static struct i2c_driver pixcir_i2c_ts_driver = {
	.driver = {
		.owner	= THIS_MODULE,
		.name	= "ft5206_ts",
		//.pm	= &pixcir_dev_pm_ops,
	},
	.probe		= pixcir_i2c_ts_probe,
	.remove		= __devexit_p(pixcir_i2c_ts_remove),
	.id_table	= pixcir_i2c_ts_id,
};

int sprd_add_i2c_device(struct sprd_i2c_setup_data *i2c_set_data, struct i2c_driver *driver)
{
	struct i2c_board_info info;
	struct i2c_adapter *adapter;
	struct i2c_client *client;
	int ret,err;


	PIXCIR_DBG("%s : i2c_bus=%d; slave_address=0x%x; i2c_name=%s",__func__,i2c_set_data->i2c_bus, \
		    i2c_set_data->i2c_address, i2c_set_data->type);

	memset(&info, 0, sizeof(struct i2c_board_info));
	info.addr = i2c_set_data->i2c_address;
	strlcpy(info.type, i2c_set_data->type, I2C_NAME_SIZE);
	if(i2c_set_data->irq > 0)
		info.irq = i2c_set_data->irq;

	adapter = i2c_get_adapter( i2c_set_data->i2c_bus);
	if (!adapter) {
#ifdef PIXCIR_DEBUG
		printk("%s: can't get i2c adapter %d\n",
			__func__,  i2c_set_data->i2c_bus);
#endif
		err = -ENODEV;
		goto err_driver;
	}

	client = i2c_new_device(adapter, &info);
	if (!client) {
#ifdef PIXCIR_DEBUG
		printk("%s:  can't add i2c device at 0x%x\n",
			__func__, (unsigned int)info.addr);
#endif
		err = -ENODEV;
		goto err_driver;
	}

	i2c_put_adapter(adapter);

	ret = i2c_add_driver(driver);
	if (ret != 0) {
#ifdef PIXCIR_DEBUG
		printk("%s: can't add i2c driver\n", __func__);
#endif
		err = -ENODEV;
		goto err_driver;
	}	

	return 0;

err_driver:
	return err;
}

void sprd_del_i2c_device(struct i2c_client *client, struct i2c_driver *driver)
{
	PIXCIR_DBG("%s : slave_address=0x%x; i2c_name=%s",__func__, client->addr, client->name);
	i2c_unregister_device(client);
	i2c_del_driver(driver);
}


static int __init pixcir_i2c_ts_init(void)
{
	int ret;
	//int ft5x0x_irq;

#if 1
	/*********************************Bee-0928-TOP****************************************/
	ret = register_chrdev(I2C_MAJOR,"ft5206_ts",&pixcir_i2c_ts_fops);
	if (ret) {
#ifdef PIXCIR_DEBUG
	    printk(KERN_ERR "%s:register chrdev failed\n",__FILE__);
#endif
		return ret;
	}

	i2c_dev_class = class_create(THIS_MODULE, "pixcir_i2c_dev");
	if (IS_ERR(i2c_dev_class)) {
		ret = PTR_ERR(i2c_dev_class);
		class_destroy(i2c_dev_class);
	}
	/********************************Bee-0928-BOTTOM******************************************/
#endif

	
#ifdef PIXCIR_DEBUG
	printk("%s\n", __func__);
#endif

	//ft5x0x_irq=pixcir_ts_config_pins();
	//ft5x0x_ts_setup.i2c_bus = 2;
	//ft5x0x_ts_setup.i2c_address = FT5206_TS_ADDR;
	//strcpy (ft5x0x_ts_setup.type,FT5206_TS_NAME);
	//ft5x0x_ts_setup.irq = ft5x0x_irq;
	return i2c_add_driver(&pixcir_i2c_ts_driver);
}

module_init(pixcir_i2c_ts_init);

static void __exit pixcir_i2c_ts_exit(void)
{

	i2c_del_driver(&pixcir_i2c_ts_driver);
	/********************************Bee-0928-TOP******************************************/
#if 1
	class_destroy(i2c_dev_class);
	unregister_chrdev(I2C_MAJOR,"ft5206_ts");
#endif
	/********************************Bee-0928-BOTTOM******************************************/
}
module_exit(pixcir_i2c_ts_exit);

MODULE_AUTHOR("<wenfs@Focaltech-systems.com>");
MODULE_DESCRIPTION("FocalTech ft5x0x TouchScreen driver");
MODULE_LICENSE("GPL");
