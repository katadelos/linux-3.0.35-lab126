/*
 * 
 */
#ifndef __LINUX_FRONTLIGHT_H
#define __LINUX_FRONTLIGHT_H 

#define FL_DEV_FILE "/dev/frontlight"

#define FL_MAGIC_NUMBER         'L'
#define FL_IOCTL_SET_INTENSITY  _IOW(FL_MAGIC_NUMBER, 0x01, int)
#define FL_IOCTL_GET_INTENSITY  _IOR(FL_MAGIC_NUMBER, 0x02, int)
#define FL_IOCTL_GET_RANGE_MAX  _IOR(FL_MAGIC_NUMBER, 0x03, int)
#define FL_IOCTL_SET_INTENSITY_FORCED  _IOW(FL_MAGIC_NUMBER, 0x04, int)

#define WARIO_FL_LEVEL0                     0
#define WARIO_FL_LEVEL12_MID                512
#define DUET_FL_LEVEL12_MID                 240
#define WARIO_FL_LO_TRANSITION_LEVEL        42
#define WARIO_FL_LO_GRP_HOP_LEVEL           1
#define WARIO_FL_MED_GRP_HOP_LEVEL          10
#define WARIO_FL_LO_GRP_DELAY_US            1000
    
#endif

