/*
 * Copyright (C) 2012, Freescale Semiconductor, Inc. All Rights Reserved.
 * THIS SOURCE CODE IS CONFIDENTIAL AND PROPRIETARY AND MAY NOT
 * BE USED OR DISTRIBUTED WITHOUT THE WRITTEN PERMISSION OF
 * Freescale Semiconductor, Inc.
*/

// File: sjc_iomux_config.c

#include <asm/io.h>
#include "include/iomux_define.h"
#include "include/iomux_register.h"

// Function to config iomux for instance sjc.
void sjc_iomux_config(void)
{
    // Config sjc.MOD to pad JTAG_MOD(Y14)
    // Pad Control Register:
    // IOMUXC_SW_PAD_CTL_PAD_JTAG_MOD(0x020E045C)
    //   LVE (22) - Low Voltage Enable Field Reset: LVE_DISABLED
    //              Read Only Field
    //     LVE_DISABLED (0) - High Voltage
    //   HYS (16) - Hysteresis Enable Field Reset: HYS_DISABLED
    //              Select one out of next values for pad: JTAG_MOD.
    //     HYS_DISABLED (0) - Hysteresis Disabled
    //     HYS_ENABLED (1) - Hysteresis Enabled
    //   PUS (15-14) - Pull Up / Down Configure Field Reset: PUS_100KOHM_PU
    //                 Select one out of next values for pad: JTAG_MOD.
    //     PUS_100KOHM_PD (0) - 100K Ohm Pull Down
    //     PUS_47KOHM_PU (1) - 47K Ohm Pull Up
    //     PUS_100KOHM_PU (2) - 100K Ohm Pull Up
    //     PUS_22KOHM_PU (3) - 22K Ohm Pull Up
    //   PUE (13) - Pull / Keep Select Field Reset: PUE_PULL
    //              Read Only Field
    //     PUE_PULL (1) - Pull
    //   PKE (12) - Pull / Keep Enable Field Reset: PKE_ENABLED
    //              Select one out of next values for pad: JTAG_MOD.
    //     PKE_DISABLED (0) - Pull/Keeper Disabled
    //     PKE_ENABLED (1) - Pull/Keeper Enabled
    //   ODE (11) - Open Drain Enable Field Reset: ODE_DISABLED
    //              Read Only Field
    //     ODE_DISABLED (0) - Open Drain Disabled
    //   SPEED (7-6) - Speed Field Reset: SPD_50MHZ
    //                 Read Only Field
    //     SPD_50MHZ (1) - Low(50 MHz)
    //   DSE (5-3) - Drive Strength Field Reset: DSE_60OHM
    //               Read Only Field
    //     DSE_60OHM (4) - 60 Ohm
    //   SRE (0) - Slew Rate Field Reset: SRE_SLOW
    //             Read Only Field
    //     SRE_SLOW (0) - Slow Slew Rate
    __raw_writel((LVE_DISABLED & 0x1) << 22 | (HYS_DISABLED & 0x1) << 16 | (PUS_100KOHM_PU & 0x3) << 14 |
           (PUE_PULL & 0x1) << 13 | (PKE_ENABLED & 0x1) << 12 | (ODE_DISABLED & 0x1) << 11 |
           (SPD_50MHZ & 0x3) << 6 | (DSE_60OHM & 0x7) << 3 | (SRE_SLOW & 0x1), IOMUXC_SW_PAD_CTL_PAD_JTAG_MOD);

    // Config sjc.TCK to pad JTAG_TCK(AA14)
    // Pad Control Register:
    // IOMUXC_SW_PAD_CTL_PAD_JTAG_TCK(0x020E0460)
    //   LVE (22) - Low Voltage Enable Field Reset: LVE_DISABLED
    //              Read Only Field
    //     LVE_DISABLED (0) - High Voltage
    //   HYS (16) - Hysteresis Enable Field Reset: HYS_DISABLED
    //              Select one out of next values for pad: JTAG_TCK.
    //     HYS_DISABLED (0) - Hysteresis Disabled
    //     HYS_ENABLED (1) - Hysteresis Enabled
    //   PUS (15-14) - Pull Up / Down Configure Field Reset: PUS_47KOHM_PU
    //                 Select one out of next values for pad: JTAG_TCK.
    //     PUS_100KOHM_PD (0) - 100K Ohm Pull Down
    //     PUS_47KOHM_PU (1) - 47K Ohm Pull Up
    //     PUS_100KOHM_PU (2) - 100K Ohm Pull Up
    //     PUS_22KOHM_PU (3) - 22K Ohm Pull Up
    //   PUE (13) - Pull / Keep Select Field Reset: PUE_PULL
    //              Read Only Field
    //     PUE_PULL (1) - Pull
    //   PKE (12) - Pull / Keep Enable Field Reset: PKE_ENABLED
    //              Select one out of next values for pad: JTAG_TCK.
    //     PKE_DISABLED (0) - Pull/Keeper Disabled
    //     PKE_ENABLED (1) - Pull/Keeper Enabled
    //   ODE (11) - Open Drain Enable Field Reset: ODE_DISABLED
    //              Read Only Field
    //     ODE_DISABLED (0) - Open Drain Disabled
    //   SPEED (7-6) - Speed Field Reset: SPD_50MHZ
    //                 Read Only Field
    //     SPD_50MHZ (1) - Low(50 MHz)
    //   DSE (5-3) - Drive Strength Field Reset: DSE_60OHM
    //               Read Only Field
    //     DSE_60OHM (4) - 60 Ohm
    //   SRE (0) - Slew Rate Field Reset: SRE_SLOW
    //             Read Only Field
    //     SRE_SLOW (0) - Slow Slew Rate
    __raw_writel((LVE_DISABLED & 0x1) << 22 | (HYS_DISABLED & 0x1) << 16 | (PUS_47KOHM_PU & 0x3) << 14 |
           (PUE_PULL & 0x1) << 13 | (PKE_ENABLED & 0x1) << 12 | (ODE_DISABLED & 0x1) << 11 |
           (SPD_50MHZ & 0x3) << 6 | (DSE_60OHM & 0x7) << 3 | (SRE_SLOW & 0x1), IOMUXC_SW_PAD_CTL_PAD_JTAG_TCK);

    // Config sjc.TDI to pad JTAG_TDI(W14)
    // Pad Control Register:
    // IOMUXC_SW_PAD_CTL_PAD_JTAG_TDI(0x020E0464)
    //   LVE (22) - Low Voltage Enable Field Reset: LVE_DISABLED
    //              Read Only Field
    //     LVE_DISABLED (0) - High Voltage
    //   HYS (16) - Hysteresis Enable Field Reset: HYS_DISABLED
    //              Select one out of next values for pad: JTAG_TDI.
    //     HYS_DISABLED (0) - Hysteresis Disabled
    //     HYS_ENABLED (1) - Hysteresis Enabled
    //   PUS (15-14) - Pull Up / Down Configure Field Reset: PUS_47KOHM_PU
    //                 Select one out of next values for pad: JTAG_TDI.
    //     PUS_100KOHM_PD (0) - 100K Ohm Pull Down
    //     PUS_47KOHM_PU (1) - 47K Ohm Pull Up
    //     PUS_100KOHM_PU (2) - 100K Ohm Pull Up
    //     PUS_22KOHM_PU (3) - 22K Ohm Pull Up
    //   PUE (13) - Pull / Keep Select Field Reset: PUE_PULL
    //              Read Only Field
    //     PUE_PULL (1) - Pull
    //   PKE (12) - Pull / Keep Enable Field Reset: PKE_ENABLED
    //              Select one out of next values for pad: JTAG_TDI.
    //     PKE_DISABLED (0) - Pull/Keeper Disabled
    //     PKE_ENABLED (1) - Pull/Keeper Enabled
    //   ODE (11) - Open Drain Enable Field Reset: ODE_DISABLED
    //              Read Only Field
    //     ODE_DISABLED (0) - Open Drain Disabled
    //   SPEED (7-6) - Speed Field Reset: SPD_50MHZ
    //                 Read Only Field
    //     SPD_50MHZ (1) - Low(50 MHz)
    //   DSE (5-3) - Drive Strength Field Reset: DSE_60OHM
    //               Read Only Field
    //     DSE_60OHM (4) - 60 Ohm
    //   SRE (0) - Slew Rate Field Reset: SRE_SLOW
    //             Read Only Field
    //     SRE_SLOW (0) - Slow Slew Rate
    __raw_writel((LVE_DISABLED & 0x1) << 22 | (HYS_DISABLED & 0x1) << 16 | (PUS_47KOHM_PU & 0x3) << 14 |
           (PUE_PULL & 0x1) << 13 | (PKE_ENABLED & 0x1) << 12 | (ODE_DISABLED & 0x1) << 11 |
           (SPD_50MHZ & 0x3) << 6 | (DSE_60OHM & 0x7) << 3 | (SRE_SLOW & 0x1), IOMUXC_SW_PAD_CTL_PAD_JTAG_TDI);

    // Config sjc.TDO to pad JTAG_TDO(W15)
    // Pad Control Register:
    // IOMUXC_SW_PAD_CTL_PAD_JTAG_TDO(0x020E0468)
    //   LVE (22) - Low Voltage Enable Field Reset: LVE_DISABLED
    //              Read Only Field
    //     LVE_DISABLED (0) - High Voltage
    //   HYS (16) - Hysteresis Enable Field Reset: HYS_DISABLED
    //              Read Only Field
    //     HYS_DISABLED (0) - Hysteresis Disabled
    //   PUS (15-14) - Pull Up / Down Configure Field Reset: PUS_100KOHM_PU
    //                 Read Only Field
    //     PUS_100KOHM_PU (2) - 100K Ohm Pull Up
    //   PUE (13) - Pull / Keep Select Field Reset: PUE_KEEP
    //              Read Only Field
    //     PUE_KEEP (0) - Keeper
    //   PKE (12) - Pull / Keep Enable Field Reset: PKE_ENABLED
    //              Select one out of next values for pad: JTAG_TDO.
    //     PKE_DISABLED (0) - Pull/Keeper Disabled
    //     PKE_ENABLED (1) - Pull/Keeper Enabled
    //   ODE (11) - Open Drain Enable Field Reset: ODE_DISABLED
    //              Read Only Field
    //     ODE_DISABLED (0) - Open Drain Disabled
    //   SPEED (7-6) - Speed Field Reset: SPD_100MHZ
    //                 Read Only Field
    //     SPD_100MHZ (2) - Medium(100 MHz)
    //   DSE (5-3) - Drive Strength Field Reset: DSE_40OHM
    //               Read Only Field
    //     DSE_40OHM (6) - 40 Ohm
    //   SRE (0) - Slew Rate Field Reset: SRE_FAST
    //             Read Only Field
    //     SRE_FAST (1) - Fast Slew Rate
    __raw_writel((LVE_DISABLED & 0x1) << 22 | (HYS_DISABLED & 0x1) << 16 | (PUS_100KOHM_PU & 0x3) << 14 |
           (PUE_KEEP & 0x1) << 13 | (PKE_ENABLED & 0x1) << 12 | (ODE_DISABLED & 0x1) << 11 |
           (SPD_100MHZ & 0x3) << 6 | (DSE_40OHM & 0x7) << 3 | (SRE_FAST & 0x1), IOMUXC_SW_PAD_CTL_PAD_JTAG_TDO);

    // Config sjc.TMS to pad JTAG_TMS(Y15)
    // Pad Control Register:
    // IOMUXC_SW_PAD_CTL_PAD_JTAG_TMS(0x020E046C)
    //   LVE (22) - Low Voltage Enable Field Reset: LVE_DISABLED
    //              Read Only Field
    //     LVE_DISABLED (0) - High Voltage
    //   HYS (16) - Hysteresis Enable Field Reset: HYS_DISABLED
    //              Select one out of next values for pad: JTAG_TMS.
    //     HYS_DISABLED (0) - Hysteresis Disabled
    //     HYS_ENABLED (1) - Hysteresis Enabled
    //   PUS (15-14) - Pull Up / Down Configure Field Reset: PUS_47KOHM_PU
    //                 Select one out of next values for pad: JTAG_TMS.
    //     PUS_100KOHM_PD (0) - 100K Ohm Pull Down
    //     PUS_47KOHM_PU (1) - 47K Ohm Pull Up
    //     PUS_100KOHM_PU (2) - 100K Ohm Pull Up
    //     PUS_22KOHM_PU (3) - 22K Ohm Pull Up
    //   PUE (13) - Pull / Keep Select Field Reset: PUE_PULL
    //              Read Only Field
    //     PUE_PULL (1) - Pull
    //   PKE (12) - Pull / Keep Enable Field Reset: PKE_ENABLED
    //              Select one out of next values for pad: JTAG_TMS.
    //     PKE_DISABLED (0) - Pull/Keeper Disabled
    //     PKE_ENABLED (1) - Pull/Keeper Enabled
    //   ODE (11) - Open Drain Enable Field Reset: ODE_DISABLED
    //              Read Only Field
    //     ODE_DISABLED (0) - Open Drain Disabled
    //   SPEED (7-6) - Speed Field Reset: SPD_50MHZ
    //                 Read Only Field
    //     SPD_50MHZ (1) - Low(50 MHz)
    //   DSE (5-3) - Drive Strength Field Reset: DSE_60OHM
    //               Read Only Field
    //     DSE_60OHM (4) - 60 Ohm
    //   SRE (0) - Slew Rate Field Reset: SRE_SLOW
    //             Read Only Field
    //     SRE_SLOW (0) - Slow Slew Rate
    __raw_writel((LVE_DISABLED & 0x1) << 22 | (HYS_DISABLED & 0x1) << 16 | (PUS_47KOHM_PU & 0x3) << 14 |
           (PUE_PULL & 0x1) << 13 | (PKE_ENABLED & 0x1) << 12 | (ODE_DISABLED & 0x1) << 11 |
           (SPD_50MHZ & 0x3) << 6 | (DSE_60OHM & 0x7) << 3 | (SRE_SLOW & 0x1), IOMUXC_SW_PAD_CTL_PAD_JTAG_TMS);

    // Config sjc.TRSTB to pad JTAG_TRSTB(AA15)
    // Pad Control Register:
    // IOMUXC_SW_PAD_CTL_PAD_JTAG_TRSTB(0x020E0470)
    //   LVE (22) - Low Voltage Enable Field Reset: LVE_DISABLED
    //              Read Only Field
    //     LVE_DISABLED (0) - High Voltage
    //   HYS (16) - Hysteresis Enable Field Reset: HYS_DISABLED
    //              Select one out of next values for pad: JTAG_TRSTB.
    //     HYS_DISABLED (0) - Hysteresis Disabled
    //     HYS_ENABLED (1) - Hysteresis Enabled
    //   PUS (15-14) - Pull Up / Down Configure Field Reset: PUS_47KOHM_PU
    //                 Select one out of next values for pad: JTAG_TRSTB.
    //     PUS_100KOHM_PD (0) - 100K Ohm Pull Down
    //     PUS_47KOHM_PU (1) - 47K Ohm Pull Up
    //     PUS_100KOHM_PU (2) - 100K Ohm Pull Up
    //     PUS_22KOHM_PU (3) - 22K Ohm Pull Up
    //   PUE (13) - Pull / Keep Select Field Reset: PUE_PULL
    //              Read Only Field
    //     PUE_PULL (1) - Pull
    //   PKE (12) - Pull / Keep Enable Field Reset: PKE_ENABLED
    //              Select one out of next values for pad: JTAG_TRSTB.
    //     PKE_DISABLED (0) - Pull/Keeper Disabled
    //     PKE_ENABLED (1) - Pull/Keeper Enabled
    //   ODE (11) - Open Drain Enable Field Reset: ODE_DISABLED
    //              Read Only Field
    //     ODE_DISABLED (0) - Open Drain Disabled
    //   SPEED (7-6) - Speed Field Reset: SPD_50MHZ
    //                 Read Only Field
    //     SPD_50MHZ (1) - Low(50 MHz)
    //   DSE (5-3) - Drive Strength Field Reset: DSE_60OHM
    //               Read Only Field
    //     DSE_60OHM (4) - 60 Ohm
    //   SRE (0) - Slew Rate Field Reset: SRE_SLOW
    //             Read Only Field
    //     SRE_SLOW (0) - Slow Slew Rate
    __raw_writel((LVE_DISABLED & 0x1) << 22 | (HYS_DISABLED & 0x1) << 16 | (PUS_47KOHM_PU & 0x3) << 14 |
           (PUE_PULL & 0x1) << 13 | (PKE_ENABLED & 0x1) << 12 | (ODE_DISABLED & 0x1) << 11 |
           (SPD_50MHZ & 0x3) << 6 | (DSE_60OHM & 0x7) << 3 | (SRE_SLOW & 0x1), IOMUXC_SW_PAD_CTL_PAD_JTAG_TRSTB);
}
