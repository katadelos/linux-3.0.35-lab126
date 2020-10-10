/*
 * SX9500 Cap Touch 
 * Currently Supports:
 *  SX9500
 *
 * Copyright 2012 Semtech Corp.
 *
 * Licensed under the GPL-2 or later.
 */

#ifndef _SX9500_I2C_REG_H_
#define _SX9500_I2C_REG_H_

/*
 *  I2C Registers
 */
#define SX9500_IRQSTAT_REG	0x00
#define SX9500_TCHCMPSTAT_REG	0x01
#define SX9500_IRQ_ENABLE_REG	0x03
#define SX9500_CPS_CTRL0_REG	0x06
#define SX9500_CPS_CTRL1_REG	0x07
#define SX9500_CPS_CTRL2_REG	0x08
#define SX9500_CPS_CTRL3_REG	0x09
#define SX9500_CPS_CTRL4_REG	0x0A
#define SX9500_CPS_CTRL5_REG	0x0B
#define SX9500_CPS_CTRL6_REG	0x0C
#define SX9500_CPS_CTRL7_REG	0x0D
#define SX9500_CPS_CTRL8_REG	0x0E
#define SX9500_SOFTRESET_REG	0x7F

/*      Sensor Readback */
#define SX9500_CPSRD		0x20

#define SX9500_USEMSB		0x21
#define SX9500_USELSB		0x22

#define SX9500_AVGMSB		0x23
#define SX9500_AVGLSB		0x24

#define SX9500_DIFFMSB		0x25
#define SX9500_DIFFLSB		0x26

/*      IrqStat 0:Inactive 1:Active     */
#define SX9500_IRQSTAT_RESET_FLAG      0x80
#define SX9500_IRQSTAT_TOUCH_FLAG      0x40
#define SX9500_IRQSTAT_RELEASE_FLAG    0x20
#define SX9500_IRQSTAT_COMPDONE_FLAG   0x10
#define SX9500_IRQSTAT_CONV_FLAG       0x08
#define SX9500_IRQSTAT_TXENSTAT_FLAG   0x01


/* CpsStat  */
#define SX9500_TCHCMPSTAT_TCHSTAT3_FLAG   0x80
#define SX9500_TCHCMPSTAT_TCHSTAT2_FLAG   0x40
#define SX9500_TCHCMPSTAT_TCHSTAT1_FLAG   0x20
#define SX9500_TCHCMPSTAT_TCHSTAT0_FLAG   0x10



/*      SoftReset */
#define SX9500_SOFTRESET	0xDE

#endif /* _SX9500_I2C_REG_H_*/


#ifndef _SX9306_I2C_REG_H_
#define _SX9306_I2C_REG_H_

/*
 *  I2C Registers
 */
/*      Interrupt & status        */
#define SX9306_IRQSRC_REG	0x00
#define SX9306_STAT_REG		0x01
#define SX9306_IRQMASK_REG	0x03     /* ENABLE/DISABLE */

/*      proximity sensing control */
#define SX9306_PROX_CTRL0_REG	0x06
#define SX9306_PROX_CTRL1_REG	0x07
#define SX9306_PROX_CTRL2_REG	0x08
#define SX9306_PROX_CTRL3_REG	0x09
#define SX9306_PROX_CTRL4_REG	0x0A
#define SX9306_PROX_CTRL5_REG	0x0B
#define SX9306_PROX_CTRL6_REG	0x0C
#define SX9306_PROX_CTRL7_REG	0x0D
#define SX9306_PROX_CTRL8_REG	0x0E

/*      smart SAR engine control  */
#define SX9306_SAR_CTRL0_REG	0x0F
#define SX9306_SAR_CTRL1_REG	0x10

/*      Sensor Readback regsiters */
#define SX9306_SENSOR_SEL_REG	0x20
#define SX9306_USE_MSB_REG	0x21
#define SX9306_USE_LSB_REG	0x22
#define SX9306_AVG_MSB_REG	0x23
#define SX9306_AVG_LSB_REG	0x24
#define SX9306_DIFF_MSB_REG	0x25
#define SX9306_DIFF_LSB_REG	0x26
#define SX9306_OFFSET_MSB_REG	0x27
#define SX9306_OFFSET_LSB_REG	0x28
#define SX9306_SAR_DELTA_REG	0x29
#define SX9306_SAR_RATIO_REG	0x2A

/*      Software reset            */
#define SX9306_SOFTRESET_REG	0x7F

/* REG0: IrqStat 0:Inactive 1:Active     */
#define SX9306_IRQSTAT_RESET_FLAG      0x80
#define SX9306_IRQSTAT_TOUCH_FLAG      0x40  /* close */
#define SX9306_IRQSTAT_RELEASE_FLAG    0x20  /* far   */
#define SX9306_IRQSTAT_COMPDONE_FLAG   0x10  /* compensation */
#define SX9306_IRQSTAT_CONV_FLAG       0x08  /* conversion   */
#define SX9306_IRQSTAT_TXENSTAT_FLAG   0x01

/* REG1: Stat                     */
#define SX9306_PROX_STAT3_FLAG	0x80
#define SX9306_PROX_STAT2_FLAG	0x40
#define SX9306_PROX_STAT1_FLAG	0x20
#define SX9306_PROX_STAT0_FLAG	0x10
#define SX9306_COMP_STAT_FLAG	0x0F

#endif /* _SX9306_I2C_REG_H_*/
