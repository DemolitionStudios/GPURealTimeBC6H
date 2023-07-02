#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdlib.h>
#include <malloc.h>
#include <memory.h>
#include <tchar.h>
#include <vector>
#include <string>
#include <stdint.h>

#include <d3d11.h>
#include <d3dcompiler.h>

// Note: that is incomplete, needs some work
#define HAVE_QUALITY_MEASUREMENT 0

struct Vec2
{
  Vec2()
    : x(0.f)
    , y(0.f)
  {
  }

  Vec2(float x_, float y_)
    : x(x_)
    , y(y_)
  {
  }

  float x;
  float y;
};

struct Vec3
{
  float x;
  float y;
  float z;
};

struct SImage
{
  enum struct ImageFormat
  {
    RGBA32F,
    BC6H,
  };

  ImageFormat m_format;
  unsigned m_width;
  unsigned m_height;
  uint8_t* m_data;
  unsigned m_dataSize;
};

uint32_t const MAX_QUERY_FRAME_NUM = 5;
uint32_t const BLIT_MODE_NUM = 4;





class GPURealTimeBC6H
{
public:
  GPURealTimeBC6H();
  ~GPURealTimeBC6H();

  enum struct Preset
  {
    Quality,
    Speed,
  };

  bool Init(Preset preset);
  void Release();
  bool Compress(const SImage* srcImage, SImage* dstImage);
  void FreeImage(SImage* dstImage);

  ID3D11Device* GetDevice() { return m_device; }
  ID3D11DeviceContext* GetCtx() { return m_ctx; }

private:
  ID3D11Device* m_device = nullptr;
  ID3D11DeviceContext* m_ctx = nullptr;
  ID3D11RenderTargetView* m_backBufferView = nullptr;
  ID3D11SamplerState* m_pointSampler = nullptr;
  ID3D11Buffer* m_constantBuffer = nullptr;

  ID3D11Query* m_disjointQueries[MAX_QUERY_FRAME_NUM];
  ID3D11Query* m_timeBeginQueries[MAX_QUERY_FRAME_NUM];
  ID3D11Query* m_timeEndQueries[MAX_QUERY_FRAME_NUM];
  float m_timeAcc = 0.0f;
  unsigned m_timeAccSampleNum = 0;
  float m_compressionTime = 0.0f;

  // Shaders
  ID3D11VertexShader* m_blitVS = nullptr;
  ID3D11PixelShader* m_blitPS = nullptr;
  ID3D11ComputeShader* m_compressCS = nullptr;

  // Resources
  ID3D11Buffer* m_ib = nullptr;
  ID3D11Texture2D* m_sourceTextureRes = nullptr;
  ID3D11ShaderResourceView* m_sourceTextureView = nullptr;
  ID3D11Texture2D* m_compressedTextureRes = nullptr;
  ID3D11ShaderResourceView* m_compressedTextureView = nullptr;
  ID3D11Texture2D* m_compressTargetRes = nullptr;
  ID3D11UnorderedAccessView* m_compressTargetUAV = nullptr;
#if HAVE_QUALITY_MEASUREMENT
  ID3D11Texture2D* m_tmpTargetRes = nullptr;
  ID3D11RenderTargetView* m_tmpTargetView = nullptr;
#endif
	ID3D11Texture2D* m_tmpStagingRes = nullptr;

  HWND m_windowHandle = 0;
  Vec2 m_texelBias = Vec2(0.0f, 0.0f);
  float m_texelScale = 1.0f;
  float m_imageZoom = 0.0f;
  float m_imageExposure = 0.0f;
  bool m_dragEnabled = false;
  Vec2 m_dragStart = Vec2(0.0f, 0.0f);
  bool m_updateRMSE = true;
  uint32_t m_imageWidth = 0;
  uint32_t m_imageHeight = 0;
  uint64_t m_frameID = 0;
  Preset m_preset;

  uint32_t m_blitMode = 1;

  // Compression error
  float m_rgbRMSLE = 0.0f;
  float m_lumRMSLE = 0.0f;

  bool CreateImage(const SImage* img);
  void DestroyImage();
  void CreateShaders();
  void DestroyShaders();
  void CreateTargets();
  void DestroyTargets();
  void CreateQueries();
  void CreateConstantBuffer();
  void UpdateRMSE();
  void CopyTexture(Vec3* image, ID3D11ShaderResourceView* srcView);
};