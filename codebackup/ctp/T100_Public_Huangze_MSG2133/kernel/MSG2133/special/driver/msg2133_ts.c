/* 
 * code/3rdparty/tp/MSG2133/special/driver/msg2133_ts.c
 *
 * msg2133_ts TouchScreen driver. 
 *
 * Copyright (c) 2012 05 03
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
 *    1.0		  2012-05-03			RGK
 *
 * note: only support mulititouch	RGK 2012-05-03
 */

#include <linux/delay.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/input.h>
#include <asm/uaccess.h>
#include <mach/ldo.h>
#include <linux/gpio.h>
#include <linux/smp_lock.h>
#include <linux/earlysuspend.h>
#include <linux/platform_device.h>

#include "msg2133_ts.h"

#define MSG2133_DEBUG		1

#define CONFIG_MSG2133_MULTITOUCH	

#ifdef MSG2133_DEBUG
#define MSG2133_DBG(format, ...)	\
		printk(KERN_INFO "MSG2133_TS " format "\n", ## __VA_ARGS__)
#else
#define MSG2133_DBG(format, ...)
#endif


#define SLAVE_ADDR		    0x70
#define BOOTLOADER_ADDR		0x71

#ifndef I2C_MAJOR
#define I2C_MAJOR 		125  //zhucaihua ???
#endif

#define I2C_MINORS 		256

#define CALIBRATION_FLAG	1
#define BOOTLOADER		7
#define RESET_TP		9

#define ENABLE_IRQ		10
#define DISABLE_IRQ		11
#define BOOTLOADER_STU		16
#define ATTB_VALUE		17

#define MAX_FINGER_NUM		5
#define X_OFFSET		30
#define Y_OFFSET		40

struct i2c_dev
{
	struct list_head list;
	struct i2c_adapter *adap;
	struct device *dev;
};

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

struct msg2133_ts_data {
	struct msg2133_ts_platform_data *pdata;
	struct input_dev	*input_dev;
	struct ts_event		event;
	struct work_struct 	pen_event_work;
	struct workqueue_struct *ts_workqueue;
	struct early_suspend	early_suspend;
};

struct sprd_i2c_setup_data {
	unsigned i2c_bus;  //the same number as i2c->adap.nr in adapter probe function
	unsigned short i2c_address;
	int irq;
	char type[I2C_NAME_SIZE];
};

struct msg2133_i2c_ts_data {
	struct i2c_client *client;
	struct input_dev *input;
	struct ts_event		event;
	bool exiting;
};

struct point_node_t{
	unsigned char 	active ;
	unsigned char	finger_id;
	int	posx;
	int	posy;
};

static int tsp_keycodes_q100[CFG_NUMOFKEYS] ={
	 KEY_MENU,
	 KEY_BACK,
	 KEY_HOME,
	 KEY_SEARCH
};

extern int sprd_3rdparty_gpio_tp_rst ;
extern int sprd_3rdparty_gpio_tp_irq ;

static unsigned char status_reg = 0;
int global_irq;
static  int key_id = 0,kave_VK = 0;
static REPORT_FINGER_INFO_T _st_finger_infos[CFG_MAX_POINT_NUM];
static  unsigned short msg2133_fm_major=0, msg2133_fm_minor=0;

static struct i2c_driver msg2133_i2c_ts_driver;
static struct class *i2c_dev_class;
static LIST_HEAD( i2c_dev_list);
static DEFINE_SPINLOCK( i2c_dev_list_lock);

static struct i2c_client *this_client;
static int msg2133_irq;
static int suspend_flag;
static struct early_suspend	msg2133_early_suspend;

static struct point_node_t point_slot[MAX_FINGER_NUM*2];

static ssize_t msg2133_set_calibrate(struct device* cd, struct device_attribute *attr,
		       const char* buf, size_t len);
static ssize_t msg2133_show_suspend(struct device* cd,struct device_attribute *attr, char* buf);
static ssize_t msg2133_store_suspend(struct device* cd, struct device_attribute *attr,const char* buf, size_t len);

static void msg2133_reset(void);
static void msg2133_ts_suspend(struct early_suspend *handler);
static void msg2133_ts_resume(struct early_suspend *handler);
static void msg2133_ts_pwron(void);
static void msg2133_ts_pwroff(void);


/*
static int msg2133_i2c_rxdata(char *rxdata, int length)
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
        {
            printk("%s i2c read error: %d\n", __func__, ret);
            pr_err("%s i2c read error: %d\n", __func__, ret);
        }
        
        return ret;
}
*/

static int msg2133_i2c_rxdata(char *rxdata, int length)
{
    int ret;
    struct i2c_msg msgs;
    
    msgs.addr   = this_client->addr;
    msgs.flags  = I2C_M_RD;
    msgs.len    = length;
    msgs.buf    = rxdata;
   
    ret = i2c_transfer(this_client->adapter, &msgs,1);
    if (ret < 0)
    {
        printk("%s i2c read error: %d\n", __func__, ret);
        pr_err("%s i2c read error: %d\n", __func__, ret);
    }
    return ret;
}

static int msg2133_i2c_txdata(char *txdata, int length)
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
		printk("==%s==ret = %d\n",__func__ ,ret);
		if (ret < 0)
			pr_err("%s i2c write error: %d\n", __func__, ret);

		return ret;
}

static int msg2133_i2c_write_data(unsigned char addr, unsigned char data)
{
	unsigned char buf[2];
	buf[0]=addr;
	buf[1]=data;
	return msg2133_i2c_txdata(buf, 2); 
}

static DEVICE_ATTR(calibrate, S_IRUGO | S_IWUSR, NULL, msg2133_set_calibrate);
static DEVICE_ATTR(suspend, S_IRUGO | S_IWUSR, msg2133_show_suspend, msg2133_store_suspend);

static ssize_t msg2133_set_calibrate(struct device* cd, struct device_attribute *attr,
		       const char* buf, size_t len)
{
	unsigned long on_off = simple_strtoul(buf, NULL, 10);
	
	if(on_off==1)
	{
#ifdef MSG2133_DEBUG
		printk("%s: PIXCIR calibrate\n",__func__);
#endif
		msg2133_i2c_write_data(0x3a , 0x03);
		msleep(5*1000);
	}
	
	return len;
}

static void msg2133_ts_pwroff(void)
{
    msg2133_i2c_write_data(0xA5, 0x03);

    MSG2133_DBG("%s",__func__);

    LDO_TurnOffLDO(LDO_LDO_SIM2);
}

static void msg2133_ts_suspend(struct early_suspend *handler)
{
#ifdef MSG2133_DEBUG
	printk("==start %s==\n", __func__);
#endif
   	msg2133_ts_pwroff();
}

static ssize_t msg2133_show_suspend(struct device* cd,
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

static ssize_t msg2133_store_suspend(struct device* cd, struct device_attribute *attr,
		       const char* buf, size_t len)
{
	unsigned long on_off = simple_strtoul(buf, NULL, 10);
	suspend_flag = on_off;
	
	if(on_off==1)
	{
#ifdef MSG2133_DEBUG
		printk("Pixcir Entry Suspend\n");
#endif
		msg2133_ts_suspend(NULL);
	}
	else
	{
#ifdef MSG2133_DEBUG
		printk("Pixcir Entry Resume\n");
#endif
		msg2133_ts_resume(NULL);
	}
	
	return len;
}

static int msg2133_create_sysfs(struct i2c_client *client)
{
	int err;
	struct device *dev = &(client->dev);

#ifdef MSG2133_DEBUG
	MSG2133_DBG("%s\n", __func__);
#endif	
	err = device_create_file(dev, &dev_attr_calibrate);
	err = device_create_file(dev, &dev_attr_suspend);

	return err;
}

static void msg2133_ts_resume(struct early_suspend *handler)
{	
#ifdef MSG2133_DEBUG
	printk("==%s==\n", __func__);
#endif
	msg2133_ts_pwron();
	msg2133_reset();
}

#ifdef TOUCH_VIRTUAL_KEYS
#define SC8810_KEY_HOME	102
#define SC8810_KEY_MENU	30
#define SC8810_KEY_BACK	17
#define SC8810_KEY_SEARCH  217

static ssize_t virtual_keys_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	return sprintf(buf,
	     __stringify(EV_KEY) ":" __stringify(KEY_HOME)   ":0:500:80:100"
	 ":" __stringify(EV_KEY) ":" __stringify(KEY_MENU)   ":100:500:80:100"
	 ":" __stringify(EV_KEY) ":" __stringify(KEY_BACK)   ":200:500:80:100"
	 ":" __stringify(EV_KEY) ":" __stringify(KEY_SEARCH) ":300:500:80:100"
	 "\n");
}

static struct kobj_attribute virtual_keys_attr = {
    .attr = {
        .name = "virtualkeys.msg2133_ts", 
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

static void msg2133_ts_virtual_keys_init(void)
{
    int ret;
    struct kobject *properties_kobj;
	
    MSG2133_DBG("%s\n",__func__);
	
    properties_kobj = kobject_create_and_add("board_properties", NULL);
    if (properties_kobj)
        MSG2133_DBG("%s  properties_kobj\n",__func__);
        ret = sysfs_create_group(properties_kobj,
                     &properties_attr_group);
    if (!properties_kobj || ret)
        pr_err("failed to create board_properties\n");    
}
#endif

static void msg2133_ts_pwron(void)
{
    MSG2133_DBG("%s",__func__);

    LDO_SetVoltLevel(LDO_LDO_SIM2, LDO_VOLT_LEVEL0);
    LDO_TurnOnLDO(LDO_LDO_SIM2);
    msleep(20);
}

static int  msg2133_ts_config_pins(void)
{
	MSG2133_DBG("==msg2133_pixcir_ts_config_pins1==\n");
	msg2133_ts_pwron();
	MSG2133_DBG("==msg2133_pixcir_ts_config_pins2==\n");
	gpio_direction_input(sprd_3rdparty_gpio_tp_irq);	
	msg2133_irq=sprd_alloc_gpio_irq(sprd_3rdparty_gpio_tp_irq);
	msg2133_reset();
	MSG2133_DBG("==msg2133_pixcir_ts_config_pins2==msg2133_irq=%d\n",msg2133_irq);
	return msg2133_irq;
}

static int attb_read_val(void)
{
    return gpio_get_value(sprd_3rdparty_gpio_tp_irq);
}

static void msg2133_reset(void)
{
    MSG2133_DBG("==msg2133_pixcir_reset==\n");
    MSG2133_DBG("%s\n",__func__);
    gpio_direction_output(sprd_3rdparty_gpio_tp_rst, 1);
    msleep(3);
    gpio_set_value(sprd_3rdparty_gpio_tp_rst, 0);
    msleep(10);
    gpio_set_value(sprd_3rdparty_gpio_tp_rst,1);
    msleep(10);
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
#ifdef MSG2133_DEBUG
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

unsigned char tpd_check_sum(unsigned char *pval)
{
    int i, sum = 0;

    for(i = 0; i < 7; i++)
    {
        sum += pval[i];
    }

    return (unsigned char)((-sum) & 0xFF);
}

static int msg2133_read_data_wb_custom(void)
{
    unsigned char buf[12] = {0},i=0;
    unsigned int temp_x0 = 0, temp_y0 = 0, temp_x1 = 0,temp_y1 = 0;
    int temp_x1_dst = 0, temp_y1_dst = 0;
    int temp;
    int ret = -1;
    int Touch_State = 0;
    struct msg2133_ts_data *data = i2c_get_clientdata(this_client);

    ret = msg2133_i2c_rxdata(buf, 12);

    printk("\n");
    printk("\n#####line = %d, func = %s ret = %d#####\n", __LINE__, __func__,ret);
    if (ret < 0) {
        printk("%s read_data i2c_rxdata failed: %d\n", __func__, ret);
        return ret;
    }


  	if (ret > 0)
    	{
		//Judge Touch State
		if(buf[0] == 0x00) //Check the data if valid
		{ 
			return 0;
		}
		else if((buf[0]>>6) == 0x01)//Scan finger number on panel
		{
			if((buf[0] & 0x03) == 0x01)
			{
				Touch_State = Touch_State_One_Finger;
			}
			else if((buf[0] & 0x03) == 0x02)
			{
				Touch_State = Touch_State_Two_Finger;	
			}
			else if((buf[0] & 0x03) == 0x0)
			{
				Touch_State = Touch_State_No_Touch;
			}
			
			temp_x0 = ((buf[1] & 0xF0) << 4) | buf[2];
			temp_y0 = ((buf[1] & 0x0F) << 8) | buf[3];
			temp_x1 = ((buf[4] & 0xF0) << 4) | buf[5];
			temp_y1 = ((buf[4] & 0x0F) <<8 ) | buf[6];
			
		}
		else if((buf[0] >> 6) == 0x02)//VK 
		{
			Touch_State =  Touch_State_VK;
			key_id = buf[0] & 0x1F;
			temp_x0 = ((buf[1] & 0xF0) << 4) | buf[2];
			temp_y0 = ((buf[1] & 0x0F) << 8) | buf[3];
			temp_x1 = ((buf[4] & 0xF0) << 4) | buf[5];
			temp_y1 = ((buf[4] & 0x0F) <<8 ) | buf[6];
		}
			
	}

		if(Touch_State == Touch_State_One_Finger)
		{
			
			/*Mapping CNT touch coordinate to Android coordinate*/
		      _st_finger_infos[0].i2_x = (temp_x0*SCREEN_MAX_X) / CNT_RESOLUTION_X ;
		      _st_finger_infos[0].i2_y = (temp_y0*SCREEN_MAX_Y) / CNT_RESOLUTION_Y ;
			printk("####x = %d, y = %d######\n",_st_finger_infos[0].i2_x, _st_finger_infos[0].i2_y);

			input_report_abs(data->input_dev, ABS_MT_TOUCH_MAJOR, 1);        	
			input_report_abs(data->input_dev, ABS_MT_POSITION_X, _st_finger_infos[0].i2_x);
			input_report_abs(data->input_dev, ABS_MT_POSITION_Y, _st_finger_infos[0].i2_y);
			input_mt_sync(data->input_dev);	
			input_sync(data->input_dev);
			
		}        
		else if(Touch_State == Touch_State_Two_Finger)
		{
			printk("\n++++++++Touch_State == Touch_State_Two_Finger \n++++++++\n");
			/*Mapping CNT touch coordinate to Android coordinate*/
		      _st_finger_infos[0].i2_x = (temp_x0*SCREEN_MAX_X) / CNT_RESOLUTION_X ;
		      _st_finger_infos[0].i2_y = (temp_y0*SCREEN_MAX_Y) / CNT_RESOLUTION_Y ;

		      _st_finger_infos[1].i2_x = (temp_x1*SCREEN_MAX_X) / CNT_RESOLUTION_X ;
		      _st_finger_infos[1].i2_y = (temp_y1*SCREEN_MAX_Y) / CNT_RESOLUTION_Y ;


			/*report first point*/
			input_report_abs(data->input_dev, ABS_MT_TOUCH_MAJOR, 1);
			input_report_abs(data->input_dev, ABS_MT_POSITION_X, _st_finger_infos[0].i2_x);
			input_report_abs(data->input_dev, ABS_MT_POSITION_Y, _st_finger_infos[0].i2_y);
			input_mt_sync(data->input_dev);
			/*report second point*/
			input_report_abs(data->input_dev, ABS_MT_TOUCH_MAJOR, 1);
			input_report_abs(data->input_dev, ABS_MT_POSITION_X, _st_finger_infos[1].i2_x);
			input_report_abs(data->input_dev, ABS_MT_POSITION_Y, _st_finger_infos[1].i2_y);
			input_mt_sync(data->input_dev);

			input_sync(data->input_dev);
			
		
		}
		else if(Touch_State == Touch_State_VK)
		{
			/*Mapping CNT touch coordinate to Android coordinate*/
		      _st_finger_infos[0].i2_x = (temp_x0*SCREEN_MAX_X) / CNT_RESOLUTION_X ;
		      _st_finger_infos[0].i2_y = (temp_y0*SCREEN_MAX_Y) / CNT_RESOLUTION_Y ;

			printk("####x = %d, y = %d######\n",_st_finger_infos[0].i2_x, _st_finger_infos[0].i2_y);

			/*you can implement your VK releated function here*/	
			if(key_id == 0x01)
			{
				input_report_key(data->input_dev, tsp_keycodes_q100[2], 1);
			}
			else if(key_id == 0x02)
			{
				input_report_key(data->input_dev, tsp_keycodes_q100[0], 1);
			}
			else if(key_id == 0x04)
			{
				input_report_key(data->input_dev, tsp_keycodes_q100[1], 1);
			}
			else if(key_id == 0x08)
			{
				input_report_key(data->input_dev, tsp_keycodes_q100[3], 1);
			}
			input_report_key(data->input_dev, BTN_TOUCH, 1);
			kave_VK=1;	
			
		}
		else/*Finger up*/
		{
			printk("\n++++++++Touch_State == Finger up \n++++++++\n");
			if(kave_VK==1)
			{
				
				printk("\n++++++++Touch_State == Finger up kave_VK==1\n++++++++\n");
				if(key_id == 0x01)
				{
					input_report_key(data->input_dev, tsp_keycodes_q100[2], 0);
				}
				else if(key_id == 0x02)
				{
					input_report_key(data->input_dev, tsp_keycodes_q100[0], 0);
				}
				else if(key_id == 0x04)
				{
					input_report_key(data->input_dev, tsp_keycodes_q100[1], 0);
				}
				else if(key_id == 0x08)
				{
					input_report_key(data->input_dev, tsp_keycodes_q100[3], 0);
				}
				input_report_key(data->input_dev, BTN_TOUCH, 0);
				kave_VK=0;
			}
			input_report_abs(data->input_dev, ABS_MT_TOUCH_MAJOR, 0);
			input_mt_sync(data->input_dev);
			input_sync(data->input_dev);

		}

	return 0;  
    

}

static void msg2133_report_value(struct msg2133_i2c_ts_data *data)
{
	struct ts_event *event = &data->event;

#ifdef CONFIG_MSG2133_MULTITOUCH
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
#else	/* CONFIG_MSG2133_MULTITOUCH*/
	if (event->touch_point == 1) {
		input_report_abs(data->input, ABS_X, event->x1);
		input_report_abs(data->input, ABS_Y, event->y1);
		input_report_abs(data->input, ABS_PRESSURE, event->pressure);
	}
	input_report_key(data->input, BTN_TOUCH, 1);
#endif	/* CONFIG_MSG2133_MULTITOUCH*/
	input_sync(data->input);

//	dev_dbg(&this_client->dev, "%s: 1:%d %d 2:%d %d \n", __func__,
//		event->x1, event->y1, event->x2, event->y2);
//	printk("%s: 1:%d %d 2:%d %d \n", __func__,
//		event->x1, event->y1, event->x2, event->y2);
}	/*end msg2133_report_value*/

void   msg2133_read_fw_ver_hz_custom(void)
{	
    unsigned char buf[12] = {0};	
    int ret = -1;	
    int i;	    	

    ret = msg2133_i2c_rxdata(buf, 12);    	
    
    if (ret < 0) 
    {		
        printk("%s read_data i2c_rxdata failed: %d\n", __func__, ret);
    }	

    printk("msg2133_read_fw_ver_hz_custom++");

    printk("\n");
    msg2133_fm_major = buf[10];	
    msg2133_fm_minor = buf[11];	
    printk("***major = %d ***\n", msg2133_fm_major);     
    printk("***minor = %d ***\n", msg2133_fm_minor);
}

static irqreturn_t msg2133_ts_isr(int irq, void *dev_id)
{
    struct msg2133_i2c_ts_data *tsdata = dev_id;
    int ret;
    int try_times = 0;
	
    disable_irq_nosync(irq);

    printk("msg2133_ts_isr===\n");

    ret = msg2133_read_data_wb_custom();

    printk("%s: end ===msg2133_ts_isr===  ret=%d\n",__func__,ret);	

    enable_irq(irq);

    return IRQ_HANDLED;
}

#ifdef CONFIG_PM_SLEEP
static int msg2133_i2c_ts_suspend(struct device *dev)
{
	return 0;
}

static int msg2133_i2c_ts_resume(struct device *dev)
{
	return 0;
}
#endif

static SIMPLE_DEV_PM_OPS(pixcir_dev_pm_ops,
			 msg2133_i2c_ts_suspend, msg2133_i2c_ts_resume);

static int __devinit msg2133_i2c_ts_probe(struct i2c_client *client,
					 const struct i2c_device_id *id)
{
    struct msg2133_i2c_ts_data *tsdata;
    struct input_dev *input;
    struct device *dev;
    struct i2c_dev *i2c_dev;
    int i, error;

    printk("==msg2133_ts_probe=\n");
    printk("msg2133 client adr = %x\n", client->addr);
    this_client = client;
    client->irq = msg2133_ts_config_pins(); //reset pin set to 0 or 1 and platform init

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
	msg2133_ts_virtual_keys_init();
#endif

	tsdata->client = client;
	tsdata->input = input;
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

	error = request_threaded_irq(client->irq, NULL, msg2133_ts_isr,
				     IRQF_TRIGGER_FALLING,
				     client->name, tsdata);
	if (error) {
		dev_err(&client->dev, "Unable to request touchscreen IRQ.\n");
		goto err_free_mem;
	}
	disable_irq_nosync(client->irq);

	error = input_register_device(input);
	printk("error");
	if (error)
		goto err_free_irq;

	i2c_set_clientdata(client, tsdata);
	device_init_wakeup(&client->dev, 1);

	i2c_dev = get_free_i2c_dev(client->adapter);
	if (IS_ERR(i2c_dev)) {
		error = PTR_ERR(i2c_dev);
		return error;
	}

	dev = device_create(i2c_dev_class, &client->adapter->dev, MKDEV(I2C_MAJOR,
			client->adapter->nr), NULL, "msg2133_ts%d", 0);
	if (IS_ERR(dev)) {
		error = PTR_ERR(dev);
		return error;
	}

	msg2133_early_suspend.level = EARLY_SUSPEND_LEVEL_BLANK_SCREEN + 1;
	msg2133_early_suspend.suspend = msg2133_ts_suspend;
	msg2133_early_suspend.resume	= msg2133_ts_resume;
	register_early_suspend(&msg2133_early_suspend);

	msg2133_create_sysfs(client);

#ifdef MSG2133_DEBUG
	dev_err(&tsdata->client->dev, "insmod successfully!\n");
#endif	
	enable_irq(client->irq);
	return 0;

err_free_irq:
	free_irq(client->irq, tsdata);
	sprd_free_gpio_irq(msg2133_irq);
err_free_mem:
	input_free_device(input);
	kfree(tsdata);
	return error;
}

static int __devexit msg2133_i2c_ts_remove(struct i2c_client *client)
{
	int error;
	struct i2c_dev *i2c_dev;
	struct msg2133_i2c_ts_data *tsdata = i2c_get_clientdata(client);

	device_init_wakeup(&client->dev, 0);

	tsdata->exiting = true;
	mb();
	free_irq(client->irq, tsdata);

	i2c_dev = get_free_i2c_dev(client->adapter);
	if (IS_ERR(i2c_dev)) {
		error = PTR_ERR(i2c_dev);
		return error;
	}

	return_i2c_dev(i2c_dev);
	device_destroy(i2c_dev_class, MKDEV(I2C_MAJOR, client->adapter->nr));

	unregister_early_suspend(&msg2133_early_suspend);
	sprd_free_gpio_irq(msg2133_irq);
	input_unregister_device(tsdata->input);
	kfree(tsdata);

	return 0;
}

static int msg2133_open(struct inode *inode, struct file *file)
{
	int subminor;
	struct i2c_client *client;
	struct i2c_adapter *adapter;
	struct i2c_dev *i2c_dev;
	int ret = 0;
	MSG2133_DBG("enter msg2133_open function\n");

	subminor = iminor(inode);

	lock_kernel();
	i2c_dev = i2c_dev_get_by_minor(subminor);
	if (!i2c_dev) {
#ifdef MSG2133_DEBUG
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

	snprintf(client->name, I2C_NAME_SIZE, "msg2133_ts%d", adapter->nr);
	client->driver = &msg2133_i2c_ts_driver;
	client->adapter = adapter;
	
	file->private_data = client;

	return 0;
}

static long msg2133_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct i2c_client *client = (struct i2c_client *) file->private_data;

	MSG2133_DBG("msg2133_ioctl(),cmd = %d,arg = %ld\n", cmd, arg);
	
	printk("pppppixcir_ioctl(),cmd = %d,arg = %ld\n", cmd, arg);

	switch (cmd)
	{
	case CALIBRATION_FLAG:	//CALIBRATION_FLAG = 1
		client->addr = SLAVE_ADDR;
		status_reg = CALIBRATION_FLAG;
		break;

	case BOOTLOADER:	//BOOTLOADER = 7
		client->addr = BOOTLOADER_ADDR;
		status_reg = BOOTLOADER;

		msg2133_reset();
		mdelay(5);
		break;

	case RESET_TP:		//RESET_TP = 9
		msg2133_reset();
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

		msg2133_reset();
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

static ssize_t msg2133_read (struct file *file, char __user *buf, size_t count,loff_t *offset)
{
	struct i2c_client *client = (struct i2c_client *)file->private_data;
	unsigned char *tmp, bootloader_stu[4], attb_value[1];
	int ret = 0;

	switch(status_reg)
	{
	case BOOTLOADER_STU:
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

static ssize_t msg2133_write(struct file *file,const char __user *buf,size_t count, loff_t *ppos)
{
	struct i2c_client *client;
	unsigned char *tmp, bootload_data[143];
	int ret=0, i=0;

	client = file->private_data;

	switch(status_reg)
	{
	case CALIBRATION_FLAG:	//CALIBRATION_FLAG=1
		break;

	case BOOTLOADER:
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

static int msg2133_release(struct inode *inode, struct file *file)
{
	struct i2c_client *client = file->private_data;

	MSG2133_DBG("enter msg2133_release funtion\n");

	i2c_put_adapter(client->adapter);
	kfree(client);
	file->private_data = NULL;

	return 0;
}

static const struct file_operations msg2133_i2c_ts_fops =
{	.owner		= THIS_MODULE,
	.read		= msg2133_read,
	.write		= msg2133_write,
	.open		= msg2133_open,
	.unlocked_ioctl = msg2133_ioctl,
	.release	= msg2133_release,
};


static const struct i2c_device_id msg2133_i2c_ts_id[] = {
	{ "msg2133_ts", 0 },
	{ }
};

MODULE_DEVICE_TABLE(i2c, msg2133_i2c_ts_id);

static struct i2c_driver msg2133_i2c_ts_driver = {
	.driver = {
		.owner	= THIS_MODULE,
		.name	= "msg2133_ts",
	},
	.probe		= msg2133_i2c_ts_probe,
	.remove		= __devexit_p(msg2133_i2c_ts_remove),
	.id_table	= msg2133_i2c_ts_id,
};

int sprd_add_i2c_device(struct sprd_i2c_setup_data *i2c_set_data, struct i2c_driver *driver)
{
	struct i2c_board_info info;
	struct i2c_adapter *adapter;
	struct i2c_client *client;
	int ret,err;


	MSG2133_DBG("%s : i2c_bus=%d; slave_address=0x%x; i2c_name=%s",__func__,i2c_set_data->i2c_bus, \
		    i2c_set_data->i2c_address, i2c_set_data->type);

	memset(&info, 0, sizeof(struct i2c_board_info));
	info.addr = i2c_set_data->i2c_address;
	printk("%s:  info.addr = %x\n",
	__func__, (unsigned int)info.addr);
	
	strlcpy(info.type, i2c_set_data->type, I2C_NAME_SIZE);
	if(i2c_set_data->irq > 0)
		info.irq = i2c_set_data->irq;

	adapter = i2c_get_adapter( i2c_set_data->i2c_bus);
	if (!adapter) {
#ifdef MSG2133_DEBUG
		printk("%s: can't get i2c adapter %d\n",
			__func__,  i2c_set_data->i2c_bus);
#endif
		err = -ENODEV;
		goto err_driver;
	}

	client = i2c_new_device(adapter, &info);
	printk("%s:  client = %x\n",__func__, client);
	if (!client) {
#ifdef MSG2133_DEBUG
		printk("%s:  can't add i2c device at 0x%x\n",
			__func__, (unsigned int)info.addr);
#endif
		err = -ENODEV;
		goto err_driver;
	}

	i2c_put_adapter(adapter);

	ret = i2c_add_driver(driver);
	if (ret != 0) {
#ifdef MSG2133_DEBUG
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
	MSG2133_DBG("%s : slave_address=0x%x; i2c_name=%s",__func__, client->addr, client->name);
	printk("%s : slave_address=0x%x; i2c_name=%s",__func__, client->addr, client->name);
	i2c_unregister_device(client);
	i2c_del_driver(driver);
}

static int __init msg2133_i2c_ts_init(void)
{
	int ret;
	printk("==msg2133_pixcir_i2c_ts_init==\n");

#if 1
    ret = register_chrdev(I2C_MAJOR,"msg2133_ts",&msg2133_i2c_ts_fops);
    printk("==msg2133_pixcir_i2c_ts_init==ret = %d\n",ret);
    if (ret) {
#ifdef MSG2133_DEBUG
        printk(KERN_ERR "%s:register chrdev failed\n",__FILE__);
#endif
        return ret;
    }

	i2c_dev_class = class_create(THIS_MODULE, "pixcir_i2c_dev");
	if (IS_ERR(i2c_dev_class)) {
		ret = PTR_ERR(i2c_dev_class);
		class_destroy(i2c_dev_class);
	}
#endif
	
#ifdef MSG2133_DEBUG
	printk("%s\n", __func__);
#endif

	return i2c_add_driver(&msg2133_i2c_ts_driver);
}

module_init(msg2133_i2c_ts_init);

static void __exit msg2133_i2c_ts_exit(void)
{
	printk("==msg2133_ts_exit==\n");

	i2c_del_driver(&msg2133_i2c_ts_driver);
#if 1
	class_destroy(i2c_dev_class);
	unregister_chrdev(I2C_MAJOR,"msg2133_ts");
#endif

}

module_exit(msg2133_i2c_ts_exit);

MODULE_AUTHOR("<hefei@ragentek.com>");
MODULE_DESCRIPTION("MSG2133 TouchScreen driver");
MODULE_LICENSE("GPL");

