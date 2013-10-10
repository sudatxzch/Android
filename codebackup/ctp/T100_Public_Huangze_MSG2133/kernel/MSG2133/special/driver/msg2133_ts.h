/* 
 * include/linux/input/msg2133_ts.h
 *
 * msg 2133 TouchScreen driver. 
 *
 *
 *
 * VERSION      	DATE			AUTHOR
 *    1.0		  2012-03-08		xiaoanxiang	
 *
 * note: single finger support 
 */

#ifndef __LINUX_MSG2133_TS_H__
#define __LINUX_MSG2133_TS_H__


#define X_MAX 320
#define Y_MAX 480

#define SCREEN_MAX_X    320
#define SCREEN_MAX_Y    480



#define Touch_State_No_Touch   0x0
#define Touch_State_One_Finger 0x1
#define Touch_State_Two_Finger 0x2
#define Touch_State_VK 0x3

#define CFG_MAX_POINT_NUM            0x2    //max touch points supported
#define CFG_NUMOFKEYS                    0x4    //number of touch keys

#define CNT_RESOLUTION_X  2048
#define CNT_RESOLUTION_Y  2048

#define PRESS_MAX       255

#define PMODE_ACTIVE        0x00
#define PMODE_MONITOR       0x01
#define PMODE_STANDBY       0x02
#define PMODE_HIBERNATE     0x03

typedef struct _REPORT_FINGER_INFO_T
{
	short	ui2_id; 			  /* ID information, from 0 to	CFG_MAX_POINT_NUM - 1*/
	short	 u2_pressure;	 /* ***pressure information, valid from 0 -63 **********/
	short	 i2_x;				  /*********** X coordinate, 0 - 2047 ****************/
	short	 i2_y;				  /* **********Y coordinate, 0 - 2047 ****************/
} REPORT_FINGER_INFO_T;
struct msg2133_ts_platform_data{
	int	irq;		/* irq number	*/
	int reset;
};

#endif
