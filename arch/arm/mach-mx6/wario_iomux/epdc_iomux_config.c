/*
 * Copyright (C) 2012, Freescale Semiconductor, Inc. All Rights Reserved.
 * THIS SOURCE CODE IS CONFIDENTIAL AND PROPRIETARY AND MAY NOT
 * BE USED OR DISTRIBUTED WITHOUT THE WRITTEN PERMISSION OF
 * Freescale Semiconductor, Inc.
*/

// File: epdc_iomux_config.c

#include <asm/io.h>
#include "include/iomux_define.h"
#include "include/iomux_register.h"

// Function to config iomux for instance epdc.
void epdc_iomux_config(void)
{
    // Config epdc.GDCLK to pad EPDC_GDCLK(A12)
    // Mux Register:
    // IOMUXC_SW_MUX_CTL_PAD_EPDC_GDCLK(0x020E00D0)
    //   SION (4) - Software Input On Field Reset: SION_DISABLED
    //              Force the selected mux mode Input path no matter of MUX_MODE functionality.
    //     SION_DISABLED (0) - Input Path is determined by functionality of the selected mux mode (regular).
    //     SION_ENABLED (1) - Force input path of pad EPDC_GDCLK.
    //   MUX_MODE (2-0) - MUX Mode Select Field Reset: ALT5
    //                    Select 1 of 8 iomux modes to be used for pad: EPDC_GDCLK.
    //     ALT0 (0) - Select mux mode: ALT0 mux port: GDCLK of instance: epdc.
    //     ALT1 (1) - Select mux mode: ALT1 mux port: SS2 of instance: ecspi2.
    //     ALT2 (2) - Select mux mode: ALT2 mux port: YCKR of instance: spdc.
    //     ALT3 (3) - Select mux mode: ALT3 mux port: PIXCLK of instance: csi.
    //                NOTE: - Config Register IOMUXC_CSI_IPP_CSI_PIXCLK_SELECT_INPUT for mode ALT3.
    //     ALT4 (4) - Select mux mode: ALT4 mux port: YCKL of instance: spdc.
    //     ALT5 (5) - Select mux mode: ALT5 mux port: GPIO[31] of instance: gpio1.
    //     ALT6 (6) - Select mux mode: ALT6 mux port: RST of instance: usdhc2.
    __raw_writel((SION_DISABLED & 0x1) << 4 | (ALT0 & 0x7), (IOMUXC_SW_MUX_CTL_PAD_EPDC_GDCLK));
    // Pad Control Register:
    // IOMUXC_SW_PAD_CTL_PAD_EPDC_GDCLK(0x020E03C0)
    //   LVE (22) - Low Voltage Enable Field Reset: LVE_DISABLED
    //              Select one out of next values for pad: EPDC_GDCLK.
    //     LVE_DISABLED (0) - High Voltage
    //     LVE_ENABLED (1) - Low Voltage
    //   HYS (16) - Hysteresis Enable Field Reset: HYS_ENABLED
    //              Select one out of next values for pad: EPDC_GDCLK.
    //     HYS_DISABLED (0) - Hysteresis Disabled
    //     HYS_ENABLED (1) - Hysteresis Enabled
    //   PUS (15-14) - Pull Up / Down Configure Field Reset: PUS_100KOHM_PD
    //                 Select one out of next values for pad: EPDC_GDCLK.
    //     PUS_100KOHM_PD (0) - 100K Ohm Pull Down
    //     PUS_47KOHM_PU (1) - 47K Ohm Pull Up
    //     PUS_100KOHM_PU (2) - 100K Ohm Pull Up
    //     PUS_22KOHM_PU (3) - 22K Ohm Pull Up
    //   PUE (13) - Pull / Keep Select Field Reset: PUE_KEEP
    //              Select one out of next values for pad: EPDC_GDCLK.
    //     PUE_KEEP (0) - Keeper
    //     PUE_PULL (1) - Pull
    //   PKE (12) - Pull / Keep Enable Field Reset: PKE_ENABLED
    //              Select one out of next values for pad: EPDC_GDCLK.
    //     PKE_DISABLED (0) - Pull/Keeper Disabled
    //     PKE_ENABLED (1) - Pull/Keeper Enabled
    //   ODE (11) - Open Drain Enable Field Reset: ODE_DISABLED
    //              Select one out of next values for pad: EPDC_GDCLK.
    //     ODE_DISABLED (0) - Open Drain Disabled
    //     ODE_ENABLED (1) - Open Drain Enabled
    //   SPEED (7-6) - Speed Field Reset: SPD_100MHZ
    //                 Select one out of next values for pad: EPDC_GDCLK.
    //     SPD_TBD (0) - TBD
    //     SPD_50MHZ (1) - Low(50 MHz)
    //     SPD_100MHZ (2) - Medium(100 MHz)
    //     SPD_200MHZ (3) - Maximum(200 MHz)
    //   DSE (5-3) - Drive Strength Field Reset: DSE_40OHM
    //               Select one out of next values for pad: EPDC_GDCLK.
    //     DSE_DISABLED (0) - Output driver disabled.
    //     DSE_240OHM (1) - 240 Ohm
    //     DSE_120OHM (2) - 120 Ohm
    //     DSE_80OHM (3) - 80 Ohm
    //     DSE_60OHM (4) - 60 Ohm
    //     DSE_48OHM (5) - 48 Ohm
    //     DSE_40OHM (6) - 40 Ohm
    //     DSE_34OHM (7) - 34 Ohm
    //   SRE (0) - Slew Rate Field Reset: SRE_SLOW
    //             Select one out of next values for pad: EPDC_GDCLK.
    //     SRE_SLOW (0) - Slow Slew Rate
    //     SRE_FAST (1) - Fast Slew Rate
    __raw_writel((LVE_DISABLED & 0x1) << 22 | (HYS_ENABLED & 0x1) << 16 | (PUS_100KOHM_PD & 0x3) << 14 |
           (PUE_KEEP & 0x1) << 13 | (PKE_ENABLED & 0x1) << 12 | (ODE_DISABLED & 0x1) << 11 |
           (SPD_100MHZ & 0x3) << 6 | (DSE_60OHM & 0x7) << 3 | (SRE_SLOW & 0x1), (IOMUXC_SW_PAD_CTL_PAD_EPDC_GDCLK));

    // Config epdc.GDOE to pad EPDC_GDOE(B13)
    // Mux Register:
    // IOMUXC_SW_MUX_CTL_PAD_EPDC_GDOE(0x020E00D4)
    //   SION (4) - Software Input On Field Reset: SION_DISABLED
    //              Force the selected mux mode Input path no matter of MUX_MODE functionality.
    //     SION_DISABLED (0) - Input Path is determined by functionality of the selected mux mode (regular).
    //     SION_ENABLED (1) - Force input path of pad EPDC_GDOE.
    //   MUX_MODE (2-0) - MUX Mode Select Field Reset: ALT5
    //                    Select 1 of 8 iomux modes to be used for pad: EPDC_GDOE.
    //     ALT0 (0) - Select mux mode: ALT0 mux port: GDOE of instance: epdc.
    //     ALT1 (1) - Select mux mode: ALT1 mux port: SS3 of instance: ecspi2.
    //     ALT2 (2) - Select mux mode: ALT2 mux port: YOER of instance: spdc.
    //     ALT3 (3) - Select mux mode: ALT3 mux port: HSYNC of instance: csi.
    //                NOTE: - Config Register IOMUXC_CSI_IPP_CSI_HSYNC_SELECT_INPUT for mode ALT3.
    //     ALT4 (4) - Select mux mode: ALT4 mux port: YOEL of instance: spdc.
    //     ALT5 (5) - Select mux mode: ALT5 mux port: GPIO[0] of instance: gpio2.
    //     ALT6 (6) - Select mux mode: ALT6 mux port: VSELECT of instance: usdhc2.
    __raw_writel((SION_DISABLED & 0x1) << 4 | (ALT0 & 0x7), (IOMUXC_SW_MUX_CTL_PAD_EPDC_GDOE));
    // Pad Control Register:
    // IOMUXC_SW_PAD_CTL_PAD_EPDC_GDOE(0x020E03C4)
    //   LVE (22) - Low Voltage Enable Field Reset: LVE_DISABLED
    //              Select one out of next values for pad: EPDC_GDOE.
    //     LVE_DISABLED (0) - High Voltage
    //     LVE_ENABLED (1) - Low Voltage
    //   HYS (16) - Hysteresis Enable Field Reset: HYS_ENABLED
    //              Select one out of next values for pad: EPDC_GDOE.
    //     HYS_DISABLED (0) - Hysteresis Disabled
    //     HYS_ENABLED (1) - Hysteresis Enabled
    //   PUS (15-14) - Pull Up / Down Configure Field Reset: PUS_100KOHM_PD
    //                 Select one out of next values for pad: EPDC_GDOE.
    //     PUS_100KOHM_PD (0) - 100K Ohm Pull Down
    //     PUS_47KOHM_PU (1) - 47K Ohm Pull Up
    //     PUS_100KOHM_PU (2) - 100K Ohm Pull Up
    //     PUS_22KOHM_PU (3) - 22K Ohm Pull Up
    //   PUE (13) - Pull / Keep Select Field Reset: PUE_KEEP
    //              Select one out of next values for pad: EPDC_GDOE.
    //     PUE_KEEP (0) - Keeper
    //     PUE_PULL (1) - Pull
    //   PKE (12) - Pull / Keep Enable Field Reset: PKE_ENABLED
    //              Select one out of next values for pad: EPDC_GDOE.
    //     PKE_DISABLED (0) - Pull/Keeper Disabled
    //     PKE_ENABLED (1) - Pull/Keeper Enabled
    //   ODE (11) - Open Drain Enable Field Reset: ODE_DISABLED
    //              Select one out of next values for pad: EPDC_GDOE.
    //     ODE_DISABLED (0) - Open Drain Disabled
    //     ODE_ENABLED (1) - Open Drain Enabled
    //   SPEED (7-6) - Speed Field Reset: SPD_100MHZ
    //                 Select one out of next values for pad: EPDC_GDOE.
    //     SPD_TBD (0) - TBD
    //     SPD_50MHZ (1) - Low(50 MHz)
    //     SPD_100MHZ (2) - Medium(100 MHz)
    //     SPD_200MHZ (3) - Maximum(200 MHz)
    //   DSE (5-3) - Drive Strength Field Reset: DSE_40OHM
    //               Select one out of next values for pad: EPDC_GDOE.
    //     DSE_DISABLED (0) - Output driver disabled.
    //     DSE_240OHM (1) - 240 Ohm
    //     DSE_120OHM (2) - 120 Ohm
    //     DSE_80OHM (3) - 80 Ohm
    //     DSE_60OHM (4) - 60 Ohm
    //     DSE_48OHM (5) - 48 Ohm
    //     DSE_40OHM (6) - 40 Ohm
    //     DSE_34OHM (7) - 34 Ohm
    //   SRE (0) - Slew Rate Field Reset: SRE_SLOW
    //             Select one out of next values for pad: EPDC_GDOE.
    //     SRE_SLOW (0) - Slow Slew Rate
    //     SRE_FAST (1) - Fast Slew Rate
    __raw_writel((LVE_DISABLED & 0x1) << 22 | (HYS_ENABLED & 0x1) << 16 | (PUS_100KOHM_PD & 0x3) << 14 |
           (PUE_KEEP & 0x1) << 13 | (PKE_ENABLED & 0x1) << 12 | (ODE_DISABLED & 0x1) << 11 |
           (SPD_100MHZ & 0x3) << 6 | (DSE_60OHM & 0x7) << 3 | (SRE_SLOW & 0x1), (IOMUXC_SW_PAD_CTL_PAD_EPDC_GDOE));

    // Config epdc.GDSP to pad EPDC_GDSP(A11)
    // Mux Register:
    // IOMUXC_SW_MUX_CTL_PAD_EPDC_GDSP(0x020E00DC)
    //   SION (4) - Software Input On Field Reset: SION_DISABLED
    //              Force the selected mux mode Input path no matter of MUX_MODE functionality.
    //     SION_DISABLED (0) - Input Path is determined by functionality of the selected mux mode (regular).
    //     SION_ENABLED (1) - Force input path of pad EPDC_GDSP.
    //   MUX_MODE (2-0) - MUX Mode Select Field Reset: ALT5
    //                    Select 1 of 8 iomux modes to be used for pad: EPDC_GDSP.
    //     ALT0 (0) - Select mux mode: ALT0 mux port: GDSP of instance: epdc.
    //     ALT1 (1) - Select mux mode: ALT1 mux port: PWMO of instance: pwm4.
    //     ALT2 (2) - Select mux mode: ALT2 mux port: YDIODR of instance: spdc.
    //     ALT3 (3) - Select mux mode: ALT3 mux port: VSYNC of instance: csi.
    //                NOTE: - Config Register IOMUXC_CSI_IPP_CSI_VSYNC_SELECT_INPUT for mode ALT3.
    //     ALT4 (4) - Select mux mode: ALT4 mux port: YDIODL of instance: spdc.
    //     ALT5 (5) - Select mux mode: ALT5 mux port: GPIO[2] of instance: gpio2.
    //     ALT6 (6) - Select mux mode: ALT6 mux port: CD of instance: usdhc2.
    //                NOTE: - Config Register IOMUXC_USDHC2_IPP_CARD_DET_SELECT_INPUT for mode ALT6.
    __raw_writel((SION_DISABLED & 0x1) << 4 | (ALT0 & 0x7), (IOMUXC_SW_MUX_CTL_PAD_EPDC_GDSP));
    // Pad Control Register:
    // IOMUXC_SW_PAD_CTL_PAD_EPDC_GDSP(0x020E03CC)
    //   LVE (22) - Low Voltage Enable Field Reset: LVE_DISABLED
    //              Select one out of next values for pad: EPDC_GDSP.
    //     LVE_DISABLED (0) - High Voltage
    //     LVE_ENABLED (1) - Low Voltage
    //   HYS (16) - Hysteresis Enable Field Reset: HYS_ENABLED
    //              Select one out of next values for pad: EPDC_GDSP.
    //     HYS_DISABLED (0) - Hysteresis Disabled
    //     HYS_ENABLED (1) - Hysteresis Enabled
    //   PUS (15-14) - Pull Up / Down Configure Field Reset: PUS_100KOHM_PD
    //                 Select one out of next values for pad: EPDC_GDSP.
    //     PUS_100KOHM_PD (0) - 100K Ohm Pull Down
    //     PUS_47KOHM_PU (1) - 47K Ohm Pull Up
    //     PUS_100KOHM_PU (2) - 100K Ohm Pull Up
    //     PUS_22KOHM_PU (3) - 22K Ohm Pull Up
    //   PUE (13) - Pull / Keep Select Field Reset: PUE_KEEP
    //              Select one out of next values for pad: EPDC_GDSP.
    //     PUE_KEEP (0) - Keeper
    //     PUE_PULL (1) - Pull
    //   PKE (12) - Pull / Keep Enable Field Reset: PKE_ENABLED
    //              Select one out of next values for pad: EPDC_GDSP.
    //     PKE_DISABLED (0) - Pull/Keeper Disabled
    //     PKE_ENABLED (1) - Pull/Keeper Enabled
    //   ODE (11) - Open Drain Enable Field Reset: ODE_DISABLED
    //              Select one out of next values for pad: EPDC_GDSP.
    //     ODE_DISABLED (0) - Open Drain Disabled
    //     ODE_ENABLED (1) - Open Drain Enabled
    //   SPEED (7-6) - Speed Field Reset: SPD_100MHZ
    //                 Select one out of next values for pad: EPDC_GDSP.
    //     SPD_TBD (0) - TBD
    //     SPD_50MHZ (1) - Low(50 MHz)
    //     SPD_100MHZ (2) - Medium(100 MHz)
    //     SPD_200MHZ (3) - Maximum(200 MHz)
    //   DSE (5-3) - Drive Strength Field Reset: DSE_40OHM
    //               Select one out of next values for pad: EPDC_GDSP.
    //     DSE_DISABLED (0) - Output driver disabled.
    //     DSE_240OHM (1) - 240 Ohm
    //     DSE_120OHM (2) - 120 Ohm
    //     DSE_80OHM (3) - 80 Ohm
    //     DSE_60OHM (4) - 60 Ohm
    //     DSE_48OHM (5) - 48 Ohm
    //     DSE_40OHM (6) - 40 Ohm
    //     DSE_34OHM (7) - 34 Ohm
    //   SRE (0) - Slew Rate Field Reset: SRE_SLOW
    //             Select one out of next values for pad: EPDC_GDSP.
    //     SRE_SLOW (0) - Slow Slew Rate
    //     SRE_FAST (1) - Fast Slew Rate
    __raw_writel((LVE_DISABLED & 0x1) << 22 | (HYS_ENABLED & 0x1) << 16 | (PUS_100KOHM_PD & 0x3) << 14 |
           (PUE_KEEP & 0x1) << 13 | (PKE_ENABLED & 0x1) << 12 | (ODE_DISABLED & 0x1) << 11 |
           (SPD_100MHZ & 0x3) << 6 | (DSE_60OHM & 0x7) << 3 | (SRE_SLOW & 0x1), (IOMUXC_SW_PAD_CTL_PAD_EPDC_GDSP));

    // Config epdc.PWRCOM to pad EPDC_PWRCOM(B11)
    // Mux Register:
    // IOMUXC_SW_MUX_CTL_PAD_EPDC_PWRCOM(0x020E00E0)
    //   SION (4) - Software Input On Field Reset: SION_DISABLED
    //              Force the selected mux mode Input path no matter of MUX_MODE functionality.
    //     SION_DISABLED (0) - Input Path is determined by functionality of the selected mux mode (regular).
    //     SION_ENABLED (1) - Force input path of pad EPDC_PWRCOM.
    //   MUX_MODE (2-0) - MUX Mode Select Field Reset: ALT5
    //                    Select 1 of 8 iomux modes to be used for pad: EPDC_PWRCOM.
    //     ALT0 (0) - Select mux mode: ALT0 mux port: PWRCOM of instance: epdc.
    //     ALT1 (1) - Select mux mode: ALT1 mux port: DAT0 of instance: usdhc4.
    //                NOTE: - Config Register IOMUXC_USDHC4_IPP_DAT0_IN_SELECT_INPUT for mode ALT1.
    //     ALT2 (2) - Select mux mode: ALT2 mux port: DAT[20] of instance: lcdif.
    //                NOTE: - Config Register IOMUXC_LCDIF_LCDIF_RXDATA_20_SELECT_INPUT for mode ALT2.
    //     ALT3 (3) - Select mux mode: ALT3 mux port: WEIM_BCLK of instance: weim.
    //     ALT4 (4) - Select mux mode: ALT4 mux port: USB_OTG1_ID of instance: usb.
    //                NOTE: - Config Register IOMUXC_ANALOG_USB_OTG_ID_SELECT_INPUT for mode ALT4.
    //     ALT5 (5) - Select mux mode: ALT5 mux port: GPIO[11] of instance: gpio2.
    //     ALT6 (6) - Select mux mode: ALT6 mux port: RST of instance: usdhc3.
    __raw_writel((SION_DISABLED & 0x1) << 4 | (ALT0 & 0x7), (IOMUXC_SW_MUX_CTL_PAD_EPDC_PWRCOM));
    // Pad Control Register:
    // IOMUXC_SW_PAD_CTL_PAD_EPDC_PWRCOM(0x020E03D0)
    //   LVE (22) - Low Voltage Enable Field Reset: LVE_DISABLED
    //              Select one out of next values for pad: EPDC_PWRCOM.
    //     LVE_DISABLED (0) - High Voltage
    //     LVE_ENABLED (1) - Low Voltage
    //   HYS (16) - Hysteresis Enable Field Reset: HYS_ENABLED
    //              Select one out of next values for pad: EPDC_PWRCOM.
    //     HYS_DISABLED (0) - Hysteresis Disabled
    //     HYS_ENABLED (1) - Hysteresis Enabled
    //   PUS (15-14) - Pull Up / Down Configure Field Reset: PUS_100KOHM_PD
    //                 Select one out of next values for pad: EPDC_PWRCOM.
    //     PUS_100KOHM_PD (0) - 100K Ohm Pull Down
    //     PUS_47KOHM_PU (1) - 47K Ohm Pull Up
    //     PUS_100KOHM_PU (2) - 100K Ohm Pull Up
    //     PUS_22KOHM_PU (3) - 22K Ohm Pull Up
    //   PUE (13) - Pull / Keep Select Field Reset: PUE_KEEP
    //              Select one out of next values for pad: EPDC_PWRCOM.
    //     PUE_KEEP (0) - Keeper
    //     PUE_PULL (1) - Pull
    //   PKE (12) - Pull / Keep Enable Field Reset: PKE_ENABLED
    //              Select one out of next values for pad: EPDC_PWRCOM.
    //     PKE_DISABLED (0) - Pull/Keeper Disabled
    //     PKE_ENABLED (1) - Pull/Keeper Enabled
    //   ODE (11) - Open Drain Enable Field Reset: ODE_DISABLED
    //              Select one out of next values for pad: EPDC_PWRCOM.
    //     ODE_DISABLED (0) - Open Drain Disabled
    //     ODE_ENABLED (1) - Open Drain Enabled
    //   SPEED (7-6) - Speed Field Reset: SPD_100MHZ
    //                 Select one out of next values for pad: EPDC_PWRCOM.
    //     SPD_TBD (0) - TBD
    //     SPD_50MHZ (1) - Low(50 MHz)
    //     SPD_100MHZ (2) - Medium(100 MHz)
    //     SPD_200MHZ (3) - Maximum(200 MHz)
    //   DSE (5-3) - Drive Strength Field Reset: DSE_40OHM
    //               Select one out of next values for pad: EPDC_PWRCOM.
    //     DSE_DISABLED (0) - Output driver disabled.
    //     DSE_240OHM (1) - 240 Ohm
    //     DSE_120OHM (2) - 120 Ohm
    //     DSE_80OHM (3) - 80 Ohm
    //     DSE_60OHM (4) - 60 Ohm
    //     DSE_48OHM (5) - 48 Ohm
    //     DSE_40OHM (6) - 40 Ohm
    //     DSE_34OHM (7) - 34 Ohm
    //   SRE (0) - Slew Rate Field Reset: SRE_SLOW
    //             Select one out of next values for pad: EPDC_PWRCOM.
    //     SRE_SLOW (0) - Slow Slew Rate
    //     SRE_FAST (1) - Fast Slew Rate
    __raw_writel((LVE_ENABLED & 0x1) << 22 | (HYS_ENABLED & 0x1) << 16 | (PUS_100KOHM_PD & 0x3) << 14 |
           (PUE_KEEP & 0x1) << 13 | (PKE_ENABLED & 0x1) << 12 | (ODE_DISABLED & 0x1) << 11 |
           (SPD_100MHZ & 0x3) << 6 | (DSE_60OHM & 0x7) << 3 | (SRE_SLOW & 0x1), (IOMUXC_SW_PAD_CTL_PAD_EPDC_PWRCOM));

    // Config epdc.PWRSTAT to pad EPDC_PWRSTAT(E10)
    // Mux Register:
    // IOMUXC_SW_MUX_CTL_PAD_EPDC_PWRSTAT(0x020E00F8)
    //   SION (4) - Software Input On Field Reset: SION_DISABLED
    //              Force the selected mux mode Input path no matter of MUX_MODE functionality.
    //     SION_DISABLED (0) - Input Path is determined by functionality of the selected mux mode (regular).
    //     SION_ENABLED (1) - Force input path of pad EPDC_PWRSTAT.
    //   MUX_MODE (2-0) - MUX Mode Select Field Reset: ALT5
    //                    Select 1 of 8 iomux modes to be used for pad: EPDC_PWRSTAT.
    //     ALT0 (0) - Select mux mode: ALT0 mux port: PWRSTAT of instance: epdc.
    //                NOTE: - Config Register IOMUXC_EPDC_IPP_EPDC_PWRSTAT_SELECT_INPUT for mode ALT0.
    //     ALT1 (1) - Select mux mode: ALT1 mux port: DAT2 of instance: usdhc4.
    //                NOTE: - Config Register IOMUXC_USDHC4_IPP_DAT2_IN_SELECT_INPUT for mode ALT1.
    //     ALT2 (2) - Select mux mode: ALT2 mux port: DAT[22] of instance: lcdif.
    //                NOTE: - Config Register IOMUXC_LCDIF_LCDIF_RXDATA_22_SELECT_INPUT for mode ALT2.
    //     ALT3 (3) - Select mux mode: ALT3 mux port: WEIM_WAIT of instance: weim.
    //                NOTE: - Config Register IOMUXC_WEIM_IPP_IND_WAIT_B_SELECT_INPUT for mode ALT3.
    //     ALT4 (4) - Select mux mode: ALT4 mux port: EVENTI of instance: cortex_a9.
    //     ALT5 (5) - Select mux mode: ALT5 mux port: GPIO[13] of instance: gpio2.
    //     ALT6 (6) - Select mux mode: ALT6 mux port: WP of instance: usdhc3.
    //                NOTE: - Config Register IOMUXC_USDHC3_IPP_WP_ON_SELECT_INPUT for mode ALT6.
    __raw_writel((SION_DISABLED & 0x1) << 4 | (ALT5 & 0x7), (IOMUXC_SW_MUX_CTL_PAD_EPDC_PWRSTAT));
    // Pad Control Register:
    // IOMUXC_SW_PAD_CTL_PAD_EPDC_PWRSTAT(0x020E03E8)
    //   LVE (22) - Low Voltage Enable Field Reset: LVE_DISABLED
    //              Select one out of next values for pad: EPDC_PWRSTAT.
    //     LVE_DISABLED (0) - High Voltage
    //     LVE_ENABLED (1) - Low Voltage
    //   HYS (16) - Hysteresis Enable Field Reset: HYS_ENABLED
    //              Select one out of next values for pad: EPDC_PWRSTAT.
    //     HYS_DISABLED (0) - Hysteresis Disabled
    //     HYS_ENABLED (1) - Hysteresis Enabled
    //   PUS (15-14) - Pull Up / Down Configure Field Reset: PUS_100KOHM_PD
    //                 Select one out of next values for pad: EPDC_PWRSTAT.
    //     PUS_100KOHM_PD (0) - 100K Ohm Pull Down
    //     PUS_47KOHM_PU (1) - 47K Ohm Pull Up
    //     PUS_100KOHM_PU (2) - 100K Ohm Pull Up
    //     PUS_22KOHM_PU (3) - 22K Ohm Pull Up
    //   PUE (13) - Pull / Keep Select Field Reset: PUE_KEEP
    //              Select one out of next values for pad: EPDC_PWRSTAT.
    //     PUE_KEEP (0) - Keeper
    //     PUE_PULL (1) - Pull
    //   PKE (12) - Pull / Keep Enable Field Reset: PKE_ENABLED
    //              Select one out of next values for pad: EPDC_PWRSTAT.
    //     PKE_DISABLED (0) - Pull/Keeper Disabled
    //     PKE_ENABLED (1) - Pull/Keeper Enabled
    //   ODE (11) - Open Drain Enable Field Reset: ODE_DISABLED
    //              Select one out of next values for pad: EPDC_PWRSTAT.
    //     ODE_DISABLED (0) - Open Drain Disabled
    //     ODE_ENABLED (1) - Open Drain Enabled
    //   SPEED (7-6) - Speed Field Reset: SPD_100MHZ
    //                 Select one out of next values for pad: EPDC_PWRSTAT.
    //     SPD_TBD (0) - TBD
    //     SPD_50MHZ (1) - Low(50 MHz)
    //     SPD_100MHZ (2) - Medium(100 MHz)
    //     SPD_200MHZ (3) - Maximum(200 MHz)
    //   DSE (5-3) - Drive Strength Field Reset: DSE_40OHM
    //               Select one out of next values for pad: EPDC_PWRSTAT.
    //     DSE_DISABLED (0) - Output driver disabled.
    //     DSE_240OHM (1) - 240 Ohm
    //     DSE_120OHM (2) - 120 Ohm
    //     DSE_80OHM (3) - 80 Ohm
    //     DSE_60OHM (4) - 60 Ohm
    //     DSE_48OHM (5) - 48 Ohm
    //     DSE_40OHM (6) - 40 Ohm
    //     DSE_34OHM (7) - 34 Ohm
    //   SRE (0) - Slew Rate Field Reset: SRE_SLOW
    //             Select one out of next values for pad: EPDC_PWRSTAT.
    //     SRE_SLOW (0) - Slow Slew Rate
    //     SRE_FAST (1) - Fast Slew Rate
    __raw_writel((LVE_ENABLED & 0x1) << 22 | (HYS_ENABLED & 0x1) << 16 | (PUS_100KOHM_PU & 0x3) << 14 |
           (PUE_PULL & 0x1) << 13 | (PKE_ENABLED & 0x1) << 12 | (ODE_ENABLED & 0x1) << 11 |
           (SPD_100MHZ & 0x3) << 6 | (DSE_60OHM & 0x7) << 3 | (SRE_SLOW & 0x1), (IOMUXC_SW_PAD_CTL_PAD_EPDC_PWRSTAT));

    // Config epdc.SDCE[0] to pad EPDC_SDCE0(C11)
    // Mux Register:
    // IOMUXC_SW_MUX_CTL_PAD_EPDC_SDCE0(0x020E0100)
    //   SION (4) - Software Input On Field Reset: SION_DISABLED
    //              Force the selected mux mode Input path no matter of MUX_MODE functionality.
    //     SION_DISABLED (0) - Input Path is determined by functionality of the selected mux mode (regular).
    //     SION_ENABLED (1) - Force input path of pad EPDC_SDCE0.
    //   MUX_MODE (2-0) - MUX Mode Select Field Reset: ALT5
    //                    Select 1 of 8 iomux modes to be used for pad: EPDC_SDCE0.
    //     ALT0 (0) - Select mux mode: ALT0 mux port: SDCE[0] of instance: epdc.
    //     ALT1 (1) - Select mux mode: ALT1 mux port: SS1 of instance: ecspi2.
    //                NOTE: - Config Register IOMUXC_ECSPI2_IPP_IND_SS_B_1_SELECT_INPUT for mode ALT1.
    //     ALT2 (2) - Select mux mode: ALT2 mux port: PWMO of instance: pwm3.
    //     ALT3 (3) - Select mux mode: ALT3 mux port: WEIM_CS[2] of instance: weim.
    //     ALT4 (4) - Select mux mode: ALT4 mux port: YCKR of instance: spdc.
    //     ALT5 (5) - Select mux mode: ALT5 mux port: GPIO[27] of instance: gpio1.
    __raw_writel((SION_DISABLED & 0x1) << 4 | (ALT0 & 0x7), (IOMUXC_SW_MUX_CTL_PAD_EPDC_SDCE0));
    // Pad Control Register:
    // IOMUXC_SW_PAD_CTL_PAD_EPDC_SDCE0(0x020E03F0)
    //   LVE (22) - Low Voltage Enable Field Reset: LVE_DISABLED
    //              Select one out of next values for pad: EPDC_SDCE0.
    //     LVE_DISABLED (0) - High Voltage
    //     LVE_ENABLED (1) - Low Voltage
    //   HYS (16) - Hysteresis Enable Field Reset: HYS_ENABLED
    //              Select one out of next values for pad: EPDC_SDCE0.
    //     HYS_DISABLED (0) - Hysteresis Disabled
    //     HYS_ENABLED (1) - Hysteresis Enabled
    //   PUS (15-14) - Pull Up / Down Configure Field Reset: PUS_100KOHM_PD
    //                 Select one out of next values for pad: EPDC_SDCE0.
    //     PUS_100KOHM_PD (0) - 100K Ohm Pull Down
    //     PUS_47KOHM_PU (1) - 47K Ohm Pull Up
    //     PUS_100KOHM_PU (2) - 100K Ohm Pull Up
    //     PUS_22KOHM_PU (3) - 22K Ohm Pull Up
    //   PUE (13) - Pull / Keep Select Field Reset: PUE_KEEP
    //              Select one out of next values for pad: EPDC_SDCE0.
    //     PUE_KEEP (0) - Keeper
    //     PUE_PULL (1) - Pull
    //   PKE (12) - Pull / Keep Enable Field Reset: PKE_ENABLED
    //              Select one out of next values for pad: EPDC_SDCE0.
    //     PKE_DISABLED (0) - Pull/Keeper Disabled
    //     PKE_ENABLED (1) - Pull/Keeper Enabled
    //   ODE (11) - Open Drain Enable Field Reset: ODE_DISABLED
    //              Select one out of next values for pad: EPDC_SDCE0.
    //     ODE_DISABLED (0) - Open Drain Disabled
    //     ODE_ENABLED (1) - Open Drain Enabled
    //   SPEED (7-6) - Speed Field Reset: SPD_100MHZ
    //                 Select one out of next values for pad: EPDC_SDCE0.
    //     SPD_TBD (0) - TBD
    //     SPD_50MHZ (1) - Low(50 MHz)
    //     SPD_100MHZ (2) - Medium(100 MHz)
    //     SPD_200MHZ (3) - Maximum(200 MHz)
    //   DSE (5-3) - Drive Strength Field Reset: DSE_40OHM
    //               Select one out of next values for pad: EPDC_SDCE0.
    //     DSE_DISABLED (0) - Output driver disabled.
    //     DSE_240OHM (1) - 240 Ohm
    //     DSE_120OHM (2) - 120 Ohm
    //     DSE_80OHM (3) - 80 Ohm
    //     DSE_60OHM (4) - 60 Ohm
    //     DSE_48OHM (5) - 48 Ohm
    //     DSE_40OHM (6) - 40 Ohm
    //     DSE_34OHM (7) - 34 Ohm
    //   SRE (0) - Slew Rate Field Reset: SRE_SLOW
    //             Select one out of next values for pad: EPDC_SDCE0.
    //     SRE_SLOW (0) - Slow Slew Rate
    //     SRE_FAST (1) - Fast Slew Rate
    __raw_writel((LVE_DISABLED & 0x1) << 22 | (HYS_ENABLED & 0x1) << 16 | (PUS_100KOHM_PD & 0x3) << 14 |
           (PUE_KEEP & 0x1) << 13 | (PKE_ENABLED & 0x1) << 12 | (ODE_DISABLED & 0x1) << 11 |
           (SPD_100MHZ & 0x3) << 6 | (DSE_60OHM & 0x7) << 3 | (SRE_SLOW & 0x1), (IOMUXC_SW_PAD_CTL_PAD_EPDC_SDCE0));

    // Config epdc.SDCLK to pad EPDC_SDCLK(B10)
    // Mux Register:
    // IOMUXC_SW_MUX_CTL_PAD_EPDC_SDCLK(0x020E0110)
    //   SION (4) - Software Input On Field Reset: SION_DISABLED
    //              Force the selected mux mode Input path no matter of MUX_MODE functionality.
    //     SION_DISABLED (0) - Input Path is determined by functionality of the selected mux mode (regular).
    //     SION_ENABLED (1) - Force input path of pad EPDC_SDCLK.
    //   MUX_MODE (2-0) - MUX Mode Select Field Reset: ALT5
    //                    Select 1 of 8 iomux modes to be used for pad: EPDC_SDCLK.
    //     ALT0 (0) - Select mux mode: ALT0 mux port: SDCLK of instance: epdc.
    //     ALT1 (1) - Select mux mode: ALT1 mux port: MOSI of instance: ecspi2.
    //                NOTE: - Config Register IOMUXC_ECSPI2_IPP_IND_MOSI_SELECT_INPUT for mode ALT1.
    //     ALT2 (2) - Select mux mode: ALT2 mux port: SCL of instance: i2c2.
    //                NOTE: - Config Register IOMUXC_I2C2_IPP_SCL_IN_SELECT_INPUT for mode ALT2.
    //     ALT3 (3) - Select mux mode: ALT3 mux port: D[8] of instance: csi.
    //                NOTE: - Config Register IOMUXC_CSI_IPP_CSI_D_8_SELECT_INPUT for mode ALT3.
    //     ALT4 (4) - Select mux mode: ALT4 mux port: CL of instance: spdc.
    //     ALT5 (5) - Select mux mode: ALT5 mux port: GPIO[23] of instance: gpio1.
    __raw_writel((SION_DISABLED & 0x1) << 4 | (ALT0 & 0x7), (IOMUXC_SW_MUX_CTL_PAD_EPDC_SDCLK));
    // Pad Control Register:
    // IOMUXC_SW_PAD_CTL_PAD_EPDC_SDCLK(0x020E0400)
    //   LVE (22) - Low Voltage Enable Field Reset: LVE_DISABLED
    //              Select one out of next values for pad: EPDC_SDCLK.
    //     LVE_DISABLED (0) - High Voltage
    //     LVE_ENABLED (1) - Low Voltage
    //   HYS (16) - Hysteresis Enable Field Reset: HYS_ENABLED
    //              Select one out of next values for pad: EPDC_SDCLK.
    //     HYS_DISABLED (0) - Hysteresis Disabled
    //     HYS_ENABLED (1) - Hysteresis Enabled
    //   PUS (15-14) - Pull Up / Down Configure Field Reset: PUS_100KOHM_PD
    //                 Select one out of next values for pad: EPDC_SDCLK.
    //     PUS_100KOHM_PD (0) - 100K Ohm Pull Down
    //     PUS_47KOHM_PU (1) - 47K Ohm Pull Up
    //     PUS_100KOHM_PU (2) - 100K Ohm Pull Up
    //     PUS_22KOHM_PU (3) - 22K Ohm Pull Up
    //   PUE (13) - Pull / Keep Select Field Reset: PUE_KEEP
    //              Select one out of next values for pad: EPDC_SDCLK.
    //     PUE_KEEP (0) - Keeper
    //     PUE_PULL (1) - Pull
    //   PKE (12) - Pull / Keep Enable Field Reset: PKE_ENABLED
    //              Select one out of next values for pad: EPDC_SDCLK.
    //     PKE_DISABLED (0) - Pull/Keeper Disabled
    //     PKE_ENABLED (1) - Pull/Keeper Enabled
    //   ODE (11) - Open Drain Enable Field Reset: ODE_DISABLED
    //              Select one out of next values for pad: EPDC_SDCLK.
    //     ODE_DISABLED (0) - Open Drain Disabled
    //     ODE_ENABLED (1) - Open Drain Enabled
    //   SPEED (7-6) - Speed Field Reset: SPD_100MHZ
    //                 Select one out of next values for pad: EPDC_SDCLK.
    //     SPD_TBD (0) - TBD
    //     SPD_50MHZ (1) - Low(50 MHz)
    //     SPD_100MHZ (2) - Medium(100 MHz)
    //     SPD_200MHZ (3) - Maximum(200 MHz)
    //   DSE (5-3) - Drive Strength Field Reset: DSE_40OHM
    //               Select one out of next values for pad: EPDC_SDCLK.
    //     DSE_DISABLED (0) - Output driver disabled.
    //     DSE_240OHM (1) - 240 Ohm
    //     DSE_120OHM (2) - 120 Ohm
    //     DSE_80OHM (3) - 80 Ohm
    //     DSE_60OHM (4) - 60 Ohm
    //     DSE_48OHM (5) - 48 Ohm
    //     DSE_40OHM (6) - 40 Ohm
    //     DSE_34OHM (7) - 34 Ohm
    //   SRE (0) - Slew Rate Field Reset: SRE_SLOW
    //             Select one out of next values for pad: EPDC_SDCLK.
    //     SRE_SLOW (0) - Slow Slew Rate
    //     SRE_FAST (1) - Fast Slew Rate
    __raw_writel((LVE_DISABLED & 0x1) << 22 | (HYS_ENABLED & 0x1) << 16 | (PUS_100KOHM_PD & 0x3) << 14 |
           (PUE_KEEP & 0x1) << 13 | (PKE_ENABLED & 0x1) << 12 | (ODE_DISABLED & 0x1) << 11 |
           (SPD_100MHZ & 0x3) << 6 | (DSE_48OHM & 0x7) << 3 | (SRE_FAST & 0x1), (IOMUXC_SW_PAD_CTL_PAD_EPDC_SDCLK));

    // Config epdc.SDDO[0] to pad EPDC_D0(A18)
    // Mux Register:
    // IOMUXC_SW_MUX_CTL_PAD_EPDC_D0(0x020E0090)
    //   SION (4) - Software Input On Field Reset: SION_DISABLED
    //              Force the selected mux mode Input path no matter of MUX_MODE functionality.
    //     SION_DISABLED (0) - Input Path is determined by functionality of the selected mux mode (regular).
    //     SION_ENABLED (1) - Force input path of pad EPDC_D0.
    //   MUX_MODE (2-0) - MUX Mode Select Field Reset: ALT5
    //                    Select 1 of 8 iomux modes to be used for pad: EPDC_D0.
    //     ALT0 (0) - Select mux mode: ALT0 mux port: SDDO[0] of instance: epdc.
    //     ALT1 (1) - Select mux mode: ALT1 mux port: MOSI of instance: ecspi4.
    //                NOTE: - Config Register IOMUXC_ECSPI4_IPP_IND_MOSI_SELECT_INPUT for mode ALT1.
    //     ALT2 (2) - Select mux mode: ALT2 mux port: DAT[24] of instance: lcdif.
    //     ALT3 (3) - Select mux mode: ALT3 mux port: D[0] of instance: csi.
    //                NOTE: - Config Register IOMUXC_CSI_IPP_CSI_D_0_SELECT_INPUT for mode ALT3.
    //     ALT4 (4) - Select mux mode: ALT4 mux port: E_DATA[0] of instance: spdc.
    //     ALT5 (5) - Select mux mode: ALT5 mux port: GPIO[7] of instance: gpio1.
    __raw_writel((SION_DISABLED & 0x1) << 4 | (ALT0 & 0x7), (IOMUXC_SW_MUX_CTL_PAD_EPDC_D0));
    // Pad Control Register:
    // IOMUXC_SW_PAD_CTL_PAD_EPDC_D0(0x020E0380)
    //   LVE (22) - Low Voltage Enable Field Reset: LVE_DISABLED
    //              Select one out of next values for pad: EPDC_D0.
    //     LVE_DISABLED (0) - High Voltage
    //     LVE_ENABLED (1) - Low Voltage
    //   HYS (16) - Hysteresis Enable Field Reset: HYS_ENABLED
    //              Select one out of next values for pad: EPDC_D0.
    //     HYS_DISABLED (0) - Hysteresis Disabled
    //     HYS_ENABLED (1) - Hysteresis Enabled
    //   PUS (15-14) - Pull Up / Down Configure Field Reset: PUS_100KOHM_PD
    //                 Select one out of next values for pad: EPDC_D0.
    //     PUS_100KOHM_PD (0) - 100K Ohm Pull Down
    //     PUS_47KOHM_PU (1) - 47K Ohm Pull Up
    //     PUS_100KOHM_PU (2) - 100K Ohm Pull Up
    //     PUS_22KOHM_PU (3) - 22K Ohm Pull Up
    //   PUE (13) - Pull / Keep Select Field Reset: PUE_KEEP
    //              Select one out of next values for pad: EPDC_D0.
    //     PUE_KEEP (0) - Keeper
    //     PUE_PULL (1) - Pull
    //   PKE (12) - Pull / Keep Enable Field Reset: PKE_ENABLED
    //              Select one out of next values for pad: EPDC_D0.
    //     PKE_DISABLED (0) - Pull/Keeper Disabled
    //     PKE_ENABLED (1) - Pull/Keeper Enabled
    //   ODE (11) - Open Drain Enable Field Reset: ODE_DISABLED
    //              Select one out of next values for pad: EPDC_D0.
    //     ODE_DISABLED (0) - Open Drain Disabled
    //     ODE_ENABLED (1) - Open Drain Enabled
    //   SPEED (7-6) - Speed Field Reset: SPD_100MHZ
    //                 Select one out of next values for pad: EPDC_D0.
    //     SPD_TBD (0) - TBD
    //     SPD_50MHZ (1) - Low(50 MHz)
    //     SPD_100MHZ (2) - Medium(100 MHz)
    //     SPD_200MHZ (3) - Maximum(200 MHz)
    //   DSE (5-3) - Drive Strength Field Reset: DSE_40OHM
    //               Select one out of next values for pad: EPDC_D0.
    //     DSE_DISABLED (0) - Output driver disabled.
    //     DSE_240OHM (1) - 240 Ohm
    //     DSE_120OHM (2) - 120 Ohm
    //     DSE_80OHM (3) - 80 Ohm
    //     DSE_60OHM (4) - 60 Ohm
    //     DSE_48OHM (5) - 48 Ohm
    //     DSE_40OHM (6) - 40 Ohm
    //     DSE_34OHM (7) - 34 Ohm
    //   SRE (0) - Slew Rate Field Reset: SRE_SLOW
    //             Select one out of next values for pad: EPDC_D0.
    //     SRE_SLOW (0) - Slow Slew Rate
    //     SRE_FAST (1) - Fast Slew Rate
    __raw_writel((LVE_DISABLED & 0x1) << 22 | (HYS_ENABLED & 0x1) << 16 | (PUS_100KOHM_PD & 0x3) << 14 |
           (PUE_KEEP & 0x1) << 13 | (PKE_ENABLED & 0x1) << 12 | (ODE_DISABLED & 0x1) << 11 |
           (SPD_100MHZ & 0x3) << 6 | (DSE_60OHM & 0x7) << 3 | (SRE_FAST & 0x1), (IOMUXC_SW_PAD_CTL_PAD_EPDC_D0));

    // Config epdc.SDDO[1] to pad EPDC_D1(A17)
    // Mux Register:
    // IOMUXC_SW_MUX_CTL_PAD_EPDC_D1(0x020E0094)
    //   SION (4) - Software Input On Field Reset: SION_DISABLED
    //              Force the selected mux mode Input path no matter of MUX_MODE functionality.
    //     SION_DISABLED (0) - Input Path is determined by functionality of the selected mux mode (regular).
    //     SION_ENABLED (1) - Force input path of pad EPDC_D1.
    //   MUX_MODE (2-0) - MUX Mode Select Field Reset: ALT5
    //                    Select 1 of 8 iomux modes to be used for pad: EPDC_D1.
    //     ALT0 (0) - Select mux mode: ALT0 mux port: SDDO[1] of instance: epdc.
    //     ALT1 (1) - Select mux mode: ALT1 mux port: MISO of instance: ecspi4.
    //                NOTE: - Config Register IOMUXC_ECSPI4_IPP_IND_MISO_SELECT_INPUT for mode ALT1.
    //     ALT2 (2) - Select mux mode: ALT2 mux port: DAT[25] of instance: lcdif.
    //     ALT3 (3) - Select mux mode: ALT3 mux port: D[1] of instance: csi.
    //                NOTE: - Config Register IOMUXC_CSI_IPP_CSI_D_1_SELECT_INPUT for mode ALT3.
    //     ALT4 (4) - Select mux mode: ALT4 mux port: E_DATA[1] of instance: spdc.
    //     ALT5 (5) - Select mux mode: ALT5 mux port: GPIO[8] of instance: gpio1.
    __raw_writel((SION_DISABLED & 0x1) << 4 | (ALT0 & 0x7), (IOMUXC_SW_MUX_CTL_PAD_EPDC_D1));
    // Pad Control Register:
    // IOMUXC_SW_PAD_CTL_PAD_EPDC_D1(0x020E0384)
    //   LVE (22) - Low Voltage Enable Field Reset: LVE_DISABLED
    //              Select one out of next values for pad: EPDC_D1.
    //     LVE_DISABLED (0) - High Voltage
    //     LVE_ENABLED (1) - Low Voltage
    //   HYS (16) - Hysteresis Enable Field Reset: HYS_ENABLED
    //              Select one out of next values for pad: EPDC_D1.
    //     HYS_DISABLED (0) - Hysteresis Disabled
    //     HYS_ENABLED (1) - Hysteresis Enabled
    //   PUS (15-14) - Pull Up / Down Configure Field Reset: PUS_100KOHM_PD
    //                 Select one out of next values for pad: EPDC_D1.
    //     PUS_100KOHM_PD (0) - 100K Ohm Pull Down
    //     PUS_47KOHM_PU (1) - 47K Ohm Pull Up
    //     PUS_100KOHM_PU (2) - 100K Ohm Pull Up
    //     PUS_22KOHM_PU (3) - 22K Ohm Pull Up
    //   PUE (13) - Pull / Keep Select Field Reset: PUE_KEEP
    //              Select one out of next values for pad: EPDC_D1.
    //     PUE_KEEP (0) - Keeper
    //     PUE_PULL (1) - Pull
    //   PKE (12) - Pull / Keep Enable Field Reset: PKE_ENABLED
    //              Select one out of next values for pad: EPDC_D1.
    //     PKE_DISABLED (0) - Pull/Keeper Disabled
    //     PKE_ENABLED (1) - Pull/Keeper Enabled
    //   ODE (11) - Open Drain Enable Field Reset: ODE_DISABLED
    //              Select one out of next values for pad: EPDC_D1.
    //     ODE_DISABLED (0) - Open Drain Disabled
    //     ODE_ENABLED (1) - Open Drain Enabled
    //   SPEED (7-6) - Speed Field Reset: SPD_100MHZ
    //                 Select one out of next values for pad: EPDC_D1.
    //     SPD_TBD (0) - TBD
    //     SPD_50MHZ (1) - Low(50 MHz)
    //     SPD_100MHZ (2) - Medium(100 MHz)
    //     SPD_200MHZ (3) - Maximum(200 MHz)
    //   DSE (5-3) - Drive Strength Field Reset: DSE_40OHM
    //               Select one out of next values for pad: EPDC_D1.
    //     DSE_DISABLED (0) - Output driver disabled.
    //     DSE_240OHM (1) - 240 Ohm
    //     DSE_120OHM (2) - 120 Ohm
    //     DSE_80OHM (3) - 80 Ohm
    //     DSE_60OHM (4) - 60 Ohm
    //     DSE_48OHM (5) - 48 Ohm
    //     DSE_40OHM (6) - 40 Ohm
    //     DSE_34OHM (7) - 34 Ohm
    //   SRE (0) - Slew Rate Field Reset: SRE_SLOW
    //             Select one out of next values for pad: EPDC_D1.
    //     SRE_SLOW (0) - Slow Slew Rate
    //     SRE_FAST (1) - Fast Slew Rate
    __raw_writel((LVE_DISABLED & 0x1) << 22 | (HYS_ENABLED & 0x1) << 16 | (PUS_100KOHM_PD & 0x3) << 14 |
           (PUE_KEEP & 0x1) << 13 | (PKE_ENABLED & 0x1) << 12 | (ODE_DISABLED & 0x1) << 11 |
           (SPD_100MHZ & 0x3) << 6 | (DSE_60OHM & 0x7) << 3 | (SRE_FAST & 0x1), (IOMUXC_SW_PAD_CTL_PAD_EPDC_D1));

    // Config epdc.SDDO[2] to pad EPDC_D2(B17)
    // Mux Register:
    // IOMUXC_SW_MUX_CTL_PAD_EPDC_D2(0x020E00B0)
    //   SION (4) - Software Input On Field Reset: SION_DISABLED
    //              Force the selected mux mode Input path no matter of MUX_MODE functionality.
    //     SION_DISABLED (0) - Input Path is determined by functionality of the selected mux mode (regular).
    //     SION_ENABLED (1) - Force input path of pad EPDC_D2.
    //   MUX_MODE (2-0) - MUX Mode Select Field Reset: ALT5
    //                    Select 1 of 8 iomux modes to be used for pad: EPDC_D2.
    //     ALT0 (0) - Select mux mode: ALT0 mux port: SDDO[2] of instance: epdc.
    //     ALT1 (1) - Select mux mode: ALT1 mux port: SS0 of instance: ecspi4.
    //                NOTE: - Config Register IOMUXC_ECSPI4_IPP_IND_SS_B_0_SELECT_INPUT for mode ALT1.
    //     ALT2 (2) - Select mux mode: ALT2 mux port: DAT[26] of instance: lcdif.
    //     ALT3 (3) - Select mux mode: ALT3 mux port: D[2] of instance: csi.
    //                NOTE: - Config Register IOMUXC_CSI_IPP_CSI_D_2_SELECT_INPUT for mode ALT3.
    //     ALT4 (4) - Select mux mode: ALT4 mux port: E_DATA[2] of instance: spdc.
    //     ALT5 (5) - Select mux mode: ALT5 mux port: GPIO[9] of instance: gpio1.
    __raw_writel((SION_DISABLED & 0x1) << 4 | (ALT0 & 0x7), (IOMUXC_SW_MUX_CTL_PAD_EPDC_D2));
    // Pad Control Register:
    // IOMUXC_SW_PAD_CTL_PAD_EPDC_D2(0x020E03A0)
    //   LVE (22) - Low Voltage Enable Field Reset: LVE_DISABLED
    //              Select one out of next values for pad: EPDC_D2.
    //     LVE_DISABLED (0) - High Voltage
    //     LVE_ENABLED (1) - Low Voltage
    //   HYS (16) - Hysteresis Enable Field Reset: HYS_ENABLED
    //              Select one out of next values for pad: EPDC_D2.
    //     HYS_DISABLED (0) - Hysteresis Disabled
    //     HYS_ENABLED (1) - Hysteresis Enabled
    //   PUS (15-14) - Pull Up / Down Configure Field Reset: PUS_100KOHM_PD
    //                 Select one out of next values for pad: EPDC_D2.
    //     PUS_100KOHM_PD (0) - 100K Ohm Pull Down
    //     PUS_47KOHM_PU (1) - 47K Ohm Pull Up
    //     PUS_100KOHM_PU (2) - 100K Ohm Pull Up
    //     PUS_22KOHM_PU (3) - 22K Ohm Pull Up
    //   PUE (13) - Pull / Keep Select Field Reset: PUE_KEEP
    //              Select one out of next values for pad: EPDC_D2.
    //     PUE_KEEP (0) - Keeper
    //     PUE_PULL (1) - Pull
    //   PKE (12) - Pull / Keep Enable Field Reset: PKE_ENABLED
    //              Select one out of next values for pad: EPDC_D2.
    //     PKE_DISABLED (0) - Pull/Keeper Disabled
    //     PKE_ENABLED (1) - Pull/Keeper Enabled
    //   ODE (11) - Open Drain Enable Field Reset: ODE_DISABLED
    //              Select one out of next values for pad: EPDC_D2.
    //     ODE_DISABLED (0) - Open Drain Disabled
    //     ODE_ENABLED (1) - Open Drain Enabled
    //   SPEED (7-6) - Speed Field Reset: SPD_100MHZ
    //                 Select one out of next values for pad: EPDC_D2.
    //     SPD_TBD (0) - TBD
    //     SPD_50MHZ (1) - Low(50 MHz)
    //     SPD_100MHZ (2) - Medium(100 MHz)
    //     SPD_200MHZ (3) - Maximum(200 MHz)
    //   DSE (5-3) - Drive Strength Field Reset: DSE_40OHM
    //               Select one out of next values for pad: EPDC_D2.
    //     DSE_DISABLED (0) - Output driver disabled.
    //     DSE_240OHM (1) - 240 Ohm
    //     DSE_120OHM (2) - 120 Ohm
    //     DSE_80OHM (3) - 80 Ohm
    //     DSE_60OHM (4) - 60 Ohm
    //     DSE_48OHM (5) - 48 Ohm
    //     DSE_40OHM (6) - 40 Ohm
    //     DSE_34OHM (7) - 34 Ohm
    //   SRE (0) - Slew Rate Field Reset: SRE_SLOW
    //             Select one out of next values for pad: EPDC_D2.
    //     SRE_SLOW (0) - Slow Slew Rate
    //     SRE_FAST (1) - Fast Slew Rate
    __raw_writel((LVE_DISABLED & 0x1) << 22 | (HYS_ENABLED & 0x1) << 16 | (PUS_100KOHM_PD & 0x3) << 14 |
           (PUE_KEEP & 0x1) << 13 | (PKE_ENABLED & 0x1) << 12 | (ODE_DISABLED & 0x1) << 11 |
           (SPD_100MHZ & 0x3) << 6 | (DSE_60OHM & 0x7) << 3 | (SRE_FAST & 0x1), (IOMUXC_SW_PAD_CTL_PAD_EPDC_D2));

    // Config epdc.SDDO[3] to pad EPDC_D3(A16)
    // Mux Register:
    // IOMUXC_SW_MUX_CTL_PAD_EPDC_D3(0x020E00B4)
    //   SION (4) - Software Input On Field Reset: SION_DISABLED
    //              Force the selected mux mode Input path no matter of MUX_MODE functionality.
    //     SION_DISABLED (0) - Input Path is determined by functionality of the selected mux mode (regular).
    //     SION_ENABLED (1) - Force input path of pad EPDC_D3.
    //   MUX_MODE (2-0) - MUX Mode Select Field Reset: ALT5
    //                    Select 1 of 8 iomux modes to be used for pad: EPDC_D3.
    //     ALT0 (0) - Select mux mode: ALT0 mux port: SDDO[3] of instance: epdc.
    //     ALT1 (1) - Select mux mode: ALT1 mux port: SCLK of instance: ecspi4.
    //                NOTE: - Config Register IOMUXC_ECSPI4_IPP_CSPI_CLK_IN_SELECT_INPUT for mode ALT1.
    //     ALT2 (2) - Select mux mode: ALT2 mux port: DAT[27] of instance: lcdif.
    //     ALT3 (3) - Select mux mode: ALT3 mux port: D[3] of instance: csi.
    //                NOTE: - Config Register IOMUXC_CSI_IPP_CSI_D_3_SELECT_INPUT for mode ALT3.
    //     ALT4 (4) - Select mux mode: ALT4 mux port: E_DATA[3] of instance: spdc.
    //     ALT5 (5) - Select mux mode: ALT5 mux port: GPIO[10] of instance: gpio1.
    __raw_writel((SION_DISABLED & 0x1) << 4 | (ALT0 & 0x7), (IOMUXC_SW_MUX_CTL_PAD_EPDC_D3));
    // Pad Control Register:
    // IOMUXC_SW_PAD_CTL_PAD_EPDC_D3(0x020E03A4)
    //   LVE (22) - Low Voltage Enable Field Reset: LVE_DISABLED
    //              Select one out of next values for pad: EPDC_D3.
    //     LVE_DISABLED (0) - High Voltage
    //     LVE_ENABLED (1) - Low Voltage
    //   HYS (16) - Hysteresis Enable Field Reset: HYS_ENABLED
    //              Select one out of next values for pad: EPDC_D3.
    //     HYS_DISABLED (0) - Hysteresis Disabled
    //     HYS_ENABLED (1) - Hysteresis Enabled
    //   PUS (15-14) - Pull Up / Down Configure Field Reset: PUS_100KOHM_PD
    //                 Select one out of next values for pad: EPDC_D3.
    //     PUS_100KOHM_PD (0) - 100K Ohm Pull Down
    //     PUS_47KOHM_PU (1) - 47K Ohm Pull Up
    //     PUS_100KOHM_PU (2) - 100K Ohm Pull Up
    //     PUS_22KOHM_PU (3) - 22K Ohm Pull Up
    //   PUE (13) - Pull / Keep Select Field Reset: PUE_KEEP
    //              Select one out of next values for pad: EPDC_D3.
    //     PUE_KEEP (0) - Keeper
    //     PUE_PULL (1) - Pull
    //   PKE (12) - Pull / Keep Enable Field Reset: PKE_ENABLED
    //              Select one out of next values for pad: EPDC_D3.
    //     PKE_DISABLED (0) - Pull/Keeper Disabled
    //     PKE_ENABLED (1) - Pull/Keeper Enabled
    //   ODE (11) - Open Drain Enable Field Reset: ODE_DISABLED
    //              Select one out of next values for pad: EPDC_D3.
    //     ODE_DISABLED (0) - Open Drain Disabled
    //     ODE_ENABLED (1) - Open Drain Enabled
    //   SPEED (7-6) - Speed Field Reset: SPD_100MHZ
    //                 Select one out of next values for pad: EPDC_D3.
    //     SPD_TBD (0) - TBD
    //     SPD_50MHZ (1) - Low(50 MHz)
    //     SPD_100MHZ (2) - Medium(100 MHz)
    //     SPD_200MHZ (3) - Maximum(200 MHz)
    //   DSE (5-3) - Drive Strength Field Reset: DSE_40OHM
    //               Select one out of next values for pad: EPDC_D3.
    //     DSE_DISABLED (0) - Output driver disabled.
    //     DSE_240OHM (1) - 240 Ohm
    //     DSE_120OHM (2) - 120 Ohm
    //     DSE_80OHM (3) - 80 Ohm
    //     DSE_60OHM (4) - 60 Ohm
    //     DSE_48OHM (5) - 48 Ohm
    //     DSE_40OHM (6) - 40 Ohm
    //     DSE_34OHM (7) - 34 Ohm
    //   SRE (0) - Slew Rate Field Reset: SRE_SLOW
    //             Select one out of next values for pad: EPDC_D3.
    //     SRE_SLOW (0) - Slow Slew Rate
    //     SRE_FAST (1) - Fast Slew Rate
    __raw_writel((LVE_DISABLED & 0x1) << 22 | (HYS_ENABLED & 0x1) << 16 | (PUS_100KOHM_PD & 0x3) << 14 |
           (PUE_KEEP & 0x1) << 13 | (PKE_ENABLED & 0x1) << 12 | (ODE_DISABLED & 0x1) << 11 |
           (SPD_100MHZ & 0x3) << 6 | (DSE_60OHM & 0x7) << 3 | (SRE_FAST & 0x1), (IOMUXC_SW_PAD_CTL_PAD_EPDC_D3));

    // Config epdc.SDDO[4] to pad EPDC_D4(B16)
    // Mux Register:
    // IOMUXC_SW_MUX_CTL_PAD_EPDC_D4(0x020E00B8)
    //   SION (4) - Software Input On Field Reset: SION_DISABLED
    //              Force the selected mux mode Input path no matter of MUX_MODE functionality.
    //     SION_DISABLED (0) - Input Path is determined by functionality of the selected mux mode (regular).
    //     SION_ENABLED (1) - Force input path of pad EPDC_D4.
    //   MUX_MODE (2-0) - MUX Mode Select Field Reset: ALT5
    //                    Select 1 of 8 iomux modes to be used for pad: EPDC_D4.
    //     ALT0 (0) - Select mux mode: ALT0 mux port: SDDO[4] of instance: epdc.
    //     ALT1 (1) - Select mux mode: ALT1 mux port: SS1 of instance: ecspi4.
    //                NOTE: - Config Register IOMUXC_ECSPI4_IPP_IND_SS_B_1_SELECT_INPUT for mode ALT1.
    //     ALT2 (2) - Select mux mode: ALT2 mux port: DAT[28] of instance: lcdif.
    //     ALT3 (3) - Select mux mode: ALT3 mux port: D[4] of instance: csi.
    //                NOTE: - Config Register IOMUXC_CSI_IPP_CSI_D_4_SELECT_INPUT for mode ALT3.
    //     ALT4 (4) - Select mux mode: ALT4 mux port: E_DATA[4] of instance: spdc.
    //     ALT5 (5) - Select mux mode: ALT5 mux port: GPIO[11] of instance: gpio1.
    __raw_writel((SION_DISABLED & 0x1) << 4 | (ALT0 & 0x7), (IOMUXC_SW_MUX_CTL_PAD_EPDC_D4));
    // Pad Control Register:
    // IOMUXC_SW_PAD_CTL_PAD_EPDC_D4(0x020E03A8)
    //   LVE (22) - Low Voltage Enable Field Reset: LVE_DISABLED
    //              Select one out of next values for pad: EPDC_D4.
    //     LVE_DISABLED (0) - High Voltage
    //     LVE_ENABLED (1) - Low Voltage
    //   HYS (16) - Hysteresis Enable Field Reset: HYS_ENABLED
    //              Select one out of next values for pad: EPDC_D4.
    //     HYS_DISABLED (0) - Hysteresis Disabled
    //     HYS_ENABLED (1) - Hysteresis Enabled
    //   PUS (15-14) - Pull Up / Down Configure Field Reset: PUS_100KOHM_PD
    //                 Select one out of next values for pad: EPDC_D4.
    //     PUS_100KOHM_PD (0) - 100K Ohm Pull Down
    //     PUS_47KOHM_PU (1) - 47K Ohm Pull Up
    //     PUS_100KOHM_PU (2) - 100K Ohm Pull Up
    //     PUS_22KOHM_PU (3) - 22K Ohm Pull Up
    //   PUE (13) - Pull / Keep Select Field Reset: PUE_KEEP
    //              Select one out of next values for pad: EPDC_D4.
    //     PUE_KEEP (0) - Keeper
    //     PUE_PULL (1) - Pull
    //   PKE (12) - Pull / Keep Enable Field Reset: PKE_ENABLED
    //              Select one out of next values for pad: EPDC_D4.
    //     PKE_DISABLED (0) - Pull/Keeper Disabled
    //     PKE_ENABLED (1) - Pull/Keeper Enabled
    //   ODE (11) - Open Drain Enable Field Reset: ODE_DISABLED
    //              Select one out of next values for pad: EPDC_D4.
    //     ODE_DISABLED (0) - Open Drain Disabled
    //     ODE_ENABLED (1) - Open Drain Enabled
    //   SPEED (7-6) - Speed Field Reset: SPD_100MHZ
    //                 Select one out of next values for pad: EPDC_D4.
    //     SPD_TBD (0) - TBD
    //     SPD_50MHZ (1) - Low(50 MHz)
    //     SPD_100MHZ (2) - Medium(100 MHz)
    //     SPD_200MHZ (3) - Maximum(200 MHz)
    //   DSE (5-3) - Drive Strength Field Reset: DSE_40OHM
    //               Select one out of next values for pad: EPDC_D4.
    //     DSE_DISABLED (0) - Output driver disabled.
    //     DSE_240OHM (1) - 240 Ohm
    //     DSE_120OHM (2) - 120 Ohm
    //     DSE_80OHM (3) - 80 Ohm
    //     DSE_60OHM (4) - 60 Ohm
    //     DSE_48OHM (5) - 48 Ohm
    //     DSE_40OHM (6) - 40 Ohm
    //     DSE_34OHM (7) - 34 Ohm
    //   SRE (0) - Slew Rate Field Reset: SRE_SLOW
    //             Select one out of next values for pad: EPDC_D4.
    //     SRE_SLOW (0) - Slow Slew Rate
    //     SRE_FAST (1) - Fast Slew Rate
    __raw_writel((LVE_DISABLED & 0x1) << 22 | (HYS_ENABLED & 0x1) << 16 | (PUS_100KOHM_PD & 0x3) << 14 |
           (PUE_KEEP & 0x1) << 13 | (PKE_ENABLED & 0x1) << 12 | (ODE_DISABLED & 0x1) << 11 |
           (SPD_100MHZ & 0x3) << 6 | (DSE_60OHM & 0x7) << 3 | (SRE_FAST & 0x1), (IOMUXC_SW_PAD_CTL_PAD_EPDC_D4));

    // Config epdc.SDDO[5] to pad EPDC_D5(A15)
    // Mux Register:
    // IOMUXC_SW_MUX_CTL_PAD_EPDC_D5(0x020E00BC)
    //   SION (4) - Software Input On Field Reset: SION_DISABLED
    //              Force the selected mux mode Input path no matter of MUX_MODE functionality.
    //     SION_DISABLED (0) - Input Path is determined by functionality of the selected mux mode (regular).
    //     SION_ENABLED (1) - Force input path of pad EPDC_D5.
    //   MUX_MODE (2-0) - MUX Mode Select Field Reset: ALT5
    //                    Select 1 of 8 iomux modes to be used for pad: EPDC_D5.
    //     ALT0 (0) - Select mux mode: ALT0 mux port: SDDO[5] of instance: epdc.
    //     ALT1 (1) - Select mux mode: ALT1 mux port: SS2 of instance: ecspi4.
    //                NOTE: - Config Register IOMUXC_ECSPI4_IPP_IND_SS_B_2_SELECT_INPUT for mode ALT1.
    //     ALT2 (2) - Select mux mode: ALT2 mux port: DAT[29] of instance: lcdif.
    //     ALT3 (3) - Select mux mode: ALT3 mux port: D[5] of instance: csi.
    //                NOTE: - Config Register IOMUXC_CSI_IPP_CSI_D_5_SELECT_INPUT for mode ALT3.
    //     ALT4 (4) - Select mux mode: ALT4 mux port: E_DATA[5] of instance: spdc.
    //     ALT5 (5) - Select mux mode: ALT5 mux port: GPIO[12] of instance: gpio1.
    __raw_writel((SION_DISABLED & 0x1) << 4 | (ALT0 & 0x7), (IOMUXC_SW_MUX_CTL_PAD_EPDC_D5));
    // Pad Control Register:
    // IOMUXC_SW_PAD_CTL_PAD_EPDC_D5(0x020E03AC)
    //   LVE (22) - Low Voltage Enable Field Reset: LVE_DISABLED
    //              Select one out of next values for pad: EPDC_D5.
    //     LVE_DISABLED (0) - High Voltage
    //     LVE_ENABLED (1) - Low Voltage
    //   HYS (16) - Hysteresis Enable Field Reset: HYS_ENABLED
    //              Select one out of next values for pad: EPDC_D5.
    //     HYS_DISABLED (0) - Hysteresis Disabled
    //     HYS_ENABLED (1) - Hysteresis Enabled
    //   PUS (15-14) - Pull Up / Down Configure Field Reset: PUS_100KOHM_PD
    //                 Select one out of next values for pad: EPDC_D5.
    //     PUS_100KOHM_PD (0) - 100K Ohm Pull Down
    //     PUS_47KOHM_PU (1) - 47K Ohm Pull Up
    //     PUS_100KOHM_PU (2) - 100K Ohm Pull Up
    //     PUS_22KOHM_PU (3) - 22K Ohm Pull Up
    //   PUE (13) - Pull / Keep Select Field Reset: PUE_KEEP
    //              Select one out of next values for pad: EPDC_D5.
    //     PUE_KEEP (0) - Keeper
    //     PUE_PULL (1) - Pull
    //   PKE (12) - Pull / Keep Enable Field Reset: PKE_ENABLED
    //              Select one out of next values for pad: EPDC_D5.
    //     PKE_DISABLED (0) - Pull/Keeper Disabled
    //     PKE_ENABLED (1) - Pull/Keeper Enabled
    //   ODE (11) - Open Drain Enable Field Reset: ODE_DISABLED
    //              Select one out of next values for pad: EPDC_D5.
    //     ODE_DISABLED (0) - Open Drain Disabled
    //     ODE_ENABLED (1) - Open Drain Enabled
    //   SPEED (7-6) - Speed Field Reset: SPD_100MHZ
    //                 Select one out of next values for pad: EPDC_D5.
    //     SPD_TBD (0) - TBD
    //     SPD_50MHZ (1) - Low(50 MHz)
    //     SPD_100MHZ (2) - Medium(100 MHz)
    //     SPD_200MHZ (3) - Maximum(200 MHz)
    //   DSE (5-3) - Drive Strength Field Reset: DSE_40OHM
    //               Select one out of next values for pad: EPDC_D5.
    //     DSE_DISABLED (0) - Output driver disabled.
    //     DSE_240OHM (1) - 240 Ohm
    //     DSE_120OHM (2) - 120 Ohm
    //     DSE_80OHM (3) - 80 Ohm
    //     DSE_60OHM (4) - 60 Ohm
    //     DSE_48OHM (5) - 48 Ohm
    //     DSE_40OHM (6) - 40 Ohm
    //     DSE_34OHM (7) - 34 Ohm
    //   SRE (0) - Slew Rate Field Reset: SRE_SLOW
    //             Select one out of next values for pad: EPDC_D5.
    //     SRE_SLOW (0) - Slow Slew Rate
    //     SRE_FAST (1) - Fast Slew Rate
    __raw_writel((LVE_DISABLED & 0x1) << 22 | (HYS_ENABLED & 0x1) << 16 | (PUS_100KOHM_PD & 0x3) << 14 |
           (PUE_KEEP & 0x1) << 13 | (PKE_ENABLED & 0x1) << 12 | (ODE_DISABLED & 0x1) << 11 |
           (SPD_100MHZ & 0x3) << 6 | (DSE_60OHM & 0x7) << 3 | (SRE_FAST & 0x1), (IOMUXC_SW_PAD_CTL_PAD_EPDC_D5));

    // Config epdc.SDDO[6] to pad EPDC_D6(B15)
    // Mux Register:
    // IOMUXC_SW_MUX_CTL_PAD_EPDC_D6(0x020E00C0)
    //   SION (4) - Software Input On Field Reset: SION_DISABLED
    //              Force the selected mux mode Input path no matter of MUX_MODE functionality.
    //     SION_DISABLED (0) - Input Path is determined by functionality of the selected mux mode (regular).
    //     SION_ENABLED (1) - Force input path of pad EPDC_D6.
    //   MUX_MODE (2-0) - MUX Mode Select Field Reset: ALT5
    //                    Select 1 of 8 iomux modes to be used for pad: EPDC_D6.
    //     ALT0 (0) - Select mux mode: ALT0 mux port: SDDO[6] of instance: epdc.
    //     ALT1 (1) - Select mux mode: ALT1 mux port: SS3 of instance: ecspi4.
    //     ALT2 (2) - Select mux mode: ALT2 mux port: DAT[30] of instance: lcdif.
    //     ALT3 (3) - Select mux mode: ALT3 mux port: D[6] of instance: csi.
    //                NOTE: - Config Register IOMUXC_CSI_IPP_CSI_D_6_SELECT_INPUT for mode ALT3.
    //     ALT4 (4) - Select mux mode: ALT4 mux port: E_DATA[6] of instance: spdc.
    //     ALT5 (5) - Select mux mode: ALT5 mux port: GPIO[13] of instance: gpio1.
    __raw_writel((SION_DISABLED & 0x1) << 4 | (ALT0 & 0x7), (IOMUXC_SW_MUX_CTL_PAD_EPDC_D6));
    // Pad Control Register:
    // IOMUXC_SW_PAD_CTL_PAD_EPDC_D6(0x020E03B0)
    //   LVE (22) - Low Voltage Enable Field Reset: LVE_DISABLED
    //              Select one out of next values for pad: EPDC_D6.
    //     LVE_DISABLED (0) - High Voltage
    //     LVE_ENABLED (1) - Low Voltage
    //   HYS (16) - Hysteresis Enable Field Reset: HYS_ENABLED
    //              Select one out of next values for pad: EPDC_D6.
    //     HYS_DISABLED (0) - Hysteresis Disabled
    //     HYS_ENABLED (1) - Hysteresis Enabled
    //   PUS (15-14) - Pull Up / Down Configure Field Reset: PUS_100KOHM_PD
    //                 Select one out of next values for pad: EPDC_D6.
    //     PUS_100KOHM_PD (0) - 100K Ohm Pull Down
    //     PUS_47KOHM_PU (1) - 47K Ohm Pull Up
    //     PUS_100KOHM_PU (2) - 100K Ohm Pull Up
    //     PUS_22KOHM_PU (3) - 22K Ohm Pull Up
    //   PUE (13) - Pull / Keep Select Field Reset: PUE_KEEP
    //              Select one out of next values for pad: EPDC_D6.
    //     PUE_KEEP (0) - Keeper
    //     PUE_PULL (1) - Pull
    //   PKE (12) - Pull / Keep Enable Field Reset: PKE_ENABLED
    //              Select one out of next values for pad: EPDC_D6.
    //     PKE_DISABLED (0) - Pull/Keeper Disabled
    //     PKE_ENABLED (1) - Pull/Keeper Enabled
    //   ODE (11) - Open Drain Enable Field Reset: ODE_DISABLED
    //              Select one out of next values for pad: EPDC_D6.
    //     ODE_DISABLED (0) - Open Drain Disabled
    //     ODE_ENABLED (1) - Open Drain Enabled
    //   SPEED (7-6) - Speed Field Reset: SPD_100MHZ
    //                 Select one out of next values for pad: EPDC_D6.
    //     SPD_TBD (0) - TBD
    //     SPD_50MHZ (1) - Low(50 MHz)
    //     SPD_100MHZ (2) - Medium(100 MHz)
    //     SPD_200MHZ (3) - Maximum(200 MHz)
    //   DSE (5-3) - Drive Strength Field Reset: DSE_40OHM
    //               Select one out of next values for pad: EPDC_D6.
    //     DSE_DISABLED (0) - Output driver disabled.
    //     DSE_240OHM (1) - 240 Ohm
    //     DSE_120OHM (2) - 120 Ohm
    //     DSE_80OHM (3) - 80 Ohm
    //     DSE_60OHM (4) - 60 Ohm
    //     DSE_48OHM (5) - 48 Ohm
    //     DSE_40OHM (6) - 40 Ohm
    //     DSE_34OHM (7) - 34 Ohm
    //   SRE (0) - Slew Rate Field Reset: SRE_SLOW
    //             Select one out of next values for pad: EPDC_D6.
    //     SRE_SLOW (0) - Slow Slew Rate
    //     SRE_FAST (1) - Fast Slew Rate
    __raw_writel((LVE_DISABLED & 0x1) << 22 | (HYS_ENABLED & 0x1) << 16 | (PUS_100KOHM_PD & 0x3) << 14 |
           (PUE_KEEP & 0x1) << 13 | (PKE_ENABLED & 0x1) << 12 | (ODE_DISABLED & 0x1) << 11 |
           (SPD_100MHZ & 0x3) << 6 | (DSE_60OHM & 0x7) << 3 | (SRE_FAST & 0x1), (IOMUXC_SW_PAD_CTL_PAD_EPDC_D6));

    // Config epdc.SDDO[7] to pad EPDC_D7(C15)
    // Mux Register:
    // IOMUXC_SW_MUX_CTL_PAD_EPDC_D7(0x020E00C4)
    //   SION (4) - Software Input On Field Reset: SION_DISABLED
    //              Force the selected mux mode Input path no matter of MUX_MODE functionality.
    //     SION_DISABLED (0) - Input Path is determined by functionality of the selected mux mode (regular).
    //     SION_ENABLED (1) - Force input path of pad EPDC_D7.
    //   MUX_MODE (2-0) - MUX Mode Select Field Reset: ALT5
    //                    Select 1 of 8 iomux modes to be used for pad: EPDC_D7.
    //     ALT0 (0) - Select mux mode: ALT0 mux port: SDDO[7] of instance: epdc.
    //     ALT1 (1) - Select mux mode: ALT1 mux port: RDY of instance: ecspi4.
    //     ALT2 (2) - Select mux mode: ALT2 mux port: DAT[31] of instance: lcdif.
    //     ALT3 (3) - Select mux mode: ALT3 mux port: D[7] of instance: csi.
    //                NOTE: - Config Register IOMUXC_CSI_IPP_CSI_D_7_SELECT_INPUT for mode ALT3.
    //     ALT4 (4) - Select mux mode: ALT4 mux port: E_DATA[7] of instance: spdc.
    //     ALT5 (5) - Select mux mode: ALT5 mux port: GPIO[14] of instance: gpio1.
    __raw_writel((SION_DISABLED & 0x1) << 4 | (ALT0 & 0x7), (IOMUXC_SW_MUX_CTL_PAD_EPDC_D7));
    // Pad Control Register:
    // IOMUXC_SW_PAD_CTL_PAD_EPDC_D7(0x020E03B4)
    //   LVE (22) - Low Voltage Enable Field Reset: LVE_DISABLED
    //              Select one out of next values for pad: EPDC_D7.
    //     LVE_DISABLED (0) - High Voltage
    //     LVE_ENABLED (1) - Low Voltage
    //   HYS (16) - Hysteresis Enable Field Reset: HYS_ENABLED
    //              Select one out of next values for pad: EPDC_D7.
    //     HYS_DISABLED (0) - Hysteresis Disabled
    //     HYS_ENABLED (1) - Hysteresis Enabled
    //   PUS (15-14) - Pull Up / Down Configure Field Reset: PUS_100KOHM_PD
    //                 Select one out of next values for pad: EPDC_D7.
    //     PUS_100KOHM_PD (0) - 100K Ohm Pull Down
    //     PUS_47KOHM_PU (1) - 47K Ohm Pull Up
    //     PUS_100KOHM_PU (2) - 100K Ohm Pull Up
    //     PUS_22KOHM_PU (3) - 22K Ohm Pull Up
    //   PUE (13) - Pull / Keep Select Field Reset: PUE_KEEP
    //              Select one out of next values for pad: EPDC_D7.
    //     PUE_KEEP (0) - Keeper
    //     PUE_PULL (1) - Pull
    //   PKE (12) - Pull / Keep Enable Field Reset: PKE_ENABLED
    //              Select one out of next values for pad: EPDC_D7.
    //     PKE_DISABLED (0) - Pull/Keeper Disabled
    //     PKE_ENABLED (1) - Pull/Keeper Enabled
    //   ODE (11) - Open Drain Enable Field Reset: ODE_DISABLED
    //              Select one out of next values for pad: EPDC_D7.
    //     ODE_DISABLED (0) - Open Drain Disabled
    //     ODE_ENABLED (1) - Open Drain Enabled
    //   SPEED (7-6) - Speed Field Reset: SPD_100MHZ
    //                 Select one out of next values for pad: EPDC_D7.
    //     SPD_TBD (0) - TBD
    //     SPD_50MHZ (1) - Low(50 MHz)
    //     SPD_100MHZ (2) - Medium(100 MHz)
    //     SPD_200MHZ (3) - Maximum(200 MHz)
    //   DSE (5-3) - Drive Strength Field Reset: DSE_40OHM
    //               Select one out of next values for pad: EPDC_D7.
    //     DSE_DISABLED (0) - Output driver disabled.
    //     DSE_240OHM (1) - 240 Ohm
    //     DSE_120OHM (2) - 120 Ohm
    //     DSE_80OHM (3) - 80 Ohm
    //     DSE_60OHM (4) - 60 Ohm
    //     DSE_48OHM (5) - 48 Ohm
    //     DSE_40OHM (6) - 40 Ohm
    //     DSE_34OHM (7) - 34 Ohm
    //   SRE (0) - Slew Rate Field Reset: SRE_SLOW
    //             Select one out of next values for pad: EPDC_D7.
    //     SRE_SLOW (0) - Slow Slew Rate
    //     SRE_FAST (1) - Fast Slew Rate
    __raw_writel((LVE_DISABLED & 0x1) << 22 | (HYS_ENABLED & 0x1) << 16 | (PUS_100KOHM_PD & 0x3) << 14 |
           (PUE_KEEP & 0x1) << 13 | (PKE_ENABLED & 0x1) << 12 | (ODE_DISABLED & 0x1) << 11 |
           (SPD_100MHZ & 0x3) << 6 | (DSE_60OHM & 0x7) << 3 | (SRE_FAST & 0x1), (IOMUXC_SW_PAD_CTL_PAD_EPDC_D7));

    // Config epdc.SDLE to pad EPDC_SDLE(B8)
    // Mux Register:
    // IOMUXC_SW_MUX_CTL_PAD_EPDC_SDLE(0x020E0114)
    //   SION (4) - Software Input On Field Reset: SION_DISABLED
    //              Force the selected mux mode Input path no matter of MUX_MODE functionality.
    //     SION_DISABLED (0) - Input Path is determined by functionality of the selected mux mode (regular).
    //     SION_ENABLED (1) - Force input path of pad EPDC_SDLE.
    //   MUX_MODE (2-0) - MUX Mode Select Field Reset: ALT5
    //                    Select 1 of 8 iomux modes to be used for pad: EPDC_SDLE.
    //     ALT0 (0) - Select mux mode: ALT0 mux port: SDLE of instance: epdc.
    //     ALT1 (1) - Select mux mode: ALT1 mux port: MISO of instance: ecspi2.
    //                NOTE: - Config Register IOMUXC_ECSPI2_IPP_IND_MISO_SELECT_INPUT for mode ALT1.
    //     ALT2 (2) - Select mux mode: ALT2 mux port: SDA of instance: i2c2.
    //                NOTE: - Config Register IOMUXC_I2C2_IPP_SDA_IN_SELECT_INPUT for mode ALT2.
    //     ALT3 (3) - Select mux mode: ALT3 mux port: D[9] of instance: csi.
    //                NOTE: - Config Register IOMUXC_CSI_IPP_CSI_D_9_SELECT_INPUT for mode ALT3.
    //     ALT4 (4) - Select mux mode: ALT4 mux port: LD of instance: spdc.
    //     ALT5 (5) - Select mux mode: ALT5 mux port: GPIO[24] of instance: gpio1.
    __raw_writel((SION_DISABLED & 0x1) << 4 | (ALT0 & 0x7), (IOMUXC_SW_MUX_CTL_PAD_EPDC_SDLE));
    // Pad Control Register:
    // IOMUXC_SW_PAD_CTL_PAD_EPDC_SDLE(0x020E0404)
    //   LVE (22) - Low Voltage Enable Field Reset: LVE_DISABLED
    //              Select one out of next values for pad: EPDC_SDLE.
    //     LVE_DISABLED (0) - High Voltage
    //     LVE_ENABLED (1) - Low Voltage
    //   HYS (16) - Hysteresis Enable Field Reset: HYS_ENABLED
    //              Select one out of next values for pad: EPDC_SDLE.
    //     HYS_DISABLED (0) - Hysteresis Disabled
    //     HYS_ENABLED (1) - Hysteresis Enabled
    //   PUS (15-14) - Pull Up / Down Configure Field Reset: PUS_100KOHM_PD
    //                 Select one out of next values for pad: EPDC_SDLE.
    //     PUS_100KOHM_PD (0) - 100K Ohm Pull Down
    //     PUS_47KOHM_PU (1) - 47K Ohm Pull Up
    //     PUS_100KOHM_PU (2) - 100K Ohm Pull Up
    //     PUS_22KOHM_PU (3) - 22K Ohm Pull Up
    //   PUE (13) - Pull / Keep Select Field Reset: PUE_KEEP
    //              Select one out of next values for pad: EPDC_SDLE.
    //     PUE_KEEP (0) - Keeper
    //     PUE_PULL (1) - Pull
    //   PKE (12) - Pull / Keep Enable Field Reset: PKE_ENABLED
    //              Select one out of next values for pad: EPDC_SDLE.
    //     PKE_DISABLED (0) - Pull/Keeper Disabled
    //     PKE_ENABLED (1) - Pull/Keeper Enabled
    //   ODE (11) - Open Drain Enable Field Reset: ODE_DISABLED
    //              Select one out of next values for pad: EPDC_SDLE.
    //     ODE_DISABLED (0) - Open Drain Disabled
    //     ODE_ENABLED (1) - Open Drain Enabled
    //   SPEED (7-6) - Speed Field Reset: SPD_100MHZ
    //                 Select one out of next values for pad: EPDC_SDLE.
    //     SPD_TBD (0) - TBD
    //     SPD_50MHZ (1) - Low(50 MHz)
    //     SPD_100MHZ (2) - Medium(100 MHz)
    //     SPD_200MHZ (3) - Maximum(200 MHz)
    //   DSE (5-3) - Drive Strength Field Reset: DSE_40OHM
    //               Select one out of next values for pad: EPDC_SDLE.
    //     DSE_DISABLED (0) - Output driver disabled.
    //     DSE_240OHM (1) - 240 Ohm
    //     DSE_120OHM (2) - 120 Ohm
    //     DSE_80OHM (3) - 80 Ohm
    //     DSE_60OHM (4) - 60 Ohm
    //     DSE_48OHM (5) - 48 Ohm
    //     DSE_40OHM (6) - 40 Ohm
    //     DSE_34OHM (7) - 34 Ohm
    //   SRE (0) - Slew Rate Field Reset: SRE_SLOW
    //             Select one out of next values for pad: EPDC_SDLE.
    //     SRE_SLOW (0) - Slow Slew Rate
    //     SRE_FAST (1) - Fast Slew Rate
    __raw_writel((LVE_DISABLED & 0x1) << 22 | (HYS_ENABLED & 0x1) << 16 | (PUS_100KOHM_PD & 0x3) << 14 |
           (PUE_KEEP & 0x1) << 13 | (PKE_ENABLED & 0x1) << 12 | (ODE_DISABLED & 0x1) << 11 |
           (SPD_100MHZ & 0x3) << 6 | (DSE_60OHM & 0x7) << 3 | (SRE_FAST & 0x1), (IOMUXC_SW_PAD_CTL_PAD_EPDC_SDLE));

    // Config epdc.SDOE to pad EPDC_SDOE(E7)
    // Mux Register:
    // IOMUXC_SW_MUX_CTL_PAD_EPDC_SDOE(0x020E0118)
    //   SION (4) - Software Input On Field Reset: SION_DISABLED
    //              Force the selected mux mode Input path no matter of MUX_MODE functionality.
    //     SION_DISABLED (0) - Input Path is determined by functionality of the selected mux mode (regular).
    //     SION_ENABLED (1) - Force input path of pad EPDC_SDOE.
    //   MUX_MODE (2-0) - MUX Mode Select Field Reset: ALT5
    //                    Select 1 of 8 iomux modes to be used for pad: EPDC_SDOE.
    //     ALT0 (0) - Select mux mode: ALT0 mux port: SDOE of instance: epdc.
    //     ALT1 (1) - Select mux mode: ALT1 mux port: SS0 of instance: ecspi2.
    //                NOTE: - Config Register IOMUXC_ECSPI2_IPP_IND_SS_B_0_SELECT_INPUT for mode ALT1.
    //     ALT2 (2) - Select mux mode: ALT2 mux port: XDIOR of instance: spdc.
    //     ALT3 (3) - Select mux mode: ALT3 mux port: D[10] of instance: csi.
    //                NOTE: - Config Register IOMUXC_CSI_IPP_CSI_D_10_SELECT_INPUT for mode ALT3.
    //     ALT4 (4) - Select mux mode: ALT4 mux port: XDIOL of instance: spdc.
    //     ALT5 (5) - Select mux mode: ALT5 mux port: GPIO[25] of instance: gpio1.
    __raw_writel((SION_DISABLED & 0x1) << 4 | (ALT0 & 0x7), (IOMUXC_SW_MUX_CTL_PAD_EPDC_SDOE));
    // Pad Control Register:
    // IOMUXC_SW_PAD_CTL_PAD_EPDC_SDOE(0x020E0408)
    //   LVE (22) - Low Voltage Enable Field Reset: LVE_DISABLED
    //              Select one out of next values for pad: EPDC_SDOE.
    //     LVE_DISABLED (0) - High Voltage
    //     LVE_ENABLED (1) - Low Voltage
    //   HYS (16) - Hysteresis Enable Field Reset: HYS_ENABLED
    //              Select one out of next values for pad: EPDC_SDOE.
    //     HYS_DISABLED (0) - Hysteresis Disabled
    //     HYS_ENABLED (1) - Hysteresis Enabled
    //   PUS (15-14) - Pull Up / Down Configure Field Reset: PUS_100KOHM_PD
    //                 Select one out of next values for pad: EPDC_SDOE.
    //     PUS_100KOHM_PD (0) - 100K Ohm Pull Down
    //     PUS_47KOHM_PU (1) - 47K Ohm Pull Up
    //     PUS_100KOHM_PU (2) - 100K Ohm Pull Up
    //     PUS_22KOHM_PU (3) - 22K Ohm Pull Up
    //   PUE (13) - Pull / Keep Select Field Reset: PUE_KEEP
    //              Select one out of next values for pad: EPDC_SDOE.
    //     PUE_KEEP (0) - Keeper
    //     PUE_PULL (1) - Pull
    //   PKE (12) - Pull / Keep Enable Field Reset: PKE_ENABLED
    //              Select one out of next values for pad: EPDC_SDOE.
    //     PKE_DISABLED (0) - Pull/Keeper Disabled
    //     PKE_ENABLED (1) - Pull/Keeper Enabled
    //   ODE (11) - Open Drain Enable Field Reset: ODE_DISABLED
    //              Select one out of next values for pad: EPDC_SDOE.
    //     ODE_DISABLED (0) - Open Drain Disabled
    //     ODE_ENABLED (1) - Open Drain Enabled
    //   SPEED (7-6) - Speed Field Reset: SPD_100MHZ
    //                 Select one out of next values for pad: EPDC_SDOE.
    //     SPD_TBD (0) - TBD
    //     SPD_50MHZ (1) - Low(50 MHz)
    //     SPD_100MHZ (2) - Medium(100 MHz)
    //     SPD_200MHZ (3) - Maximum(200 MHz)
    //   DSE (5-3) - Drive Strength Field Reset: DSE_40OHM
    //               Select one out of next values for pad: EPDC_SDOE.
    //     DSE_DISABLED (0) - Output driver disabled.
    //     DSE_240OHM (1) - 240 Ohm
    //     DSE_120OHM (2) - 120 Ohm
    //     DSE_80OHM (3) - 80 Ohm
    //     DSE_60OHM (4) - 60 Ohm
    //     DSE_48OHM (5) - 48 Ohm
    //     DSE_40OHM (6) - 40 Ohm
    //     DSE_34OHM (7) - 34 Ohm
    //   SRE (0) - Slew Rate Field Reset: SRE_SLOW
    //             Select one out of next values for pad: EPDC_SDOE.
    //     SRE_SLOW (0) - Slow Slew Rate
    //     SRE_FAST (1) - Fast Slew Rate
    __raw_writel((LVE_DISABLED & 0x1) << 22 | (HYS_ENABLED & 0x1) << 16 | (PUS_100KOHM_PD & 0x3) << 14 |
           (PUE_KEEP & 0x1) << 13 | (PKE_ENABLED & 0x1) << 12 | (ODE_DISABLED & 0x1) << 11 |
           (SPD_100MHZ & 0x3) << 6 | (DSE_48OHM & 0x7) << 3 | (SRE_FAST & 0x1), (IOMUXC_SW_PAD_CTL_PAD_EPDC_SDOE));
}
