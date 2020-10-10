/*
 * Copyright (C) 2012, Freescale Semiconductor, Inc. All Rights Reserved.
 * THIS SOURCE CODE IS CONFIDENTIAL AND PROPRIETARY AND MAY NOT
 * BE USED OR DISTRIBUTED WITHOUT THE WRITTEN PERMISSION OF
 * Freescale Semiconductor, Inc.
*/

// File: i2c2_iomux_config.c

#include <asm/io.h>
#include "include/iomux_define.h"
#include "include/iomux_register.h"
#include <mach/boardid.h>

// Function to config iomux for instance i2c2.
void i2c2_iomux_config(void)
{
    // Config i2c2.SCL to pad I2C2_SCL(E18)
    // Mux Register:
    // IOMUXC_SW_MUX_CTL_PAD_I2C2_SCL(0x020E0164)
    //   SION (4) - Software Input On Field Reset: SION_DISABLED
    //              Force the selected mux mode Input path no matter of MUX_MODE functionality.
    //     SION_DISABLED (0) - Input Path is determined by functionality of the selected mux mode (regular).
    //     SION_ENABLED (1) - Force input path of pad I2C2_SCL.
    //   MUX_MODE (2-0) - MUX Mode Select Field Reset: ALT5
    //                    Select 1 of 8 iomux modes to be used for pad: I2C2_SCL.
    //     ALT0 (0) - Select mux mode: ALT0 mux port: SCL of instance: i2c2.
    //                NOTE: - Config Register IOMUXC_I2C2_IPP_SCL_IN_SELECT_INPUT for mode ALT0.
    //     ALT1 (1) - Select mux mode: ALT1 mux port: AUD4_RXFS of instance: audmux.
    //                NOTE: - Config Register IOMUXC_AUDMUX_P4_INPUT_RXFS_AMX_SELECT_INPUT for mode ALT1.
    //     ALT2 (2) - Select mux mode: ALT2 mux port: IN1 of instance: spdif.
    //                NOTE: - Config Register IOMUXC_SPDIF_SPDIF_IN1_SELECT_INPUT for mode ALT2.
    //     ALT3 (3) - Select mux mode: ALT3 mux port: TDATA[1] of instance: fec.
    //     ALT4 (4) - Select mux mode: ALT4 mux port: WP of instance: usdhc3.
    //                NOTE: - Config Register IOMUXC_USDHC3_IPP_WP_ON_SELECT_INPUT for mode ALT4.
    //     ALT5 (5) - Select mux mode: ALT5 mux port: GPIO[14] of instance: gpio3.
    //     ALT6 (6) - Select mux mode: ALT6 mux port: RDY of instance: ecspi1.
    //                NOTE: - Config Register IOMUXC_ECSPI1_IPP_IND_DATAREADY_B_SELECT_INPUT for mode ALT6.
     __raw_writel((SION_ENABLED & 0x1) << 4 | (ALT0 & 0x7), IOMUXC_SW_MUX_CTL_PAD_I2C2_SCL);
    // Pad I2C2_SCL is involved in Daisy Chain.
    // Input Select Register:
    // IOMUXC_I2C2_IPP_SCL_IN_SELECT_INPUT(0x020E0724)
    //   DAISY (1-0) Reset: SEL_EPDC_SDCLK_ALT2
    //                 Selecting Pads Involved in Daisy Chain.
    //                 NOTE: Instance: i2c2,   In Pin: ipp_scl_in
    //     SEL_EPDC_SDCLK_ALT2 (0) - Selecting Pad: EPDC_SDCLK for Mode: ALT2.
    //     SEL_I2C2_SCL_ALT0 (1) - Selecting Pad: I2C2_SCL for Mode: ALT0.
    //     SEL_KEY_COL0_ALT1 (2) - Selecting Pad: KEY_COL0 for Mode: ALT1.
    //     SEL_LCD_DAT16_ALT4 (3) - Selecting Pad: LCD_DAT16 for Mode: ALT4.
    __raw_writel((SEL_I2C2_SCL_ALT0 & 0x3), IOMUXC_I2C2_IPP_SCL_IN_SELECT_INPUT);
    // Pad Control Register:
    // IOMUXC_SW_PAD_CTL_PAD_I2C2_SCL(0x020E0454)
    //   LVE (22) - Low Voltage Enable Field Reset: LVE_DISABLED
    //              Select one out of next values for pad: I2C2_SCL.
    //     LVE_DISABLED (0) - High Voltage
    //     LVE_ENABLED (1) - Low Voltage
    //   HYS (16) - Hysteresis Enable Field Reset: HYS_ENABLED
    //              Select one out of next values for pad: I2C2_SCL.
    //     HYS_DISABLED (0) - Hysteresis Disabled
    //     HYS_ENABLED (1) - Hysteresis Enabled
    //   PUS (15-14) - Pull Up / Down Configure Field Reset: PUS_100KOHM_PD
    //                 Select one out of next values for pad: I2C2_SCL.
    //     PUS_100KOHM_PD (0) - 100K Ohm Pull Down
    //     PUS_47KOHM_PU (1) - 47K Ohm Pull Up
    //     PUS_100KOHM_PU (2) - 100K Ohm Pull Up
    //     PUS_22KOHM_PU (3) - 22K Ohm Pull Up
    //   PUE (13) - Pull / Keep Select Field Reset: PUE_KEEP
    //              Select one out of next values for pad: I2C2_SCL.
    //     PUE_KEEP (0) - Keeper
    //     PUE_PULL (1) - Pull
    //   PKE (12) - Pull / Keep Enable Field Reset: PKE_ENABLED
    //              Select one out of next values for pad: I2C2_SCL.
    //     PKE_DISABLED (0) - Pull/Keeper Disabled
    //     PKE_ENABLED (1) - Pull/Keeper Enabled
    //   ODE (11) - Open Drain Enable Field Reset: ODE_DISABLED
    //              Select one out of next values for pad: I2C2_SCL.
    //     ODE_DISABLED (0) - Open Drain Disabled
    //     ODE_ENABLED (1) - Open Drain Enabled
    //   SPEED (7-6) - Speed Field Reset: SPD_100MHZ
    //                 Select one out of next values for pad: I2C2_SCL.
    //     SPD_TBD (0) - TBD
    //     SPD_50MHZ (1) - Low(50 MHz)
    //     SPD_100MHZ (2) - Medium(100 MHz)
    //     SPD_200MHZ (3) - Maximum(200 MHz)
    //   DSE (5-3) - Drive Strength Field Reset: DSE_40OHM
    //               Select one out of next values for pad: I2C2_SCL.
    //     DSE_DISABLED (0) - Output driver disabled.
    //     DSE_240OHM (1) - 240 Ohm
    //     DSE_120OHM (2) - 120 Ohm
    //     DSE_80OHM (3) - 80 Ohm
    //     DSE_60OHM (4) - 60 Ohm
    //     DSE_48OHM (5) - 48 Ohm
    //     DSE_40OHM (6) - 40 Ohm
    //     DSE_34OHM (7) - 34 Ohm
    //   SRE (0) - Slew Rate Field Reset: SRE_SLOW
    //             Select one out of next values for pad: I2C2_SCL.
    //     SRE_SLOW (0) - Slow Slew Rate
    //     SRE_FAST (1) - Fast Slew Rate
	if  (lab126_board_is(BOARD_ID_BOURBON_WFO) ||
		 lab126_board_is(BOARD_ID_WARIO_4_256M_CFG_C) ||
		 lab126_board_is(BOARD_ID_BOURBON_WFO_PREEVT2) ) {
        __raw_writel((LVE_DISABLED & 0x1) << 22 | (HYS_ENABLED & 0x1) << 16 | (PUS_100KOHM_PD & 0x3) << 14 |
               (PUE_KEEP & 0x1) << 13 | (PKE_DISABLED & 0x1) << 12 | (ODE_ENABLED & 0x1) << 11 |
               (SPD_100MHZ & 0x3) << 6 | (DSE_40OHM & 0x7) << 3 | (SRE_SLOW & 0x1), IOMUXC_SW_PAD_CTL_PAD_I2C2_SCL);
    } else {
        __raw_writel((LVE_ENABLED & 0x1) << 22 | (HYS_ENABLED & 0x1) << 16 | (PUS_100KOHM_PD & 0x3) << 14 |
               (PUE_KEEP & 0x1) << 13 | (PKE_DISABLED & 0x1) << 12 | (ODE_ENABLED & 0x1) << 11 |
               (SPD_100MHZ & 0x3) << 6 | (DSE_40OHM & 0x7) << 3 | (SRE_SLOW & 0x1), IOMUXC_SW_PAD_CTL_PAD_I2C2_SCL);
    }

    // Config i2c2.SDA to pad I2C2_SDA(D18)
    // Mux Register:
    // IOMUXC_SW_MUX_CTL_PAD_I2C2_SDA(0x020E0168)
    //   SION (4) - Software Input On Field Reset: SION_DISABLED
    //              Force the selected mux mode Input path no matter of MUX_MODE functionality.
    //     SION_DISABLED (0) - Input Path is determined by functionality of the selected mux mode (regular).
    //     SION_ENABLED (1) - Force input path of pad I2C2_SDA.
    //   MUX_MODE (2-0) - MUX Mode Select Field Reset: ALT5
    //                    Select 1 of 8 iomux modes to be used for pad: I2C2_SDA.
    //     ALT0 (0) - Select mux mode: ALT0 mux port: SDA of instance: i2c2.
    //                NOTE: - Config Register IOMUXC_I2C2_IPP_SDA_IN_SELECT_INPUT for mode ALT0.
    //     ALT1 (1) - Select mux mode: ALT1 mux port: AUD4_RXC of instance: audmux.
    //                NOTE: - Config Register IOMUXC_AUDMUX_P4_INPUT_RXCLK_AMX_SELECT_INPUT for mode ALT1.
    //     ALT2 (2) - Select mux mode: ALT2 mux port: OUT1 of instance: spdif.
    //     ALT3 (3) - Select mux mode: ALT3 mux port: REF_OUT of instance: fec.
    //     ALT4 (4) - Select mux mode: ALT4 mux port: CD of instance: usdhc3.
    //                NOTE: - Config Register IOMUXC_USDHC3_IPP_CARD_DET_SELECT_INPUT for mode ALT4.
    //     ALT5 (5) - Select mux mode: ALT5 mux port: GPIO[15] of instance: gpio3.
     __raw_writel((SION_ENABLED & 0x1) << 4 | (ALT0 & 0x7), IOMUXC_SW_MUX_CTL_PAD_I2C2_SDA);
    // Pad I2C2_SDA is involved in Daisy Chain.
    // Input Select Register:
    // IOMUXC_I2C2_IPP_SDA_IN_SELECT_INPUT(0x020E0728)
    //   DAISY (1-0) Reset: SEL_EPDC_SDLE_ALT2
    //                 Selecting Pads Involved in Daisy Chain.
    //                 NOTE: Instance: i2c2,   In Pin: ipp_sda_in
    //     SEL_EPDC_SDLE_ALT2 (0) - Selecting Pad: EPDC_SDLE for Mode: ALT2.
    //     SEL_I2C2_SDA_ALT0 (1) - Selecting Pad: I2C2_SDA for Mode: ALT0.
    //     SEL_KEY_ROW0_ALT1 (2) - Selecting Pad: KEY_ROW0 for Mode: ALT1.
    //     SEL_LCD_DAT17_ALT4 (3) - Selecting Pad: LCD_DAT17 for Mode: ALT4.
    __raw_writel((SEL_I2C2_SDA_ALT0 & 0x3), IOMUXC_I2C2_IPP_SDA_IN_SELECT_INPUT);
    // Pad Control Register:
    // IOMUXC_SW_PAD_CTL_PAD_I2C2_SDA(0x020E0458)
    //   LVE (22) - Low Voltage Enable Field Reset: LVE_DISABLED
    //              Select one out of next values for pad: I2C2_SDA.
    //     LVE_DISABLED (0) - High Voltage
    //     LVE_ENABLED (1) - Low Voltage
    //   HYS (16) - Hysteresis Enable Field Reset: HYS_ENABLED
    //              Select one out of next values for pad: I2C2_SDA.
    //     HYS_DISABLED (0) - Hysteresis Disabled
    //     HYS_ENABLED (1) - Hysteresis Enabled
    //   PUS (15-14) - Pull Up / Down Configure Field Reset: PUS_100KOHM_PD
    //                 Select one out of next values for pad: I2C2_SDA.
    //     PUS_100KOHM_PD (0) - 100K Ohm Pull Down
    //     PUS_47KOHM_PU (1) - 47K Ohm Pull Up
    //     PUS_100KOHM_PU (2) - 100K Ohm Pull Up
    //     PUS_22KOHM_PU (3) - 22K Ohm Pull Up
    //   PUE (13) - Pull / Keep Select Field Reset: PUE_KEEP
    //              Select one out of next values for pad: I2C2_SDA.
    //     PUE_KEEP (0) - Keeper
    //     PUE_PULL (1) - Pull
    //   PKE (12) - Pull / Keep Enable Field Reset: PKE_ENABLED
    //              Select one out of next values for pad: I2C2_SDA.
    //     PKE_DISABLED (0) - Pull/Keeper Disabled
    //     PKE_ENABLED (1) - Pull/Keeper Enabled
    //   ODE (11) - Open Drain Enable Field Reset: ODE_DISABLED
    //              Select one out of next values for pad: I2C2_SDA.
    //     ODE_DISABLED (0) - Open Drain Disabled
    //     ODE_ENABLED (1) - Open Drain Enabled
    //   SPEED (7-6) - Speed Field Reset: SPD_100MHZ
    //                 Select one out of next values for pad: I2C2_SDA.
    //     SPD_TBD (0) - TBD
    //     SPD_50MHZ (1) - Low(50 MHz)
    //     SPD_100MHZ (2) - Medium(100 MHz)
    //     SPD_200MHZ (3) - Maximum(200 MHz)
    //   DSE (5-3) - Drive Strength Field Reset: DSE_40OHM
    //               Select one out of next values for pad: I2C2_SDA.
    //     DSE_DISABLED (0) - Output driver disabled.
    //     DSE_240OHM (1) - 240 Ohm
    //     DSE_120OHM (2) - 120 Ohm
    //     DSE_80OHM (3) - 80 Ohm
    //     DSE_60OHM (4) - 60 Ohm
    //     DSE_48OHM (5) - 48 Ohm
    //     DSE_40OHM (6) - 40 Ohm
    //     DSE_34OHM (7) - 34 Ohm
    //   SRE (0) - Slew Rate Field Reset: SRE_SLOW
    //             Select one out of next values for pad: I2C2_SDA.
    //     SRE_SLOW (0) - Slow Slew Rate
    //     SRE_FAST (1) - Fast Slew Rate
	if  (lab126_board_is(BOARD_ID_BOURBON_WFO) ||
		 lab126_board_is(BOARD_ID_WARIO_4_256M_CFG_C) ||
		 lab126_board_is(BOARD_ID_BOURBON_WFO_PREEVT2) ) {
        __raw_writel((LVE_DISABLED & 0x1) << 22 | (HYS_ENABLED & 0x1) << 16 | (PUS_100KOHM_PD & 0x3) << 14 |
               (PUE_KEEP & 0x1) << 13 | (PKE_DISABLED & 0x1) << 12 | (ODE_ENABLED & 0x1) << 11 |
               (SPD_100MHZ & 0x3) << 6 | (DSE_40OHM & 0x7) << 3 | (SRE_SLOW & 0x1), IOMUXC_SW_PAD_CTL_PAD_I2C2_SDA);
    } else {
        __raw_writel((LVE_ENABLED & 0x1) << 22 | (HYS_ENABLED & 0x1) << 16 | (PUS_100KOHM_PD & 0x3) << 14 |
               (PUE_KEEP & 0x1) << 13 | (PKE_DISABLED & 0x1) << 12 | (ODE_ENABLED & 0x1) << 11 |
               (SPD_100MHZ & 0x3) << 6 | (DSE_40OHM & 0x7) << 3 | (SRE_SLOW & 0x1), IOMUXC_SW_PAD_CTL_PAD_I2C2_SDA);
    }
}
