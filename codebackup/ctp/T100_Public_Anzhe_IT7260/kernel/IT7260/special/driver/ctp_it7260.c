/* He Fei 2012-4-13 modify to solve TYLL-NO £¬Double fingers to touch the CTP*/
/* 
 * drivers/input/touchscreen/it7260_ts.c
 *
 * ITE IT7260 TouchScreen driver. 
 *
 * Copyright (c) 2012  Ragentek
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
 * VERSION      	        DATE			AUTHOR
 * 1.0		        2012-4-4			chris.zhu
 *
 */

/*TYLL-NO Double fingers to touch the CTP Modify By He Fei 2012-4-13 start*/
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

#include "ctp_it7260.h"

/*********************************Bee-0928-TOP****************************************/
//#define PIXCIR_DEBUG		1

#define SCREEN_WIDTH  320
#define SCREEN_HEIGHT 480 

#define PIXCIR_DEBUG
#ifdef PIXCIR_DEBUG
//#define PIXCIR_DBG(format, ...)	\
//		printk(KERN_INFO "PIXCIR_TS " format "\n", ## __VA_ARGS__)
#define PIXCIR_DBG(format, ...)	 printk(format)
//		printk(KERN_INFO "PIXCIR_TS " format "\n", ## __VA_ARGS__)
#else
#define PIXCIR_DBG(format, ...)
#endif

#ifndef I2C_MAJOR
#define I2C_MAJOR 		125
#endif

#define I2C_MINORS 		256

struct ioctl_cmd168 {
	unsigned short bufferIndex;
	unsigned short length;
	unsigned short buffer[144];
};

#define IOC_MAGIC				'd'
#define IOCTL_SET 				_IOW(IOC_MAGIC, 1, struct ioctl_cmd168)
#define IOCTL_GET 				_IOR(IOC_MAGIC, 2, struct ioctl_cmd168)

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
extern int sprd_3rdparty_gpio_tp_irq ;
static struct i2c_client *this_client;
static int pixcir_irq;
static struct early_suspend	pixcir_early_suspend;

static void pixcir_ts_suspend(struct early_suspend *handler);
static void pixcir_ts_resume(struct early_suspend *handler);
static void pixcir_ts_pwron(void);
static void pixcir_ts_pwroff(void);


static int pixcir_i2c_rxdata(char *rxdata, int length)
{
        int ret;
        int i;
        struct i2c_msg msgs[] = {
                {
                        .addr   = this_client->addr,
                        .flags  = I2C_M_NOSTART,
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
		int ret,i;
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

int i2cWriteToIt7260( unsigned char bufferIndex, unsigned char const dataBuffer[], unsigned char dataLength)
{
     int i;
 	unsigned char buf[144];
 	buf[0] = bufferIndex;
 	for( i=0 ; i<dataLength ; i++){
		 buf[i+1] = dataBuffer[i];
  	}
 	return pixcir_i2c_txdata( buf, dataLength+1 );	
}

int i2cReadFromIt7260( unsigned char bufferIndex, unsigned char dataBuffer[], unsigned char dataLength)
{
	dataBuffer[0] = bufferIndex;
	return pixcir_i2c_rxdata( dataBuffer, dataLength);
}

static void pixcir_ts_suspend(struct early_suspend *handler)
{
	printk("==%s==\n", __func__);
//   	pixcir_ts_pwroff();
//	disable_irq_nosync(this_client->irq);
}


static void pixcir_ts_resume(struct early_suspend *handler)
{	
	printk("==%s==\n", __func__);
	pixcir_ts_pwron();
	msleep(100);
//	enable_irq(this_client->irq);
}


#ifdef TOUCH_VIRTUAL_KEYS
#define SC8810_KEY_HOME	102
#define SC8810_KEY_MENU	30
#define SC8810_KEY_BACK	17
#define SC8810_KEY_SEARCH      217

static ssize_t virtual_keys_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	return sprintf(buf,
	 __stringify(EV_KEY) ":" __stringify(KEY_HOME) ":40:560:60:100"
	 ":" __stringify(EV_KEY) ":" __stringify(KEY_MENU) ":120:560:60:100"
	 ":" __stringify(EV_KEY) ":" __stringify(KEY_BACK) ":210:560:80:100"
	 ":" __stringify(EV_KEY) ":" __stringify(KEY_SEARCH) ":260:560:60:100"
	 "\n");
}

static struct kobj_attribute virtual_keys_attr = {
    .attr = {
        .name = "virtualkeys.ctp_it7260",
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
	printk("==%s==\n", __func__);
	LDO_SetVoltLevel(LDO_LDO_SIM2, LDO_VOLT_LEVEL0);
	LDO_TurnOnLDO(LDO_LDO_SIM2);
	msleep(20);
}

static void pixcir_ts_pwroff(void)
{
	printk("==%s==\n", __func__);
	LDO_TurnOffLDO(LDO_LDO_SIM2);
}

static int  pixcir_ts_config_pins(void)
{
	pixcir_ts_pwron();
	gpio_direction_input(sprd_3rdparty_gpio_tp_irq);	
	pixcir_irq=sprd_alloc_gpio_irq(sprd_3rdparty_gpio_tp_irq);

	return pixcir_irq;
}

static int  pixcir_init(void)
{
	int irq;
	PIXCIR_DBG("%s\n",__func__);
	irq = pixcir_ts_config_pins();
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
		printk(KERN_ERR "i2c-dev: Out of device minors (%d)\n",
				adap->nr);
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

struct pixcir_i2c_ts_data {
	struct i2c_client *client;
	struct input_dev *input;
};

static unsigned char rdbuf[14];
static int x[2] = { (int) -1, (int) -1 };
static int y[2] = { (int) -1, (int) -1 };
static bool finger[2]={0, 0};
static int pos[2]={0, 0};

static void pixcir_ts_poscheck(struct pixcir_i2c_ts_data *data)
{
	struct pixcir_i2c_ts_data *tsdata = data;
	
	int xraw=-1;
	int yraw=-1;

	int ret, i;
	static bool flag = 0;
    
	rdbuf[0]=0x80;
	pixcir_i2c_rxdata(rdbuf, 1);

	if (rdbuf[0]&0x01) // IC busy or not
	{
		enable_irq(this_client->irq);
		return IRQ_HANDLED;
	}
	else
	{
		if(rdbuf[0] &0x80)
		{
			rdbuf[0]=0xE0;
		        ret = pixcir_i2c_rxdata(rdbuf, 14);
			//for (i=0;i<14;i++)
			//{
			//	printk("rdbuf[%d]=%x, ", i, rdbuf[i]);
			//}
			//printk("\n");

		 	//if (ret)
			{
			        if(rdbuf[0] &0xF0)
		        	{
					if((rdbuf[0]&0x40) && (rdbuf[0]&0x01))
					{
						if(rdbuf[2])//button down
						{
							switch(rdbuf[1])
							{
							case 1:
								pos[0]=40;
								pos[1]=560;
								break;
							case 2:
								pos[0]=120;
								pos[1]=560;
								break;						
							case 3:
								pos[0]=210;
								pos[1]=560;
								break;	
							case 4:
								pos[0]=260;
								pos[1]=560;
								break;	
							}
					           printk("virtual key down x=%d,y=%d \n",pos[0],pos[1]);
							input_report_abs(tsdata->input, ABS_MT_POSITION_X,  pos[0]);
							input_report_abs(tsdata->input, ABS_MT_POSITION_Y,  pos[1]);
							input_report_abs(tsdata->input, ABS_MT_TOUCH_MAJOR, 200);//15
							input_report_abs(tsdata->input, ABS_MT_WIDTH_MAJOR, 1);

							input_report_abs(tsdata->input, ABS_PRESSURE, 200);

							input_mt_sync(tsdata->input);
							input_sync(tsdata->input);


                            
							input_report_abs(tsdata->input, ABS_MT_POSITION_X,  pos[0]);
							input_report_abs(tsdata->input, ABS_MT_POSITION_Y,  pos[1]);
							input_report_abs(tsdata->input, ABS_MT_TOUCH_MAJOR, 0);//15
							input_report_abs(tsdata->input, ABS_MT_WIDTH_MAJOR, 0);
							input_report_abs(tsdata->input, ABS_PRESSURE, 0);

							input_mt_sync(tsdata->input);
							input_sync(tsdata->input);

#if 0
							input_report_abs(tsdata->input, ABS_MT_POSITION_X,  pos[0]);
							input_report_abs(tsdata->input, ABS_MT_POSITION_Y,  pos[1]);
							input_report_abs(tsdata->input, ABS_MT_TOUCH_MAJOR, 0);//15
							input_report_abs(tsdata->input, ABS_MT_WIDTH_MAJOR, 0);

							input_report_abs(tsdata->input, ABS_PRESSURE, 0);

							input_mt_sync(tsdata->input);	
							input_sync(tsdata->input);
#endif
						}
						else//button up
						{
#if 0
					           printk("virtual key up x=%d,y=%d \n",pos[0],pos[1]);
						     switch(rdbuf[1])
							{
							case 1:
								pos[0]=40;
								pos[1]=560;
								break;
							case 2:
								pos[0]=120;
								pos[1]=560;
								break;						
							case 3:
								pos[0]=210;
								pos[1]=560;
								break;	
							case 4:
								pos[0]=260;
								pos[1]=560;
								break;	
							}		
							input_report_abs(tsdata->input, ABS_MT_POSITION_X,  pos[0]);
							input_report_abs(tsdata->input, ABS_MT_POSITION_Y,  pos[1]);
							input_report_abs(tsdata->input, ABS_MT_TOUCH_MAJOR, 0);//15
							input_report_abs(tsdata->input, ABS_MT_WIDTH_MAJOR, 0);

							input_report_abs(tsdata->input, ABS_PRESSURE, 0);

							input_mt_sync(tsdata->input);	
							input_sync(tsdata->input);	
#endif							
						}
					}
	                      enable_irq(this_client->irq);	
					return IRQ_HANDLED;
			        }

				if(rdbuf[1]&0x01)//palm
				{
					PIXCIR_DBG("palm detect\n");
					return;
				}

				if(!(rdbuf[0]&0x08))//no more data
				{
					PIXCIR_DBG("no more data push pen up\n");
					input_report_abs(tsdata->input, ABS_MT_TOUCH_MAJOR, 0);
					input_report_abs(tsdata->input, ABS_MT_WIDTH_MAJOR, 0);
					input_mt_sync(tsdata->input);
					flag=1;
					if(finger[0])                  
					{
						finger[0]=0;
					}
					if(finger[1])                  
					{
						finger[1]=0;
					}
					if(flag)
					{
					 	input_sync(tsdata->input);
						flag = 0;
					}
					enable_irq(this_client->irq);
					return;
				}

				if(rdbuf[0]&0x04)//4// 3 finger
				{
					PIXCIR_DBG("3point detect,enable irq and return");
					return;
				}

				if(rdbuf[0]&0x01)//4// Finger 1
				{
					xraw=(((rdbuf[3]&0x0F)<<8) |rdbuf[2]);
					yraw=(((rdbuf[3]&0xF0)<<4) |rdbuf[4]);	

					PIXCIR_DBG("push pen 1 down x=%d,y=%d\n",xraw,yraw);
					input_report_abs(tsdata->input, ABS_MT_POSITION_X,  xraw);
					input_report_abs(tsdata->input, ABS_MT_POSITION_Y,  yraw);
					input_report_abs(tsdata->input, ABS_MT_TOUCH_MAJOR, 200);
					input_report_abs(tsdata->input, ABS_MT_WIDTH_MAJOR, 1);
					input_report_abs(tsdata->input, ABS_PRESSURE, 200);
					
					input_mt_sync(tsdata->input);
					x[0]=xraw;
					y[0]=yraw;
					finger[0]=1;
					flag=1;
				}
                
				if(rdbuf[0]&0x02)//4// Finger 2
				{
					xraw=((rdbuf[7]&0x0F)<<8) + rdbuf[6];
					yraw=((rdbuf[7]&0xF0)<<4) + rdbuf[8];			
					PIXCIR_DBG("push pen 2 down x=%d,y=%d\n",xraw,yraw);
					input_report_abs(tsdata->input, ABS_MT_POSITION_X,  xraw);
					input_report_abs(tsdata->input, ABS_MT_POSITION_Y,  yraw);
					input_report_abs(tsdata->input, ABS_MT_TOUCH_MAJOR, 200);
					input_report_abs(tsdata->input, ABS_MT_WIDTH_MAJOR, 1);
		                            
					input_mt_sync(tsdata->input);
					x[1] = xraw;
					y[1] = yraw;
					finger[1]=1;
					flag=1;
				}
                      if(flag)
				      input_sync(tsdata->input);
			}
		}
	}
	enable_irq(this_client->irq);	
	return IRQ_HANDLED;
}

static irqreturn_t pixcir_ts_isr(int irq, void *dev_id)
{
	struct pixcir_i2c_ts_data *tsdata = dev_id;
	ktime_t expires = ktime_set(0, 10000000UL);
	int ret = -1;
	
	disable_irq_nosync(irq);
    
    	pixcir_ts_poscheck(tsdata);
	set_current_state(TASK_UNINTERRUPTIBLE);
	schedule_hrtimeout(&expires, HRTIMER_MODE_REL);

	enable_irq(irq);

	return IRQ_HANDLED;
}

#ifdef CONFIG_PM_SLEEP
static int pixcir_i2c_ts_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	return 0;
}

static int pixcir_i2c_ts_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);

	if (device_may_wakeup(&client->dev))
		disable_irq_wake(client->irq);

	return 0;
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
			client->adapter->nr), NULL, "IT7260");
	if (IS_ERR(dev)) {
		error = PTR_ERR(dev);
		return error;
	}
	/*********************************Bee-0928-BOTTOM****************************************/

	pixcir_early_suspend.level = EARLY_SUSPEND_LEVEL_BLANK_SCREEN + 1;
	pixcir_early_suspend.suspend = pixcir_ts_suspend;
	pixcir_early_suspend.resume	= pixcir_ts_resume;
	register_early_suspend(&pixcir_early_suspend);

	dev_err(&tsdata->client->dev, "insmod successfully!\n");
	
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

static int __devexit pixcir_i2c_ts_remove(struct i2c_client *client)
{
	int error;
	struct i2c_dev *i2c_dev;
	struct pixcir_i2c_ts_data *tsdata = i2c_get_clientdata(client);

	device_init_wakeup(&client->dev, 0);
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

struct ite7260_data {
	unsigned short bufferIndex;
	unsigned short length;
	unsigned short buffer[144];
};

static int pixcir_open(struct inode *inode, struct file *file)
{
	int i;
	struct ite7260_data *dev;

	dev = kmalloc(sizeof(struct ite7260_data), GFP_KERNEL);
	if (dev == NULL) {
		return -ENOMEM;
	}

	for (i = 0; i < 144; i++) {
		dev->buffer[i] = 0xFF;
	}

	file->private_data = dev;

	return 0;
}

/*************************************Bee-0928****************************************/
/*                        	     pixcir_ioctl                                    */
/*************************************Bee-0928****************************************/
static long pixcir_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{


	struct ite7260_data *dev = file->private_data;
	int retval = 0;
	int i;
	unsigned char buffer[144];
	struct ioctl_cmd168 data;
	unsigned char datalen;
	unsigned char ent[] = {0x60, 0x00, 0x49, 0x54, 0x37, 0x32};
	unsigned char ext[] = {0x60, 0x80, 0x49, 0x54, 0x37, 0x32};

	memset(&data, 0, sizeof(struct ioctl_cmd168));
	switch (cmd) {
	case IOCTL_SET:
		if (!access_ok(VERIFY_READ, (void __user *)arg, _IOC_SIZE(cmd))) {
			retval = -EFAULT;
			goto done;
		}

		if ( copy_from_user(&data, (int __user *)arg, sizeof(struct ioctl_cmd168)) ) {
                retval = -EFAULT;
			goto done;
		}              
		
		buffer[0] = (unsigned char) data.bufferIndex;
		for (i = 1; i < data.length + 1; i++) {
			buffer[i] = (unsigned char) data.buffer[i - 1];
		}
		
        if (!memcmp(&(buffer[1]), ent, sizeof(ent))) {
	        disable_irq(this_client->irq);
        }

        if (!memcmp(&(buffer[1]), ext, sizeof(ext))) {
	        enable_irq(this_client->irq);
        }
        
		datalen = (unsigned char) (data.length + 1);
		retval = i2cWriteToIt7260( (unsigned char) data.bufferIndex, &(buffer[1]), datalen - 1);
		retval = 0;
		break;

	case IOCTL_GET:		 
		if (!access_ok(VERIFY_WRITE, (void __user *)arg, _IOC_SIZE(cmd))) {
			retval = -EFAULT;
			goto done;
		}

		if (!access_ok(VERIFY_READ, (void __user *)arg, _IOC_SIZE(cmd))) {
			retval = -EFAULT;
			goto done;
		}

		if ( copy_from_user(&data, (int __user *)arg, sizeof(struct ioctl_cmd168)) ) {
			retval = -EFAULT;
			goto done;
		}

		retval = i2cReadFromIt7260( (unsigned char) data.bufferIndex, (unsigned char*) buffer, (unsigned char) data.length);
		retval = 0;
		for (i = 0; i < data.length; i++) {
			data.buffer[i] = (unsigned short) buffer[i];
		}

		if ( copy_to_user((int __user *)arg, &data, sizeof(struct ioctl_cmd168)) ) {
			retval = -EFAULT;
			goto done;
		}
		break;
		
	default:
		retval = -ENOTTY;
		break;
	}

	done:
	return (retval);

}

/***********************************Bee-0928****************************************/
/*                        	  pixcir_release                                   */
/***********************************Bee-0928****************************************/
static int pixcir_release(struct inode *inode, struct file *file)
{
	struct ite7260_data *dev = file->private_data;
	if(dev)
		kfree(dev);
	return 0;
}

/*********************************Bee-0928-TOP****************************************/
static const struct file_operations pixcir_i2c_ts_fops =
{	.owner		= THIS_MODULE,
	.open		= pixcir_open,
	.unlocked_ioctl = pixcir_ioctl,
	.release	= pixcir_release,
};
/*********************************Bee-0928-BOTTOM****************************************/


static const struct i2c_device_id pixcir_i2c_ts_id[] = {
	{ "ctp_it7260", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, pixcir_i2c_ts_id);

static struct i2c_driver pixcir_i2c_ts_driver = {
    .driver = {
        .owner = THIS_MODULE,
        .name  = "ctp_it7260",
        //.pm  = &pixcir_dev_pm_ops,
    },
    .probe		= pixcir_i2c_ts_probe,
    .remove		= __devexit_p(pixcir_i2c_ts_remove),
    .id_table	= pixcir_i2c_ts_id,
};

static int __init pixcir_i2c_ts_init(void)
{
	int ret;
	/*********************************Bee-0928-TOP****************************************/
	ret = register_chrdev(I2C_MAJOR,"ctp_it7260",&pixcir_i2c_ts_fops);//IT7260
	if (ret) {
		printk(KERN_ERR "%s:register chrdev failed\n",__FILE__);
		return ret;
	}

	i2c_dev_class = class_create(THIS_MODULE, "pixcir_i2c_dev");
	if (IS_ERR(i2c_dev_class)) {
		ret = PTR_ERR(i2c_dev_class);
		class_destroy(i2c_dev_class);
	}
	/********************************Bee-0928-BOTTOM******************************************/

	return i2c_add_driver(&pixcir_i2c_ts_driver);
}
module_init(pixcir_i2c_ts_init);

static void __exit pixcir_i2c_ts_exit(void)
{
	i2c_del_driver(&pixcir_i2c_ts_driver);
	/********************************Bee-0928-TOP******************************************/
	class_destroy(i2c_dev_class);
	unregister_chrdev(I2C_MAJOR,"ctp_it7260");
	/********************************Bee-0928-BOTTOM******************************************/
}
module_exit(pixcir_i2c_ts_exit);

MODULE_AUTHOR("Jianchun Bian <jcbian@pixcir.com.cn>");
MODULE_DESCRIPTION("Pixcir I2C Touchscreen Driver");
MODULE_LICENSE("GPL");
/*TYLL-NO Double fingers to touch the CTP Modify By He Fei 2012-4-13 end*/

