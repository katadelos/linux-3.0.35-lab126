/*
 * drv26xx.h
 * Driver for Texas Instruments DRV26xx Haptic Contollers
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef _LINUX_DRV26XX_I2C_H
#define _LINUX_DRV26XX_I2C_H

#define DRV26XX_NAME	    "drv26xx"
#define DRV26XX_PRINTK_NAME "[DRV26XX]"
#define DRV26XX_MAX_STR_LEN (200)

static int drv26xx_debug = 0;

#define printk_drv26xx_err(f...)   \
    printk(KERN_ERR DRV26XX_PRINTK_NAME f)

#define DRV26XX_VERSION_MAJOR      (1)
#define DRV26XX_VERSION_MINOR      (2)

//max waveforms = 256 / 5bytes = 51
#define DRV26XX_MAX_NUM_WAVEFORMS  (51)

#define DRV26XX_MAX_LINE_CHARS_CFG (50)
#define DRV26XX_PAGE_SIZE          (256)

#define DRV26XX_I2C_ADDRESS        (0x59)

#define DRV26XX_CONTROL_PAGE       (0x00)

#define DRV26XX_ID_ADDRESS         (0x01)
#define DRV26XX_GAIN_ADDRESS       (0x01)
#define DRV26XX_RESET_ADDRESS      (0x02)
#define DRV26XX_STANDBY_ADDRESS    (0x02)
#define DRV26XX_GO_ADDRESS         (0x02)
#define DRV26XX_SEQUENCER_ADDRESS  (0x03)
#define DRV26XX_FIFO_FULL_ADDRESS  (0x00)
#define DRV26XX_FIFO_ADDRESS       (0x0B)
#define DRV26XX_PAGE_ADDRESS       (0xFF)

#define DRV26XX_RESET_CMD          (0x80)
#define DRV26XX_STANDBY_CMD        (0x40)
#define DRV26XX_EXIT_STANDBY_CMD   (0x00)
#define DRV26XX_GO_CMD             (0x01)

#define DRV26XX_ID                 (0x07)
#define DRV26XX_ID_MASK            (0x78)
#define DRV26XX_ID_SHIFT           (3)

#define DRV26XX_ALLOWED_R_BYTES	(25)
#define DRV26XX_ALLOWED_W_BYTES	(2)
#define DRV26XX_MAX_RW_RETRIES	(5)
#define DRV26XX_I2C_RETRY_DELAY (10)

#define DRV26XX_FIFO_FEED_SIZE (100)

#define DRV26XX_STANDBY		0x40
#define DRV26XX_RESET		0x80

#define DRV26XX_WAV_CHUNK_ID    (0x46464952) //"FFIR", or "RIFF" backwards
#define DRV26XX_WAV_FORMAT      (0x45564157) //"EVAW", or "WAVE" backward
#define DRV26XX_WAV_FORMAT_ID   (0x20746D66) //" TMF", or "FMT " backwards
#define DRV26XX_WAV_FORMAT_SIZE (16) // Should be 16 for PCM
#define DRV26XX_WAV_AUDIO_FMT   (1)  // PCM = 1 for linear quantization (no compression)
#define DRV26XX_WAV_NUM_CHAN    (1)  // Mono for now
#define DRV26XX_WAV_SAMP_RATE   (8000)  // 8 kilosamples per second
#define DRV26XX_WAV_BYTE_RATE   (8000)  // 8 kilobytes per second
#define DRV26XX_WAV_BLK_ALIGN   (1)  // == NumaChannels * BitsPerSample / 8
#define DRV26XX_WAV_BITS_SAMP   (8)  // 8 bits per sample
#define DRV26XX_WAV_DATA_ID     (0x61746164) //"ATAD", or "DATA" backwards

struct drv26xx_waveform_header{
    u32 chunk_id;
    u32 chunk_size;
    u32 format;
    u32 format_id;
    u32 format_size;
    u16 audio_format;
    u16 num_channels;
    u32 sample_rate;
    u32 byte_rate;
    u16 block_align;
    u16 bits_per_sample;
    u32 data_id;
    u32 data_size;
};

void printk_drv26xx_info(const char* format, ...)
{
    if(drv26xx_debug)
    {
	char printbuf[DRV26XX_MAX_STR_LEN];
	va_list argptr;
	va_start(argptr, format);
	vsnprintf(printbuf, DRV26XX_MAX_STR_LEN, format, argptr);
	va_end(argptr);

	printk(KERN_INFO DRV26XX_PRINTK_NAME "%s", printbuf);
    }
}

void play_waveform(void);
int envelope_time_lookup[16]={0,
                              32,
                              64,
                              96,
                              128,
                              160,
                              192,
                              224,
                              256,
                              512,
                              768,
                              1024,
                              1280,
                              1536,
                              1792,
                              2048};

#endif
