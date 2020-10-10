#ifndef __BT_PWR_CTRL_H__
#define __BT_PWR_CTRL_H__

struct bt_pwr_data {
	unsigned bt_rst;	
	unsigned bt_host_wake;
	unsigned bt_dev_wake;
	/* HCI platform structures -- UART in our case */
	struct platform_device *uart_pdev;
};

#endif /* __BT_PWR_CTRL_H__ */
