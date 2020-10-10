/*
 * Copyright (C) 2012, Freescale Semiconductor, Inc. All Rights Reserved.
 * THIS SOURCE CODE IS CONFIDENTIAL AND PROPRIETARY AND MAY NOT
 * BE USED OR DISTRIBUTED WITHOUT THE WRITTEN PERMISSION OF
 * Freescale Semiconductor, Inc.
*/

// File: uart4_iomux_config.c

#include <asm/io.h>
#include "include/iomux_define.h"
#include "include/iomux_register.h"
#include <mach/boardid.h>

// Function to config iomux for instance uart4.
void uart4_iomux_config(void)
{
    // Config uart4.RXD_MUX to pad SD1_DAT4(A22)
    // Mux Register:
    // IOMUXC_SW_MUX_CTL_PAD_SD1_DAT4(0x020E0244)
    //   SION (4) - Software Input On Field Reset: SION_DISABLED
    //              Force the selected mux mode Input path no matter of MUX_MODE functionality.
    //     SION_DISABLED (0) - Input Path is determined by functionality of the selected mux mode (regular).
    //     SION_ENABLED (1) - Force input path of pad SD1_DAT4.
    //   MUX_MODE (2-0) - MUX Mode Select Field Reset: ALT5
    //                    Select 1 of 8 iomux modes to be used for pad: SD1_DAT4.
    //     ALT0 (0) - Select mux mode: ALT0 mux port: DAT4 of instance: usdhc1.
    //     ALT1 (1) - Select mux mode: ALT1 mux port: MDC of instance: fec.
    //     ALT2 (2) - Select mux mode: ALT2 mux port: COL[3] of instance: kpp.
    //                NOTE: - Config Register IOMUXC_KPP_IPP_IND_COL_3_SELECT_INPUT for mode ALT2.
    //     ALT3 (3) - Select mux mode: ALT3 mux port: SDCLKN of instance: epdc.
    //     ALT4 (4) - Select mux mode: ALT4 mux port: RXD_MUX of instance: uart4.
    //                NOTE: - Config Register IOMUXC_UART4_IPP_UART_RXD_MUX_SELECT_INPUT for mode ALT4.
    //     ALT5 (5) - Select mux mode: ALT5 mux port: GPIO[12] of instance: gpio5.
    if (lab126_board_rev_greater(BOARD_ID_WHISKY_WAN_HVT1) || lab126_board_rev_greater(BOARD_ID_WHISKY_WFO_HVT1) ||
         lab126_board_rev_greater_eq(BOARD_ID_WOODY_2)) {
       __raw_writel((SION_DISABLED & 0x1) << 4 | (ALT5 & 0x7), IOMUXC_SW_MUX_CTL_PAD_SD1_DAT4);
    } else {
       __raw_writel((SION_DISABLED & 0x1) << 4 | (ALT4 & 0x7), IOMUXC_SW_MUX_CTL_PAD_SD1_DAT4);
    // Pad SD1_DAT4 is involved in Daisy Chain.
    // Input Select Register:
    // IOMUXC_UART4_IPP_UART_RXD_MUX_SELECT_INPUT(0x020E0814)
    //   DAISY (2-0) Reset: SEL_AUD_RXD_ALT2
    //                 Selecting Pads Involved in Daisy Chain.
    //                 NOTE: Instance: uart4,   In Pin: ipp_uart_rxd_mux
    //     SEL_AUD_RXD_ALT2 (0) - Selecting Pad: AUD_RXD for Mode: ALT2.
    //     SEL_AUD_TXC_ALT2 (1) - Selecting Pad: AUD_TXC for Mode: ALT2.
    //     SEL_KEY_COL6_ALT1 (2) - Selecting Pad: KEY_COL6 for Mode: ALT1.
    //     SEL_KEY_ROW6_ALT1 (3) - Selecting Pad: KEY_ROW6 for Mode: ALT1.
    //     SEL_SD1_DAT4_ALT4 (4) - Selecting Pad: SD1_DAT4 for Mode: ALT4.
    //     SEL_SD1_DAT5_ALT4 (5) - Selecting Pad: SD1_DAT5 for Mode: ALT4.
    //     SEL_UART1_RXD_ALT2 (6) - Selecting Pad: UART1_RXD for Mode: ALT2.
    //     SEL_UART1_TXD_ALT2 (7) - Selecting Pad: UART1_TXD for Mode: ALT2.
       __raw_writel((SEL_SD1_DAT4_ALT4 & 0x7), IOMUXC_UART4_IPP_UART_RXD_MUX_SELECT_INPUT);
    }
    // Pad Control Register:
    // IOMUXC_SW_PAD_CTL_PAD_SD1_DAT4(0x020E054C)
    //   LVE (22) - Low Voltage Enable Field Reset: LVE_DISABLED
    //              Select one out of next values for pad: SD1_DAT4.
    //     LVE_DISABLED (0) - High Voltage
    //     LVE_ENABLED (1) - Low Voltage
    //   HYS (16) - Hysteresis Enable Field Reset: HYS_ENABLED
    //              Select one out of next values for pad: SD1_DAT4.
    //     HYS_DISABLED (0) - Hysteresis Disabled
    //     HYS_ENABLED (1) - Hysteresis Enabled
    //   PUS (15-14) - Pull Up / Down Configure Field Reset: PUS_100KOHM_PD
    //                 Select one out of next values for pad: SD1_DAT4.
    //     PUS_100KOHM_PD (0) - 100K Ohm Pull Down
    //     PUS_47KOHM_PU (1) - 47K Ohm Pull Up
    //     PUS_100KOHM_PU (2) - 100K Ohm Pull Up
    //     PUS_22KOHM_PU (3) - 22K Ohm Pull Up
    //   PUE (13) - Pull / Keep Select Field Reset: PUE_KEEP
    //              Select one out of next values for pad: SD1_DAT4.
    //     PUE_KEEP (0) - Keeper
    //     PUE_PULL (1) - Pull
    //   PKE (12) - Pull / Keep Enable Field Reset: PKE_ENABLED
    //              Select one out of next values for pad: SD1_DAT4.
    //     PKE_DISABLED (0) - Pull/Keeper Disabled
    //     PKE_ENABLED (1) - Pull/Keeper Enabled
    //   ODE (11) - Open Drain Enable Field Reset: ODE_DISABLED
    //              Select one out of next values for pad: SD1_DAT4.
    //     ODE_DISABLED (0) - Open Drain Disabled
    //     ODE_ENABLED (1) - Open Drain Enabled
    //   SPEED (7-6) - Speed Field Reset: SPD_100MHZ
    //                 Select one out of next values for pad: SD1_DAT4.
    //     SPD_TBD (0) - TBD
    //     SPD_50MHZ (1) - Low(50 MHz)
    //     SPD_100MHZ (2) - Medium(100 MHz)
    //     SPD_200MHZ (3) - Maximum(200 MHz)
    //   DSE (5-3) - Drive Strength Field Reset: DSE_40OHM
    //               Select one out of next values for pad: SD1_DAT4.
    //     DSE_DISABLED (0) - Output driver disabled.
    //     DSE_240OHM (1) - 240 Ohm
    //     DSE_120OHM (2) - 120 Ohm
    //     DSE_80OHM (3) - 80 Ohm
    //     DSE_60OHM (4) - 60 Ohm
    //     DSE_48OHM (5) - 48 Ohm
    //     DSE_40OHM (6) - 40 Ohm
    //     DSE_34OHM (7) - 34 Ohm
    //   SRE (0) - Slew Rate Field Reset: SRE_SLOW
    //             Select one out of next values for pad: SD1_DAT4.
    //     SRE_SLOW (0) - Slow Slew Rate
    //     SRE_FAST (1) - Fast Slew Rate
    if(lab126_board_rev_greater(BOARD_ID_BOURBON_WFO_EVT1) || lab126_board_is(BOARD_ID_WARIO_4_256M_CFG_C) ||
	lab126_board_is(BOARD_ID_BOURBON_WFO_PREEVT2) ) {
        __raw_writel((LVE_DISABLED & 0x1) << 22 | (HYS_ENABLED & 0x1) << 16 | (PUS_100KOHM_PD & 0x3) << 14 |
           (PUE_PULL & 0x1) << 13 | (PKE_ENABLED & 0x1) << 12 | (ODE_DISABLED & 0x1) << 11 |
           (SPD_50MHZ & 0x3) << 6 | (DSE_40OHM & 0x7) << 3 | (SRE_SLOW & 0x1), IOMUXC_SW_PAD_CTL_PAD_SD1_DAT4);
     } else {
        __raw_writel((LVE_ENABLED & 0x1) << 22 | (HYS_ENABLED & 0x1) << 16 | (PUS_100KOHM_PU & 0x3) << 14 |
           (PUE_PULL & 0x1) << 13 | (PKE_ENABLED & 0x1) << 12 | (ODE_DISABLED & 0x1) << 11 |
           (SPD_100MHZ & 0x3) << 6 | (DSE_40OHM & 0x7) << 3 | (SRE_SLOW & 0x1), IOMUXC_SW_PAD_CTL_PAD_SD1_DAT4);
     }

    // Config uart4.TXD_MUX to pad SD1_DAT5(A21)
    // Mux Register:
    // IOMUXC_SW_MUX_CTL_PAD_SD1_DAT5(0x020E0248)
    //   SION (4) - Software Input On Field Reset: SION_DISABLED
    //              Force the selected mux mode Input path no matter of MUX_MODE functionality.
    //     SION_DISABLED (0) - Input Path is determined by functionality of the selected mux mode (regular).
    //     SION_ENABLED (1) - Force input path of pad SD1_DAT5.
    //   MUX_MODE (2-0) - MUX Mode Select Field Reset: ALT5
    //                    Select 1 of 8 iomux modes to be used for pad: SD1_DAT5.
    //     ALT0 (0) - Select mux mode: ALT0 mux port: DAT5 of instance: usdhc1.
    //     ALT1 (1) - Select mux mode: ALT1 mux port: RDATA[0] of instance: fec.
    //                NOTE: - Config Register IOMUXC_FEC_FEC_RDATA_0_SELECT_INPUT for mode ALT1.
    //     ALT2 (2) - Select mux mode: ALT2 mux port: ROW[3] of instance: kpp.
    //                NOTE: - Config Register IOMUXC_KPP_IPP_IND_ROW_3_SELECT_INPUT for mode ALT2.
    //     ALT3 (3) - Select mux mode: ALT3 mux port: SDOED of instance: epdc.
    //     ALT4 (4) - Select mux mode: ALT4 mux port: TXD_MUX of instance: uart4.
    //                NOTE: - Config Register IOMUXC_UART4_IPP_UART_RXD_MUX_SELECT_INPUT for mode ALT4.
    //     ALT5 (5) - Select mux mode: ALT5 mux port: GPIO[9] of instance: gpio5.
    if (lab126_board_rev_greater(BOARD_ID_WHISKY_WAN_HVT1) || lab126_board_rev_greater(BOARD_ID_WHISKY_WFO_HVT1) ||
        lab126_board_rev_greater_eq(BOARD_ID_WOODY_2)) {
        __raw_writel((SION_DISABLED & 0x1) << 4 | (ALT5 & 0x7), IOMUXC_SW_MUX_CTL_PAD_SD1_DAT5);
    } else {
        __raw_writel((SION_DISABLED & 0x1) << 4 | (ALT4 & 0x7), IOMUXC_SW_MUX_CTL_PAD_SD1_DAT5);
    }
    // Pad SD1_DAT5 is involved in Daisy Chain.
    // Input Select Register:
    // IOMUXC_UART4_IPP_UART_RXD_MUX_SELECT_INPUT(0x020E0814)
    //   DAISY (2-0) Reset: SEL_AUD_RXD_ALT2
    //                 Selecting Pads Involved in Daisy Chain.
    //                 NOTE: Instance: uart4,   In Pin: ipp_uart_rxd_mux
    //     SEL_AUD_RXD_ALT2 (0) - Selecting Pad: AUD_RXD for Mode: ALT2.
    //     SEL_AUD_TXC_ALT2 (1) - Selecting Pad: AUD_TXC for Mode: ALT2.
    //     SEL_KEY_COL6_ALT1 (2) - Selecting Pad: KEY_COL6 for Mode: ALT1.
    //     SEL_KEY_ROW6_ALT1 (3) - Selecting Pad: KEY_ROW6 for Mode: ALT1.
    //     SEL_SD1_DAT4_ALT4 (4) - Selecting Pad: SD1_DAT4 for Mode: ALT4.
    //     SEL_SD1_DAT5_ALT4 (5) - Selecting Pad: SD1_DAT5 for Mode: ALT4.
    //     SEL_UART1_RXD_ALT2 (6) - Selecting Pad: UART1_RXD for Mode: ALT2.
    //     SEL_UART1_TXD_ALT2 (7) - Selecting Pad: UART1_TXD for Mode: ALT2.
//    __raw_writel((SEL_AUD_RXD_ALT2 & 0x7), IOMUXC_UART4_IPP_UART_RXD_MUX_SELECT_INPUT);
    // Pad Control Register:
    // IOMUXC_SW_PAD_CTL_PAD_SD1_DAT5(0x020E0550)
    //   LVE (22) - Low Voltage Enable Field Reset: LVE_DISABLED
    //              Select one out of next values for pad: SD1_DAT5.
    //     LVE_DISABLED (0) - High Voltage
    //     LVE_ENABLED (1) - Low Voltage
    //   HYS (16) - Hysteresis Enable Field Reset: HYS_ENABLED
    //              Select one out of next values for pad: SD1_DAT5.
    //     HYS_DISABLED (0) - Hysteresis Disabled
    //     HYS_ENABLED (1) - Hysteresis Enabled
    //   PUS (15-14) - Pull Up / Down Configure Field Reset: PUS_100KOHM_PD
    //                 Select one out of next values for pad: SD1_DAT5.
    //     PUS_100KOHM_PD (0) - 100K Ohm Pull Down
    //     PUS_47KOHM_PU (1) - 47K Ohm Pull Up
    //     PUS_100KOHM_PU (2) - 100K Ohm Pull Up
    //     PUS_22KOHM_PU (3) - 22K Ohm Pull Up
    //   PUE (13) - Pull / Keep Select Field Reset: PUE_KEEP
    //              Select one out of next values for pad: SD1_DAT5.
    //     PUE_KEEP (0) - Keeper
    //     PUE_PULL (1) - Pull
    //   PKE (12) - Pull / Keep Enable Field Reset: PKE_ENABLED
    //              Select one out of next values for pad: SD1_DAT5.
    //     PKE_DISABLED (0) - Pull/Keeper Disabled
    //     PKE_ENABLED (1) - Pull/Keeper Enabled
    //   ODE (11) - Open Drain Enable Field Reset: ODE_DISABLED
    //              Select one out of next values for pad: SD1_DAT5.
    //     ODE_DISABLED (0) - Open Drain Disabled
    //     ODE_ENABLED (1) - Open Drain Enabled
    //   SPEED (7-6) - Speed Field Reset: SPD_100MHZ
    //                 Select one out of next values for pad: SD1_DAT5.
    //     SPD_TBD (0) - TBD
    //     SPD_50MHZ (1) - Low(50 MHz)
    //     SPD_100MHZ (2) - Medium(100 MHz)
    //     SPD_200MHZ (3) - Maximum(200 MHz)
    //   DSE (5-3) - Drive Strength Field Reset: DSE_40OHM
    //               Select one out of next values for pad: SD1_DAT5.
    //     DSE_DISABLED (0) - Output driver disabled.
    //     DSE_240OHM (1) - 240 Ohm
    //     DSE_120OHM (2) - 120 Ohm
    //     DSE_80OHM (3) - 80 Ohm
    //     DSE_60OHM (4) - 60 Ohm
    //     DSE_48OHM (5) - 48 Ohm
    //     DSE_40OHM (6) - 40 Ohm
    //     DSE_34OHM (7) - 34 Ohm
    //   SRE (0) - Slew Rate Field Reset: SRE_SLOW
    //             Select one out of next values for pad: SD1_DAT5.
    //     SRE_SLOW (0) - Slow Slew Rate
    //     SRE_FAST (1) - Fast Slew Rate
    if(lab126_board_rev_greater(BOARD_ID_BOURBON_WFO_EVT1) || lab126_board_is(BOARD_ID_WARIO_4_256M_CFG_C) ||
 	lab126_board_is(BOARD_ID_BOURBON_WFO_PREEVT2) ) {
        __raw_writel((LVE_DISABLED & 0x1) << 22 | (HYS_ENABLED & 0x1) << 16 | (PUS_100KOHM_PD & 0x3) << 14 |
           (PUE_PULL & 0x1) << 13 | (PKE_ENABLED & 0x1) << 12 | (ODE_DISABLED & 0x1) << 11 |
           (SPD_50MHZ & 0x3) << 6 | (DSE_40OHM & 0x7) << 3 | (SRE_SLOW & 0x1), IOMUXC_SW_PAD_CTL_PAD_SD1_DAT5);
    } else {
        __raw_writel((LVE_ENABLED & 0x1) << 22 | (HYS_ENABLED & 0x1) << 16 | (PUS_100KOHM_PD & 0x3) << 14 |
           (PUE_PULL & 0x1) << 13 | (PKE_ENABLED & 0x1) << 12 | (ODE_DISABLED & 0x1) << 11 |
           (SPD_100MHZ & 0x3) << 6 | (DSE_40OHM & 0x7) << 3 | (SRE_SLOW & 0x1), IOMUXC_SW_PAD_CTL_PAD_SD1_DAT5);
    }
}
