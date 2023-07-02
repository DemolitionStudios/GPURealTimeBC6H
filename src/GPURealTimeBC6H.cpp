#include "GPURealTimeBC6H.h"
#include <iostream>

namespace Shaders
{
  #include "shaders/compress_quality.inc"
  #include "shaders/compress_speed.inc"
}

#define SAFE_RELEASE(x) { if (x) { safeRelease(reinterpret_cast<void**>(&x), #x); } }

namespace 
{
  const uint32_t BC_BLOCK_SIZE = 4;

  struct SShaderCB
  {
    unsigned m_textureSizeInBlocks[2];

    Vec2 m_imageSizeRcp;
    Vec2 m_texelBias;

    float m_texelScale;
    float m_exposure;
    uint32_t m_blitMode;
    uint32_t m_padding;
  };

  // https://gist.github.com/rygorous/2144712
  static float HalfToFloat(uint16_t h)
  {
    union FP32
    {
      uint32_t    u;
      float       f;
      struct
      {
        unsigned Mantissa : 23;
        unsigned Exponent : 8;
        unsigned Sign : 1;
      };
    };

    static const FP32 magic = { (254 - 15) << 23 };
    static const FP32 was_infnan = { (127 + 16) << 23 };

    FP32 o;
    o.u = (h & 0x7fff) << 13;     // exponent/mantissa bits
    o.f *= magic.f;                 // exponent adjust
    if (o.f >= was_infnan.f)        // make sure Inf/NaN survive
      o.u |= 255 << 23;
    o.u |= (h & 0x8000) << 16;    // sign bit
    return o.f;
  }

  uint32_t DivideAndRoundUp(uint32_t x, uint32_t divisor)
  {
    return (x + divisor - 1) / divisor;
  }

  uint32_t roundUp(uint32_t numToRound, uint32_t multiple)
  {
    if (multiple == 0)
      return numToRound;

    uint32_t remainder = numToRound % multiple;
    if (remainder == 0)
      return numToRound;

    return numToRound + multiple - remainder;
  }

  struct BufferBC6H
  {
    UINT color[4];
  };

  void safeRelease(void** ptr, const char* name)
  {
    IUnknown* obj = reinterpret_cast<IUnknown*>(*ptr);
    if (obj)
    { 
      int refs = obj->Release();
      if (refs != 0)
        std::cerr << name << ": " << refs << " refs left";
      *ptr = nullptr; 
    }
  }
}

GPURealTimeBC6H::GPURealTimeBC6H()
{
}

GPURealTimeBC6H::~GPURealTimeBC6H()
{
	Release();
}

bool GPURealTimeBC6H::Init(Preset preset)
{
	D3D_FEATURE_LEVEL featureLevels[] = { D3D_FEATURE_LEVEL_11_0 };
	D3D_FEATURE_LEVEL retFeatureLevel;

	unsigned flags = 0;
#ifdef _DEBUG
	flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

	HRESULT res;
	res = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, 0, flags, featureLevels, ARRAYSIZE(featureLevels), D3D11_SDK_VERSION, &m_device, &retFeatureLevel, &m_ctx);
	_ASSERT(SUCCEEDED(res));

	// ID3D11Texture2D* backBuffer = NULL;
	// res = m_swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (LPVOID*)&backBuffer);
	// _ASSERT(SUCCEEDED(res));

	// res = m_device->CreateRenderTargetView(backBuffer, nullptr, &m_backBufferView);
	// _ASSERT(SUCCEEDED(res));
	// backBuffer->Release();

  m_preset = preset;

	CreateShaders();
	CreateTargets();
	CreateQueries();
	CreateConstantBuffer();

	HRESULT hr;
	D3D11_SAMPLER_DESC samplerDesc =
	{
		D3D11_FILTER_MIN_MAG_MIP_POINT,
		D3D11_TEXTURE_ADDRESS_BORDER,
		D3D11_TEXTURE_ADDRESS_BORDER,
		D3D11_TEXTURE_ADDRESS_BORDER,
		0.0f,
		1,
		D3D11_COMPARISON_ALWAYS,
		0.0f,
		0.0f,
		0.0f,
		0.0f,
		0.0f,
		D3D11_FLOAT32_MAX
	};
	hr = m_device->CreateSamplerState(&samplerDesc, &m_pointSampler);
	_ASSERT(SUCCEEDED(hr));

	D3D11_BUFFER_DESC bd;
	ZeroMemory(&bd, sizeof(bd));
	bd.Usage = D3D11_USAGE_DEFAULT;
	bd.ByteWidth = sizeof(uint16_t) * 4;
	bd.BindFlags = D3D11_BIND_INDEX_BUFFER;
	bd.CPUAccessFlags = 0;

	uint16_t indices[] = { 0, 1, 2, 3 };
	D3D11_SUBRESOURCE_DATA initData;
	ZeroMemory(&initData, sizeof(initData));
	initData.pSysMem = indices;

	hr = m_device->CreateBuffer(&bd, &initData, &m_ib);
	_ASSERT(SUCCEEDED(hr));

	return true;
}

void GPURealTimeBC6H::CreateTargets()
{
	D3D11_TEXTURE2D_DESC texDesc;
	texDesc.Width = DivideAndRoundUp(m_imageWidth, BC_BLOCK_SIZE);
	texDesc.Height = DivideAndRoundUp(m_imageHeight, BC_BLOCK_SIZE);
	texDesc.MipLevels = 1;
	texDesc.ArraySize = 1;
	texDesc.Format = DXGI_FORMAT_R32G32B32A32_UINT;
	texDesc.SampleDesc.Count = 1;
	texDesc.SampleDesc.Quality = 0;
	texDesc.Usage = D3D11_USAGE_DEFAULT;
	texDesc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
	texDesc.CPUAccessFlags = 0;
	texDesc.MiscFlags = 0;
	HRESULT hr = m_device->CreateTexture2D(&texDesc, nullptr, &m_compressTargetRes);
	_ASSERT(SUCCEEDED(hr));

	hr = m_device->CreateUnorderedAccessView(m_compressTargetRes, nullptr, &m_compressTargetUAV);
	_ASSERT(SUCCEEDED(hr));

  /// TODO: don't need
	texDesc.Width = m_imageWidth;
	texDesc.Height = m_imageHeight;
	texDesc.MipLevels = 1;
	texDesc.ArraySize = 1;
	texDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
	texDesc.SampleDesc.Count = 1;
	texDesc.SampleDesc.Quality = 0;
	texDesc.Usage = D3D11_USAGE_DEFAULT;
	texDesc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
	texDesc.CPUAccessFlags = 0;
	texDesc.MiscFlags = 0;
	hr = m_device->CreateTexture2D(&texDesc, nullptr, &m_tmpTargetRes);
	_ASSERT(SUCCEEDED(hr));

	hr = m_device->CreateRenderTargetView(m_tmpTargetRes, nullptr, &m_tmpTargetView);
	_ASSERT(SUCCEEDED(hr));

	texDesc.Width = DivideAndRoundUp(m_imageWidth, BC_BLOCK_SIZE);
	texDesc.Height = DivideAndRoundUp(m_imageHeight, BC_BLOCK_SIZE);
	texDesc.MipLevels = 1;
	texDesc.ArraySize = 1;
	texDesc.Format = DXGI_FORMAT_R32G32B32A32_UINT;
	texDesc.SampleDesc.Count = 1;
	texDesc.SampleDesc.Quality = 0;
	texDesc.Usage = D3D11_USAGE_STAGING;
	texDesc.BindFlags = 0;
	texDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
	texDesc.MiscFlags = 0;
	hr = m_device->CreateTexture2D(&texDesc, nullptr, &m_tmpStagingRes);
	_ASSERT(SUCCEEDED(hr));
}

void GPURealTimeBC6H::DestroyTargets()
{
	SAFE_RELEASE(m_compressTargetUAV);
	SAFE_RELEASE(m_compressTargetRes);
	SAFE_RELEASE(m_tmpTargetView);
	SAFE_RELEASE(m_tmpTargetRes);
	SAFE_RELEASE(m_tmpStagingRes);
}

void GPURealTimeBC6H::CreateQueries()
{
	D3D11_QUERY_DESC queryDesc;
	queryDesc.MiscFlags = 0;

	for (unsigned i = 0; i < MAX_QUERY_FRAME_NUM; ++i)
	{
		queryDesc.Query = D3D11_QUERY_TIMESTAMP_DISJOINT;
		m_device->CreateQuery(&queryDesc, &m_disjointQueries[i]);

		queryDesc.Query = D3D11_QUERY_TIMESTAMP;
		m_device->CreateQuery(&queryDesc, &m_timeBeginQueries[i]);
		m_device->CreateQuery(&queryDesc, &m_timeEndQueries[i]);
	}
}

void GPURealTimeBC6H::CreateConstantBuffer()
{
  // For a constant buffer (BindFlags of D3D11_BUFFER_DESC set to D3D11_BIND_CONSTANT_BUFFER), 
  // you must set the ByteWidth value of D3D11_BUFFER_DESC in multiples of 16, and less than or equal 
  // to D3D11_REQ_CONSTANT_BUFFER_ELEMENT_COUNT

	D3D11_BUFFER_DESC desc;
	ZeroMemory(&desc, sizeof(desc));
	desc.Usage = D3D11_USAGE_DYNAMIC;
	desc.ByteWidth = roundUp(sizeof(SShaderCB), 16);
	desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

	m_device->CreateBuffer(&desc, nullptr, &m_constantBuffer);
}

bool GPURealTimeBC6H::CreateImage(const SImage* img)
{
  DXGI_FORMAT textureFormat;
  switch (img->m_format)
  {
  case SImage::ImageFormat::RGBA32F:
    textureFormat = DXGI_FORMAT_R32G32B32A32_FLOAT;
    break;
  default:
    return false;
  }

	D3D11_SUBRESOURCE_DATA initialData;
	initialData.pSysMem = img->m_data;
	initialData.SysMemPitch = img->m_width * 4 * 2;
	initialData.SysMemSlicePitch = 0;

	D3D11_TEXTURE2D_DESC desc;
	ZeroMemory(&desc, sizeof(desc));
	desc.Format = textureFormat;
	desc.Width = img->m_width;
	desc.Height = img->m_height;
	desc.MipLevels = 1;
	desc.ArraySize = 1;
	desc.Usage = D3D11_USAGE_IMMUTABLE;
	desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	desc.SampleDesc.Count = 1;
	desc.SampleDesc.Quality = 0;
	HRESULT hr = m_device->CreateTexture2D(&desc, &initialData, &m_sourceTextureRes);
	_ASSERT(SUCCEEDED(hr));

	D3D11_SHADER_RESOURCE_VIEW_DESC resViewDesc;
	resViewDesc.Format = desc.Format;
	resViewDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
	resViewDesc.Texture2D.MostDetailedMip = 0;
	resViewDesc.Texture2D.MipLevels = desc.MipLevels;
	hr = m_device->CreateShaderResourceView(m_sourceTextureRes, &resViewDesc, &m_sourceTextureView);
	_ASSERT(SUCCEEDED(hr));

  if (m_compressedTextureRes == nullptr || m_imageWidth != img->m_width || m_imageHeight != img->m_height)
  {
    m_imageWidth = img->m_width;
    m_imageHeight = img->m_height;

    desc.Format = DXGI_FORMAT_BC6H_UF16;
    desc.Usage = D3D11_USAGE_DEFAULT;
    hr = m_device->CreateTexture2D(&desc, nullptr, &m_compressedTextureRes);
    _ASSERT(SUCCEEDED(hr));

    resViewDesc.Format = desc.Format;
    resViewDesc.Texture2D.MostDetailedMip = 0;
    resViewDesc.Texture2D.MipLevels = desc.MipLevels;

    hr = m_device->CreateShaderResourceView(m_compressedTextureRes, &resViewDesc, &m_compressedTextureView);
    _ASSERT(SUCCEEDED(hr));
  }

  return true;
}

void GPURealTimeBC6H::DestoryImage()
{
	SAFE_RELEASE(m_compressedTextureView);
	SAFE_RELEASE(m_compressedTextureRes);
	SAFE_RELEASE(m_sourceTextureView);
	SAFE_RELEASE(m_sourceTextureRes);
}

void GPURealTimeBC6H::CreateShaders()
{
	unsigned shaderFlags = D3DCOMPILE_ENABLE_STRICTNESS;
#ifdef _DEBUG
	shaderFlags |= D3DCOMPILE_DEBUG | D3DCOMPILE_PREFER_FLOW_CONTROL;
#endif

  /// TODO: use new shader compiler
  if (m_preset == Preset::Quality)
  {
    m_device->CreateComputeShader(Shaders::Compress_Quality, sizeof(Shaders::Compress_Quality), nullptr, &m_compressCS);
  }
  else
  {
    m_device->CreateComputeShader(Shaders::Compress_Speed, sizeof(Shaders::Compress_Speed), nullptr, &m_compressCS);
  }
}

void GPURealTimeBC6H::DestroyShaders()
{
	SAFE_RELEASE(m_blitVS);
	SAFE_RELEASE(m_blitPS);
  SAFE_RELEASE(m_compressCS);
}

void GPURealTimeBC6H::Release()
{
	DestroyTargets();
	DestroyShaders();
	SAFE_RELEASE(m_ctx);
	SAFE_RELEASE(m_device);
}

bool GPURealTimeBC6H::Compress(const SImage* srcImage, SImage* dstImage)
{
  if (!CreateImage(srcImage))
    return false;

	m_ctx->ClearState();

	m_ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
	m_ctx->IASetIndexBuffer(m_ib, DXGI_FORMAT_R16_UINT, 0);

	SShaderCB shaderCB;
	shaderCB.m_textureSizeInBlocks[0] = DivideAndRoundUp(m_imageWidth, BC_BLOCK_SIZE);
	shaderCB.m_textureSizeInBlocks[1] = DivideAndRoundUp(m_imageHeight, BC_BLOCK_SIZE);
	shaderCB.m_imageSizeRcp.x = 1.0f / m_imageWidth;
	shaderCB.m_imageSizeRcp.y = 1.0f / m_imageHeight;
	shaderCB.m_texelBias = m_texelBias;
	shaderCB.m_texelScale = m_texelScale;
	shaderCB.m_exposure = static_cast<float>(exp(m_imageExposure));
	shaderCB.m_blitMode = m_blitMode;

	D3D11_MAPPED_SUBRESOURCE mappedRes;
	m_ctx->Map(m_constantBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedRes);
	memcpy(mappedRes.pData, &shaderCB, sizeof(shaderCB));
	m_ctx->Unmap(m_constantBuffer, 0);

	m_ctx->Begin(m_disjointQueries[m_frameID % MAX_QUERY_FRAME_NUM]);
	m_ctx->End(m_timeBeginQueries[m_frameID % MAX_QUERY_FRAME_NUM]);

	if (m_compressCS)
	{
		m_ctx->CSSetShader(m_compressCS, nullptr, 0);
		m_ctx->CSSetUnorderedAccessViews(0, 1, &m_compressTargetUAV, nullptr);
		m_ctx->CSSetShaderResources(0, 1, &m_sourceTextureView);
		m_ctx->CSSetSamplers(0, 1, &m_pointSampler);
		m_ctx->CSSetConstantBuffers(0, 1, &m_constantBuffer);

		uint32_t threadsX = 8;
		uint32_t threadsY = 8;
		m_ctx->Dispatch(DivideAndRoundUp(m_imageWidth, BC_BLOCK_SIZE * threadsX), DivideAndRoundUp(m_imageHeight, BC_BLOCK_SIZE * threadsY), 1);
	}
  else
  {
    return false;
  }

	m_ctx->End(m_timeEndQueries[m_frameID % MAX_QUERY_FRAME_NUM]);
	m_ctx->End(m_disjointQueries[m_frameID % MAX_QUERY_FRAME_NUM]);

	m_ctx->CopyResource(m_compressedTextureRes, m_tmpStagingRes);

  // Read the compressed texture
  D3D11_MAPPED_SUBRESOURCE mappedTexRes;
  m_ctx->Map(m_tmpStagingRes, 0, D3D11_MAP_READ, 0, &mappedTexRes);
  if (mappedRes.pData)
  {
    /*for (unsigned y = 0; y < m_imageHeight; ++y)
    {
      for (unsigned x = 0; x < m_imageWidth; ++x)
      {
        uint16_t bc6hData[4];
        memcpy(&bc6hData, (uint8_t*)mappedRes.pData + mappedRes.RowPitch * y + x * sizeof(bc6hData), sizeof(bc6hData));

        bc6hData[x + y * m_imageWidth].x = bc6hData[0];
        bc6hData[x + y * m_imageWidth].y = bc6hData[1];
        bc6hData[x + y * m_imageWidth].z = HalfToFloat(bc6hData[2]);
      }
    }*/

    // https://github.com/walbourn/directx-sdk-samples/blob/main/BC6HBC7EncoderCS/utils.cpp
    dstImage->m_width = DivideAndRoundUp(m_imageWidth, BC_BLOCK_SIZE);
    dstImage->m_height = DivideAndRoundUp(m_imageHeight, BC_BLOCK_SIZE);
    dstImage->m_format = SImage::ImageFormat::BC6H;
    dstImage->m_dataSize = dstImage->m_width * dstImage->m_height * sizeof(BufferBC6H);
    dstImage->m_data = static_cast<uint8_t*>(malloc(dstImage->m_dataSize));
    if (mappedRes.RowPitch == dstImage->m_width * sizeof(BufferBC6H))
      memcpy(dstImage->m_data, mappedRes.pData, dstImage->m_dataSize);
    else
      memset(dstImage->m_data, 255, dstImage->m_dataSize);

    m_ctx->Unmap(m_tmpStagingRes, 0);
  }
  else
  {
    return false;
  }

	//if (m_blitVS && m_blitPS)
	//{
	//	m_ctx->OMSetRenderTargets(1, &m_backBufferView, nullptr);
	//	D3D11_VIEWPORT vp;
	//	vp.Width = (float)m_backbufferWidth;
	//	vp.Height = (float)m_backbufferHeight;
	//	vp.MinDepth = 0.0f;
	//	vp.MaxDepth = 1.0f;
	//	vp.TopLeftX = 0;
	//	vp.TopLeftY = 0;
	//	m_ctx->RSSetViewports(1, &vp);

	//	m_ctx->VSSetShader(m_blitVS, nullptr, 0);
	//	m_ctx->PSSetShader(m_blitPS, nullptr, 0);
	//	m_ctx->PSSetShaderResources(0, 1, &m_sourceTextureView);
	//	m_ctx->PSSetShaderResources(1, 1, &m_compressedTextureView);
	//	m_ctx->PSSetSamplers(0, 1, &m_pointSampler);
	//	m_ctx->PSSetConstantBuffers(0, 1, &m_constantBuffer);

	//	m_ctx->DrawIndexed(4, 0, 0);
	//}

	//if (m_updateRMSE)
	//{
	//	UpdateRMSE();
	//	m_updateRMSE = false;
	//}

	++m_frameID;
  // TODO: replacement?
	// m_swapChain->Present(0, 0);

	D3D11_QUERY_DATA_TIMESTAMP_DISJOINT disjointData;
	uint64_t timeStart;
	uint64_t timeEnd;

	if (m_frameID > m_frameID % MAX_QUERY_FRAME_NUM)
	{
		while (m_ctx->GetData(m_disjointQueries[m_frameID % MAX_QUERY_FRAME_NUM], &disjointData, sizeof(disjointData), 0) != S_OK)
		{
			int e = 0;
		}

		while (m_ctx->GetData(m_timeBeginQueries[m_frameID % MAX_QUERY_FRAME_NUM], &timeStart, sizeof(timeStart), 0) != S_OK)
		{
			int e = 0;
		}

		while (m_ctx->GetData(m_timeEndQueries[m_frameID % MAX_QUERY_FRAME_NUM], &timeEnd, sizeof(timeEnd), 0) != S_OK)
		{
			int e = 0;
		}

		if (!disjointData.Disjoint)
		{
			uint64_t delta = (timeEnd - timeStart) * 1000;
			m_timeAcc += delta / (float)disjointData.Frequency;
			++m_timeAccSampleNum;
		}

		if (m_timeAccSampleNum > 100)
		{
			m_compressionTime = m_timeAcc / m_timeAccSampleNum;
			m_timeAcc = 0.0f;
			m_timeAccSampleNum = 0;
		}
	}
  return true;
}

void GPURealTimeBC6H::FreeImage(SImage* dstImage)
{
  free(dstImage->m_data);
  dstImage->m_data = nullptr;
}

//HRESULT GPURealTimeBC6H::TextureToBytes(ID3D11Texture2D* pSrcTexture, std::vector<ID3D11Buffer*>& subTextureAsBufs)
//{
//  HRESULT hr = S_OK;
//
//  D3D11_TEXTURE2D_DESC desc;
//  pSrcTexture->GetDesc(&desc);
//
//  if ((desc.ArraySize * desc.MipLevels) != (UINT)subTextureAsBufs.size())
//    return E_INVALIDARG;
//
//  auto image = std::make_unique<ScratchImage>();
//  if (!image)
//  {
//    return E_OUTOFMEMORY;
//  }
//  hr = image->Initialize2D(dstFormat, desc.Width, desc.Height, desc.ArraySize, desc.MipLevels);
//  if (FAILED(hr))
//    return hr;
//
//  UINT srcW = desc.Width, srcH = desc.Height;
//  for (UINT item = 0; item < desc.ArraySize; ++item)
//  {
//    desc.Width = srcW; desc.Height = srcH;
//    for (UINT level = 0; level < desc.MipLevels; ++level)
//    {
//      ID3D11Buffer* pReadbackbuf = CreateAndCopyToCPUBuf(m_pDevice, m_pContext, subTextureAsBufs[item * desc.MipLevels + level]);
//      if (!pReadbackbuf)
//      {
//        hr = E_OUTOFMEMORY;
//        return hr;
//      }
//
//      D3D11_MAPPED_SUBRESOURCE mappedSrc;
//#pragma warning (push)
//#pragma warning (disable:6387)
//      m_pContext->Map(pReadbackbuf, 0, D3D11_MAP_READ, 0, &mappedSrc);
//      memcpy(image->GetImage(level, item, 0)->pixels, mappedSrc.pData, desc.Height * desc.Width * sizeof(BufferBC6HBC7) / BLOCK_SIZE);
//      m_pContext->Unmap(pReadbackbuf, 0);
//#pragma warning (pop)
//
//      SAFE_RELEASE(pReadbackbuf);
//
//      desc.Width >>= 1; if (desc.Width < 4) desc.Width = 4;
//      desc.Height >>= 1; if (desc.Height < 4) desc.Height = 4;
//    }
//  }
//
//  TexMetadata info;
//  info = image->GetMetadata();
//  info.miscFlags = desc.MiscFlags;                                    // handle the case if TEX_MISC_TEXTURECUBE is present
//  if (IsSRGB(desc.Format) && dstFormat == DXGI_FORMAT_BC7_UNORM)    // input is sRGB, so save the encoded file also as sRGB format
//  {
//    info.format = DXGI_FORMAT_BC7_UNORM_SRGB;
//  }
//  hr = SaveToDDSFile(image->GetImages(), image->GetImageCount(), info, DDS_FLAGS_NONE, strFilename);
//
//  return hr;
//}

void GPURealTimeBC6H::CopyTexture(Vec3* image, ID3D11ShaderResourceView* srcView)
{
	if (m_blitVS && m_blitPS)
	{
		m_ctx->OMSetRenderTargets(1, &m_tmpTargetView, nullptr);
		D3D11_VIEWPORT vp;
		vp.Width = (float)m_imageWidth;
		vp.Height = (float)m_imageHeight;
		vp.MinDepth = 0.0f;
		vp.MaxDepth = 1.0f;
		vp.TopLeftX = 0;
		vp.TopLeftY = 0;
		m_ctx->RSSetViewports(1, &vp);

		m_ctx->VSSetShader(m_blitVS, nullptr, 0);
		m_ctx->PSSetShader(m_blitPS, nullptr, 0);
		m_ctx->PSSetShaderResources(0, 1, &srcView);
		m_ctx->PSSetShaderResources(1, 1, &srcView);
		m_ctx->PSSetSamplers(0, 1, &m_pointSampler);

		m_ctx->DrawIndexed(4, 0, 0);
		m_ctx->CopyResource(m_tmpStagingRes, m_tmpTargetRes);

		D3D11_MAPPED_SUBRESOURCE mappedRes;
		m_ctx->Map(m_tmpStagingRes, 0, D3D11_MAP_READ, 0, &mappedRes);
		if (mappedRes.pData)
		{
			for (unsigned y = 0; y < m_imageHeight; ++y)
			{
				for (unsigned x = 0; x < m_imageWidth; ++x)
				{
					uint16_t tmp[4];
					memcpy(&tmp, (uint8_t*)mappedRes.pData + mappedRes.RowPitch * y + x * sizeof(tmp), sizeof(tmp));

					image[x + y * m_imageWidth].x = HalfToFloat(tmp[0]);
					image[x + y * m_imageWidth].y = HalfToFloat(tmp[1]);
					image[x + y * m_imageWidth].z = HalfToFloat(tmp[2]);
				}
			}

			m_ctx->Unmap(m_tmpStagingRes, 0);
		}
	}
}

void GPURealTimeBC6H::UpdateRMSE()
{
	SShaderCB shaderCB;
	shaderCB.m_imageSizeRcp.x = 1.0f / m_imageWidth;
	shaderCB.m_imageSizeRcp.y = 1.0f / m_imageHeight;
	shaderCB.m_texelBias = Vec2(0.0f, 0.0f);
	shaderCB.m_texelScale = 1.0f;
	shaderCB.m_exposure = 1.0f;
	shaderCB.m_blitMode = 0;

	D3D11_MAPPED_SUBRESOURCE mappedRes;
	m_ctx->Map(m_constantBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedRes);
	memcpy(mappedRes.pData, &shaderCB, sizeof(shaderCB));
	m_ctx->Unmap(m_constantBuffer, 0);
	m_ctx->PSSetConstantBuffers(0, 1, &m_constantBuffer);

	Vec3* imageA = new Vec3[m_imageWidth * m_imageHeight];
	Vec3* imageB = new Vec3[m_imageWidth * m_imageHeight];

	CopyTexture(imageA, m_sourceTextureView);
	CopyTexture(imageB, m_compressedTextureView);
	
	// Compute RGB and Luminance RMSE errors in log space
	double rSum = 0.0;
	double gSum = 0.0;
	double bSum = 0.0;
	for (unsigned y = 0; y < m_imageHeight; ++y)
	{
		for (unsigned x = 0; x < m_imageWidth; ++x)
		{
			double x0 = imageA[x + y * m_imageWidth].x;
			double y0 = imageA[x + y * m_imageWidth].y;
			double z0 = imageA[x + y * m_imageWidth].z;
			double x1 = imageB[x + y * m_imageWidth].x;
			double y1 = imageB[x + y * m_imageWidth].y;
			double z1 = imageB[x + y * m_imageWidth].z;

			double dx = log(x1 + 1.0) - log(x0 + 1.0);
			double dy = log(y1 + 1.0) - log(y0 + 1.0);
			double dz = log(z1 + 1.0) - log(z0 + 1.0);
			rSum += dx * dx;
			gSum += dy * dy;
			bSum += dy * dy;
		}
	}
	m_rgbRMSLE = (float)sqrt((rSum + gSum + bSum) / (3.0 * m_imageWidth * m_imageHeight));
	m_lumRMSLE = (float)sqrt((0.299 * rSum + 0.587 * gSum + 0.114 * bSum) / (1.0 * m_imageWidth * m_imageHeight));

	delete imageA;
	delete imageB;

	char rmseString[256];
	rmseString[0] = 0;
	sprintf_s(rmseString, "rgbRMSLE:%.4f lumRMSLE:%.4f Mode:%s\n", m_rgbRMSLE, m_lumRMSLE, m_preset == Preset::Quality ? "Quality" : "Fast");
	OutputDebugStringA(rmseString);
}
