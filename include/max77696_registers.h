#ifndef MAX77696_REGISTERS_H
#define MAX77696_REGISTERS_H

#define BIT(x) 							(1<<(x))
#undef  BITS
#define BITS(_msb, _lsb)                 ((BIT(_msb)-BIT(_lsb))+BIT(_msb))

/* LEDs */
#define LEDIND_STAT1LEDSET_REG      0x71
#define LEDIND_STAT2LEDSET_REG      0x72
#define LEDIND_STAT1FLASH_REG       0x73
#define LEDIND_STAT2FLASH_REG       0x74

#define LEDIND_STATLEDSET_REG(led)  (LEDIND_STAT1LEDSET_REG+(led))
#define LEDIND_STATFLASH_REG(led)   (LEDIND_STAT1FLASH_REG +(led))

#define LEDIND_STATLEDSET_LEDEN_M   BIT (7)
#define LEDIND_STATLEDSET_LEDEN_S   7
#define LEDIND_STATLEDSET_LEDSET_M  BITS(6,0)
#define LEDIND_STATLEDSET_LEDSET_S  0

#define LEDIND_STATFLASH_FLASHD_M   BITS(7,4)
#define LEDIND_STATFLASH_FLASHD_S   4
#define LEDIND_STATFLASH_FLASHP_M   BITS(3,0)
#define LEDIND_STATFLASH_FLASHP_S   0

#define LEDIND_REG(reg)                  ((u8)(LEDIND_##reg##_REG))
#define LEDIND_REG_BITMASK(reg, bit)     ((u8)(LEDIND_##reg##_##bit##_M))
#define LEDIND_REG_BITSHIFT(reg, bit)         (LEDIND_##reg##_##bit##_S)

#define LEDIND_REG_BITGET(reg, bit, val) \
        ((u8)(((val) & LEDIND_REG_BITMASK(reg, bit))\
        >> LEDIND_REG_BITSHIFT(reg, bit)))
#define LEDIND_REG_BITSET(reg, bit, val) \
        ((u8)(((val) << LEDIND_REG_BITSHIFT(reg, bit))\
        & LEDIND_REG_BITMASK(reg, bit)))



/* backlight */

#define WLED_LEDBST_CNTRL1_REG              0x6C
#define WLED_LEDBST_CNTRL1_LED1EN_M         BIT (7)
#define WLED_LEDBST_CNTRL1_LED1EN_S         7
//      RESERVED                            BITS(6,5)
#define WLED_LEDBST_CNTRL1_LEDPWM1EN_M      BIT (4)
#define WLED_LEDBST_CNTRL1_LEDPWM1EN_S      4
#define WLED_LEDBST_CNTRL1_LEDFOSC_M        BIT (2)
#define WLED_LEDBST_CNTRL1_LEDFOSC_S        2
//      RESERVED                            BITS(1,0)

#define WLED_LED1CURRENT_1_REG              0x6D
#define WLED_LED1CURRENT_2_REG              0x6E

#define WLED_LEDBST_INT_REG                 0x6F

#define WLED_LEDBST_INTM_REG                0x70
#define WLED_LEDBST_INTM_EDOVPM_M           BIT(7)
#define WLED_LEDBST_INTM_EDOVPM_S           7
#define WLED_LEDBST_INTM_LED1SHORTM_M       BIT(6)
#define WLED_LEDBST_INTM_LED1SHORTM_S       6
//      RESERVED                            BITS(5,4)
#define WLED_LEDBST_INTM_LEDOLM_M           BIT(3)
#define WLED_LEDBST_INTM_LEDOLM_S           3
//      RESERVED                            BITS(2,0)

#define WLED_REG(reg)                   ((u8)(WLED_##reg##_REG))
#define WLED_REG_BITMASK(reg, bit)      ((u8)(WLED_##reg##_##bit##_M))
#define WLED_REG_BITSHIFT(reg, bit)          (WLED_##reg##_##bit##_S)

#define WLED_REG_BITGET(reg, bit, val) \
	((u8)(((val) & WLED_REG_BITMASK(reg, bit))\
		>> WLED_REG_BITSHIFT(reg, bit)))
#define WLED_REG_BITSET(reg, bit, val) \
	((u8)(((val) << WLED_REG_BITSHIFT(reg, bit))\
		& WLED_REG_BITMASK(reg, bit)))


/* RTC */
#define RTC_HRMODE_12H              0
#define RTC_HRMODE_24H              1

#define RTC_MASTER                  0

#define RTC_YEAR_BASE               2000
#define RTC_YEAR_OFFSET             (RTC_YEAR_BASE - 1900)

#define RTC_RTCINT_REG              0x00
#define RTC_RTCINTM_REG             0x01

#define RTC_RTCINT_WTSR             BIT (5)
#define RTC_RTCINT_RTC1S            BIT (4)
#define RTC_RTCINT_SMPL             BIT (3)
#define RTC_RTCINT_RTCA2            BIT (2)
#define RTC_RTCINT_RTCA1            BIT (1)
#define RTC_RTCINT_RTC60S           BIT (0)
#define RTC_RTCINT_ALL              0x3F

#define RTC_RTCCNTLM_REG            0x02
//      RESERVED                    BITS(7,2)
#define RTC_RTCCNTLM_HRMODEM_M      BIT (1)
#define RTC_RTCCNTLM_HRMODEM_S      1
#define RTC_RTCCNTLM_BCDM_M         BIT (0)
#define RTC_RTCCNTLM_BCDM_S         0

#define RTC_RTCCNTL_REG             0x03
//      RESERVED                    BITS(7,2)
#define RTC_RTCCNTL_HRMODE_M        BIT (1)
#define RTC_RTCCNTL_HRMODE_S        1
#define RTC_RTCCNTL_BCD_M           BIT (0)
#define RTC_RTCCNTL_BCD_S           0

#define RTC_RTCUPDATE0_REG          0x04
//      RESERVED                    BITS(7,5)
#define RTC_RTCUPDATE0_RBUDR_M      BIT (4)
#define RTC_RTCUPDATE0_RBUDR_S      4
#define RTC_RTCUPDATE0_RTCWAKE_M    BIT (3)
#define RTC_RTCUPDATE0_RTCWAKE_S    3
#define RTC_RTCUPDATE0_FREEZE_SEC_M BIT (2)
#define RTC_RTCUPDATE0_FREEZE_SEC_S 2
#define RTC_RTCUPDATE0_FCUR_M       BIT (1)
#define RTC_RTCUPDATE0_FCUR_S       1
#define RTC_RTCUPDATE0_UDR_M        BIT (0)
#define RTC_RTCUPDATE0_UDR_S        0

#define RTC_RTCSMPL_REG             0x06
#define RTC_RTCSMPL_SMPLEN_M        BIT (7)
#define RTC_RTCSMPL_SMPLEN_S        7
#define RTC_RTCSMPL_WTSR_M          BIT (6)
#define RTC_RTCSMPL_WTSR_S          6
#define RTC_RTCSMPL_SMPLT_M         BITS(3,2)
#define RTC_RTCSMPL_SMPLT_S         2
#define RTC_RTCSMPL_WTSRT_M         BITS(1,0)
#define RTC_RTCSMPL_WTSRT_S         0

#define RTC_RTCSEC_REG              0x07
#define RTC_RTCMIN_REG              0x08
#define RTC_RTCHOUR_REG             0x09
#define RTC_RTCDOW_REG              0x0A
#define RTC_RTCMONTH_REG            0x0B
#define RTC_RTCYEAR_REG             0x0C
#define RTC_RTCDOM_REG              0x0D

#define RTC_RTCAE_NONE              0

#define RTC_RTCAE1_REG              0x0E
#define RTC_RTCSECA1_REG            0x0F
#define RTC_RTCMINA1_REG            0x10
#define RTC_RTCHOURA1_REG           0x11
#define RTC_RTCDOWA1_REG            0x12
#define RTC_RTCMONTHA1_REG          0x13
#define RTC_RTCYEARA1_REG           0x14
#define RTC_RTCDOMA1_REG            0x15

#define RTC_RTCAE2_REG              0x16
#define RTC_RTCSECA2_REG            0x17
#define RTC_RTCMINA2_REG            0x18
#define RTC_RTCHOURA2_REG           0x19
#define RTC_RTCDOWA2_REG            0x1A
#define RTC_RTCMONTHA2_REG          0x1B
#define RTC_RTCYEARA2_REG           0x1C
#define RTC_RTCDOMA2_REG            0x1D

#define RTC_RTCHOUR_AMPM_MASK       BIT (6)
#define RTC_RTCHOUR_AM              0x00
#define RTC_RTCHOUR_PM              0x40

#define RTC_RTCAE_SEC               BIT (0)
#define RTC_RTCAE_MIN               BIT (1)
#define RTC_RTCAE_HOUR              BIT (2)
#define RTC_RTCAE_DOW               BIT (3)
#define RTC_RTCAE_MONTH             BIT (4)
#define RTC_RTCAE_YEAR              BIT (5)
#define RTC_RTCAE_DOM               BIT (6)
#define RTC_RTCAE_ALL               0x7F

/* Default mode of RTC data registers */
#define RTC_DATA_MODE  RTC_REG_BITSET(RTCCNTL, BCD,    0)|\
                       RTC_REG_BITSET(RTCCNTL, HRMODE, RTC_HRMODE)

enum {
    RTC_SEC = 0,
    RTC_MIN,
    RTC_HOUR,
    RTC_DOW,
    RTC_MONTH,
    RTC_YEAR,
    RTC_DOM,
    RTC_REGCNT
};

#define RTC_SEC_MASK   (0x7F)
#define RTC_MIN_MASK   (0x7F)
#define RTC_HOUR_MASK  (0x3F)
#define RTC_DOW_MASK   (0x7F)
#define RTC_MONTH_MASK (0x1F)
#define RTC_YEAR_MASK  (0xFF)
#define RTC_DOM_MASK   (0x3F)

#define RTC_REG(reg)                  ((u8)(RTC_##reg##_REG))
#define RTC_REG_BITMASK(reg, bit)     ((u8)(RTC_##reg##_##bit##_M))
#define RTC_REG_BITSHIFT(reg, bit)         (RTC_##reg##_##bit##_S)

#define RTC_REG_BITGET(reg, bit, val) \
        ((u8)(((val) & RTC_REG_BITMASK(reg, bit))\
        >> RTC_REG_BITSHIFT(reg, bit)))
#define RTC_REG_BITSET(reg, bit, val) \
        ((u8)(((val) << RTC_REG_BITSHIFT(reg, bit))\
        & RTC_REG_BITMASK(reg, bit)))


/* VDDQ */
#define VDDQ_VDDQSET_REG               0x6B
#define VDDQ_VDDQSET_ADJ_M             BITS(7,3)
#define VDDQ_VDDQSET_ADJ_S             3
#define VDDQ_VDDQSET_BYP_VDDOK_M       BIT (2)
#define VDDQ_VDDQSET_BYP_VDDOK_S       2

#define VDDQ_REG(reg)                  ((u8)(VDDQ_##reg##_REG))
#define VDDQ_REG_BITMASK(reg, bit)     ((u8)(VDDQ_##reg##_##bit##_M))
#define VDDQ_REG_BITSHIFT(reg, bit)         (VDDQ_##reg##_##bit##_S)

/* Load Switches */
#define LSW_SW_CNTRL_LSADE_M          BIT (3)
#define LSW_SW_CNTRL_LSADE_S          3
#define LSW_SW_CNTRL_LSRR_M           BITS(2,1)
#define LSW_SW_CNTRL_LSRR_S           1
#define LSW_SW_CNTRL_OUTLS_M          BIT (0)
#define LSW_SW_CNTRL_OUTLS_S          0

#define LSW_SW1_CNTRL_REG             0x5C
#define LSW_SW2_CNTRL_REG             0x5D
#define LSW_SW3_CNTRL_REG             0x5E
#define LSW_SW4_CNTRL_REG             0x5F

#define LSW_REG(reg)                  ((u8)(LSW_##reg##_REG))
#define LSW_REG_BITMASK(reg, bit)     ((u8)(LSW_##reg##_##bit##_M))
#define LSW_REG_BITSHIFT(reg, bit)         (LSW_##reg##_##bit##_S)

#define LSW_REG_BITGET(reg, bit, val) \
        ((u8)(((val) & LSW_REG_BITMASK(reg, bit))\
        >> LSW_REG_BITSHIFT(reg, bit)))
#define LSW_REG_BITSET(reg, bit, val) \
        ((u8)(((val) << LSW_REG_BITSHIFT(reg, bit))\
        & LSW_REG_BITMASK(reg, bit)))


/* LDOs */
#define LDO_CNFG1_PWRMD_M             BITS(7,6)
#define LDO_CNFG1_PWRMD_S             6
#define LDO_CNFG1_TV_M                BITS(5,0)
#define LDO_CNFG1_TV_S                0

#define LDO_CNFG2_OVCLMP_EN_M         BIT (7)
#define LDO_CNFG2_OVCLMP_EN_S         7
#define LDO_CNFG2_ALPM_EN_M           BIT (6)
#define LDO_CNFG2_ALPM_EN_S           6
#define LDO_CNFG2_COMP_M              BITS(5,4)
#define LDO_CNFG2_COMP_S              4
#define LDO_CNFG2_POK_M               BIT (3)
#define LDO_CNFG2_POK_S               3
#define LDO_CNFG2_IMON_EN_M           BIT (2)
#define LDO_CNFG2_IMON_EN_S           2
#define LDO_CNFG2_ADE_M               BIT (1)
#define LDO_CNFG2_ADE_S               1
#define LDO_CNFG2_RAMP_M              BIT (0)
#define LDO_CNFG2_RAMP_S              0

#define LDO_CNFG3_L_IMON_TF_M         BITS(5,3)
#define LDO_CNFG3_L_IMON_TF_S         3
#define LDO_CNFG3_L_IMON_EN_M         BIT (2)
#define LDO_CNFG3_L_IMON_EN_S         2
#define LDO_CNFG3_SBIASEN_M           BIT (1)
#define LDO_CNFG3_SBIASEN_S           1
#define LDO_CNFG3_BIASEN_M            BIT (0)
#define LDO_CNFG3_BIASEN_S            0

#define LDO_CNFG3_IMON_TF_1K_DEF      3
#define LDO_CNFG3_BIAS_DIS            0  
#define LDO_CNFG3_BIAS_EN             1  

#define LDO_L01_CNFG1_REG             0x43
#define LDO_L01_CNFG2_REG             0x44
#define LDO_L02_CNFG1_REG             0x45
#define LDO_L02_CNFG2_REG             0x46
#define LDO_L03_CNFG1_REG             0x47
#define LDO_L03_CNFG2_REG             0x48
#define LDO_L04_CNFG1_REG             0x49
#define LDO_L04_CNFG2_REG             0x4A
#define LDO_L05_CNFG1_REG             0x4B
#define LDO_L05_CNFG2_REG             0x4C
#define LDO_L06_CNFG1_REG             0x4D
#define LDO_L06_CNFG2_REG             0x4E
#define LDO_L07_CNFG1_REG             0x4F
#define LDO_L07_CNFG2_REG             0x50
#define LDO_L08_CNFG1_REG             0x51
#define LDO_L08_CNFG2_REG             0x52
#define LDO_L09_CNFG1_REG             0x53
#define LDO_L09_CNFG2_REG             0x54
#define LDO_L10_CNFG1_REG             0x55
#define LDO_L10_CNFG2_REG             0x56

#define LDO_INT1_REG                  0x57
#define LDO_INT2_REG                  0x58
#define LDO_INT1M_REG                 0x59
#define LDO_INT2M_REG                 0x5A
#define LDO_CNFG3_REG                 0x5B

#define LDO_INVALID_REG               0xFF

#define LDO_REG(reg)                  ((u8)(LDO_##reg##_REG))
#define LDO_REG_BITMASK(reg, bit)     ((u8)(LDO_##reg##_##bit##_M))
#define LDO_REG_BITSHIFT(reg, bit)         (LDO_##reg##_##bit##_S)

#define LDO_REG_BITGET(reg, bit, val) \
        ((u8)(((val) & LDO_REG_BITMASK(reg, bit))\
        >> LDO_REG_BITSHIFT(reg, bit)))
#define LDO_REG_BITSET(reg, bit, val) \
        ((u8)(((val) << LDO_REG_BITSHIFT(reg, bit))\
        & LDO_REG_BITMASK(reg, bit)))


/* EPD Regulators */
#define VREG_EPDCNFG_REG            0x60
#define VREG_EPDCNFG_EPDEN_M        BIT (7)
#define VREG_EPDCNFG_EPDEN_S        7
#define VREG_EPDCNFG_VCOMEN_M       BIT (6)
#define VREG_EPDCNFG_VCOMEN_S       6
#define VREG_EPDCNFG_SSHVINP_M      BIT (5)
#define VREG_EPDCNFG_SSHVINP_S      5
#define VREG_EPDCNFG_SSHVEE_M       BIT (4)
#define VREG_EPDCNFG_SSHVEE_S       4
#define VREG_EPDCNFG_SSHVNEG_M      BIT (3)
#define VREG_EPDCNFG_SSHVNEG_S      3
#define VREG_EPDCNFG_SSHVPOS_M      BIT (2)
#define VREG_EPDCNFG_SSHVPOS_S      2
#define VREG_EPDCNFG_SSHVDDH_M      BIT (1)
#define VREG_EPDCNFG_SSHVDDH_S      1

#define VREG_EPDINTS_REG            0x61
#define VREG_EPDINT_REG             0x62
#define VREG_EPDINTM_REG            0x63
#define VREG_EPDVCOM_REG            0x64
#define VREG_EPDVEE_REG             0x65
#define VREG_EPDVNEG_REG            0x66
#define VREG_EPDVPOS_REG            0x67
#define VREG_EPDVDDH_REG            0x68
#define VREG_EPDSEQ_REG             0x69
#define VREG_EPDOKINTS_REG          0x6A

#define VREG_EPDOKINT_REG           0xB2
#define VREG_EPDOKINT_EPDPOK_M      BIT (7)
#define VREG_EPDOKINT_EPDPN_M       BIT (6)
#define VREG_EPDOKINT_EPDPOK_S      7
#define VREG_EPDOKINT_EPDPN_S       6 
#define VREG_EPDOKINTM_REG          0xB3
#define VREG_EPDVCOMR_REG           0xB4
#define VREG_EPDVCOMR_VCOMR_M       BIT (0)
#define VREG_EPDVCOMR_VCOMR_S       0
#define VREG_EPDDIS_REG             0xB5

#define VREG_INVALID_REG            0xFF

#define VREG_REG(reg)                  ((u8)(VREG_##reg##_REG))
#define VREG_REG_BITMASK(reg, bit)     ((u8)(VREG_##reg##_##bit##_M))
#define VREG_REG_BITSHIFT(reg, bit)         (VREG_##reg##_##bit##_S)

#define VREG_REG_BITGET(reg, bit, val) \
	((u8)(((val) & VREG_REG_BITMASK(reg, bit))\
	>> VREG_REG_BITSHIFT(reg, bit)))
#define VREG_REG_BITSET(reg, bit, val) \
	((u8)(((val) << VREG_REG_BITSHIFT(reg, bit))\
        & VREG_REG_BITMASK(reg, bit)))


/* DDR */
#define VDDQ_SET_REG			0x69
#define VDDQ_VDDQOUT_M			0xF8
#define VDDQ_VDDQOUT_SFT		3

#define VDDQ_REG_BITGET(reg, bit, val) \
        ((u8)(((val) & VDDQ_REG_BITMASK(reg, bit))\
        >> VDDQ_REG_BITSHIFT(reg, bit)))
#define VDDQ_REG_BITSET(reg, bit, val) \
        ((u8)(((val) << VDDQ_REG_BITSHIFT(reg, bit))\
        & VDDQ_REG_BITMASK(reg, bit)))


/* BUCK */
#define BUCK_VOUTCNFG_BUCKRSR_M        BITS(7,6)
#define BUCK_VOUTCNFG_BUCKRSR_S        6
#define BUCK_VOUTCNFG_BUCKPWRMD_M      BITS(5,4)
#define BUCK_VOUTCNFG_BUCKPWRMD_S      4
#define BUCK_VOUTCNFG_BUCKADEN_M       BIT (3)
#define BUCK_VOUTCNFG_BUCKADEN_S       3
#define BUCK_VOUTCNFG_BUCKFPWMEN_M     BIT (2)
#define BUCK_VOUTCNFG_BUCKFPWMEN_S     2
#define BUCK_VOUTCNFG_BUCKIMONEN_M     BIT (1)
#define BUCK_VOUTCNFG_BUCKIMONEN_S     1
#define BUCK_VOUTCNFG_BUCKFSREN_M      BIT (0)
#define BUCK_VOUTCNFG_BUCKFSREN_S      0

#define BUCK_VOUT1_REG                 0x31
#define BUCK_VOUT1DVS_REG              0x32
#define BUCK_VOUT2_REG                 0x33
#define BUCK_VOUT2DVS_REG              0x34
#define BUCK_VOUT3_REG                 0x35
#define BUCK_VOUT4_REG                 0x36
#define BUCK_VOUT5_REG                 0x37
#define BUCK_VOUT6_REG                 0x38
#define BUCK_VOUTCNFG1_REG             0x39
#define BUCK_VOUTCNFG2_REG             0x3A
#define BUCK_VOUTCNFG3_REG             0x3B
#define BUCK_VOUTCNFG4_REG             0x3C
#define BUCK_VOUTCNFG5_REG             0x3D
#define BUCK_VOUTCNFG6_REG             0x3E
#define BUCK_FREQ_REG                  0x3F
#define BUCK_INT_REG                   0x40
#define BUCK_INTM_REG                  0x41
#define BUCK_INTS_REG                  0x42

#define CNFG_FPS0_REG                  0x80
#define CNFG_FPS1_REG                  0x81
#define CNFG_FPS2_REG                  0x82

#define CNFG_FPS_TFPS_M                BITS(5,3)
#define CNFG_FPS_TFPS_S                3

#define CNFG_FPS_REG(reg)              (CNFG_##reg##_REG)
#define CNFG_FPS_REG_BITMASK(bit)      ((u8)(CNFG_FPS_##bit##_M))
#define CNFG_FPS_REG_BITSHIFT(bit)     (CNFG_FPS_##bit##_S)

#define CNFG_FPS_REG_BITGET(bit, val) \
        ((u8)(((val) & CNFG_FPS_REG_BITMASK(bit))\
        >> CNFG_FPS_REG_BITSHIFT(bit)))
#define CNFG_FPS_REG_BITSET(bit, val) \
        ((u8)(((val) << CNFG_FPS_REG_BITSHIFT(bit))\
        & CNFG_FPS_REG_BITMASK(bit)))

#define BUCK_INVALID_REG               0xFF

#define BUCK_FPSBUCK1_REG               0x8C
#define BUCK_FPSBUCK2_REG               0x8D
#define BUCK_FPSBUCK3_REG               0x8E
#define BUCK_FPSBUCK4_REG               0x8F
#define BUCK_FPSBUCK5_REG               0x90
#define BUCK_FPSBUCK6_REG               0x91
#define BUCK_FPSRSTOUT_REG              0x9A

#define BUCK_FPSBUCK_SRC_M              BITS(7,6)
#define BUCK_FPSBUCK_SRC_S              6
#define BUCK_FPSBUCK_PU_M               BITS(5,3)
#define BUCK_FPSBUCK_PU_S               3
#define BUCK_FPSBUCK_PD_M               BITS(2,0)
#define BUCK_FPSBUCK_PD_S               0

#define BUCK_REG(reg)                  ((u8)(BUCK_##reg##_REG))
#define BUCK_REG_BITMASK(reg, bit)     ((u8)(BUCK_##reg##_##bit##_M))
#define BUCK_REG_BITSHIFT(reg, bit)         (BUCK_##reg##_##bit##_S)

#define BUCK_REG_BITGET(reg, bit, val) \
        ((u8)(((val) & BUCK_REG_BITMASK(reg, bit))\
        >> BUCK_REG_BITSHIFT(reg, bit)))
#define BUCK_REG_BITSET(reg, bit, val) \
        ((u8)(((val) << BUCK_REG_BITSHIFT(reg, bit))\
        & BUCK_REG_BITMASK(reg, bit)))


/* Charger B */
#define CHGB_MBCCTRL1_REG              0x36
#define CHGB_MBCCTRL2_REG              0x37

#define CHGB_MBCCTRL3_REG              0x38
#define CHGB_MBCCTRL3_MBCCV_M          BITS(3,0)
#define CHGB_MBCCTRL3_MBCCV_S          0

#define CHGB_MBCCTRL4_REG              0x39
#define CHGB_MBCCTRL4_MBCICHFC_M       BITS(4,0)
#define CHGB_MBCCTRL4_MBCICHFC_S       0

#define CHGB_MBCCTRL7_REG              0x3A
#define CHGB_MBCCTRL8_REG              0x3B

#define CHGB_CHGINB_CTL_REG            0x3C
#define CHGB_CHGINB_CTL_ACC_ILM_M      BIT (7)
#define CHGB_CHGINB_CTL_ACC_ILM_S      7
#define CHGB_CHGINB_CTL_BCHG_ENB_M     BIT (6)
#define CHGB_CHGINB_CTL_BCHG_ENB_S     6
#define CHGB_CHGINB_CTL_ACC_EN_M       BIT (5)
#define CHGB_CHGINB_CTL_ACC_EN_S       5

#define CHGB_CHGINB_STAT_REG           0x3D
#define CHGB_CHGINB_STAT_ACCFLTINTS_M  BIT (7)
#define CHGB_CHGINB_STAT_ACCFLTINTS_S  7
#define CHGB_CHGINB_STAT_ACCONINTS_M   BIT (6)
#define CHGB_CHGINB_STAT_ACCONINTS_S   6
#define CHGB_CHGINB_STAT_BCHGPOK_M     BIT (5)
#define CHGB_CHGINB_STAT_BCHGPOK_S     5
#define CHGB_CHGINB_STAT_BCHGON_M      BIT (4)
#define CHGB_CHGINB_STAT_BCHGON_S      4

#define CHGB_CHGINB_INT_REG            0x3E
#define CHGB_CHGINB_INTM_REG           0x3F

#define CHGB_INT_ACCFLT                BIT (7) /* Accessory OL interrupt */
#define CHGB_INT_ACCON                 BIT (6) /* Accessory ON interrupt */
#define CHGB_INT_BCHGPOK               BIT (5) /* CHGINB power input OK */
#define CHGB_INT_ACCLD                 BIT (4) /* Accessory Loaded over accessory turn-on threshold (2mA) Interrupt */

#define CHGB_REG(reg)                  ((u8)(CHGB_##reg##_REG))
#define CHGB_REG_BITMASK(reg, bit)     ((u8)(CHGB_##reg##_##bit##_M))
#define CHGB_REG_BITSHIFT(reg, bit)         (CHGB_##reg##_##bit##_S)

#define CHGB_REG_BITGET(reg, bit, val) \
        ((u8)(((val) & CHGB_REG_BITMASK(reg, bit))\
        >> CHGB_REG_BITSHIFT(reg, bit)))
#define CHGB_REG_BITSET(reg, bit, val) \
        ((u8)(((val) << CHGB_REG_BITSHIFT(reg, bit))\
        & CHGB_REG_BITMASK(reg, bit)))


/* Charger A */
#define CHGA_CHG_INT_REG               0x08
#define CHGA_CHG_INT_MASK_REG          0x09

#define CHGA_CHG_INT_OK_REG            0x0A
#define CHGA_CHG_INT_OK_CHGINA_M       BIT (6)
#define CHGA_CHG_INT_OK_CHGINA_S       6
/*
 * Single-Bit CHGINA Input Status Indicator. See CHGINA_DTLS[1:0] for
 * more information.
 * 0=The CHGINA input is invalid. CHGINA_DTLS[1:0]!=0b11.
 * 1=The CHGINA input is valid (i.e. okay). CHGINA_DTLS[1:0]=0b11.
 */
#define CHGA_CHG_INT_OK_CHG_M          BIT (4)
#define CHGA_CHG_INT_OK_CHG_S          4
/*
 * Single-Bit Charger Status Indicator. See CHG_DTLS[3:0] for more
 * information.
 * 0=The charger has suspended charging.
 * 1=The charger is okay.
 */
#define CHGA_CHG_INT_OK_BAT_M          BIT (3)
#define CHGA_CHG_INT_OK_BAT_S          3
/*
 * Single-Bit Battery Status Indicator. See BAT_DTLS for more
 * information.
 * 0=The battery has an issue and the charger has been suspended.
 *   BAT_DTLS[2:0]!=0b011 or 0b100.
 * 1=The battery is okay. BAT_DTLS[2:0]=0b011 or 0b100.
 */
#define CHGA_CHG_INT_OK_THM_M          BIT (2)
#define CHGA_CHG_INT_OK_THM_S          2
/*
 * Single-Bit Thermistor Status Indicator. See the THM_DTLS[2:0] for
 * more information.
 * 0=The thermistor temperature is <T1 or >T4. THM_DTLS[2:0]!=0b000
 * 1=The thermistor temperature is inside of the allowable range for
 *   charging (i.e. okay). THM_DTLS[2:0]=0b000
 */
#define CHGA_CHG_INT_OK_SYS2_M         BIT (0)
#define CHGA_CHG_INT_OK_SYS2_S         0
/*
 * Single-Bit SYS2 Status Indicator. See SYS2_DTLS[3:0] for more
 * information.
 * 0=Something powered by the SYS2 node has hit current limit.
 *   SYS2_DTLS[3:0]!=0b000.
 * 1=The SYS2 node is okay. SYS2_DTLS[1:0]=0b000.
 */

#define CHGA_CHG_DTLS_00_REG           0x0B
#define CHGA_CHG_DTLS_00_CHGINA_DTLS_M BITS(6,5)
#define CHGA_CHG_DTLS_00_CHGINA_DTLS_S 5
#define CHGA_CHG_DTLS_00_THM_DTLS_M    BITS(2,0)
#define CHGA_CHG_DTLS_00_THM_DTLS_S    0

#define CHGA_CHG_DTLS_01_REG           0x0C
#define CHGA_CHG_DTLS_01_TREG_M        BIT (7)
#define CHGA_CHG_DTLS_01_TREG_S        7
#define CHGA_CHG_DTLS_01_BAT_DTLS_M    BITS(6,4)
#define CHGA_CHG_DTLS_01_BAT_DTLS_S    4
#define CHGA_CHG_DTLS_01_CHG_DTLS_M    BITS(3,0)
#define CHGA_CHG_DTLS_01_CHG_DTLS_S    0

#define CHGA_CHG_DTLS_02_REG           0x0D
#define CHGA_CHG_DTLS_02_SYS2_DTLS_M   BITS(3,0)
#define CHGA_CHG_DTLS_02_SYS2_DTLS_S   0

//                                     BIT (7)
#define CHGA_INT_CHGINA                BIT (6)
//                                     BIT (5)
#define CHGA_INT_CHG                   BIT (4)
#define CHGA_INT_BAT                   BIT (3)
#define CHGA_INT_THM                   BIT (2)
//                                     BIT (1)
#define CHGA_INT_SYS2                  BIT (0)

#define CHGA_CHG_CNFG_00_REG           0x0F
#define CHGA_CHG_CNFG_00_WDTEN_M       BIT (4)
#define CHGA_CHG_CNFG_00_WDTEN_S       4
#define CHGA_CHG_CNFG_00_MODE_M        BITS(3,0)
#define CHGA_CHG_CNFG_00_MODE_S        0

#define CHGA_CHG_CNFG_00_MODE_CHG     0b0101

#define CHGA_CHG_CNFG_01_REG           0x10
#define CHGA_CHG_CNFG_01_PQEN_M        BIT (7)
#define CHGA_CHG_CNFG_01_PQEN_S        7
#define CHGA_CHG_CNFG_01_CHG_RSTRT_M   BITS(5,4)
#define CHGA_CHG_CNFG_01_CHG_RSTRT_S   4
#define CHGA_CHG_CNFG_01_FCHGTIME_M    BITS(2,0)
#define CHGA_CHG_CNFG_01_FCHGTIME_S    0

#define CHGA_CHG_CNFG_02_REG           0x11
#define CHGA_CHG_CNFG_02_OTGA_ILIM_M   BIT (7)
#define CHGA_CHG_CNFG_02_OTGA_ILIM_S   7
#define CHGA_CHG_CNFG_02_CHG_CC_M      BITS(5,0)
#define CHGA_CHG_CNFG_02_CHG_CC_S      0

#define CHGA_CHG_CNFG_03_REG           0x12
#define CHGA_CHG_CNFG_03_TO_TIME_M     BITS(5,3)
#define CHGA_CHG_CNFG_03_TO_TIME_S     3
#define CHGA_CHG_CNFG_03_TO_ITH_M      BITS(2,0)
#define CHGA_CHG_CNFG_03_TO_ITH_S      0

#define CHGA_CHG_CNFG_04_REG           0x13
#define CHGA_CHG_CNFG_04_MINVSYS1_M    BITS(7,5)
#define CHGA_CHG_CNFG_04_MINVSYS1_S    5
#define CHGA_CHG_CNFG_04_CHG_CV_PRM_M  BITS(4,0)
#define CHGA_CHG_CNFG_04_CHG_CV_PRM_S  0

#define CHGA_CHG_CNFG_05_REG           0x14
#define CHGA_CHG_CNFG_05_CHG_CV_JTA_M  BITS(4,0)
#define CHGA_CHG_CNFG_05_CHG_CV_JTA_S  0

#define CHGA_CHG_CNFG_06_REG           0x15
#define CHGA_CHG_CNFG_06_CHGPROT_M     BITS(3,2)
#define CHGA_CHG_CNFG_06_CHGPROT_S     2
#define CHGA_CHG_CNFG_06_WDTCLR_M      BITS(1,0)
#define CHGA_CHG_CNFG_06_WDTCLR_S      0

#define CHGA_CHG_CNFG_07_REG           0x16
#define CHGA_CHG_CNFG_07_REGTEMP_M     BITS(6,5)
#define CHGA_CHG_CNFG_07_REGTEMP_S     5

#define CHGA_CHG_CNFG_08_REG           0x17

#define CHGA_CHG_CNFG_09_REG           0x18
#define CHGA_CHG_CNFG_09_CHGA_ICL_M    BIT (7)
#define CHGA_CHG_CNFG_09_CHGA_ICL_S    7
#define CHGA_CHG_CNFG_09_CHGINA_ILIM_M BITS(6,0)
#define CHGA_CHG_CNFG_09_CHGINA_ILIM_S 0

#define CHGA_CHG_CNFG_10_REG           0x19

#define CHGA_CHG_CNFG_11_REG           0x1A
#define CHGA_CHG_CNFG_11_VSYS2SET_M    BITS(6,0)
#define CHGA_CHG_CNFG_11_VSYS2SET_S    0

#define CHGA_CHG_CNFG_12_REG           0x1B
#define CHGA_CHG_CNFG_12_CHG_LPM_M     BIT (7)
#define CHGA_CHG_CNFG_12_CHG_LPM_S     7
#define CHGA_CHG_CNFG_12_VCHGIN_REG_M  BITS(4,3)
#define CHGA_CHG_CNFG_12_VCHGIN_REG_S  3
#define CHGA_CHG_CNFG_12_B2SOVRC_M     BITS(2,0)
#define CHGA_CHG_CNFG_12_B2SOVRC_S     0

#define CHGA_CHG_CNFG_13_REG           0x1C

#define CHGA_CHG_CNFG_14_REG           0x1D
#define CHGA_CHG_CNFG_14_JEITA_M       BIT (7)
#define CHGA_CHG_CNFG_14_JEITA_S       7
#define CHGA_CHG_CNFG_14_T4_M          BITS(6,5)
#define CHGA_CHG_CNFG_14_T4_S          5
#define CHGA_CHG_CNFG_14_T3_M          BITS(4,3)
#define CHGA_CHG_CNFG_14_T3_S          3
#define CHGA_CHG_CNFG_14_T2_M          BIT (2)
#define CHGA_CHG_CNFG_14_T2_S          2
#define CHGA_CHG_CNFG_14_T1_M          BITS(1,0)
#define CHGA_CHG_CNFG_14_T1_S          0

#define CHGA_PROT_CNFG_01              BIT (1) 
#define CHGA_PROT_CNFG_02              BIT (2) 
#define CHGA_PROT_CNFG_03              BIT (3)
#define CHGA_PROT_CNFG_04              BIT (4)
#define CHGA_PROT_CNFG_05              BIT (5)
#define CHGA_PROT_CNFG_07              BIT (7)
#define CHGA_PROTECTABLE_CNFG_REGS     ((u8)(CHGA_PROT_CNFG_01 | CHGA_PROT_CNFG_02 | \
                                             CHGA_PROT_CNFG_03 | CHGA_PROT_CNFG_04 | \
                                             CHGA_PROT_CNFG_05 | CHGA_PROT_CNFG_07))

#define CHGA_REG(reg)                  ((u8)(CHGA_##reg##_REG))
#define CHGA_REG_BITMASK(reg, bit)     ((u8)(CHGA_##reg##_##bit##_M))
#define CHGA_REG_BITSHIFT(reg, bit)         (CHGA_##reg##_##bit##_S)

#define CHGA_REG_BITGET(reg, bit, val) \
	((u8)(((val) & CHGA_REG_BITMASK(reg, bit))\
		>> CHGA_REG_BITSHIFT(reg, bit)))
#define CHGA_REG_BITSET(reg, bit, val) \
	((u8)(((val) << CHGA_REG_BITSHIFT(reg, bit))\
		& CHGA_REG_BITMASK(reg, bit)))


/* Battery */
#define GAUGE_DEFAULT_SNS_RESISTOR  (10000)
#define GAUGE_LEARNED_NR_REGS       MAX77696_GAUGE_LEARNED_NR_INFOS

#define FG_STATUS_POR            BIT( 1) /* Power-On Reset */
#define FG_STATUS_BST            BIT( 3) /* Battery Status */
#define FG_STATUS_VMN            BIT( 8) /* Minimum V_alrt Threshold Exceeded */
#define FG_STATUS_TMN            BIT( 9) /* Minimum T_alrt Threshold Exceeded */
#define FG_STATUS_SMN            BIT(10) /* Minimum SOC_alrt Threshold Exceeded */
#define FG_STATUS_BI             BIT(11) /* Battery Insertion */
#define FG_STATUS_VMX            BIT(12) /* Maximum V_alrt Threshold Exceeded */
#define FG_STATUS_TMX            BIT(13) /* Maximum T_alrt Threshold Exceeded */
#define FG_STATUS_SMX            BIT(14) /* Maximum SOC_alrt Threshold Exceeded */
#define FG_STATUS_BR             BIT(15) /* Battery Removal */

#define FG_CONFIG_BER            BIT( 0) /* Enable alert on battery removal */
#define FG_CONFIG_BEI            BIT( 1) /* Enable alert on battery insertion */
#define FG_CONFIG_AEN            BIT( 2) /* Enable alert on fuel-gauge outputs */
#define FG_CONFIG_FTHRM          BIT( 3) /* Force Thermistor Bias Switch */
#define FG_CONFIG_ETHRM          BIT( 4) /* Enable Thermistor */
#define FG_CONFIG_ALSH           BIT( 5)
#define FG_CONFIG_I2CSH          BIT( 6) /* I2C Shutdown */
#define FG_CONFIG_SHDN           BIT( 7) /* Shutdown */
#define FG_CONFIG_TEX            BIT( 8) /* Temperature External */
#define FG_CONFIG_TEN            BIT( 9) /* Enable Temperature Channel */
#define FG_CONFIG_AINSH          BIT(10) /* Enable Fuel Gauge shutdown when the battery is removed */
#define FG_CONFIG_ALRTP          BIT(11)
#define FG_CONFIG_VS             BIT(12) /* Voltage ALRT Sticky */
#define FG_CONFIG_TS             BIT(13) /* Temperature ALRT Sticky */
#define FG_CONFIG_SS             BIT(14) /* SOC ALRT Sticky */

#define FG_STATUS_REG            0x00
#define FG_VALRT_TH_REG          0x01
#define FG_TALRT_TH_REG          0x02
#define FG_SALRT_TH_REG          0x03
#define FG_ATRATE_REG            0x04
#define FG_REMCAPREP_REG         0x05
#define FG_SOCREP_REG            0x06
#define FG_AGE_REG               0x07
#define FG_TEMP_REG              0x08
#define FG_VCELL_REG             0x09
#define FG_CURRENT_REG           0x0A
#define FG_AVGCURRENT_REG        0x0B
//                               0x0C
#define FG_SOCMIX_REG            0x0D
#define FG_SOCAV_REG             0x0E
#define FG_REMCAPMIX_REG         0x0F
#define FG_FULLCAP_REG           0x10
#define FG_TTE_REG               0x11
#define FG_QRESIDUAL00_REG       0x12
#define FG_FULLSOCTHR_REG        0x13
//                               0x14
//                               0x15
#define FG_AVGTA_REG             0x16
#define FG_CYCLES_REG            0x17
#define FG_DESIGNCAP_REG         0x18
#define FG_AVGVCELL_REG          0x19
#define FG_MINMAXTEMP_REG        0x1A
#define FG_MINMAXVOLT_REG        0x1B
#define FG_MINMAXCURR_REG        0x1C
#define FG_CONFIG_REG            0x1D
#define FG_ICHGTERM_REG          0x1E
#define FG_REMCAPAV_REG          0x1F
//                               0x20
#define FG_VERSION_REG           0x21
#define FG_QRESIDUAL10_REG       0x22
#define FG_FULLCAPNOM_REG        0x23
#define FG_TEMPNOM_REG           0x24
#define FG_TEMPLIM_REG           0x25
//                               0x26
#define FG_AIN_REG               0x27
#define FG_LEARNCFG_REG          0x28
#define FG_FILTERCFG_REG         0x29
#define FG_RELAXCFG_REG          0x2A
#define FG_MISCCFG_REG           0x2B
#define FG_TGAIN_REG             0x2C
#define FG_TOFF_REG              0x2D
#define FG_CGAIN_REG             0x2E
#define FG_COFF_REG              0x2F
//                               0x30
//                               0x31
#define FG_QRESIDUAL20_REG       0x32
//                               0x33
//                               0x34
//                               0x35
#define FG_IAVG_EMPTY_REG        0x36
#define FG_FCTC_REG              0x37
#define FG_RCOMP0_REG            0x38
#define FG_TEMPCO_REG            0x39
#define FG_V_EMPTY_REG           0x3A
//                               0x3B
//                               0x3C
#define FG_FSTAT_REG             0x3D
#define FG_TIMER_REG             0x3E
#define FG_SHDNTIMER_REG         0x3F
//                               0x40
//                               0x41
#define FG_QRESIDUAL30_REG       0x42
//                               0x43
//                               0x44
#define FG_DQACC_REG             0x45
#define FG_DPACC_REG             0x46
//                               0x47
#define FG_VFSOC0_REG            0x48
//                               0x49
//                               0x4A
//                               0x4B
//                               0x4C
#define FG_QH_REG                0x4D
//                               0x4E
//                               0x4F
//                               0x50 ... 0x5F
#define FG_VFSOC_EN_REG          0x60
//                               0x61
#define FG_MLOCK1_REG            0x62
#define FG_MLOCK2_REG            0x63
//                               0x64 ... 0x6F
//                               0x70 ... 0x7F
#define FG_MCHAR_TBL_REG         0x80
//CHARACTERIZATION_TABLE         0x80 ... 0x8F
//CHARACTERIZATION_TABLE         0x90 ... 0x9F
//CHARACTERIZATION_TABLE         0xA0 ... 0xAF
//                               0xB0 ... 0xBF
//                               0xC0 ... 0xCF
//                               0xD0 ... 0xDF
//                               0xE0 ... 0xEF
//                               0xF0
//                               0xF1
//                               0xF2
//                               0xF3
//                               0xF4
//                               0xF5
//                               0xF6
//                               0xF7
//                               0xF8
//                               0xF9
//                               0xFA
#define FG_VFOCV_REG             0xFB
//                               0xFC
//                               0xFD
#define FG_VFSOC_REG             0xFF

#define FG_REG(reg)                      ((u8) (FG_##reg##_REG))
#define FG_REG_BITMASK(reg, bit)         ((u16)(FG_##reg##_##bit##_M))
#define FG_REG_BITSHIFT(reg, bit)              (FG_##reg##_##bit##_S)

#define FG_REG_BITGET(reg, bit, val) \
	((u8)(((val) & FG_REG_BITMASK(reg, bit))\
		>> FG_REG_BITSHIFT(reg, bit)))
#define FG_REG_BITSET(reg, bit, val) \
	((u8)(((val) << FG_REG_BITSHIFT(reg, bit))\
		& FG_REG_BITMASK(reg, bit)))


/* UIC */
#define UIC_DEVICEID_REG              0x00
#define UIC_DEVICEID_CHIPID_M         0xF0
#define UIC_DEVICEID_CHIPID_S         4
#define UIC_DEVICEID_CHIPREV_M        0x0F
#define UIC_DEVICEID_CHIPREV_S        0

#define UIC_INT1_REG                  0x01
#define UIC_INT1_DCDTMR_M             BIT (7)
#define UIC_INT1_CHGDETRISE_M         BIT (6)
#define UIC_INT1_CHGDETFALL_M         BIT (5)
#define UIC_INT1_VBVOLT_M             BIT (1)
#define UIC_INT1_CHGTYP_M             BIT (0)

#define UIC_INT2_REG                  0x02
#define UIC_INT2_ADC_M                BIT (0)

#define UIC_INTMASK2_REG              0x6
#define UIC_INTMASK2_ADC_M            BIT (0)
#define UIC_INTMASK2_ADC_S            0

#define UIC_STATUS1_REG               0x03
#define UIC_STATUS1_DCDTMR_M          BIT (7)
#define UIC_STATUS1_DCDTMR_S          7
#define UIC_STATUS1_CHGDETACT_M       BIT (6)
#define UIC_STATUS1_CHGDETACT_S       6
#define UIC_STATUS1_VBVOLT_M          BIT (4)
#define UIC_STATUS1_VBVOLT_S          4
#define UIC_STATUS1_CHGTYP_M          BITS(3,0)
#define UIC_STATUS1_CHGTYP_S          0

#define UIC_STATUS2_REG               0x04
#define UIC_STATUS2_ADCERROR_M        BIT (7)
#define UIC_STATUS2_ADCERROR_S        7
#define UIC_STATUS2_ENUSTAT_M         BIT (6)
#define UIC_STATUS2_ENUSTAT_S         6
#define UIC_STATUS2_ADC_M             BITS(4,0)
#define UIC_STATUS2_ADC_S             0

#define UIC_INTMASK1_REG              0x05
#define UIC_INTMASK2_REG              0x06

#define UIC_SYSCTRL1_REG              0x07
#define UIC_SYSCTRL1_INT_TYP_M        0x80
#define UIC_SYSCTRL1_INT_DLY_M        0x40
#define UIC_SYSCTRL1_INT_POL_M        0x20
#define UIC_SYSCTRL1_INT_EN_M         0x10
#define UIC_SYSCTRL1_USBSWC_M         0x0C
#define UIC_SYSCTRL1_USBSWC_S         2
#define UIC_SYSCTRL1_LOW_POW_M        0x01
#define UIC_SYSCTRL1_LOW_POW_S        0

#define UIC_SYSCTRL2_REG              0x08
#define UIC_SYSCTRL2_ADC_DEB_M        0x18
#define UIC_SYSCTRL2_ADC_DEB_S        3
#define UIC_SYSCTRL2_DCDCPL_M         0x04
#define UIC_SYSCTRL2_DCDCPL_S         2
#define UIC_SYSCTRL2_IDAUTOSWC_M      0x02
#define UIC_SYSCTRL2_IDAUTOSWC_S      1
#define UIC_SYSCTRL2_ADC_EN_M         0x01
#define UIC_SYSCTRL2_ADC_EN_S         0

#define UIC_CDETCTRL_REG              0x09
#define UIC_CDETCTRL_SFEN_M           0xC0
#define UIC_CDETCTRL_SFEN_S           6
#define UIC_CDETCTRL_CDPDET_M         0x20
#define UIC_CDETCTRL_CDPDET_S         5
#define UIC_CDETCTRL_DCHKTM_M         0x10
#define UIC_CDETCTRL_DCHKTM_S         4
#define UIC_CDETCTRL_DCD2SCT_M        0x08
#define UIC_CDETCTRL_DCD2SCT_S        3
#define UIC_CDETCTRL_DCDEN_M          0x04
#define UIC_CDETCTRL_DCDEN_S          2
#define UIC_CDETCTRL_CHGTYPMAN_M      0x02
#define UIC_CDETCTRL_CHGTYPMAN_S      1
#define UIC_CDETCTRL_CHGDETEN_M       0x01
#define UIC_CDETCTRL_CHGDETEN_S       0

#define UIC_MANCTRL_REG               0x0A
#define UIC_MANCTRL_OIDSET_M          0xC0
#define UIC_MANCTRL_OIDSET_S          6
#define UIC_MANCTRL_FIDSET_M          0x30
#define UIC_MANCTRL_FIDSET_S          4
#define UIC_MANCTRL_MANSET_M          0x08
#define UIC_MANCTRL_MANSET_S          3
#define UIC_MANCTRL_ISET_M            0x07
#define UIC_MANCTRL_ISET_S            0

#define UIC_CHGCTRL_REG               0x0B
#define UIC_CHGCTRL_AUTH500_M         0x80
#define UIC_CHGCTRL_ENUMSUB_M         0x40
#define UIC_CHGCTRL_ENUMSUB_S         6
#define UIC_CHGCTRL_DCPSET_M          0x30
#define UIC_CHGCTRL_DCPSET_S          4
#define UIC_CHGCTRL_ENUMEN_M          0x08
#define UIC_CHGCTRL_IMAX_M            0x07

#define UIC_ENUCTRL_REG               0x0C
#define UIC_ENUCTRL_ENUMTM_M          0x03

#define UIC_INTSTS_REG                0x1D
#define UIC_INTSTS_UICWK_M            0x80
#define UIC_INTSTS_UICWK_S            7
#define UIC_INTSTS_CHG_DET_ACT_M      0x40
#define UIC_INTSTS_FACTORY_M          0x20
#define UIC_INTSTS_DB_M               0x10
#define UIC_INTSTS_SFEN_M             0x08
#define UIC_INTSTS_ISET_M             0x07

/*** Interrupt Bits ***/
#define UIC_INT1_DCDTMR               BIT (7)
#define UIC_INT1_CHGDETACTRISE        BIT (6)
#define UIC_INT1_CHGDETACTFALL        BIT (5)
//                                    BIT (4)
//                                    BIT (3)
//                                    BIT (2)
#define UIC_INT1_VBVOLT               BIT (1)
#define UIC_INT1_CHGTYP               BIT (0)
#define UIC_INT2_ADCERROR             BIT (7)
#define UIC_INT2_ENUSTAT              BIT (6)
//                                    BIT (5)
//                                    BIT (4)
//                                    BIT (3)
//                                    BIT (2)
//                                    BIT (1)
#define UIC_INT2_ADC                  BIT (0)

#define UIC_ADC_STATUS_INVALID_10    0x10
#define UIC_ADC_STATUS_INVALID_13    0x13
#define UIC_ADC_STATUS_INVALID_16    0x16

#define UIC_REG(reg)                  ((u8)(UIC_##reg##_REG))
#define UIC_REG_BITMASK(reg, bit)     ((u8)(UIC_##reg##_##bit##_M))
#define UIC_REG_BITSHIFT(reg, bit)         (UIC_##reg##_##bit##_S)

#define UIC_REG_BITGET(reg, bit, val) \
	((u8)(((val) & UIC_REG_BITMASK(reg, bit))\
		>> UIC_REG_BITSHIFT(reg, bit)))
#define UIC_REG_BITSET(reg, bit, val) \
	((u8)(((val) << UIC_REG_BITSHIFT(reg, bit))\
		& UIC_REG_BITMASK(reg, bit)))


/* Topsys */
#define CID5_REG                         0x9C

#define GLBLCNFG0_REG                    0x00

#define GLBLCNFG1_REG                    0x01
//      RESERVED                         BIT (7)
#define GLBLCNFG1_GLBL_LPM_MASK          BIT (6)
#define GLBLCNFG1_GLBL_LPM_SHIFT         6
#define GLBLCNFG1_MREN_MASK              BIT (5)
#define GLBLCNFG1_MREN_SHIFT             5
#define GLBLCNFG1_MRT_MASK               BITS(4,2)
#define GLBLCNFG1_MRT_SHIFT              2
#define GLBLCNFG1_EN0DLY_MASK            BIT (1)
#define GLBLCNFG1_EN0DLY_SHIFT           1
#define GLBLCNFG1_STBYEN_MASK            BIT (0)
#define GLBLCNFG1_STBYEN_SHIFT           0

#define GLBLCNFG2_REG                    0x02
//      RESERVED                         BIT (7)
#define GLBLCNFG2_WDTEN_MASK             BIT (6)
#define GLBLCNFG2_WDTEN_SHIFT            6
#define GLBLCNFG2_TWD_MASK               BITS(5,4)
#define GLBLCNFG2_TWD_SHIFT              4
#define GLBLCNFG2_RTCAWK_MASK            BIT (3)
#define GLBLCNFG2_RTCAWK_SHIFT           3
#define GLBLCNFG2_WDWK_MASK              BIT (2)
#define GLBLCNFG2_WDWK_SHIFT             2
#define GLBLCNFG2_MROWK_MASK             BIT (1)
#define GLBLCNFG2_MROWK_SHIFT            1
#define GLBLCNFG2_UICWK_EDGE_MASK        BIT (0)
#define GLBLCNFG2_UICWK_EDGE_SHIFT       0

#define GLBLCNFG3_REG                    0x03
//      RESERVED                         BITS(7,5)
#define GLBLCNFG3_LBHYST_MASK            BITS(4,3)
#define GLBLCNFG3_LBHYST_SHIFT           3
#define GLBLCNFG3_LBDAC_MASK             BITS(2,0)
#define GLBLCNFG3_LBDAC_SHIFT            0

#define GLBLCNFG4_REG                    0x04
//      RESERVED                         BITS(7,2)
#define GLBLCNFG4_WDTC_MASK              BITS(1,0)
#define GLBLCNFG4_WDTC_SHIFT             0

#define GLBLCNFG5_REG                    0xAF
//      RESERVED                         BITS(7,2)
#define GLBLCNFG5_NRSO_DEL_MASK          BITS(1,0)
#define GLBLCNFG5_NRSO_DEL_SHIFT         0

#define GLBLINT_REG                      0x05
#define GLBLINTM_REG                     0x06
#define GLBLSTAT_REG                     0x07

#define GLBLINT_EN0_R                    BIT (7)
#define GLBLINT_EN0_F                    BIT (6)
#define GLBLINT_EN0_1SEC                 BIT (5)
#define GLBLINT_MRWRN                    BIT (4)
#define GLBLINT_MBATTLOW_R               BIT (3)
#define GLBLINT_TJALRM1_R                BIT (2)
#define GLBLINT_TJALRM2_R                BIT (1)
#define GLBLINT_IRQ                      BIT (0)

#define GLBLSTAT_EN0_S                   BIT (7)
#define GLBLSTAT_MBATTLOW_R              BIT (3)
#define GLBLSTAT_TJALRM1_R               BIT (2)
#define GLBLSTAT_TJALRM2_R               BIT (1)
#define GLBLSTAT_IRQ                     BIT (0)

#define GLBLSTAT_EN0_S_MASK              BIT (7)
#define GLBLSTAT_EN0_S_SHIFT             7
#define GLBLSTAT_MBATTLOW_R_MASK         BIT (3)
#define GLBLSTAT_MBATTLOW_R_SHIFT        3
#define GLBLSTAT_TJALRM1_R_MASK          BIT (2)
#define GLBLSTAT_TJALRM1_R_SHIFT         2  
#define GLBLSTAT_TJALRM2_R_MASK          BIT (1)
#define GLBLSTAT_TJALRM2_R_SHIFT         1  
#define GLBLSTAT_IRQ_MASK                BIT (0)
#define GLBLSTAT_IRQ_SHIFT               0

#define GLBLCNFG3_LBDAC_2_7V              0	
#define GLBLCNFG3_LBDAC_2_8V              1	
#define GLBLCNFG3_LBDAC_2_9V              2	
#define GLBLCNFG3_LBDAC_3_0V              3	
#define GLBLCNFG3_LBDAC_3_1V              4	
#define GLBLCNFG3_LBDAC_3_2V              5	
#define GLBLCNFG3_LBDAC_3_3V              6	
#define GLBLCNFG3_LBDAC_3_4V              7	

#define GLBLCNFG3_LBHYST_200mV            (0 << 3)
#define GLBLCNFG3_LBHYST_300mV            (1 << 3) 
#define GLBLCNFG3_LBHYST_400mV            (2 << 3) 
#define GLBLCNFG3_LBHYST_500mV            (3 << 3) 

#define TOPSYS_REG(reg)                  ((u8)(reg##_REG))
#define TOPSYS_REG_BITMASK(reg, bits)    ((u8)(reg##_##bits##_MASK))
#define TOPSYS_REG_BITSHIFT(reg, bits)        (reg##_##bits##_SHIFT)

#define TOPSYS_REG_BITGET(reg, bit, val) \
	((val) >> TOPSYS_REG_BITSHIFT(reg, bit))
#define TOPSYS_REG_BITSET(reg, bit, val) \
	((val) << TOPSYS_REG_BITSHIFT(reg, bit))

#define GLBLCNFG0_PTP     BIT(6)
#define GLBLCNFG0_FSENT   BIT(5)
#define GLBLCNFG0_FRSTRT  BIT(4)
#define GLBLCNFG0_PRSTRT  BIT(3)
#define GLBLCNFG0_FSHDN   BIT(2)
#define GLBLCNFG0_PSHDN   BIT(1)
#define GLBLCNFG0_SFTPDRR BIT(0)


/* IRQ */
#define INTTOP1_REG         0xA2
#define INTTOP1M_REG        0xA3

#define TOPSYSINT_M         BIT (7)
#define BUCKINT_M           BIT (6)
#define FGINT_M             BIT (5)
#define GPIOINT_M           BIT (4)
#define RTCINT_M            BIT (3)
#define CHGAINT_M           BIT (2)
#define LDOINT_M            BIT (1)
#define UICINT_M            BIT (0)

#define INTTOP2_REG         0xA4
#define INTTOP2M_REG        0xA5

#define ADCINT_M            BIT (7)
#define WLEDINT_M           BIT (6)
#define EPDINT_M            BIT (5)
#define CHGBINT_M           BIT (4)

#define FGINT_REG           0xA7
#define FGINTM_REG          0xA8

#define FGINT_SMX_M         MAX77696_FGIRQMASK_SMX
#define FGINT_SMN_M         MAX77696_FGIRQMASK_SMN
#define FGINT_VMX_M         MAX77696_FGIRQMASK_VMX
#define FGINT_VMN_M         MAX77696_FGIRQMASK_VMN
#define FGINT_TMX_M         MAX77696_FGIRQMASK_TMX
#define FGINT_TMN_M         MAX77696_FGIRQMASK_TMN

#define ERCFLAG0_REG        0xAA
#define ERCFLAG0_MR_FRSTRT	BIT (3)
#define ERCFLAG0_MR_FSHDN	BIT (2)
#define ERCFLAG0_WDPMIC_FRSTRT	BIT (1)
#define ERCFLAG0_WDPMIC_FSHDN	BIT (0)

#define ERCFLAG1_REG        0xAB
#define ERCFLAG1_RSTIN_PRSTRT	BIT (3)
#define ERCFLAG1_TOVLO_FSHDN	BIT (2)
#define ERCFLAG1_SYS1UVLO_FSHDN	BIT (1)
#define ERCFLAG1_LBMOK_FSHDN	BIT (0)


/* 32k */
#define OSC_32KLOAD_REG         0x7E
//      RESERVED                BITS(7,2)
#define OSC_32KLOAD_32KLOAD_M   BITS(1,0)
#define OSC_32KLOAD_32KLOAD_S   0

#define OSC_32KSTAT_REG         0x7F
//      RESERVED                BITS(7,2)
#define OSC_32KSTAT_MODE_SET_M  BIT (1)
#define OSC_32KSTAT_MODE_SET_S  1
#define OSC_32KSTAT_32KSTAT_M   BIT (0)
#define OSC_32KSTAT_32KSTAT_S   0

#define OSC_REG(reg)                   ((u8)(OSC_##reg##_REG))
#define OSC_REG_BITMASK(reg, bit)      ((u8)(OSC_##reg##_##bit##_M))
#define OSC_REG_BITSHIFT(reg, bit)          (OSC_##reg##_##bit##_S)

#define OSC_REG_BITGET(reg, bit, val) \
        ((u8)(((val) & OSC_REG_BITMASK(reg, bit))\
        >> OSC_REG_BITSHIFT(reg, bit)))
#define OSC_REG_BITSET(reg, bit, val) \
        ((u8)(((val) << OSC_REG_BITSHIFT(reg, bit))\
        & OSC_REG_BITMASK(reg, bit)))

/* ADC */
#define ADC_ADCCNTL_REG                0x26
//      RESERVED                       BITS(7,5)
#define ADC_ADCCNTL_ADCCONV_M          BIT (4)
#define ADC_ADCCNTL_ADCCONV_S          4
#define ADC_ADCCNTL_ADCAVG_M           BITS(3,2)
#define ADC_ADCCNTL_ADCAVG_S           2
#define ADC_ADCCNTL_ADCREFEN_M         BIT (1)
#define ADC_ADCCNTL_ADCREFEN_S         1
#define ADC_ADCCNTL_ADCEN_M            BIT (0)
#define ADC_ADCCNTL_ADCEN_S            0

#define ADC_ADCDLY_REG                 0x27
//      RESERVED                       BITS(7,5)
#define ADC_ADCDLY_ADCDLY_M            BITS(4,0)
#define ADC_ADCDLY_ADCDLY_S            0

#define ADC_ADCSEL0_REG                0x28
#define ADC_ADCSEL1_REG                0x29

#define ADC_ADCCHSEL_REG               0x2A
//      RESERVED                       BITS(7,5)
#define ADC_ADCCHSEL_ADCCH_M           BITS(4,0)
#define ADC_ADCCHSEL_ADCCH_S           0

#define ADC_ADCDATAL_REG               0x2B
#define ADC_ADCDATAH_REG               0x2C

#define ADC_ADCINT_REG                 0x2E
#define ADC_ADCINTM_REG                0x2F

#define ADC_ADCICNFG_REG               0x30
//      RESERVED                       BITS(7,4)
#define ADC_ADCICNFG_IADCMUX_M         BITS(3,2)
#define ADC_ADCICNFG_IADCMUX_S         2
#define ADC_ADCICNFG_IADC_M            BITS(1,0)
#define ADC_ADCICNFG_IADC_S            0

#define ADC_REG(reg)                   ((u8)(ADC_##reg##_REG))
#define ADC_REG_BITMASK(reg, bit)      ((u8)(ADC_##reg##_##bit##_M))
#define ADC_REG_BITSHIFT(reg, bit)          (ADC_##reg##_##bit##_S)

#define ADC_REG_BITGET(reg, bit, val) \
        ((u8)(((val) & ADC_REG_BITMASK(reg, bit))\
        >> ADC_REG_BITSHIFT(reg, bit)))
#define ADC_REG_BITSET(reg, bit, val) \
        ((u8)(((val) << ADC_REG_BITSHIFT(reg, bit))\
        & ADC_REG_BITMASK(reg, bit)))

/* ADC Sample Average Rate */
#define ADC_AVG_RATE_1_SAMPLE     0
#define ADC_AVG_RATE_2_SAMPLES    1
#define ADC_AVG_RATE_16_SAMPLES   2
#define ADC_AVG_RATE_32_SAMPLES   3


/* GPIO */
#define GPIO_CNFG_GPIO0_REG          0x75
#define GPIO_CNFG_GPIO1_REG          0x76
#define GPIO_CNFG_GPIO2_REG          0x77
#define GPIO_CNFG_GPIO3_REG          0x78
#define GPIO_CNFG_GPIO4_REG          0x79
#define GPIO_CNFG_GPIO_REG(gpio)     (GPIO_CNFG_GPIO0_REG+(gpio))

#define GPIO_CNFG_GPIO_DBNC_M        BITS(7,6)
#define GPIO_CNFG_GPIO_DBNC_S        6
#define GPIO_CNFG_GPIO_REFE_IRQ_M    BITS(5,4)
#define GPIO_CNFG_GPIO_REFE_IRQ_S    4
#define GPIO_CNFG_GPIO_DO_M          BIT (3)
#define GPIO_CNFG_GPIO_DO_S          3
#define GPIO_CNFG_GPIO_DI_M          BIT (2)
#define GPIO_CNFG_GPIO_DI_S          2
#define GPIO_CNFG_GPIO_DIR_M         BIT (1)
#define GPIO_CNFG_GPIO_DIR_S         1
#define GPIO_CNFG_GPIO_PPDRV_M       BIT (0)
#define GPIO_CNFG_GPIO_PPDRV_S       0

#define GPIO_PUE_GPIO_REG            0x7A
#define GPIO_PUE_GPIO_GPIO_BIAS_EN_M BIT (7)
#define GPIO_PUE_GPIO_GPIO_BIAS_EN_S 7

#define GPIO_PDE_GPIO_REG            0x7B
#define GPIO_AME_GPIO_REG            0x7C

#define GPIO_IRQ_LVL2_REG            0x7D

#define GPIO_REG(reg)                  ((u8)(GPIO_##reg##_REG))
#define GPIO_REG_BITMASK(reg, bit)     ((u8)(GPIO_##reg##_##bit##_M))
#define GPIO_REG_BITSHIFT(reg, bit)         (GPIO_##reg##_##bit##_S)

#define GPIO_REG_BITGET(reg, bit, val) \
        ((u8)(((val) & GPIO_REG_BITMASK(reg, bit))\
        >> GPIO_REG_BITSHIFT(reg, bit)))
#define GPIO_REG_BITSET(reg, bit, val) \
        ((u8)(((val) << GPIO_REG_BITSHIFT(reg, bit))\
        & GPIO_REG_BITMASK(reg, bit)))


#endif //MAX77696_REGISTERS_H
