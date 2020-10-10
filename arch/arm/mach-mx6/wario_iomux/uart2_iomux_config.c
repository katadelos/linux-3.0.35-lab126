/*
 * Copyright (C) 2012, Freescale Semiconductor, Inc. All Rights Reserved.
 * THIS SOURCE CODE IS CONFIDENTIAL AND PROPRIETARY AND MAY NOT
 * BE USED OR DISTRIBUTED WITHOUT THE WRITTEN PERMISSION OF
 * Freescale Semiconductor, Inc.
*/

// File: uart2_iomux_config.c

#include <asm/io.h>
#include "include/iomux_define.h"
#include "include/iomux_register.h"
#include <mach/boardid.h>

// Function to config iomux for instance uart2.
void uart2_iomux_config(void)
{
    // Config uart2.RXD_MUX to pad LCD_ENABLE(J24)
    // Mux Register:
    // IOMUXC_SW_MUX_CTL_PAD_LCD_ENABLE(0x020E0210)
    //   SION (4) - Software Input On Field Reset: SION_DISABLED
    //              Force the selected mux mode Input path no matter of MUX_MODE functionality.
    //     SION_DISABLED (0) - Input Path is determined by functionality of the selected mux mode (regular).
    //     SION_ENABLED (1) - Force input path of pad LCD_ENABLE.
    //   MUX_MODE (2-0) - MUX Mode Select Field Reset: ALT5
    //                    Select 1 of 8 iomux modes to be used for pad: LCD_ENABLE.
    //     ALT0 (0) - Select mux mode: ALT0 mux port: ENABLE of instance: lcdif.
    //     ALT1 (1) - Select mux mode: ALT1 mux port: DAT5 of instance: usdhc4.
    //                NOTE: - Config Register IOMUXC_USDHC4_IPP_DAT5_IN_SELECT_INPUT for mode ALT1.
    //     ALT2 (2) - Select mux mode: ALT2 mux port: RD_E of instance: lcdif.
    //     ALT3 (3) - Select mux mode: ALT3 mux port: WEIM_OE of instance: weim.
    //     ALT4 (4) - Select mux mode: ALT4 mux port: RXD_MUX of instance: uart2.
    //                NOTE: - Config Register IOMUXC_UART2_IPP_UART_RXD_MUX_SELECT_INPUT for mode ALT4.
    //     ALT5 (5) - Select mux mode: ALT5 mux port: GPIO[16] of instance: gpio2.
    if (lab126_board_rev_greater(BOARD_ID_WHISKY_WAN_HVT1) || lab126_board_rev_greater(BOARD_ID_WHISKY_WFO_HVT1) ||
        lab126_board_rev_greater_eq(BOARD_ID_WOODY_2)) {
    	__raw_writel((SION_DISABLED & 0x1) << 4 | (ALT5 & 0x7), IOMUXC_SW_MUX_CTL_PAD_LCD_ENABLE);
    } else {
    	__raw_writel((SION_DISABLED & 0x1) << 4 | (ALT4 & 0x7), IOMUXC_SW_MUX_CTL_PAD_LCD_ENABLE);
   
    // Pad LCD_ENABLE is involved in Daisy Chain.
    // Input Select Register:
    // IOMUXC_UART2_IPP_UART_RXD_MUX_SELECT_INPUT(0x020E0804)
    //   DAISY (2-0) Reset: SEL_EPDC_D12_ALT1
    //                 Selecting Pads Involved in Daisy Chain.
    //                 NOTE: Instance: uart2,   In Pin: ipp_uart_rxd_mux
    //     SEL_EPDC_D12_ALT1 (0) - Selecting Pad: EPDC_D12 for Mode: ALT1.
    //     SEL_EPDC_D13_ALT1 (1) - Selecting Pad: EPDC_D13 for Mode: ALT1.
    //     SEL_LCD_ENABLE_ALT4 (2) - Selecting Pad: LCD_ENABLE for Mode: ALT4.
    //     SEL_LCD_HSYNC_ALT4 (3) - Selecting Pad: LCD_HSYNC for Mode: ALT4.
    //     SEL_SD2_DAT4_ALT2 (4) - Selecting Pad: SD2_DAT4 for Mode: ALT2.
    //     SEL_SD2_DAT5_ALT2 (5) - Selecting Pad: SD2_DAT5 for Mode: ALT2.
       __raw_writel((SEL_EPDC_D12_ALT1 & 0x7), IOMUXC_UART2_IPP_UART_RXD_MUX_SELECT_INPUT);
    }
    // Pad Control Register:
    // IOMUXC_SW_PAD_CTL_PAD_LCD_ENABLE(0x020E0518)
    //   LVE (22) - Low Voltage Enable Field Reset: LVE_DISABLED
    //              Select one out of next values for pad: LCD_ENABLE.
    //     LVE_DISABLED (0) - High Voltage
    //     LVE_ENABLED (1) - Low Voltage
    //   HYS (16) - Hysteresis Enable Field Reset: HYS_ENABLED
    //              Select one out of next values for pad: LCD_ENABLE.
    //     HYS_DISABLED (0) - Hysteresis Disabled
    //     HYS_ENABLED (1) - Hysteresis Enabled
    //   PUS (15-14) - Pull Up / Down Configure Field Reset: PUS_100KOHM_PD
    //                 Select one out of next values for pad: LCD_ENABLE.
    //     PUS_100KOHM_PD (0) - 100K Ohm Pull Down
    //     PUS_47KOHM_PU (1) - 47K Ohm Pull Up
    //     PUS_100KOHM_PU (2) - 100K Ohm Pull Up
    //     PUS_22KOHM_PU (3) - 22K Ohm Pull Up
    //   PUE (13) - Pull / Keep Select Field Reset: PUE_KEEP
    //              Select one out of next values for pad: LCD_ENABLE.
    //     PUE_KEEP (0) - Keeper
    //     PUE_PULL (1) - Pull
    //   PKE (12) - Pull / Keep Enable Field Reset: PKE_ENABLED
    //              Select one out of next values for pad: LCD_ENABLE.
    //     PKE_DISABLED (0) - Pull/Keeper Disabled
    //     PKE_ENABLED (1) - Pull/Keeper Enabled
    //   ODE (11) - Open Drain Enable Field Reset: ODE_DISABLED
    //              Select one out of next values for pad: LCD_ENABLE.
    //     ODE_DISABLED (0) - Open Drain Disabled
    //     ODE_ENABLED (1) - Open Drain Enabled
    //   SPEED (7-6) - Speed Field Reset: SPD_100MHZ
    //                 Select one out of next values for pad: LCD_ENABLE.
    //     SPD_TBD (0) - TBD
    //     SPD_50MHZ (1) - Low(50 MHz)
    //     SPD_100MHZ (2) - Medium(100 MHz)
    //     SPD_200MHZ (3) - Maximum(200 MHz)
    //   DSE (5-3) - Drive Strength Field Reset: DSE_40OHM
    //               Select one out of next values for pad: LCD_ENABLE.
    //     DSE_DISABLED (0) - Output driver disabled.
    //     DSE_240OHM (1) - 240 Ohm
    //     DSE_120OHM (2) - 120 Ohm
    //     DSE_80OHM (3) - 80 Ohm
    //     DSE_60OHM (4) - 60 Ohm
    //     DSE_48OHM (5) - 48 Ohm
    //     DSE_40OHM (6) - 40 Ohm
    //     DSE_34OHM (7) - 34 Ohm
    //   SRE (0) - Slew Rate Field Reset: SRE_SLOW
    //             Select one out of next values for pad: LCD_ENABLE.
    //     SRE_SLOW (0) - Slow Slew Rate
    //     SRE_FAST (1) - Fast Slew Rate
    if (lab126_board_rev_greater(BOARD_ID_WHISKY_WAN_HVT1) || lab126_board_rev_greater(BOARD_ID_WHISKY_WFO_HVT1) ||
        lab126_board_rev_greater_eq(BOARD_ID_WOODY_2)) {
        __raw_writel((LVE_ENABLED& 0x1) << 22 | (HYS_ENABLED & 0x1) << 16 | (PUS_100KOHM_PD & 0x3) << 14 |
        (PUE_PULL & 0x1) << 13 | (PKE_ENABLED & 0x1) << 12 | (ODE_DISABLED & 0x1) << 11 |
        (SPD_50MHZ & 0x3) << 6 | (DSE_40OHM & 0x7) << 3 | (SRE_SLOW & 0x1), IOMUXC_SW_PAD_CTL_PAD_LCD_ENABLE);
    } else {
        __raw_writel((LVE_DISABLED & 0x1) << 22 | (HYS_ENABLED & 0x1) << 16 | (PUS_100KOHM_PU & 0x3) << 14 |
        (PUE_PULL & 0x1) << 13 | (PKE_ENABLED & 0x1) << 12 | (ODE_DISABLED & 0x1) << 11 |
        (SPD_100MHZ & 0x3) << 6 | (DSE_40OHM & 0x7) << 3 | (SRE_SLOW & 0x1), IOMUXC_SW_PAD_CTL_PAD_LCD_ENABLE);
    }
}
