/*
 * Copyright (C) 2012, Freescale Semiconductor, Inc. All Rights Reserved.
 * THIS SOURCE CODE IS CONFIDENTIAL AND PROPRIETARY AND MAY NOT
 * BE USED OR DISTRIBUTED WITHOUT THE WRITTEN PERMISSION OF
 * Freescale Semiconductor, Inc.
*/

// File: csi_iomux_config.c

#include <asm/io.h>
#include "include/iomux_define.h"
#include "include/iomux_register.h"

// Function to config iomux for instance csi.
void csi_iomux_config(void)
{
    // Config csi.I2C3_SCL to pad EPDC_SDCE2(B9)
    // WAS Instance:i2c3 Signal:SCL
    // Mux Register:
    // IOMUXC_SW_MUX_CTL_PAD_EPDC_SDCE2(0x020E0108)
    //   SION (4) - Software Input On Field Reset: SION_DISABLED
    //              Force the selected mux mode Input path no matter of MUX_MODE functionality.
    //     SION_DISABLED (0) - Input Path is determined by functionality of the selected mux mode (regular).
    //     SION_ENABLED (1) - Force input path of pad EPDC_SDCE2.
    //   MUX_MODE (2-0) - MUX Mode Select Field Reset: ALT5
    //                    Select 1 of 8 iomux modes to be used for pad: EPDC_SDCE2.
    //     ALT0 (0) - Select mux mode: ALT0 mux port: SDCE[2] of instance: epdc.
    //     ALT1 (1) - Select mux mode: ALT1 mux port: SCL of instance: i2c3.
    //                NOTE: - Config Register IOMUXC_I2C3_IPP_SCL_IN_SELECT_INPUT for mode ALT1.
    //     ALT2 (2) - Select mux mode: ALT2 mux port: PWMO of instance: pwm1.
    //     ALT3 (3) - Select mux mode: ALT3 mux port: WEIM_EB[0] of instance: weim.
    //     ALT4 (4) - Select mux mode: ALT4 mux port: YDIOUR of instance: spdc.
    //     ALT5 (5) - Select mux mode: ALT5 mux port: GPIO[29] of instance: gpio1.
    __raw_writel((SION_ENABLED & 0x1) << 4 | (ALT1 & 0x7), IOMUXC_SW_MUX_CTL_PAD_EPDC_SDCE2);
    // Pad EPDC_SDCE2 is involved in Daisy Chain.
    // Input Select Register:
    // IOMUXC_I2C3_IPP_SCL_IN_SELECT_INPUT(0x020E072C)
    //   DAISY (1-0) Reset: SEL_AUD_RXFS_ALT4
    //                 Selecting Pads Involved in Daisy Chain.
    //                 NOTE: Instance: i2c3,   In Pin: ipp_scl_in
    //     SEL_AUD_RXFS_ALT4 (0) - Selecting Pad: AUD_RXFS for Mode: ALT4.
    //     SEL_EPDC_SDCE2_ALT1 (1) - Selecting Pad: EPDC_SDCE2 for Mode: ALT1.
    //     SEL_REF_CLK_24M_ALT1 (2) - Selecting Pad: REF_CLK_24M for Mode: ALT1.
    __raw_writel((SEL_EPDC_SDCE2_ALT1 & 0x3), IOMUXC_I2C3_IPP_SCL_IN_SELECT_INPUT);
    // Pad Control Register:
    // IOMUXC_SW_PAD_CTL_PAD_EPDC_SDCE2(0x020E03F8)
    //   LVE (22) - Low Voltage Enable Field Reset: LVE_DISABLED
    //              Select one out of next values for pad: EPDC_SDCE2.
    //     LVE_DISABLED (0) - High Voltage
    //     LVE_ENABLED (1) - Low Voltage
    //   HYS (16) - Hysteresis Enable Field Reset: HYS_ENABLED
    //              Select one out of next values for pad: EPDC_SDCE2.
    //     HYS_DISABLED (0) - Hysteresis Disabled
    //     HYS_ENABLED (1) - Hysteresis Enabled
    //   PUS (15-14) - Pull Up / Down Configure Field Reset: PUS_100KOHM_PD
    //                 Select one out of next values for pad: EPDC_SDCE2.
    //     PUS_100KOHM_PD (0) - 100K Ohm Pull Down
    //     PUS_47KOHM_PU (1) - 47K Ohm Pull Up
    //     PUS_100KOHM_PU (2) - 100K Ohm Pull Up
    //     PUS_22KOHM_PU (3) - 22K Ohm Pull Up
    //   PUE (13) - Pull / Keep Select Field Reset: PUE_KEEP
    //              Select one out of next values for pad: EPDC_SDCE2.
    //     PUE_KEEP (0) - Keeper
    //     PUE_PULL (1) - Pull
    //   PKE (12) - Pull / Keep Enable Field Reset: PKE_ENABLED
    //              Select one out of next values for pad: EPDC_SDCE2.
    //     PKE_DISABLED (0) - Pull/Keeper Disabled
    //     PKE_ENABLED (1) - Pull/Keeper Enabled
    //   ODE (11) - Open Drain Enable Field Reset: ODE_DISABLED
    //              Select one out of next values for pad: EPDC_SDCE2.
    //     ODE_DISABLED (0) - Open Drain Disabled
    //     ODE_ENABLED (1) - Open Drain Enabled
    //   SPEED (7-6) - Speed Field Reset: SPD_100MHZ
    //                 Select one out of next values for pad: EPDC_SDCE2.
    //     SPD_TBD (0) - TBD
    //     SPD_50MHZ (1) - Low(50 MHz)
    //     SPD_100MHZ (2) - Medium(100 MHz)
    //     SPD_200MHZ (3) - Maximum(200 MHz)
    //   DSE (5-3) - Drive Strength Field Reset: DSE_40OHM
    //               Select one out of next values for pad: EPDC_SDCE2.
    //     DSE_DISABLED (0) - Output driver disabled.
    //     DSE_240OHM (1) - 240 Ohm
    //     DSE_120OHM (2) - 120 Ohm
    //     DSE_80OHM (3) - 80 Ohm
    //     DSE_60OHM (4) - 60 Ohm
    //     DSE_48OHM (5) - 48 Ohm
    //     DSE_40OHM (6) - 40 Ohm
    //     DSE_34OHM (7) - 34 Ohm
    //   SRE (0) - Slew Rate Field Reset: SRE_SLOW
    //             Select one out of next values for pad: EPDC_SDCE2.
    //     SRE_SLOW (0) - Slow Slew Rate
    //     SRE_FAST (1) - Fast Slew Rate
    __raw_writel((LVE_ENABLED & 0x1) << 22 | (HYS_ENABLED & 0x1) << 16 | (PUS_100KOHM_PD & 0x3) << 14 |
           (PUE_KEEP & 0x1) << 13 | (PKE_DISABLED & 0x1) << 12 | (ODE_ENABLED & 0x1) << 11 |
           (SPD_100MHZ & 0x3) << 6 | (DSE_40OHM & 0x7) << 3 | (SRE_SLOW & 0x1), IOMUXC_SW_PAD_CTL_PAD_EPDC_SDCE2);

    // Config csi.I2C3_SDA to pad EPDC_SDCE3(A9)
    // WAS Instance:i2c3 Signal:SDA
    // Mux Register:
    // IOMUXC_SW_MUX_CTL_PAD_EPDC_SDCE3(0x020E010C)
    //   SION (4) - Software Input On Field Reset: SION_DISABLED
    //              Force the selected mux mode Input path no matter of MUX_MODE functionality.
    //     SION_DISABLED (0) - Input Path is determined by functionality of the selected mux mode (regular).
    //     SION_ENABLED (1) - Force input path of pad EPDC_SDCE3.
    //   MUX_MODE (2-0) - MUX Mode Select Field Reset: ALT5
    //                    Select 1 of 8 iomux modes to be used for pad: EPDC_SDCE3.
    //     ALT0 (0) - Select mux mode: ALT0 mux port: SDCE[3] of instance: epdc.
    //     ALT1 (1) - Select mux mode: ALT1 mux port: SDA of instance: i2c3.
    //                NOTE: - Config Register IOMUXC_I2C3_IPP_SDA_IN_SELECT_INPUT for mode ALT1.
    //     ALT2 (2) - Select mux mode: ALT2 mux port: PWMO of instance: pwm2.
    //     ALT3 (3) - Select mux mode: ALT3 mux port: WEIM_EB[1] of instance: weim.
    //     ALT4 (4) - Select mux mode: ALT4 mux port: YDIODR of instance: spdc.
    //     ALT5 (5) - Select mux mode: ALT5 mux port: GPIO[30] of instance: gpio1.
    __raw_writel((SION_ENABLED & 0x1) << 4 | (ALT1 & 0x7), IOMUXC_SW_MUX_CTL_PAD_EPDC_SDCE3);
    // Pad EPDC_SDCE3 is involved in Daisy Chain.
    // Input Select Register:
    // IOMUXC_I2C3_IPP_SDA_IN_SELECT_INPUT(0x020E0730)
    //   DAISY (1-0) Reset: SEL_AUD_RXC_ALT4
    //                 Selecting Pads Involved in Daisy Chain.
    //                 NOTE: Instance: i2c3,   In Pin: ipp_sda_in
    //     SEL_AUD_RXC_ALT4 (0) - Selecting Pad: AUD_RXC for Mode: ALT4.
    //     SEL_EPDC_SDCE3_ALT1 (1) - Selecting Pad: EPDC_SDCE3 for Mode: ALT1.
    //     SEL_REF_CLK_32K_ALT1 (2) - Selecting Pad: REF_CLK_32K for Mode: ALT1.
    __raw_writel((SEL_EPDC_SDCE3_ALT1 & 0x3), IOMUXC_I2C3_IPP_SDA_IN_SELECT_INPUT);
    // Pad Control Register:
    // IOMUXC_SW_PAD_CTL_PAD_EPDC_SDCE3(0x020E03FC)
    //   LVE (22) - Low Voltage Enable Field Reset: LVE_DISABLED
    //              Select one out of next values for pad: EPDC_SDCE3.
    //     LVE_DISABLED (0) - High Voltage
    //     LVE_ENABLED (1) - Low Voltage
    //   HYS (16) - Hysteresis Enable Field Reset: HYS_ENABLED
    //              Select one out of next values for pad: EPDC_SDCE3.
    //     HYS_DISABLED (0) - Hysteresis Disabled
    //     HYS_ENABLED (1) - Hysteresis Enabled
    //   PUS (15-14) - Pull Up / Down Configure Field Reset: PUS_100KOHM_PD
    //                 Select one out of next values for pad: EPDC_SDCE3.
    //     PUS_100KOHM_PD (0) - 100K Ohm Pull Down
    //     PUS_47KOHM_PU (1) - 47K Ohm Pull Up
    //     PUS_100KOHM_PU (2) - 100K Ohm Pull Up
    //     PUS_22KOHM_PU (3) - 22K Ohm Pull Up
    //   PUE (13) - Pull / Keep Select Field Reset: PUE_KEEP
    //              Select one out of next values for pad: EPDC_SDCE3.
    //     PUE_KEEP (0) - Keeper
    //     PUE_PULL (1) - Pull
    //   PKE (12) - Pull / Keep Enable Field Reset: PKE_ENABLED
    //              Select one out of next values for pad: EPDC_SDCE3.
    //     PKE_DISABLED (0) - Pull/Keeper Disabled
    //     PKE_ENABLED (1) - Pull/Keeper Enabled
    //   ODE (11) - Open Drain Enable Field Reset: ODE_DISABLED
    //              Select one out of next values for pad: EPDC_SDCE3.
    //     ODE_DISABLED (0) - Open Drain Disabled
    //     ODE_ENABLED (1) - Open Drain Enabled
    //   SPEED (7-6) - Speed Field Reset: SPD_100MHZ
    //                 Select one out of next values for pad: EPDC_SDCE3.
    //     SPD_TBD (0) - TBD
    //     SPD_50MHZ (1) - Low(50 MHz)
    //     SPD_100MHZ (2) - Medium(100 MHz)
    //     SPD_200MHZ (3) - Maximum(200 MHz)
    //   DSE (5-3) - Drive Strength Field Reset: DSE_40OHM
    //               Select one out of next values for pad: EPDC_SDCE3.
    //     DSE_DISABLED (0) - Output driver disabled.
    //     DSE_240OHM (1) - 240 Ohm
    //     DSE_120OHM (2) - 120 Ohm
    //     DSE_80OHM (3) - 80 Ohm
    //     DSE_60OHM (4) - 60 Ohm
    //     DSE_48OHM (5) - 48 Ohm
    //     DSE_40OHM (6) - 40 Ohm
    //     DSE_34OHM (7) - 34 Ohm
    //   SRE (0) - Slew Rate Field Reset: SRE_SLOW
    //             Select one out of next values for pad: EPDC_SDCE3.
    //     SRE_SLOW (0) - Slow Slew Rate
    //     SRE_FAST (1) - Fast Slew Rate
    __raw_writel((LVE_ENABLED & 0x1) << 22 | (HYS_ENABLED & 0x1) << 16 | (PUS_100KOHM_PD & 0x3) << 14 |
           (PUE_KEEP & 0x1) << 13 | (PKE_DISABLED & 0x1) << 12 | (ODE_ENABLED & 0x1) << 11 |
           (SPD_100MHZ & 0x3) << 6 | (DSE_40OHM & 0x7) << 3 | (SRE_SLOW & 0x1), IOMUXC_SW_PAD_CTL_PAD_EPDC_SDCE3);
}
