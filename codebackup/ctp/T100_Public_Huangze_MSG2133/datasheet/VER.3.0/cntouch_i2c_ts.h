#ifndef __LINUX_CNTOUCH_TS_H__
#define __LINUX_CNTOUCH_TS_H__

//#define CONFIG_SUPPORT_CNT_CTP_UPG

#define CFG_DBG_DUMMY_INFO_SUPPORT   1     //output touch point information
#define CFG_DBG_FUCTION_INFO_SUPPORT 0     //output fouction name
#define CFG_DBG_INPUT_EVENT                   0     //debug input event


#define CFG_MAX_POINT_NUM            0x2    //max touch points supported
#define CFG_NUMOFKEYS                    0x4    //number of touch keys

#ifdef CONFIG_CNTouch_CUSTOME_ENV  
#define SCREEN_MAX_X           1024
#define SCREEN_MAX_Y           600
#else
#define SCREEN_MAX_X           800
#define SCREEN_MAX_Y           480
#endif
#define CNT_RESOLUTION_X  2048
#define CNT_RESOLUTION_Y  2048
#define PRESS_MAX                 255

#define KEY_PRESS                 0x1
#define KEY_RELEASE              0x0

#define CNT_NAME    "cnt_ts"  

#define CNT_NULL                    0x0
#define CNT_TRUE                    0x1
#define CNT_FALSE                   0x0
#define I2C_CNT_ADDRESS    0x60

#define Touch_State_No_Touch   0x0
#define Touch_State_One_Finger 0x1
#define Touch_State_Two_Finger 0x2
#define Touch_State_VK 0x3

typedef unsigned char         CNT_BYTE;    
typedef unsigned short        CNT_WORD;   
typedef unsigned int          CNT_DWRD;    
typedef unsigned char         CNT_BOOL;  



 typedef struct _REPORT_FINGER_INFO_T
 {
     short   ui2_id;               /* ID information, from 0 to  CFG_MAX_POINT_NUM - 1*/
     short    u2_pressure;    /* ***pressure information, valid from 0 -63 **********/
     short    i2_x;                /*********** X coordinate, 0 - 2047 ****************/
     short    i2_y;                /* **********Y coordinate, 0 - 2047 ****************/
 } REPORT_FINGER_INFO_T;


typedef enum
{
    ERR_OK,
    ERR_MODE,
    ERR_READID,
    ERR_ERASE,
    ERR_STATUS,
    ERR_ECC,
    ERR_DL_ERASE_FAIL,
    ERR_DL_PROGRAM_FAIL,
    ERR_DL_VERIFY_FAIL
}E_UPGRADE_ERR_TYPE;


struct CNT_TS_DATA_T {
    struct input_dev    *input_dev;
    struct work_struct     pen_event_work;
    struct workqueue_struct *ts_workqueue;
    struct early_suspend	    early_suspend;
};


#ifndef ABS_MT_TOUCH_MAJOR
#define ABS_MT_TOUCH_MAJOR    0x30    /* touching ellipse */
#define ABS_MT_TOUCH_MINOR    0x31    /* (omit if circular) */
#define ABS_MT_WIDTH_MAJOR    0x32    /* approaching ellipse */
#define ABS_MT_WIDTH_MINOR    0x33    /* (omit if circular) */
#define ABS_MT_ORIENTATION     0x34    /* Ellipse orientation */
#define ABS_MT_POSITION_X       0x35    /* Center X ellipse position */
#define ABS_MT_POSITION_Y       0x36    /* Center Y ellipse position */
#define ABS_MT_TOOL_TYPE        0x37    /* Type of touching device */
#define ABS_MT_BLOB_ID             0x38    /* Group set of pkts as blob */
#endif 


#endif

