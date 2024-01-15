#ifndef _RESOURCES_H_
#define _RESOURCES_H_

extern const uint8_t ___res_FL_MODE1_wav_start[] asm("_binary_FL_earpods_final_wav_start");
extern const uint8_t ___res_FL_MODE1_wav_end[] asm("_binary_FL_earpods_final_wav_end");
extern const uint8_t ___res_FR_MODE1_wav_start[] asm("_binary_FR_earpods_final_wav_start");
extern const uint8_t ___res_FR_MODE1_wav_end[] asm("_binary_FR_earpods_final_wav_end");

extern const uint8_t ___res_FL_MODE2_wav_start[] asm("_binary_FL_hd681_final_wav_start");
extern const uint8_t ___res_FL_MODE2_wav_end[] asm("_binary_FL_hd681_final_wav_end");
extern const uint8_t ___res_FR_MODE2_wav_start[] asm("_binary_FR_hd681_final_wav_start");
extern const uint8_t ___res_FR_MODE2_wav_end[] asm("_binary_FR_hd681_final_wav_end");

#define GET_RES_SIZE(res) (res##_end - res##_start)


#endif