#ifndef  _ART_UTF_COMMON_H_
#define  _ART_UTF_COMMON_H_

#ifdef __cplusplus
extern "C" {
#endif //__cplusplus 

#ifdef __GNUC__
#define __ATTRIB_PACK           __attribute__ ((packed))
#define __ATTRIB_PRINTF         __attribute__ ((format (printf, 1, 2)))
#define __ATTRIB_NORETURN       __attribute__ ((noreturn))
#define __ATTRIB_ALIGN(x)       __attribute__ ((aligned((x))))
#ifndef INLINE
#define INLINE                  __inline__
#endif
#else /* Not GCC */

#define __ATTRIB_PRINTF
#define __ATTRIB_NORETURN
#define __ATTRIB_ALIGN(x)
#ifndef INLINE
#define INLINE                  __inline
#endif
#endif /* End __GNUC__ */

#ifndef ATHR_WIN_DEF /* Not GCC */
#ifndef __GNUC__
#define __ATTRIB_PACK
#endif
#endif

#ifndef ATHR_WIN_DEF /* Not GCC */
#define PREPACK
#define POSTPACK				__ATTRIB_PACK
#endif

#define MPATTERN                (10*4)
#define ATH_MAC_LEN             6
#define RATE_MASK_ROW_MAX       2
#define RATE_MASK_BIT_MAX       32

#if defined (AR6002_REV2)
#define TCMD_MAX_RATES 12
#elif defined (AR6002_REV4)
#define TCMD_MAX_RATES 31    //alyang add the CCK short preamble rates at the end. Keep same as McKinley.
#else
#define TCMD_MAX_RATES 47
#endif

#ifndef ART_BUILD    //For TCMD, TX_DATA_PATTERN is defined here, but for ART, it is defined in manlib.h. Avoid redefining.
typedef enum {
    ZEROES_PATTERN = 0,
    ONES_PATTERN,
    REPEATING_10,
    PN7_PATTERN,
    PN9_PATTERN,
    PN15_PATTERN,
    USER_DEFINED_PATTERN
}TX_DATA_PATTERN;
#endif  /* ART_BUILD */

typedef enum {
    TCMD_CONT_TX_OFF = 0,
    TCMD_CONT_TX_SINE,
    TCMD_CONT_TX_FRAME,
    TCMD_CONT_TX_TX99,
    TCMD_CONT_TX_TX100,
    TCMD_CONT_TX_OFFSETTONE,
} TCMD_CONT_TX_MODE;

typedef enum {
    TCMD_WLAN_MODE_NOHT = 0,
    TCMD_WLAN_MODE_HT20 = 1,
    TCMD_WLAN_MODE_HT40PLUS = 2,
    TCMD_WLAN_MODE_HT40MINUS = 3,
    TCMD_WLAN_MODE_CCK = 4,

    TCMD_WLAN_MODE_MAX,
    TCMD_WLAN_MODE_INVALID = TCMD_WLAN_MODE_MAX,

} TCMD_WLAN_MODE;

typedef enum {
    TCMD_CONT_RX_PROMIS =0,
    TCMD_CONT_RX_FILTER,
    TCMD_CONT_RX_REPORT,
    TCMD_CONT_RX_SETMAC,
    TCMD_CONT_RX_SET_ANT_SWITCH_TABLE,
 
    TC_CMD_RESP,
    TCMD_CONT_RX_GETMAC,
} TCMD_CONT_RX_ACT;

//
// TX/RX status defines and structures
//
#define MSTREAM_UTF 3
#define MCHAIN_UTF 3

typedef struct txStats_utf 
{
	A_UINT32 totalPackets;
	A_UINT32 goodPackets;
	A_UINT32 underruns;
	A_UINT32 otherError;
	A_UINT32 excessiveRetries;
    A_UINT32 rateBit;
	//
	// retry histogram
	//
	A_INT32 shortRetry;
	A_INT32 longRetry;

	A_UINT32 startTime;
	A_UINT32 endTime;
	A_UINT32 byteCount;
	A_UINT32 dontCount;
	//
	// rssi histogram for good packets
	//
	A_INT32 rssi;
	A_INT32 rssic[MCHAIN_UTF];
	A_INT32 rssie[MCHAIN_UTF];

	A_UINT32 thermCal;/* thermal value for calibration */
} __ATTRIB_PACK TX_STATS_STRUCT_UTF;


typedef struct rxStats_uft 
{
	A_UINT32 totalPackets;
	A_UINT32 goodPackets;
	A_UINT32 otherError;
	A_UINT32 crcPackets;
    A_UINT32 decrypErrors;
    A_UINT32 rateBit;

	// Added for RX tput calculation
	A_UINT32 startTime;
	A_UINT32 endTime;
	A_UINT32 byteCount;
	A_UINT32 dontCount;
	//
	// rssi histogram for good packets
	//
	A_INT32 rssi;
	A_INT32 rssic[MCHAIN_UTF];
	A_INT32 rssie[MCHAIN_UTF];
	//
	// evm histogram for good packets
	//
	A_INT32 evm[MSTREAM_UTF];
	//
	// rssi histogram for bad packets
	//
	A_INT32 badrssi;
	A_INT32 badrssic[MCHAIN_UTF];
	A_INT32 badrssie[MCHAIN_UTF];
	//
	// evm histogram for bad packets
	//
	A_INT32 badevm[MSTREAM_UTF];

} __ATTRIB_PACK RX_STATS_STRUCT_UTF;
	
#if defined(AR6002_REV6)
#include "art_utf_ar6004.h"
#elif defined(AR6002_REV7)
#include "art_utf_ar6006.h"
#endif

#ifdef __cplusplus
}
#endif

#endif //_ART_UTF_TX_COMMON_H_



