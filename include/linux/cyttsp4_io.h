#ifndef __CYTTSP4_IO_H__
#define __CYTTSP4_IO_H__

#include <linux/lab126_touch.h>

struct cyttsp4_grip_suppression_data {
  uint16_t xa;
  uint16_t xb;
  uint16_t xexa;
  uint16_t xexb;
  uint16_t ya;
  uint16_t yb;
  uint16_t yexa;
  uint16_t yexb;
  uint8_t interval;
};

#define CY4_IOCTL_GRIP_SET_DATA          _IOW(TOUCH_MAGIC_NUMBER, 0x30, struct cyttsp4_grip_suppression_data)
#define CY4_IOCTL_GRIP_GET_DATA          _IOR(TOUCH_MAGIC_NUMBER, 0x31, struct cyttsp4_grip_suppression_data)


#endif //__CYTTSP4_IO_H__
