/* 
 * drivers/input/touchscreen/cntouch_ts.c
 *
 * CNTouch TouchScreen driver. 
 *
 * Copyright (c) 2012  SHIH HUA TECHNOLOGY Ltd.
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
 *
 *    note: only support mulititouch    ChiachuHsu 2012-02-01
 */

//#define CONFIG_CNTouch_CUSTOME_ENV
#include <asm/irq.h>
#include <asm/mach/irq.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/earlysuspend.h>
#include <linux/input.h>
#include "cntouch_i2c_ts.h"
#include <linux/interrupt.h>
#include <linux/delay.h>

/* -------------- global variable definition -----------*/
static struct i2c_client *this_client;
static REPORT_FINGER_INFO_T _st_finger_infos[CFG_MAX_POINT_NUM];
static unsigned int _sui_irq_num= IRQ_EINT(6);

int tsp_keycodes[CFG_NUMOFKEYS] ={

        KEY_BACK,
        KEY_MENU,
        KEY_HOME,
        KEY_SEARCH
};

char *tsp_keyname[CFG_NUMOFKEYS] ={

        "Back",
        "Menu",
        "Home",
        "Search"
};

static bool tsp_keystatus[CFG_NUMOFKEYS];



#ifdef CONFIG_HAS_EARLYSUSPEND
/***********************************************************************
    [function]: 
					callback:								early suspend function interface;
    [parameters]:
          handler:                early suspend callback pointer
    [return]:
          NULL
************************************************************************/
static void cnt_ts_suspend(struct early_suspend *handler)
{

	/*We use GPIO to control TP reset pin, High Active, Low Power Down*/
	/******************************************************************/
	//The GPIO API depends on your platform
	/******************************************************************/
	printk("\n [TSP]:device will suspend! \n");

}


/***********************************************************************
    [function]: 
		      callback:								power resume function interface;
    [parameters]:
          handler:                early suspend callback pointer
    [return]:
          NULL
************************************************************************/
static void cnt_ts_resume(struct early_suspend *handler)
{

	/*We use GPIO to control TP reset pin, High Active, Low Power Down*/
	/******************************************************************/
	//The GPIO API depends on your platform
	/******************************************************************/
	printk("\n [TSP]:device will resume from sleep! \n");

}
#endif  

/***********************************************************************
    [function]: 
		      callback:								calculate data checksum
    [parameters]:
			    msg:             				data buffer which is used to store touch data;
			    s32Length:             	the length of the checksum ;
    [return]:
			    												checksum value;
************************************************************************/
static u8 Calculate_8BitsChecksum( u8 *msg, int s32Length )
{
	int s32Checksum = 0;
	int i;

	for ( i = 0 ; i < s32Length; i++ )
	{
		s32Checksum += msg[i];
	}

	return (u8)( ( -s32Checksum ) & 0xFF );
}


/***********************************************************************
    [function]: 
		      callback:								read touch  data ftom controller via i2c interface;
    [parameters]:
			    rxdata[in]:             data buffer which is used to store touch data;
			    length[in]:             the length of the data buffer;
    [return]:
			    CNT_TRUE:              	success;
			    CNT_FALSE:             	fail;
************************************************************************/
static int cnt_i2c_rxdata(u8 *rxdata, int length)
{
	int ret;
	struct i2c_msg msg;
	
  msg.addr = this_client->addr;
  msg.flags = I2C_M_RD;
  msg.len = length;
  msg.buf = rxdata;
  ret = i2c_transfer(this_client->adapter, &msg, 1);
	if (ret < 0)
		pr_err("msg %s i2c write error: %d\n", __func__, ret);
		
	return ret;
}


/***********************************************************************
    [function]: 
		      callback:            		gather the finger information and calculate the X,Y
		                           		coordinate then report them to the input system;
    [parameters]:
          null;
    [return]:
          null;
************************************************************************/
int cnt_read_data(void)
{
    struct CNT_TS_DATA_T *data = i2c_get_clientdata(this_client);
    u8 buf[8] = {0};
    int key_id = 0x0, Touch_State = 0, kave_VK = 0, temp_x0, temp_y0, temp_x1, temp_y1;
	
    int i,ret = -1;

    ret = cnt_i2c_rxdata(buf, 8);
	
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
#ifdef SUPPORT_VIRTUAL_KEY				
			else if((buf[0] >> 6) == 0x02)//VK 
			{
				Touch_State =  Touch_State_VK;
				key_id = buf[0] & 0x1F;
			}
#endif

			//Calculate checksum
			if( !(Calculate_8BitsChecksum(buf, 7) == buf[7])) { return 0;}
				
		}
		else
		{
			return 0;
		}
        
		if(Touch_State == Touch_State_One_Finger)
		{
			/*Mapping CNT touch coordinate to Android coordinate*/
      _st_finger_infos[0].i2_x = (temp_x0*SCREEN_MAX_X) / CNT_RESOLUTION_X ;
      _st_finger_infos[0].i2_y = (temp_y0*SCREEN_MAX_Y) / CNT_RESOLUTION_Y ;

			input_report_abs(data->input_dev, ABS_MT_TOUCH_MAJOR, 1);        	
			input_report_abs(data->input_dev, ABS_MT_POSITION_X, _st_finger_infos[0].i2_x);
			input_report_abs(data->input_dev, ABS_MT_POSITION_Y, _st_finger_infos[0].i2_y);
			input_mt_sync(data->input_dev);	
			input_sync(data->input_dev);
			
		}        
		else if(Touch_State == Touch_State_Two_Finger)
		{
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
#ifdef SUPPORT_VIRTUAL_KEY					
		else if(Touch_State == Touch_State_VK)
		{
			/*you can implement your VK releated function here*/	
			if(key_id == 0x01)
			{
				input_report_key(data->input_dev, tsp_keycodes[0], 1);
			}
			else if(key_id = 0x02)
			{
				input_report_key(data->input_dev, tsp_keycodes[1], 1);
			}
			else if(key_id == 0x04)
			{
				input_report_key(data->input_dev, tsp_keycodes[2], 1);
			}
			else if(key_id == 0x08)
			{
				input_report_key(data->input_dev, tsp_keycodes[3], 1);
			}
			input_report_key(data->input_dev, BTN_TOUCH, 1);
			have_VK=1;		
		}
#endif
		else/*Finger up*/
		{
#ifdef SUPPORT_VIRTUAL_KEY						
			if(have_VK==1)
			{
				if(key_id == 0x01)
				{
					input_report_key(data->input_dev, tsp_keycodes[0], 0);
				}
				else if(key_id == 0x02)
				{
					input_report_key(data->input_dev, tsp_keycodes[1], 0);
				}
				else if(key_id == 0x04)
				{
					input_report_key(data->input_dev, tsp_keycodes[2], 0);
				}
				else if(key_id == 0x08)
				{
					input_report_key(data->input_dev, tsp_keycodes[3], 0);
				}
				input_report_key(data->input_dev, BTN_TOUCH, 0);
				have_VK=0;
			}
#endif			
			input_report_abs(data->input_dev, ABS_MT_TOUCH_MAJOR, 0);
			input_mt_sync(data->input_dev);
			input_sync(data->input_dev);

		}
}


static void cnt_work_func(struct work_struct *work)
{
    //printk("\n---work func ---\n");
    cnt_read_data();    
}




static irqreturn_t cnt_ts_irq(int irq, void *dev_id)
{
    struct CNT_TS_DATA_T *cnt_ts = dev_id;
    if (!work_pending(&cnt_ts->pen_event_work)) {
        queue_work(cnt_ts->ts_workqueue, &cnt_ts->pen_event_work);
    }

    return IRQ_HANDLED;
}




static int cnt_ts_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
    struct CNT_TS_DATA_T *cnt_ts;
    struct input_dev *input_dev;
    int err = 0;
    int i;


    _sui_irq_num = IRQ_EINT(6);

    if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
        err = -ENODEV;
        goto exit_check_functionality_failed;
    }

    cnt_ts = kzalloc(sizeof(*cnt_ts), GFP_KERNEL);
    if (!cnt_ts)    {
        err = -ENOMEM;
        goto exit_alloc_data_failed;
    }

    this_client = client;
    i2c_set_clientdata(client, cnt_ts);

    INIT_WORK(&cnt_ts->pen_event_work, cnt_work_func);

    cnt_ts->ts_workqueue = create_singlethread_workqueue(dev_name(&client->dev));
    if (!cnt_ts->ts_workqueue) {
        err = -ESRCH;
        goto exit_create_singlethread;
    }


#ifdef CONFIG_HAS_EARLYSUSPEND
    printk("\n [TSP]:register the early suspend \n");
    cnt_ts->early_suspend.level = EARLY_SUSPEND_LEVEL_BLANK_SCREEN + 1;
    cnt_ts->early_suspend.suspend = cnt_ts_suspend;
    cnt_ts->early_suspend.resume	 = cnt_ts_resume;
    register_early_suspend(&cnt_ts->early_suspend);
#endif


    err = request_irq(_sui_irq_num, cnt_ts_irq, IRQF_TRIGGER_FALLING, "qt602240_ts", cnt_ts);

    if (err < 0) {
        dev_err(&client->dev, "cnt_probe: request irq failed\n");
        goto exit_irq_request_failed;
    }
    disable_irq(_sui_irq_num);

    input_dev = input_allocate_device();
    if (!input_dev) {
        err = -ENOMEM;
        dev_err(&client->dev, "failed to allocate input device\n");
        goto exit_input_dev_alloc_failed;
    }
    
    cnt_ts->input_dev = input_dev;

    /***setup coordinate area******/
    set_bit(EV_ABS, input_dev->evbit);
    set_bit(ABS_MT_TOUCH_MAJOR, input_dev->absbit);
    set_bit(ABS_MT_POSITION_X, input_dev->absbit);
    set_bit(ABS_MT_POSITION_Y, input_dev->absbit);
    set_bit(ABS_MT_WIDTH_MAJOR, input_dev->absbit);
	
    /****** for multi-touch *******/
    for (i=0; i<CFG_MAX_POINT_NUM; i++)   
        _st_finger_infos[i].u2_pressure = -1;
 
    input_set_abs_params(input_dev,
                 ABS_MT_POSITION_X, 0, SCREEN_MAX_X - 1, 0, 0);
    input_set_abs_params(input_dev,
                 ABS_MT_POSITION_Y, 0, SCREEN_MAX_Y - 1, 0, 0);
    input_set_abs_params(input_dev,
                 ABS_MT_TOUCH_MAJOR, 0, PRESS_MAX, 0, 0);
    input_set_abs_params(input_dev,
                 ABS_MT_TRACKING_ID, 0, 30, 0, 0);
    /*****setup key code area******/
    set_bit(EV_SYN, input_dev->evbit);
    set_bit(EV_KEY, input_dev->evbit);
    set_bit(BTN_TOUCH, input_dev->keybit);
    input_dev->keycode = tsp_keycodes;
    for(i = 0; i < CFG_NUMOFKEYS; i++)
    {
        input_set_capability(input_dev, EV_KEY, ((int*)input_dev->keycode)[i]);
        tsp_keystatus[i] = KEY_RELEASE;
    }

    input_dev->name        = CNT_NAME;
    err = input_register_device(input_dev);
    if (err) {
        dev_err(&client->dev,
        "cnt_ts_probe: failed to register input device: %s\n",
        dev_name(&client->dev));
        goto exit_input_register_device_failed;
    }


    enable_irq(_sui_irq_num);    
    printk("[TSP] file(%s), function (%s), -- end\n", __FILE__, __FUNCTION__);
    return 0;

exit_input_register_device_failed:
    input_free_device(input_dev);
exit_input_dev_alloc_failed:
    free_irq(_sui_irq_num, cnt_ts);
exit_irq_request_failed:
    cancel_work_sync(&cnt_ts->pen_event_work);
    destroy_workqueue(cnt_ts->ts_workqueue);
exit_create_singlethread:
    printk("[TSP] ==singlethread error =\n");
    i2c_set_clientdata(client, NULL);
    kfree(cnt_ts);
exit_alloc_data_failed:
exit_check_functionality_failed:
    return err;
}



static int __devexit cnt_ts_remove(struct i2c_client *client)
{
    struct CNT_TS_DATA_T *cnt_ts;
    
    cnt_ts = (struct CNT_TS_DATA_T *)i2c_get_clientdata(client);
    free_irq(_sui_irq_num, cnt_ts);
    input_unregister_device(cnt_ts->input_dev);
    kfree(cnt_ts);
    cancel_work_sync(&cnt_ts->pen_event_work);
    destroy_workqueue(cnt_ts->ts_workqueue);
    i2c_set_clientdata(client, NULL);
    return 0;
}



static const struct i2c_device_id cnt_ts_id[] = {
    { CNT_NAME, 0 },{ }
};


MODULE_DEVICE_TABLE(i2c, cnt_ts_id);

static struct i2c_driver cnt_ts_driver = {
    .probe    = cnt_ts_probe,
    .remove   = __devexit_p(cnt_ts_remove),
    .id_table = cnt_ts_id,
    .driver   = {
    .name 		= CNT_NAME,
    },
};

static int __init cnt_ts_init(void)
{
    return i2c_add_driver(&cnt_ts_driver);
}


static void __exit cnt_ts_exit(void)
{
    i2c_del_driver(&cnt_ts_driver);
}



module_init(cnt_ts_init);
module_exit(cnt_ts_exit);

MODULE_AUTHOR("<chiachu.cc.hsu@cntouch.com>");
MODULE_DESCRIPTION("CNTouch TouchScreen driver");
MODULE_LICENSE("GPL");
