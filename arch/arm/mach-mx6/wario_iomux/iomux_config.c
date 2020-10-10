/*
 * Copyright (C) 2012, Freescale Semiconductor, Inc. All Rights Reserved.
 * THIS SOURCE CODE IS CONFIDENTIAL AND PROPRIETARY AND MAY NOT
 * BE USED OR DISTRIBUTED WITHOUT THE WRITTEN PERMISSION OF
 * Freescale Semiconductor, Inc.
*/

// File: iomux_config.c

#include "include/iomux_config.h"

// Function to configure iomux for i.MX6SL BGA board Wario rev. 1.
void iomux_config(void)
{
    audmux_iomux_config();
    csi_iomux_config();
    ecspi1_iomux_config();
    epdc_iomux_config();
    gpio1_iomux_config();
    gpio2_iomux_config();
    gpio3_iomux_config();
    gpio4_iomux_config();
    gpio5_iomux_config();
    i2c1_iomux_config();
    i2c2_iomux_config();
    pwm1_iomux_config();
    sjc_iomux_config();

    uart1_iomux_config();
    uart2_iomux_config();
    uart3_iomux_config();
    uart4_iomux_config();
    usdhc1_iomux_config();
    usdhc2_iomux_config();
    usdhc3_iomux_config(); 
    wdog1_iomux_config();
}

// Definitions for unused modules.
void ccm_iomux_config()
{
};

void cortex_a9_iomux_config()
{
};

void ecspi2_iomux_config()
{
};

void ecspi3_iomux_config()
{
};

void ecspi4_iomux_config()
{
};

void epit1_iomux_config()
{
};

void epit2_iomux_config()
{
};

void fec_iomux_config()
{
};

void gpt_iomux_config()
{
};

void i2c3_iomux_config()
{
};

void kpp_iomux_config()
{
};

void lcdif_iomux_config()
{
};

void pwm2_iomux_config()
{
};

void pwm3_iomux_config()
{
};

void pwm4_iomux_config()
{
};

void sdma_iomux_config()
{
};

void snvs_iomux_config()
{
};

void spdc_iomux_config()
{
};

void spdif_iomux_config()
{
};

void src_iomux_config()
{
};

void uart5_iomux_config()
{
};

void usb_iomux_config()
{
};

void usdhc4_iomux_config()
{
};

void wdog2_iomux_config()
{
};

void weim_iomux_config()
{
};

void xtalosc_iomux_config()
{
};
