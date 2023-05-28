#ifndef UTIL_GPUREALTIMEBC6H_C_H_
#define UTIL_GPUREALTIMEBC6H_C_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

typedef enum 
{
  GPURealTimeBC6H_ImageFormat_RGBA,
  GPURealTimeBC6H_ImageFormat_BC6H,
} GPURealTimeBC6H_ImageFormat;

typedef enum
{
  GPURealTimeBC6H_Preset_Quality,
  GPURealTimeBC6H_Preset_Speed,
} GPURealTimeBC6H_Preset;

typedef struct 
{
  unsigned width;
  unsigned height;
  uint8_t* data;
  unsigned dataSize;
} GPURealTimeBC6H_Image;

bool GPURealTimeBC6H_Initialize(uint32_t preset);
bool GPURealTimeBC6H_Compress(GPURealTimeBC6H_Image* srcImage, uint32_t format, GPURealTimeBC6H_Image* dstImage);
void GPURealTimeBC6H_FreeImage(GPURealTimeBC6H_Image* dstImage);
void GPURealTimeBC6H_Release();


#ifdef __cplusplus
}  // extern "C"
#endif

#endif // UTIL_GPUREALTIMEBC6H_C_H_