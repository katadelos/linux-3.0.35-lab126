#------------------------------------------------------------------------------
# <copyright file="makefile" company="Atheros">
#    Copyright (c) 2005-2010 Atheros Corporation.  All rights reserved.
#
# $ATH_LICENSE_HOSTSDK0_C$
#------------------------------------------------------------------------------
#==============================================================================
# Author(s): ="Atheros"
#==============================================================================

ccflags-y += -I$(obj)/include
ccflags-y += -I$(obj)/wlan/include
ccflags-y += -I$(obj)/os/linux/include
ccflags-y += -I$(obj)/os
ccflags-y += -I$(obj)/bmi/include
ccflags-y += -I$(obj)/include/AR6002

ifeq ($(CONFIG_AR600x_SD31_XXX),y)
ccflags-y += -DAR600x_SD31_XXX
endif

ifeq ($(CONFIG_AR600x_WB31_XXX),y)
ccflags-y += -DAR600x_WB31_XXX
endif

ifeq ($(CONFIG_AR600x_SD32_XXX),y)
ccflags-y += -DAR600x_SD32_XXX
endif

ifeq ($(CONFIG_AR600x_CUSTOM_XXX),y)
ccflags-y += -DAR600x_CUSTOM_XXX
endif

ifeq ($(CONFIG_AR6003_ENABLE_COEXISTENCE),y)
ccflags-y += -DENABLE_COEXISTENCE
endif

ifeq ($(CONFIG_AR600x_DUAL_ANTENNA),y)
ccflags-y += -DAR600x_DUAL_ANTENNA
endif

ifeq ($(CONFIG_AR600x_SINGLE_ANTENNA),y)
ccflags-y += -DAR600x_SINGLE_ANTENNA
endif

ifeq ($(CONFIG_AR600x_BT_QCOM),y)
ccflags-y += -DAR600x_BT_QCOM
endif

ifeq ($(CONFIG_AR600x_BT_CSR),y)
ccflags-y += -DAR600x_BT_CSR
endif

ifeq ($(CONFIG_AR600x_BT_AR3001),y)
ccflags-y += -DAR600x_BT_AR3001
endif

ifeq ($(CONFIG_AR6003_HCI_BRIDGE),y)
ccflags-y += -DATH_AR6K_ENABLE_GMBOX
ccflags-y += -DHCI_TRANSPORT_SDIO
ccflags-y += -DSETUPHCI_ENABLED
ccflags-y += -DSETUPBTDEV_ENABLED
ar6003-y += htc2/AR6000/ar6k_gmbox.o
ar6003-y += htc2/AR6000/ar6k_gmbox_hciuart.o
ar6003-y += miscdrv/ar3kconfig.o
ar6003-y += miscdrv/ar3kps/ar3kpsconfig.o
ar6003-y += miscdrv/ar3kps/ar3kpsparser.o
endif

ifeq ($(CONFIG_AR6003_CONFIG_GPIO_BT_RESET),y)
ccflags-y += -DAR6003_CONFIG_GPIO_BT_RESET
endif

ifeq ($(CONFIG_AR6003_CFG80211),y)
ccflags-y += -DATH6K_CONFIG_CFG80211
ar6003-y += os/linux/cfg80211.o
endif

ifeq ($(CONFIG_AR6003_HTC_RAW_INTERFACE),y)
ccflags-y += -DHTC_RAW_INTERFACE
endif

ifeq ($(CONFIG_AR6003_ENABLE_HOST_DEBUG),y)
ccflags-y += -DDEBUG
endif

ifeq ($(ATH_DEBUG_DRIVER),yes)
ccflags-y += -DDEBUG -DATH_DEBUG_MODULE
endif


ifeq ($(CONFIG_AR6003_ENABLE_TARGET_DEBUG_PRINTS),y)
ccflags-y += -DENABLEUARTPRINT_SET
endif

ifeq ($(CONFIG_AR6003_DISABLE_TARGET_DBGLOGS),y)
ccflags-y += -DAR6003_DISABLE_TARGET_DBGLOGS
endif

ifeq ($(CONFIG_AR6003_VIRTUAL_SCATTER_GATHER),y)
ccflags-y += -DATH6K_CONFIG_HIF_VIRTUAL_SCATTER
endif


ccflags-y += -DLINUX -DKERNEL_2_6
ccflags-y += -DTCMD
ccflags-y += -DSEND_EVENT_TO_APP
ccflags-y += -DUSER_KEYS
ccflags-y += -DNO_SYNC_FLUSH
ccflags-y += -DHTC_EP_STAT_PROFILING
ccflags-y += -DATH_AR6K_11N_SUPPORT
ccflags-y += -DWAPI_ENABLE
ccflags-y += -DATH_SUPPORT_DFS
ccflags-y += -DCHECKSUM_OFFLOAD
ccflags-y += -DWLAN_HEADERS
ccflags-y += -DINIT_MODE_DRV_ENABLED
ccflags-y += -DSOFTMAC_FILE_USED
ccflags-y += -DBMIENABLE_SET
### TONY ccflags-y += -DLAB126
ccflags-y += -DAR6002_HEADERS_DEF 
ccflags-y += -DAR6003_HEADERS_DEF 
ccflags-y += -DMCKINLEY_HEADERS_DEF 
ccflags-y += -DAR6003_CONFIG_HIF_VIRTUAL_SCATTER
ccflags-y += -DBLOCK_TX_PATH_FLAG 

obj-$(CONFIG_AR6003) := ar6003.o
ar6003-y += htc2/AR6000/ar6k.o
ar6003-y += htc2/AR6000/ar6k_events.o
ar6003-y += htc2/htc_send.o
ar6003-y += htc2/htc_recv.o
ar6003-y += htc2/htc_services.o
ar6003-y += htc2/htc.o
ar6003-y += bmi/src/bmi.o
ar6003-y += os/linux/ar6000_drv.o
ar6003-y += os/linux/ar6000_pm.o
ar6003-y += os/linux/ar6000_raw_if.o
ar6003-y += os/linux/netbuf.o
ar6003-y += os/linux/wireless_ext.o
ar6003-y += os/linux/ioctl.o
ar6003-y += os/linux/ar6k_pal.o
ar6003-y += os/linux/hci_bridge.o
ar6003-y += miscdrv/common_drv.o
ar6003-y += miscdrv/credit_dist.o
ar6003-y += wmi/wmi.o
ar6003-y += reorder/rcv_aggr.o
ar6003-y += wlan/src/wlan_node.o
ar6003-y += wlan/src/wlan_recv_beacon.o
ar6003-y += wlan/src/wlan_utils.o
ar6003-y += regtable/regtable.o 
ar6003-y += regtable/AR6003def.o 
ar6003-y += regtable/AR6002def.o 
ar6003-y += regtable/MCKINLEYdef.o


ATH_HIF_TYPE := sdio
ccflags-y += -I$(obj)/hif/sdio/linux_sdio/include
ccflags-y += -DSDIO
ar6003-y += hif/sdio/linux_sdio/src/hif.o
ar6003-y += hif/sdio/linux_sdio/src/hif_scatter.o

ifeq ($(ATH_HIF_TYPE),sdio)
ar6003-y += hif/common/hif_bmi_reg_access.o \
		 hif/common/hif_diag_reg_access.o
endif

# DFS modules
ar6003-y += dfs/dfs_ar.o 
ar6003-y += dfs/dfs_bindetects.o 
ar6003-y += dfs/dfs.o 
ar6003-y += dfs/dfs_debug.o 
ar6003-y += dfs/dfs_fcc_bin5.o
ar6003-y += dfs/dfs_init.o 
ar6003-y += dfs/dfs_misc.o
ar6003-y += dfs/dfs_process_phyerr.o
ar6003-y += dfs/dfs_process_radarevent.o
ar6003-y += dfs/dfs_staggered.o
ar6003-y += dfs/dfs_host_project.o

ATH_BUS_SUBTYPE := linux_sdio
export ATH_BUS_SUBTYPE
