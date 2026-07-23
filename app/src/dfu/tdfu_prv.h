#ifndef __MY_DFU_PRIVATE_H__
#define __MY_DFU_PRIVATE_H__

#include "app_version.h"

#define DUAL_IMAGE_HDR 0
#define SC_FILE_FORMAT_KFW 1
#define MY_FWU_STRING_NAME CONFIG_KEYCHRON_FWU_STRING_NAME
_Static_assert(sizeof(CONFIG_KEYCHRON_FWU_STRING_NAME) > 1,
               "Set CONFIG_KEYCHRON_FWU_STRING_NAME in shield .conf file");
/* "-o" marks this OpenRGB fork (stock Keychron firmware reports "-r"). Kept to
 * 2 chars so the version still fits the 10-byte fw_version field with room to
 * spare (up to e.g. 99.99.99-o). */
#define MY_FW_VERSION APP_VERSION_STRING "-o"
#define MY_HW_VERSION "v1.0.0"
unsigned char *my_dfu_get_prv_data(void);
unsigned char my_dfu_get_prv_len(void);

#if (DUAL_IMAGE_HDR)
unsigned char *my_dfu_get_prv_data_fixes(void);
unsigned char my_dfu_get_prv_len_fixes(void);
#endif

#endif
