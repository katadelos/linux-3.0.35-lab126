#include <linux/err.h>
#include <linux/gpio.h>
#include <linux/kernel.h>
#include <linux/mutex.h>
#include <linux/delay.h>
#include <linux/proc_fs.h>
#include <linux/platform_device.h>
#include <linux/miscdevice.h>
#include <linux/sysfs.h>
#include <linux/sysdev.h>

typedef enum pmic_con_state_{
	MAIN_ALONE = 0x2,
	MAIN_SUB = 0x4,
	MAIN_USB = 0x8,
	MAIN_USB_SUB = (MAIN_SUB | MAIN_USB),//0xc
	MAIN_OTG = 0x20,
	MAIN_OTG_SUB = (MAIN_SUB | MAIN_OTG),//0x24
	UNKNOWN = 0x1
} pmic_con_state;

typedef enum pmic_con_evt_{
	USB_IN,
	USB_OUT,
	OTG_IN,
	OTG_OUT,
	SUB_IN,
	SUB_OUT,
	CHG_INA_IN,
	CHG_INA_OUT
} pmic_con_evt;

typedef enum elec_state_{
	HIGH,
	LOW,
	FALLING,
	RISING
} elec_state;

static pmic_con_state conn_state = UNKNOWN;
static struct mutex  state_lock;
static unsigned ext_chg_gpio, sda_gpio;

int debug = 0;
#define DBG(X...) if(debug)printk(X);

extern int max77696_uic_is_otg_connected(void);
extern bool soda_charger_docked(void);

/*stubs of notifiers, once implementated outside of this file,these to be removed*/
void pmic_soda_connects_notify_sub_in(void){};
void pmic_soda_connects_notify_sub_out(void){};
extern void pmic_soda_connects_notify_otg_in(void);
extern void pmic_soda_connects_notify_otg_out(void);
extern void pmic_soda_connects_notify_usb_in(void);
extern void pmic_soda_connects_notify_usb_out(void);

static void process_evt( pmic_con_evt evt);

/* Event generators */

void pmic_soda_connects_usbchg_handler(void){
	//called from soda.c soda_usbchg_event_handler() for chg_det_l gpio
	volatile uint val1, val2;
	elec_state trigger;

	DBG(KERN_INFO"%s\n",__FUNCTION__);

	if(conn_state == UNKNOWN){
		DBG(KERN_INFO"Error: %s:%d\n", __FUNCTION__,__LINE__);
		return;
	}

	val1 = gpio_get_value(ext_chg_gpio);
	msleep(100);
	val2 = gpio_get_value(ext_chg_gpio);

	trigger = (val2 > val1 || val2 > 0 ? RISING : FALLING);


	if (trigger == RISING)
		process_evt(USB_OUT);
	else
		process_evt(USB_IN);
}

static elec_state chg_det_l(void){
	volatile uint val1, val2;
	elec_state trigger;

	val1 = gpio_get_value(ext_chg_gpio);
	msleep(100);
	val2 = gpio_get_value(ext_chg_gpio);

	trigger = (val2 > val1 || val2 > 0 ? RISING : FALLING);

	if (trigger == RISING)
		return HIGH;
	else
		return LOW;
}

void pmic_soda_connects_dock_handler(void){
	//call from soda_dock_event_handler threaded irq for i2c_ext_bat event
	volatile uint val1, val2;
	elec_state trigger;

	DBG(KERN_INFO"%s\n",__FUNCTION__);

	if(conn_state == UNKNOWN){
		printk(KERN_ERR"Error: %s:%d\n", __FUNCTION__,__LINE__);
		return;
	}

	val1 = gpio_get_value(sda_gpio);
	msleep(100);
	val2 = gpio_get_value(sda_gpio);
	  
	trigger = ((val1 > val2 || val2==0) ? FALLING : RISING);
	
	if (trigger == RISING)
		process_evt(SUB_OUT);
	else
		process_evt(SUB_IN);
}

void pmic_soda_connects_pwr_chgina_handler(bool in){
	//To be called from max77696_charger_chgina_work calls
	//not hooked for soda architecture
	elec_state trigger = (in ? RISING : FALLING);

	DBG(KERN_INFO"%s\n",__FUNCTION__);

	if(conn_state == UNKNOWN){
		printk(KERN_ERR"Error: %s:%d\n", __FUNCTION__,__LINE__);
		return;
	}

	if(trigger == FALLING)
		process_evt(CHG_INA_OUT);
	else
		process_evt(CHG_INA_IN);

} 

void pmic_soda_connects_otg_in_out_handler(u8 in){
//called from max77696-uic.c:max77696_uic_adc_work:in_otg = 1 /0 

	DBG(KERN_INFO"%s\n",__FUNCTION__);

	if(conn_state == UNKNOWN){
		printk(KERN_ERR"Error: %s:%d\n", __FUNCTION__,__LINE__);
		return;
	}

	if(conn_state == MAIN_ALONE ||  conn_state == MAIN_SUB){
		if(in)
			process_evt(OTG_IN);
	}
	else if(conn_state == MAIN_OTG || conn_state == MAIN_OTG_SUB){
		if(!in)
				process_evt(OTG_OUT);
	}
}

/* End Event generators */

/* State generators */

static pmic_con_state process_evt_main_alone( pmic_con_evt evt){
	switch(evt){
	case USB_IN:
		pmic_soda_connects_notify_usb_in();
		return MAIN_USB;
	case SUB_IN:
		pmic_soda_connects_notify_sub_in();
		return MAIN_SUB;
	case OTG_IN:
		pmic_soda_connects_notify_otg_in();
		return MAIN_OTG;
	default:
		return MAIN_ALONE;
	}
}

static pmic_con_state process_evt_main_sub(pmic_con_evt evt){
	switch(evt){
	case USB_IN:
		pmic_soda_connects_notify_usb_in();
		return MAIN_USB_SUB;
	case OTG_IN:
		pmic_soda_connects_notify_otg_in();
		return MAIN_OTG_SUB;
	case SUB_OUT:
		pmic_soda_connects_notify_sub_out();
		return MAIN_ALONE;
	default:
		return MAIN_SUB;
	}
}

static pmic_con_state process_evt_main_usb(pmic_con_evt evt){
	switch(evt){
	case SUB_IN:
		pmic_soda_connects_notify_sub_in();
		return MAIN_USB_SUB;
	case USB_OUT:
		pmic_soda_connects_notify_usb_out();
		return MAIN_ALONE;
	default:
		return MAIN_USB;
	}
}

static pmic_con_state process_evt_main_usb_sub(pmic_con_evt evt){
	switch(evt){
	case SUB_OUT:
		pmic_soda_connects_notify_sub_out();
		return MAIN_USB;
	case USB_OUT:
		pmic_soda_connects_notify_usb_out();
		return MAIN_SUB;
	default:
		return MAIN_USB_SUB;
	}
}

static pmic_con_state process_evt_main_otg(pmic_con_evt evt){
	switch(evt){
	case OTG_OUT:
		pmic_soda_connects_notify_otg_out();
		return MAIN_ALONE;
	case SUB_IN:
		pmic_soda_connects_notify_otg_in();
		pmic_soda_connects_notify_sub_in();
		return MAIN_OTG_SUB;
	default:
		return MAIN_OTG;
	}
}

static pmic_con_state process_evt_main_otg_sub(pmic_con_evt evt){
	switch(evt){
	case OTG_OUT:
		pmic_soda_connects_notify_otg_out();
		return MAIN_SUB;
	case SUB_OUT:
		pmic_soda_connects_notify_otg_out();
		pmic_soda_connects_notify_sub_out();
		return MAIN_OTG;
	default:
		return MAIN_OTG_SUB;
	}
}

static void process_evt( pmic_con_evt evt){
	mutex_lock(&state_lock);

	DBG(KERN_INFO"%s:%d old state:0x%x evt:%d\n", __FUNCTION__,__LINE__,conn_state, evt);

	switch(conn_state){
	case UNKNOWN:
		//att init set state
		break;

	case MAIN_ALONE:
		conn_state = process_evt_main_alone(evt);
		break;

	case MAIN_SUB:
		conn_state = process_evt_main_sub(evt);
		break;

	case MAIN_USB:
		conn_state = process_evt_main_usb(evt);
		break;

	case MAIN_USB_SUB:
		conn_state = process_evt_main_usb_sub(evt);
		break;

	case MAIN_OTG:
		conn_state = process_evt_main_otg(evt);
		break;

	case MAIN_OTG_SUB:
		conn_state = process_evt_main_otg_sub(evt);
		break;

	default:
		printk(KERN_ERR"%s:%d new state:0x%x NONEXISTANTR evt:%d\n", 
			   __FUNCTION__,__LINE__,conn_state, evt);
	}

	DBG(KERN_INFO"%s:%d new state:0x%x evt:%d\n", __FUNCTION__,__LINE__,conn_state, evt);

	mutex_unlock(&state_lock);
}

u8 pmic_soda_conects_usb_in(void){
	return (conn_state & MAIN_USB);
}

static ssize_t debug_en (struct device *dev, struct device_attribute *devattr, const char *buf, size_t count)
{
        debug = debug ? 0 : 1;
        return (ssize_t)count;
}

static const struct file_operations proc_fops = {
       .open           = &debug_en
	   
};

void pmic_soda_connects_init(unsigned ext_chg_gpio_, unsigned sda_gpio_){
	//called from soda probe once soda's state inited
	mutex_init(&state_lock);

	mutex_lock(&state_lock);

	if(!proc_create("pmic_soda_connects", S_IWUSR | S_IRUGO, NULL,
					&proc_fops)){
		printk(KERN_ERR"%s:%d Faled to create proc entry\n",__FUNCTION__,__LINE__);
		mutex_unlock(&state_lock);
		return;
	}

	ext_chg_gpio = ext_chg_gpio_;
	sda_gpio = sda_gpio_;

	conn_state = 0x0;

	if(max77696_uic_is_otg_connected()){
		conn_state |= MAIN_OTG;
	}
	else
		if(chg_det_l()==LOW){
			conn_state |= MAIN_USB;
			pmic_soda_connects_notify_usb_in();
		}

	if(soda_charger_docked()){
		conn_state |= MAIN_SUB;
	}
	

	if(conn_state == 0x0)
		conn_state = MAIN_ALONE;

	DBG(KERN_INFO"%s:%d state:0x%x\n", __FUNCTION__,__LINE__,conn_state);
	
	mutex_unlock(&state_lock);	
} 
