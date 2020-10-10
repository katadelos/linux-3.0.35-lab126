#ifndef __LINUX_MXCFB_EINK_H__
#define __LINUX_MXCFB_EINK_H__

#include <linux/mxcfb.h>

typedef unsigned int var_t;
typedef unsigned short wb_t;
typedef unsigned char upd_t;

typedef void (reagl_function_t)(
		wb_t *working_buffer_ptr_in,
		wb_t *working_buffer_ptr_out,
		struct mxcfb_rect *update_region,
		long working_buffer_width,
		long working_buffer_height);

int reagl_init(long working_buffer_width);

void reagl_free(void);

reagl_function_t do_reagl_processing_v_2_lab126;
reagl_function_t do_reagl_processing_v_4_lab126;
reagl_function_t do_reagl_processing_v2_2_1;
reagl_function_t do_reagld_processing_v2_0_1;

#endif // __LINUX_MXCFB_EINK_H__

