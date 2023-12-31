#include "GPURealTimeBC6H-c.h"
#include "GPURealTimeBC6H.h"

namespace
{
  GPURealTimeBC6H gCompressor;
}

bool GPURealTimeBC6H_Initialize(uint32_t preset)
{
  return gCompressor.Init(static_cast<GPURealTimeBC6H::Preset>(preset));
}

bool GPURealTimeBC6H_Compress(GPURealTimeBC6H_Image* srcImage, uint32_t format, GPURealTimeBC6H_Image* dstImage)
{
  SImage srcImageCpp, dstImageCpp;
  srcImageCpp.m_format = static_cast<SImage::ImageFormat>(format);
  srcImageCpp.m_width = srcImage->width;
  srcImageCpp.m_height = srcImage->height;
  srcImageCpp.m_data = srcImage->data;
  srcImageCpp.m_dataSize = srcImage->dataSize;

  dstImageCpp.m_format = SImage::ImageFormat::BC6H;
 
  /// TODO: add format to Compress parameters
  bool result = gCompressor.Compress(&srcImageCpp, &dstImageCpp);
  if (result)
  {
    dstImage->width = srcImageCpp.m_width;
    dstImage->height = srcImageCpp.m_height;
    dstImage->data = dstImageCpp.m_data;
    dstImage->dataSize = srcImageCpp.m_dataSize;
  }

  return result;
}

void GPURealTimeBC6H_FreeImage(GPURealTimeBC6H_Image* dstImage)
{
  SImage dstImageCpp;
  dstImageCpp.m_data = dstImage->data;
  gCompressor.FreeImage(&dstImageCpp);
}

void GPURealTimeBC6H_Release()
{
  gCompressor.Release();
}