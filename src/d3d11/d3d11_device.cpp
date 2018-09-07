#include <algorithm>
#include <cstring>

#include "../dxvk/dxvk_adapter.h"
#include "../dxvk/dxvk_instance.h"

#include "d3d11_buffer.h"
#include "d3d11_class_linkage.h"
#include "d3d11_context_def.h"
#include "d3d11_context_imm.h"
#include "d3d11_device.h"
#include "d3d11_input_layout.h"
#include "d3d11_interop.h"
#include "d3d11_present.h"
#include "d3d11_query.h"
#include "d3d11_resource.h"
#include "d3d11_sampler.h"
#include "d3d11_shader.h"
#include "d3d11_texture.h"

namespace dxvk {
  
  D3D11DeviceContainer::D3D11DeviceContainer() {
    
  }
  
  
  D3D11DeviceContainer::~D3D11DeviceContainer() {
    delete m_d3d11VkInterop;
    delete m_d3d11Presenter;
    delete m_d3d11Device;
    delete m_dxgiDevice;
  }
  
  
  HRESULT STDMETHODCALLTYPE D3D11DeviceContainer::QueryInterface(REFIID riid, void** ppvObject) {
    *ppvObject = nullptr;
    
    if (riid == __uuidof(IUnknown)
     || riid == __uuidof(IDXGIObject)) {
      *ppvObject = ref(this);
      return S_OK;
    }
    
    if (riid == __uuidof(IDXGIDevice)
     || riid == __uuidof(IDXGIDevice1)
     || riid == __uuidof(IDXGIDevice2)
     || riid == __uuidof(IDXGIVkDevice)) {
      *ppvObject = ref(m_dxgiDevice);
      return S_OK;
    }
    
    if (riid == __uuidof(IDXGIVkInteropDevice)) {
      *ppvObject = ref(m_d3d11VkInterop);
      return S_OK;
    }
    
    if (riid == __uuidof(ID3D10Device)
     || riid == __uuidof(ID3D10Device1)) {
      *ppvObject = ref(m_d3d11Device->GetD3D10Interface());
      return S_OK;
    }
    
    if (riid == __uuidof(ID3D11Device)
     || riid == __uuidof(ID3D11Device1)) {
      *ppvObject = ref(m_d3d11Device);
      return S_OK;
    }
    
    if (riid == __uuidof(IDXGIVkPresenter)) {
      *ppvObject = ref(m_d3d11Presenter);
      return S_OK;
    }
    
    if (riid == __uuidof(ID3D11Debug))
      return E_NOINTERFACE;      
    
    // Undocumented interfaces that are queried by some games
    if (riid == GUID{0xd56e2a4c,0x5127,0x8437,{0x65,0x8a,0x98,0xc5,0xbb,0x78,0x94,0x98}})
      return E_NOINTERFACE;
    
    Logger::warn("D3D11DeviceContainer::QueryInterface: Unknown interface query");
    Logger::warn(str::format(riid));
    return E_NOINTERFACE;
  }
  

  HRESULT STDMETHODCALLTYPE D3D11DeviceContainer::GetParent(
          REFIID                  riid,
          void**                  ppParent) {
    return m_dxgiDevice->GetParent(riid, ppParent);
  }
  
  
  D3D11Device::D3D11Device(
          IDXGIObject*        pContainer,
          IDXGIVkDevice*      pDxgiDevice,
          D3D_FEATURE_LEVEL   FeatureLevel,
          UINT                FeatureFlags)
  : m_container     (pContainer),
    m_featureLevel  (FeatureLevel),
    m_featureFlags  (FeatureFlags),
    m_dxvkDevice    (pDxgiDevice->GetDXVKDevice()),
    m_dxvkAdapter   (m_dxvkDevice->adapter()),
    m_d3d11Options  (m_dxvkAdapter->instance()->config()),
    m_dxbcOptions   (getDxbcAppOptions(env::getExeName()) |
                     getDxbcDeviceOptions(m_dxvkDevice)) {
    Com<IDXGIAdapter> adapter;
    
    if (FAILED(pDxgiDevice->GetAdapter(&adapter))
     || FAILED(adapter->QueryInterface(__uuidof(IDXGIVkAdapter),
          reinterpret_cast<void**>(&m_dxgiAdapter))))
      throw DxvkError("D3D11Device: Failed to query adapter");
    
    m_initializer = new D3D11Initializer(m_dxvkDevice);
    m_context     = new D3D11ImmediateContext(this, m_dxvkDevice);
    m_d3d10Device = new D3D10Device(this, m_context);

    m_uavCounters = CreateUAVCounterBuffer();
  }
  
  
  D3D11Device::~D3D11Device() {
    delete m_d3d10Device;
    delete m_context;
    delete m_initializer;
  }
  
  
  ULONG STDMETHODCALLTYPE D3D11Device::AddRef() {
    return m_container->AddRef();
  }
  
  
  ULONG STDMETHODCALLTYPE D3D11Device::Release() {
    return m_container->Release();
  }
  
  
  HRESULT STDMETHODCALLTYPE D3D11Device::QueryInterface(REFIID riid, void** ppvObject) {
    return m_container->QueryInterface(riid, ppvObject);
  }
    
  
  HRESULT STDMETHODCALLTYPE D3D11Device::CreateBuffer(
    const D3D11_BUFFER_DESC*      pDesc,
    const D3D11_SUBRESOURCE_DATA* pInitialData,
          ID3D11Buffer**          ppBuffer) {
    InitReturnPtr(ppBuffer);
    
    if (ppBuffer == nullptr)
      return S_FALSE;
    
    try {
      const Com<D3D11Buffer> buffer
        = new D3D11Buffer(this, pDesc);
      
      m_initializer->InitBuffer(buffer.ptr(), pInitialData);
      *ppBuffer = buffer.ref();
      return S_OK;
    } catch (const DxvkError& e) {
      Logger::err(e.message());
      return E_FAIL;
    }
  }
  
  
  HRESULT STDMETHODCALLTYPE D3D11Device::CreateTexture1D(
    const D3D11_TEXTURE1D_DESC*   pDesc,
    const D3D11_SUBRESOURCE_DATA* pInitialData,
          ID3D11Texture1D**       ppTexture1D) {
    InitReturnPtr(ppTexture1D);
    
    D3D11_COMMON_TEXTURE_DESC desc;
    desc.Width          = pDesc->Width;
    desc.Height         = 1;
    desc.Depth          = 1;
    desc.MipLevels      = pDesc->MipLevels;
    desc.ArraySize      = pDesc->ArraySize;
    desc.Format         = pDesc->Format;
    desc.SampleDesc     = DXGI_SAMPLE_DESC { 1, 0 };
    desc.Usage          = pDesc->Usage;
    desc.BindFlags      = pDesc->BindFlags;
    desc.CPUAccessFlags = pDesc->CPUAccessFlags;
    desc.MiscFlags      = pDesc->MiscFlags;
    
    if (FAILED(D3D11CommonTexture::NormalizeTextureProperties(&desc)))
      return E_INVALIDARG;
    
    if (ppTexture1D == nullptr)
      return S_FALSE;
    
    try {
      const Com<D3D11Texture1D> texture = new D3D11Texture1D(this, &desc);
      m_initializer->InitTexture(texture->GetCommonTexture(), pInitialData);
      *ppTexture1D = texture.ref();
      return S_OK;
    } catch (const DxvkError& e) {
      Logger::err(e.message());
      return E_FAIL;
    }
  }
  
  
  HRESULT STDMETHODCALLTYPE D3D11Device::CreateTexture2D(
    const D3D11_TEXTURE2D_DESC*   pDesc,
    const D3D11_SUBRESOURCE_DATA* pInitialData,
          ID3D11Texture2D**       ppTexture2D) {
    InitReturnPtr(ppTexture2D);
    
    D3D11_COMMON_TEXTURE_DESC desc;
    desc.Width          = pDesc->Width;
    desc.Height         = pDesc->Height;
    desc.Depth          = 1;
    desc.MipLevels      = pDesc->MipLevels;
    desc.ArraySize      = pDesc->ArraySize;
    desc.Format         = pDesc->Format;
    desc.SampleDesc     = pDesc->SampleDesc;
    desc.Usage          = pDesc->Usage;
    desc.BindFlags      = pDesc->BindFlags;
    desc.CPUAccessFlags = pDesc->CPUAccessFlags;
    desc.MiscFlags      = pDesc->MiscFlags;
    
    if (FAILED(D3D11CommonTexture::NormalizeTextureProperties(&desc)))
      return E_INVALIDARG;
    
    if (ppTexture2D == nullptr)
      return S_FALSE;
    
    try {
      const Com<D3D11Texture2D> texture = new D3D11Texture2D(this, &desc);
      m_initializer->InitTexture(texture->GetCommonTexture(), pInitialData);
      *ppTexture2D = texture.ref();
      return S_OK;
    } catch (const DxvkError& e) {
      Logger::err(e.message());
      return E_FAIL;
    }
  }
  
  
  HRESULT STDMETHODCALLTYPE D3D11Device::CreateTexture3D(
    const D3D11_TEXTURE3D_DESC*   pDesc,
    const D3D11_SUBRESOURCE_DATA* pInitialData,
          ID3D11Texture3D**       ppTexture3D) {
    InitReturnPtr(ppTexture3D);
    
    D3D11_COMMON_TEXTURE_DESC desc;
    desc.Width          = pDesc->Width;
    desc.Height         = pDesc->Height;
    desc.Depth          = pDesc->Depth;
    desc.MipLevels      = pDesc->MipLevels;
    desc.ArraySize      = 1;
    desc.Format         = pDesc->Format;
    desc.SampleDesc     = DXGI_SAMPLE_DESC { 1, 0 };
    desc.Usage          = pDesc->Usage;
    desc.BindFlags      = pDesc->BindFlags;
    desc.CPUAccessFlags = pDesc->CPUAccessFlags;
    desc.MiscFlags      = pDesc->MiscFlags;
    
    if (FAILED(D3D11CommonTexture::NormalizeTextureProperties(&desc)))
      return E_INVALIDARG;
    
    if (ppTexture3D == nullptr)
      return S_FALSE;
      
    try {
      const Com<D3D11Texture3D> texture = new D3D11Texture3D(this, &desc);
      m_initializer->InitTexture(texture->GetCommonTexture(), pInitialData);
      *ppTexture3D = texture.ref();
      return S_OK;
    } catch (const DxvkError& e) {
      Logger::err(e.message());
      return E_FAIL;
    }
  }
  
  
  HRESULT STDMETHODCALLTYPE D3D11Device::CreateShaderResourceView(
          ID3D11Resource*                   pResource,
    const D3D11_SHADER_RESOURCE_VIEW_DESC*  pDesc,
          ID3D11ShaderResourceView**        ppSRView) {
    InitReturnPtr(ppSRView);

    D3D11_COMMON_RESOURCE_DESC resourceDesc;
    GetCommonResourceDesc(pResource, &resourceDesc);
    
    // The description is optional. If omitted, we'll create
    // a view that covers all subresources of the image.
    D3D11_SHADER_RESOURCE_VIEW_DESC desc;
    
    if (pDesc == nullptr) {
      if (FAILED(D3D11ShaderResourceView::GetDescFromResource(pResource, &desc)))
        return E_INVALIDARG;
    } else {
      desc = *pDesc;
      
      if (FAILED(D3D11ShaderResourceView::NormalizeDesc(pResource, &desc)))
        return E_INVALIDARG;
    }
    
    if (!CheckResourceViewCompatibility(pResource, D3D11_BIND_SHADER_RESOURCE, desc.Format)) {
      Logger::err(str::format("D3D11: Cannot create shader resource view:",
        "\n  Resource type:   ", resourceDesc.Dim,
        "\n  Resource usage:  ", resourceDesc.BindFlags,
        "\n  Resource format: ", resourceDesc.Format,
        "\n  View format:     ", desc.Format));
      return E_INVALIDARG;
    }
    
    if (ppSRView == nullptr)
      return S_FALSE;
    
    try {
      *ppSRView = ref(new D3D11ShaderResourceView(this, pResource, &desc));
      return S_OK;
    } catch (const DxvkError& e) {
      Logger::err(e.message());
      return E_FAIL;
    }
  }
  
  
  HRESULT STDMETHODCALLTYPE D3D11Device::CreateUnorderedAccessView(
          ID3D11Resource*                   pResource,
    const D3D11_UNORDERED_ACCESS_VIEW_DESC* pDesc,
          ID3D11UnorderedAccessView**       ppUAView) {
    InitReturnPtr(ppUAView);
    
    D3D11_COMMON_RESOURCE_DESC resourceDesc;
    GetCommonResourceDesc(pResource, &resourceDesc);

    // The description is optional. If omitted, we'll create
    // a view that covers all subresources of the image.
    D3D11_UNORDERED_ACCESS_VIEW_DESC desc;
    
    if (pDesc == nullptr) {
      if (FAILED(D3D11UnorderedAccessView::GetDescFromResource(pResource, &desc)))
        return E_INVALIDARG;
    } else {
      desc = *pDesc;
      
      if (FAILED(D3D11UnorderedAccessView::NormalizeDesc(pResource, &desc)))
        return E_INVALIDARG;
    }
    
    if (!CheckResourceViewCompatibility(pResource, D3D11_BIND_UNORDERED_ACCESS, desc.Format)) {
      Logger::err(str::format("D3D11: Cannot create unordered access view:",
        "\n  Resource type:   ", resourceDesc.Dim,
        "\n  Resource usage:  ", resourceDesc.BindFlags,
        "\n  Resource format: ", resourceDesc.Format,
        "\n  View format:     ", desc.Format));
      return E_INVALIDARG;
    }

    if (ppUAView == nullptr)
      return S_FALSE;
    
    try {
      *ppUAView = ref(new D3D11UnorderedAccessView(this, pResource, &desc));
      return S_OK;
    } catch (const DxvkError& e) {
      Logger::err(e.message());
      return E_FAIL;
    }
  }
  
  
  HRESULT STDMETHODCALLTYPE D3D11Device::CreateRenderTargetView(
          ID3D11Resource*                   pResource,
    const D3D11_RENDER_TARGET_VIEW_DESC*    pDesc,
          ID3D11RenderTargetView**          ppRTView) {
    InitReturnPtr(ppRTView);
    
    // DXVK only supports render target views for image resources
    D3D11_RESOURCE_DIMENSION resourceDim = D3D11_RESOURCE_DIMENSION_UNKNOWN;
    pResource->GetType(&resourceDim);
    
    if (resourceDim == D3D11_RESOURCE_DIMENSION_BUFFER) {
      Logger::warn("D3D11: Cannot create render target view for a buffer");
      return S_OK; // It is required to run Battlefield 3 and Battlefield 4.
    }
    
    D3D11_COMMON_RESOURCE_DESC resourceDesc;
    GetCommonResourceDesc(pResource, &resourceDesc);
    
    // The view description is optional. If not defined, it
    // will use the resource's format and all array layers.
    D3D11_RENDER_TARGET_VIEW_DESC desc;
    
    if (pDesc == nullptr) {
      if (FAILED(D3D11RenderTargetView::GetDescFromResource(pResource, &desc)))
        return E_INVALIDARG;
    } else {
      desc = *pDesc;
      
      if (FAILED(D3D11RenderTargetView::NormalizeDesc(pResource, &desc)))
        return E_INVALIDARG;
    }
    
    if (!CheckResourceViewCompatibility(pResource, D3D11_BIND_RENDER_TARGET, desc.Format)) {
      Logger::err(str::format("D3D11: Cannot create render target view:",
        "\n  Resource type:   ", resourceDesc.Dim,
        "\n  Resource usage:  ", resourceDesc.BindFlags,
        "\n  Resource format: ", resourceDesc.Format,
        "\n  View format:     ", desc.Format));
      return E_INVALIDARG;
    }

    if (ppRTView == nullptr)
      return S_FALSE;
    
    try {
      *ppRTView = ref(new D3D11RenderTargetView(this, pResource, &desc));
      return S_OK;
    } catch (const DxvkError& e) {
      Logger::err(e.message());
      return E_FAIL;
    }
  }
  
  
  HRESULT STDMETHODCALLTYPE D3D11Device::CreateDepthStencilView(
          ID3D11Resource*                   pResource,
    const D3D11_DEPTH_STENCIL_VIEW_DESC*    pDesc,
          ID3D11DepthStencilView**          ppDepthStencilView) {
    InitReturnPtr(ppDepthStencilView);
    
    D3D11_COMMON_RESOURCE_DESC resourceDesc;
    GetCommonResourceDesc(pResource, &resourceDesc);

    // The view description is optional. If not defined, it
    // will use the resource's format and all array layers.
    D3D11_DEPTH_STENCIL_VIEW_DESC desc;
    
    if (pDesc == nullptr) {
      if (FAILED(D3D11DepthStencilView::GetDescFromResource(pResource, &desc)))
        return E_INVALIDARG;
    } else {
      desc = *pDesc;
      
      if (FAILED(D3D11DepthStencilView::NormalizeDesc(pResource, &desc)))
        return E_INVALIDARG;
    }
    
    if (!CheckResourceViewCompatibility(pResource, D3D11_BIND_DEPTH_STENCIL, desc.Format)) {
      Logger::err(str::format("D3D11: Cannot create depth-stencil view:",
        "\n  Resource type:   ", resourceDesc.Dim,
        "\n  Resource usage:  ", resourceDesc.BindFlags,
        "\n  Resource format: ", resourceDesc.Format,
        "\n  View format:     ", desc.Format));
      return E_INVALIDARG;
    }
    
    if (ppDepthStencilView == nullptr)
      return S_FALSE;
    
    try {
      *ppDepthStencilView = ref(new D3D11DepthStencilView(this, pResource, &desc));
      return S_OK;
    } catch (const DxvkError& e) {
      Logger::err(e.message());
      return E_FAIL;
    }
  }
  
  
  HRESULT STDMETHODCALLTYPE D3D11Device::CreateInputLayout(
    const D3D11_INPUT_ELEMENT_DESC*   pInputElementDescs,
          UINT                        NumElements,
    const void*                       pShaderBytecodeWithInputSignature,
          SIZE_T                      BytecodeLength,
          ID3D11InputLayout**         ppInputLayout) {
    InitReturnPtr(ppInputLayout);
    
    try {
      DxbcReader dxbcReader(reinterpret_cast<const char*>(
        pShaderBytecodeWithInputSignature), BytecodeLength);
      DxbcModule dxbcModule(dxbcReader);
      
      const Rc<DxbcIsgn> inputSignature = dxbcModule.isgn();

      uint32_t attrMask = 0;
      uint32_t bindMask = 0;
      
      std::array<DxvkVertexAttribute, D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT> attrList;
      std::array<DxvkVertexBinding,   D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT> bindList;
      
      for (uint32_t i = 0; i < NumElements; i++) {
        const DxbcSgnEntry* entry = inputSignature->find(
          pInputElementDescs[i].SemanticName,
          pInputElementDescs[i].SemanticIndex, 0);
        
        if (entry == nullptr) {
          Logger::debug(str::format(
            "D3D11Device: No such vertex shader semantic: ",
            pInputElementDescs[i].SemanticName,
            pInputElementDescs[i].SemanticIndex));
        }
        
        // Create vertex input attribute description
        DxvkVertexAttribute attrib;
        attrib.location = entry != nullptr ? entry->registerId : 0;
        attrib.binding  = pInputElementDescs[i].InputSlot;
        attrib.format   = m_dxgiAdapter->LookupFormat(
          pInputElementDescs[i].Format, DXGI_VK_FORMAT_MODE_COLOR).Format;
        attrib.offset   = pInputElementDescs[i].AlignedByteOffset;
        
        // The application may choose to let the implementation
        // generate the exact vertex layout. In that case we'll
        // pack attributes on the same binding in the order they
        // are declared, aligning each attribute to four bytes.
        if (attrib.offset == D3D11_APPEND_ALIGNED_ELEMENT) {
          attrib.offset = 0;
          
          for (uint32_t j = 1; j <= i; j++) {
            const DxvkVertexAttribute& prev = attrList.at(i - j);
            
            if (prev.binding == attrib.binding) {
              const DxvkFormatInfo* formatInfo = imageFormatInfo(prev.format);
              attrib.offset = align(prev.offset + formatInfo->elementSize, 4);
              break;
            }
          }
        }

        attrList.at(i) = attrib;
        
        // Create vertex input binding description. The
        // stride is dynamic state in D3D11 and will be
        // set by D3D11DeviceContext::IASetVertexBuffers.
        DxvkVertexBinding binding;
        binding.binding   = pInputElementDescs[i].InputSlot;
        binding.fetchRate = pInputElementDescs[i].InstanceDataStepRate;
        binding.inputRate = pInputElementDescs[i].InputSlotClass == D3D11_INPUT_PER_INSTANCE_DATA
          ? VK_VERTEX_INPUT_RATE_INSTANCE : VK_VERTEX_INPUT_RATE_VERTEX;
        
        // Check if the binding was already defined. If so, the
        // parameters must be identical (namely, the input rate).
        bool bindingDefined = false;
        
        for (uint32_t j = 0; j < i; j++) {
          uint32_t bindingId = attrList.at(j).binding;

          if (binding.binding == bindingId) {
            bindingDefined = true;
            
            if (binding.inputRate != bindList.at(bindingId).inputRate) {
              Logger::err(str::format(
                "D3D11Device: Conflicting input rate for binding ",
                binding.binding));
              return E_INVALIDARG;
            }
          }
        }

        if (!bindingDefined)
          bindList.at(binding.binding) = binding;
        
        if (entry != nullptr) {
          attrMask |= 1u << i;
          bindMask |= 1u << binding.binding;
        }
      }

      // Compact the attribute and binding lists to filter
      // out attributes and bindings not used by the shader
      uint32_t attrCount = CompactSparseList(attrList.data(), attrMask);
      uint32_t bindCount = CompactSparseList(bindList.data(), bindMask);

      // Check if there are any semantics defined in the
      // shader that are not included in the current input
      // layout.
      for (auto i = inputSignature->begin(); i != inputSignature->end(); i++) {
        bool found = i->systemValue != DxbcSystemValue::None;
        
        for (uint32_t j = 0; j < attrCount && !found; j++)
          found = attrList.at(j).location == i->registerId;
        
        if (!found) {
          Logger::warn(str::format(
            "D3D11Device: Vertex input '",
            i->semanticName, i->semanticIndex,
            "' not defined by input layout"));
        }
      }
      
      // Create the actual input layout object
      // if the application requests it.
      if (ppInputLayout != nullptr) {
        *ppInputLayout = ref(
          new D3D11InputLayout(this,
            attrCount, attrList.data(),
            bindCount, bindList.data()));
      }
      
      return S_OK;
    } catch (const DxvkError& e) {
      Logger::err(e.message());
      return E_FAIL;
    }
  }
  
  
  HRESULT STDMETHODCALLTYPE D3D11Device::CreateVertexShader(
    const void*                       pShaderBytecode,
          SIZE_T                      BytecodeLength,
          ID3D11ClassLinkage*         pClassLinkage,
          ID3D11VertexShader**        ppVertexShader) {
    InitReturnPtr(ppVertexShader);
    D3D11CommonShader module;

    DxbcModuleInfo moduleInfo;
    moduleInfo.options = m_dxbcOptions;
    
    if (FAILED(this->CreateShaderModule(&module,
        pShaderBytecode, BytecodeLength, pClassLinkage,
        &moduleInfo, DxbcProgramType::VertexShader)))
      return E_INVALIDARG;
    
    if (ppVertexShader == nullptr)
      return S_FALSE;
    
    *ppVertexShader = ref(new D3D11VertexShader(this, module));
    return S_OK;
  }
  
  
  HRESULT STDMETHODCALLTYPE D3D11Device::CreateGeometryShader(
    const void*                       pShaderBytecode,
          SIZE_T                      BytecodeLength,
          ID3D11ClassLinkage*         pClassLinkage,
          ID3D11GeometryShader**      ppGeometryShader) {
    InitReturnPtr(ppGeometryShader);
    D3D11CommonShader module;
    
    DxbcModuleInfo moduleInfo;
    moduleInfo.options = m_dxbcOptions;

    if (FAILED(this->CreateShaderModule(&module,
        pShaderBytecode, BytecodeLength, pClassLinkage,
        &moduleInfo, DxbcProgramType::GeometryShader)))
      return E_INVALIDARG;
    
    if (ppGeometryShader == nullptr)
      return S_FALSE;
    
    *ppGeometryShader = ref(new D3D11GeometryShader(this, module));
    return S_OK;
  }
  
  
  HRESULT STDMETHODCALLTYPE D3D11Device::CreateGeometryShaderWithStreamOutput(
    const void*                       pShaderBytecode,
          SIZE_T                      BytecodeLength,
    const D3D11_SO_DECLARATION_ENTRY* pSODeclaration,
          UINT                        NumEntries,
    const UINT*                       pBufferStrides,
          UINT                        NumStrides,
          UINT                        RasterizedStream,
          ID3D11ClassLinkage*         pClassLinkage,
          ID3D11GeometryShader**      ppGeometryShader) {
    InitReturnPtr(ppGeometryShader);
    Logger::err("D3D11Device::CreateGeometryShaderWithStreamOutput: Not implemented");
    
    // Returning S_OK instead of an error fixes some issues
    // with Overwatch until this is properly implemented
    return m_d3d11Options.fakeStreamOutSupport ? S_OK : E_NOTIMPL;
  }
  
  
  HRESULT STDMETHODCALLTYPE D3D11Device::CreatePixelShader(
    const void*                       pShaderBytecode,
          SIZE_T                      BytecodeLength,
          ID3D11ClassLinkage*         pClassLinkage,
          ID3D11PixelShader**         ppPixelShader) {
    InitReturnPtr(ppPixelShader);
    D3D11CommonShader module;
    
    DxbcModuleInfo moduleInfo;
    moduleInfo.options = m_dxbcOptions;

    if (FAILED(this->CreateShaderModule(&module,
        pShaderBytecode, BytecodeLength, pClassLinkage,
        &moduleInfo, DxbcProgramType::PixelShader)))
      return E_INVALIDARG;
    
    if (ppPixelShader == nullptr)
      return S_FALSE;
    
    *ppPixelShader = ref(new D3D11PixelShader(this, module));
    return S_OK;
  }
  
  
  HRESULT STDMETHODCALLTYPE D3D11Device::CreateHullShader(
    const void*                       pShaderBytecode,
          SIZE_T                      BytecodeLength,
          ID3D11ClassLinkage*         pClassLinkage,
          ID3D11HullShader**          ppHullShader) {
    InitReturnPtr(ppHullShader);
    D3D11CommonShader module;
    
    DxbcModuleInfo moduleInfo;
    moduleInfo.options = m_dxbcOptions;

    if (FAILED(this->CreateShaderModule(&module,
        pShaderBytecode, BytecodeLength, pClassLinkage,
        &moduleInfo, DxbcProgramType::HullShader)))
      return E_INVALIDARG;
    
    if (ppHullShader == nullptr)
      return S_FALSE;
    
    *ppHullShader = ref(new D3D11HullShader(this, module));
    return S_OK;
  }
  
  
  HRESULT STDMETHODCALLTYPE D3D11Device::CreateDomainShader(
    const void*                       pShaderBytecode,
          SIZE_T                      BytecodeLength,
          ID3D11ClassLinkage*         pClassLinkage,
          ID3D11DomainShader**        ppDomainShader) {
    InitReturnPtr(ppDomainShader);
    D3D11CommonShader module;
    
    DxbcModuleInfo moduleInfo;
    moduleInfo.options = m_dxbcOptions;

    if (FAILED(this->CreateShaderModule(&module,
        pShaderBytecode, BytecodeLength, pClassLinkage,
        &moduleInfo, DxbcProgramType::DomainShader)))
      return E_INVALIDARG;
    
    if (ppDomainShader == nullptr)
      return S_FALSE;
    
    *ppDomainShader = ref(new D3D11DomainShader(this, module));
    return S_OK;
  }
  
  
  HRESULT STDMETHODCALLTYPE D3D11Device::CreateComputeShader(
    const void*                       pShaderBytecode,
          SIZE_T                      BytecodeLength,
          ID3D11ClassLinkage*         pClassLinkage,
          ID3D11ComputeShader**       ppComputeShader) {
    InitReturnPtr(ppComputeShader);
    D3D11CommonShader module;
    
    DxbcModuleInfo moduleInfo;
    moduleInfo.options = m_dxbcOptions;

    if (FAILED(this->CreateShaderModule(&module,
        pShaderBytecode, BytecodeLength, pClassLinkage,
        &moduleInfo, DxbcProgramType::ComputeShader)))
      return E_INVALIDARG;
    
    if (ppComputeShader == nullptr)
      return S_FALSE;
    
    *ppComputeShader = ref(new D3D11ComputeShader(this, module));
    return S_OK;
  }
  
  
  HRESULT STDMETHODCALLTYPE D3D11Device::CreateClassLinkage(ID3D11ClassLinkage** ppLinkage) {
    *ppLinkage = ref(new D3D11ClassLinkage(this));
    return S_OK;
  }
  
  
  HRESULT STDMETHODCALLTYPE D3D11Device::CreateBlendState(
    const D3D11_BLEND_DESC*           pBlendStateDesc,
          ID3D11BlendState**          ppBlendState) {
    InitReturnPtr(ppBlendState);
    
    D3D11_BLEND_DESC1 desc = pBlendStateDesc != nullptr
      ? D3D11BlendState::PromoteDesc(pBlendStateDesc)
      : D3D11BlendState::DefaultDesc();
    
    if (FAILED(D3D11BlendState::NormalizeDesc(&desc)))
      return E_INVALIDARG;
    
    if (ppBlendState != nullptr) {
      *ppBlendState = m_bsStateObjects.Create(this, desc);
      return S_OK;
    } return S_FALSE;
  }
  
  
  HRESULT STDMETHODCALLTYPE D3D11Device::CreateBlendState1(
    const D3D11_BLEND_DESC1*          pBlendStateDesc, 
          ID3D11BlendState1**         ppBlendState) {
    InitReturnPtr(ppBlendState);
    
    D3D11_BLEND_DESC1 desc = pBlendStateDesc != nullptr
      ? *pBlendStateDesc
      : D3D11BlendState::DefaultDesc();
    
    if (FAILED(D3D11BlendState::NormalizeDesc(&desc)))
      return E_INVALIDARG;
    
    if (ppBlendState != nullptr) {
      *ppBlendState = m_bsStateObjects.Create(this, desc);
      return S_OK;
    } return S_FALSE;
  }
  
  
  HRESULT STDMETHODCALLTYPE D3D11Device::CreateDepthStencilState(
    const D3D11_DEPTH_STENCIL_DESC*   pDepthStencilDesc,
          ID3D11DepthStencilState**   ppDepthStencilState) {
    InitReturnPtr(ppDepthStencilState);
    
    D3D11_DEPTH_STENCIL_DESC desc = pDepthStencilDesc != nullptr
      ? *pDepthStencilDesc
      : D3D11DepthStencilState::DefaultDesc();
    
    if (FAILED(D3D11DepthStencilState::NormalizeDesc(&desc)))
      return E_INVALIDARG;
    
    if (ppDepthStencilState != nullptr) {
      *ppDepthStencilState = m_dsStateObjects.Create(this, desc);
      return S_OK;
    } return S_FALSE;
  }
  
  
  HRESULT STDMETHODCALLTYPE D3D11Device::CreateRasterizerState(
    const D3D11_RASTERIZER_DESC*      pRasterizerDesc,
          ID3D11RasterizerState**     ppRasterizerState) {
    InitReturnPtr(ppRasterizerState);
    
    D3D11_RASTERIZER_DESC1 desc = pRasterizerDesc != nullptr
      ? D3D11RasterizerState::PromoteDesc(pRasterizerDesc)
      : D3D11RasterizerState::DefaultDesc();
    
    if (FAILED(D3D11RasterizerState::NormalizeDesc(&desc)))
      return E_INVALIDARG;
    
    if (ppRasterizerState != nullptr) {
      *ppRasterizerState = m_rsStateObjects.Create(this, desc);
      return S_OK;
    } return S_FALSE;
  }
  
  
  HRESULT D3D11Device::CreateRasterizerState1(
    const D3D11_RASTERIZER_DESC1*     pRasterizerDesc, 
          ID3D11RasterizerState1**    ppRasterizerState) {
    InitReturnPtr(ppRasterizerState);
    
    D3D11_RASTERIZER_DESC1 desc = pRasterizerDesc != nullptr
      ? *pRasterizerDesc
      : D3D11RasterizerState::DefaultDesc();
    
    if (FAILED(D3D11RasterizerState::NormalizeDesc(&desc)))
      return E_INVALIDARG;
    
    if (ppRasterizerState != nullptr) {
      *ppRasterizerState = m_rsStateObjects.Create(this, desc);
      return S_OK;
    } return S_FALSE;
  }
  
  
  HRESULT STDMETHODCALLTYPE D3D11Device::CreateSamplerState(
    const D3D11_SAMPLER_DESC*         pSamplerDesc,
          ID3D11SamplerState**        ppSamplerState) {
    InitReturnPtr(ppSamplerState);
    D3D11_SAMPLER_DESC desc = *pSamplerDesc;
    
    if (FAILED(D3D11SamplerState::NormalizeDesc(&desc)))
      return E_INVALIDARG;
    
    if (ppSamplerState == nullptr)
      return S_FALSE;
    
    try {
      *ppSamplerState = m_samplerObjects.Create(this, desc);
      return S_OK;
    } catch (const DxvkError& e) {
      Logger::err(e.message());
      return E_FAIL;
    }
  }
  
  
  HRESULT STDMETHODCALLTYPE D3D11Device::CreateQuery(
    const D3D11_QUERY_DESC*           pQueryDesc,
          ID3D11Query**               ppQuery) {
    InitReturnPtr(ppQuery);
    
    if (pQueryDesc->Query != D3D11_QUERY_EVENT
     && pQueryDesc->Query != D3D11_QUERY_OCCLUSION
     && pQueryDesc->Query != D3D11_QUERY_TIMESTAMP
     && pQueryDesc->Query != D3D11_QUERY_TIMESTAMP_DISJOINT
     && pQueryDesc->Query != D3D11_QUERY_PIPELINE_STATISTICS
     && pQueryDesc->Query != D3D11_QUERY_OCCLUSION_PREDICATE) {
      Logger::warn(str::format("D3D11Query: Unsupported query type ", pQueryDesc->Query));
      return E_INVALIDARG;
    }
    
    if (ppQuery == nullptr)
      return S_FALSE;
    
    try {
      *ppQuery = ref(new D3D11Query(this, *pQueryDesc));
      return S_OK;
    } catch (const DxvkError& e) {
      Logger::err(e.message());
      return E_FAIL;
    }
  }
  
  
  HRESULT STDMETHODCALLTYPE D3D11Device::CreatePredicate(
    const D3D11_QUERY_DESC*           pPredicateDesc,
          ID3D11Predicate**           ppPredicate) {
    InitReturnPtr(ppPredicate);
    
    if (pPredicateDesc->Query != D3D11_QUERY_OCCLUSION_PREDICATE)
      return E_INVALIDARG;
    
    if (ppPredicate == nullptr)
      return S_FALSE;
    
    try {
      *ppPredicate = ref(new D3D11Query(this, *pPredicateDesc));
      return S_OK;
    } catch (const DxvkError& e) {
      Logger::err(e.message());
      return E_FAIL;
    }
  }
  
  
  HRESULT STDMETHODCALLTYPE D3D11Device::CreateCounter(
    const D3D11_COUNTER_DESC*         pCounterDesc,
          ID3D11Counter**             ppCounter) {
    InitReturnPtr(ppCounter);
    
    Logger::err(str::format("D3D11: Unsupported counter: ", pCounterDesc->Counter));
    return E_INVALIDARG;
  }
  
  
  HRESULT STDMETHODCALLTYPE D3D11Device::CreateDeferredContext(
          UINT                        ContextFlags,
          ID3D11DeviceContext**       ppDeferredContext) {
    *ppDeferredContext = ref(new D3D11DeferredContext(this, m_dxvkDevice, ContextFlags));
    return S_OK;
  }
  
  HRESULT STDMETHODCALLTYPE D3D11Device::CreateDeferredContext1(
          UINT                        ContextFlags, 
          ID3D11DeviceContext1**      ppDeferredContext) {
    *ppDeferredContext = ref(new D3D11DeferredContext(this, m_dxvkDevice, ContextFlags));
    return S_OK;
  }
  
  HRESULT STDMETHODCALLTYPE D3D11Device::CreateDeviceContextState(
          UINT                        Flags, 
    const D3D_FEATURE_LEVEL*          pFeatureLevels, 
          UINT                        FeatureLevels, 
          UINT                        SDKVersion, 
          REFIID                      EmulatedInterface, 
          D3D_FEATURE_LEVEL*          pChosenFeatureLevel, 
          ID3DDeviceContextState**    ppContextState) {
    InitReturnPtr(ppContextState);
    
    Logger::err("D3D11Device::CreateDeviceContextState: Not implemented");
    return E_NOTIMPL;
  }
  
  HRESULT STDMETHODCALLTYPE D3D11Device::OpenSharedResource(
          HANDLE      hResource,
          REFIID      ReturnedInterface,
          void**      ppResource) {
    InitReturnPtr(ppResource);
    
    Logger::err("D3D11Device::OpenSharedResource: Not implemented");
    return E_NOTIMPL;
  }
  
  
  HRESULT STDMETHODCALLTYPE D3D11Device::OpenSharedResource1(
          HANDLE      hResource,
          REFIID      ReturnedInterface,
          void**      ppResource) {
    InitReturnPtr(ppResource);
    
    Logger::err("D3D11Device::OpenSharedResource1: Not implemented");
    return E_NOTIMPL;
  }

  
  HRESULT STDMETHODCALLTYPE D3D11Device::OpenSharedResourceByName(
          LPCWSTR     lpName, 
          DWORD       dwDesiredAccess, 
          REFIID      returnedInterface, 
          void**      ppResource) {
    InitReturnPtr(ppResource);
    
    Logger::err("D3D11Device::OpenSharedResourceByName: Not implemented");
    return E_NOTIMPL;
  }
  
  HRESULT STDMETHODCALLTYPE D3D11Device::CheckFormatSupport(
          DXGI_FORMAT Format,
          UINT*       pFormatSupport) {
    return GetFormatSupportFlags(Format, pFormatSupport, nullptr);
  }
  
  
  HRESULT STDMETHODCALLTYPE D3D11Device::CheckMultisampleQualityLevels(
          DXGI_FORMAT Format,
          UINT        SampleCount,
          UINT*       pNumQualityLevels) {
    // There are many error conditions, so we'll just assume
    // that we will fail and return a non-zero value in case
    // the device does actually support the format.
    *pNumQualityLevels = 0;
    
    // We need to check whether the format is 
    VkFormat format = m_dxgiAdapter->LookupFormat(
      Format, DXGI_VK_FORMAT_MODE_ANY).Format;
    
    if (format == VK_FORMAT_UNDEFINED) {
      Logger::err(str::format("D3D11: Unsupported format: ", Format));
      return E_INVALIDARG;
    }
    
    // D3D may legally query non-power-of-two sample counts as well
    VkSampleCountFlagBits sampleCountFlag = VK_SAMPLE_COUNT_1_BIT;
    
    if (FAILED(DecodeSampleCount(SampleCount, &sampleCountFlag)))
      return E_INVALIDARG;
    
    // Check if the device supports the given combination of format
    // and sample count. D3D exposes the opaque concept of quality
    // levels to the application, we'll just define one such level.
    VkImageFormatProperties formatProps;
    
    VkResult status = m_dxvkAdapter->imageFormatProperties(
      format, VK_IMAGE_TYPE_2D, VK_IMAGE_TILING_OPTIMAL,
      VK_IMAGE_USAGE_SAMPLED_BIT, 0, formatProps);
    
    if ((status == VK_SUCCESS) && (formatProps.sampleCounts & sampleCountFlag))
      *pNumQualityLevels = 1;
    return S_OK;
  }
  
  
  void STDMETHODCALLTYPE D3D11Device::CheckCounterInfo(D3D11_COUNTER_INFO* pCounterInfo) {
    // We basically don't support counters
    pCounterInfo->LastDeviceDependentCounter  = D3D11_COUNTER(0);
    pCounterInfo->NumSimultaneousCounters     = 0;
    pCounterInfo->NumDetectableParallelUnits  = 0;
  }
  
  
  HRESULT STDMETHODCALLTYPE D3D11Device::CheckCounter(
    const D3D11_COUNTER_DESC* pDesc,
          D3D11_COUNTER_TYPE* pType,
          UINT*               pActiveCounters,
          LPSTR               szName,
          UINT*               pNameLength,
          LPSTR               szUnits,
          UINT*               pUnitsLength,
          LPSTR               szDescription,
          UINT*               pDescriptionLength) {
    Logger::err("D3D11: Counters not supported");
    return E_INVALIDARG;
  }
  
  
  HRESULT STDMETHODCALLTYPE D3D11Device::CheckFeatureSupport(
          D3D11_FEATURE Feature,
          void*         pFeatureSupportData,
          UINT          FeatureSupportDataSize) {
    switch (Feature) {
      case D3D11_FEATURE_THREADING: {
        if (FeatureSupportDataSize != sizeof(D3D11_FEATURE_DATA_THREADING))
          return E_INVALIDARG;
        
        // We report native support for command lists here so that we do not actually
        // have to re-implement the UpdateSubresource bug from the D3D11 runtime, see
        // https://msdn.microsoft.com/en-us/library/windows/desktop/ff476486(v=vs.85).aspx)
        auto info = static_cast<D3D11_FEATURE_DATA_THREADING*>(pFeatureSupportData);
        info->DriverConcurrentCreates = TRUE;
        info->DriverCommandLists      = TRUE;
      } return S_OK;
      
      case D3D11_FEATURE_DOUBLES: {
        if (FeatureSupportDataSize != sizeof(D3D11_FEATURE_DATA_DOUBLES))
          return E_INVALIDARG;
        
        auto info = static_cast<D3D11_FEATURE_DATA_DOUBLES*>(pFeatureSupportData);
        info->DoublePrecisionFloatShaderOps = m_dxvkDevice->features().core.features.shaderFloat64
                                           && m_dxvkDevice->features().core.features.shaderInt64;
      } return S_OK;
      
      case D3D11_FEATURE_FORMAT_SUPPORT: {
        if (FeatureSupportDataSize != sizeof(D3D11_FEATURE_DATA_FORMAT_SUPPORT))
          return E_INVALIDARG;
        
        auto info = static_cast<D3D11_FEATURE_DATA_FORMAT_SUPPORT*>(pFeatureSupportData);
        return GetFormatSupportFlags(info->InFormat, &info->OutFormatSupport, nullptr);
      } return S_OK;
      
      case D3D11_FEATURE_FORMAT_SUPPORT2: {
        if (FeatureSupportDataSize != sizeof(D3D11_FEATURE_DATA_FORMAT_SUPPORT2))
          return E_INVALIDARG;
        
        auto info = static_cast<D3D11_FEATURE_DATA_FORMAT_SUPPORT2*>(pFeatureSupportData);
        return GetFormatSupportFlags(info->InFormat, nullptr, &info->OutFormatSupport2);
      } return S_OK;
      
      case D3D11_FEATURE_D3D10_X_HARDWARE_OPTIONS: {
        if (FeatureSupportDataSize != sizeof(D3D11_FEATURE_DATA_D3D10_X_HARDWARE_OPTIONS))
          return E_INVALIDARG;
        
        auto info = static_cast<D3D11_FEATURE_DATA_D3D10_X_HARDWARE_OPTIONS*>(pFeatureSupportData);
        info->ComputeShaders_Plus_RawAndStructuredBuffers_Via_Shader_4_x = TRUE;
      } return S_OK;
      
      case D3D11_FEATURE_D3D11_OPTIONS: {
        if (FeatureSupportDataSize != sizeof(D3D11_FEATURE_DATA_D3D11_OPTIONS))
          return E_INVALIDARG;
        
        // TODO implement, most of these are required for FL 11.1
        // https://msdn.microsoft.com/en-us/library/windows/desktop/hh404457(v=vs.85).aspx
        const DxvkDeviceFeatures& features = m_dxvkDevice->features();
        
        auto info = static_cast<D3D11_FEATURE_DATA_D3D11_OPTIONS*>(pFeatureSupportData);
        info->OutputMergerLogicOp                     = features.core.features.logicOp;
        info->UAVOnlyRenderingForcedSampleCount       = FALSE;
        info->DiscardAPIsSeenByDriver                 = TRUE;
        info->FlagsForUpdateAndCopySeenByDriver       = TRUE;
        info->ClearView                               = TRUE;
        info->CopyWithOverlap                         = FALSE;
        info->ConstantBufferPartialUpdate             = TRUE;
        info->ConstantBufferOffsetting                = TRUE;
        info->MapNoOverwriteOnDynamicConstantBuffer   = TRUE;
        info->MapNoOverwriteOnDynamicBufferSRV        = TRUE;
        info->MultisampleRTVWithForcedSampleCountOne  = FALSE;
        info->SAD4ShaderInstructions                  = FALSE;
        info->ExtendedDoublesShaderInstructions       = TRUE;
        info->ExtendedResourceSharing                 = FALSE;
      } return S_OK;

      case D3D11_FEATURE_ARCHITECTURE_INFO: {
        if (FeatureSupportDataSize != sizeof(D3D11_FEATURE_DATA_ARCHITECTURE_INFO))
          return E_INVALIDARG;

        auto info = static_cast<D3D11_FEATURE_DATA_ARCHITECTURE_INFO*>(pFeatureSupportData);
        info->TileBasedDeferredRenderer = FALSE;
      } return S_OK;
      
      case D3D11_FEATURE_SHADER_MIN_PRECISION_SUPPORT: {
        if (FeatureSupportDataSize != sizeof(D3D11_FEATURE_DATA_SHADER_MIN_PRECISION_SUPPORT))
          return E_INVALIDARG;
        
        // Report that we only support full 32-bit operations
        auto info = static_cast<D3D11_FEATURE_DATA_SHADER_MIN_PRECISION_SUPPORT*>(pFeatureSupportData);
        info->PixelShaderMinPrecision          = 0;
        info->AllOtherShaderStagesMinPrecision = 0;
      } return S_OK;
      
      case D3D11_FEATURE_D3D9_SHADOW_SUPPORT: {
        if (FeatureSupportDataSize != sizeof(D3D11_FEATURE_DATA_D3D9_SHADOW_SUPPORT))
          return E_INVALIDARG;
        
        auto info = static_cast<D3D11_FEATURE_DATA_D3D9_SHADOW_SUPPORT*>(pFeatureSupportData);
        info->SupportsDepthAsTextureWithLessEqualComparisonFilter = TRUE;
        return S_OK;
      } break;

      default:
        Logger::err(str::format(
          "D3D11Device: CheckFeatureSupport: Unknown feature: ",
          Feature));
        return E_NOTIMPL;
    }
  }
  
  
  HRESULT STDMETHODCALLTYPE D3D11Device::GetPrivateData(
          REFGUID guid, UINT* pDataSize, void* pData) {
    return m_container->GetPrivateData(guid, pDataSize, pData);
  }
  
  
  HRESULT STDMETHODCALLTYPE D3D11Device::SetPrivateData(
          REFGUID guid, UINT DataSize, const void* pData) {
    return m_container->SetPrivateData(guid, DataSize, pData);
  }
  
  
  HRESULT STDMETHODCALLTYPE D3D11Device::SetPrivateDataInterface(
          REFGUID guid, const IUnknown* pData) {
    return m_container->SetPrivateDataInterface(guid, pData);
  }
  
  
  D3D_FEATURE_LEVEL STDMETHODCALLTYPE D3D11Device::GetFeatureLevel() {
    return m_featureLevel;
  }
  
  
  UINT STDMETHODCALLTYPE D3D11Device::GetCreationFlags() {
    return m_featureFlags;
  }
  
  
  HRESULT STDMETHODCALLTYPE D3D11Device::GetDeviceRemovedReason() {
    static std::atomic<bool> s_errorShown = { false };
    
    if (!s_errorShown.exchange(true))
      Logger::warn("D3D11Device::GetDeviceRemovedReason: Stub");
    
    return S_OK;
  }
  
  
  void STDMETHODCALLTYPE D3D11Device::GetImmediateContext(ID3D11DeviceContext** ppImmediateContext) {
    *ppImmediateContext = ref(m_context);
  }


  void STDMETHODCALLTYPE D3D11Device::GetImmediateContext1(ID3D11DeviceContext1 ** ppImmediateContext) {
    *ppImmediateContext = ref(m_context);
  }
  
  
  HRESULT STDMETHODCALLTYPE D3D11Device::SetExceptionMode(UINT RaiseFlags) {
    Logger::err("D3D11Device::SetExceptionMode: Not implemented");
    return E_NOTIMPL;
  }
  
  
  UINT STDMETHODCALLTYPE D3D11Device::GetExceptionMode() {
    Logger::err("D3D11Device::GetExceptionMode: Not implemented");
    return 0;
  }
  
  
  DXGI_VK_FORMAT_INFO D3D11Device::LookupFormat(
          DXGI_FORMAT           Format,
          DXGI_VK_FORMAT_MODE   Mode) const {
    return m_dxgiAdapter->LookupFormat(Format, Mode);
  }
  
  
  DXGI_VK_FORMAT_FAMILY D3D11Device::LookupFamily(
          DXGI_FORMAT           Format,
          DXGI_VK_FORMAT_MODE   Mode) const {
    return m_dxgiAdapter->LookupFormatFamily(Format, Mode);
  }
  
  
  void D3D11Device::FlushInitContext() {
    m_initializer->Flush();
  }
  
  
  VkPipelineStageFlags D3D11Device::GetEnabledShaderStages() const {
    VkPipelineStageFlags enabledShaderPipelineStages
      = VK_PIPELINE_STAGE_VERTEX_SHADER_BIT
      | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT
      | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
    
    if (m_dxvkDevice->features().core.features.geometryShader)
      enabledShaderPipelineStages |= VK_PIPELINE_STAGE_GEOMETRY_SHADER_BIT;
    
    if (m_dxvkDevice->features().core.features.tessellationShader) {
      enabledShaderPipelineStages |= VK_PIPELINE_STAGE_TESSELLATION_CONTROL_SHADER_BIT
                                  |  VK_PIPELINE_STAGE_TESSELLATION_EVALUATION_SHADER_BIT;
    }
    
    return enabledShaderPipelineStages;
  }
  
  
  bool D3D11Device::CheckFeatureLevelSupport(
    const Rc<DxvkAdapter>&  adapter,
          D3D_FEATURE_LEVEL featureLevel) {
    // We currently only support 11_0 interfaces
    if (featureLevel > GetMaxFeatureLevel(adapter))
      return false;
    
    // Check whether all features are supported
    const DxvkDeviceFeatures features
      = GetDeviceFeatures(adapter, featureLevel);
    
    if (!adapter->checkFeatureSupport(features))
      return false;
    
    // TODO also check for required limits
    return true;
  }
  
  
  DxvkDeviceFeatures D3D11Device::GetDeviceFeatures(
    const Rc<DxvkAdapter>&  adapter,
          D3D_FEATURE_LEVEL featureLevel) {
    DxvkDeviceFeatures supported = adapter->features();
    DxvkDeviceFeatures enabled   = {};

    enabled.core.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2_KHR;
    enabled.core.pNext = nullptr;
    
    if (featureLevel >= D3D_FEATURE_LEVEL_9_1) {
      enabled.core.features.depthClamp                            = VK_TRUE;
      enabled.core.features.depthBiasClamp                        = VK_TRUE;
      enabled.core.features.fillModeNonSolid                      = VK_TRUE;
      enabled.core.features.pipelineStatisticsQuery               = supported.core.features.pipelineStatisticsQuery;
      enabled.core.features.sampleRateShading                     = VK_TRUE;
      enabled.core.features.samplerAnisotropy                     = VK_TRUE;
      enabled.core.features.shaderClipDistance                    = VK_TRUE;
      enabled.core.features.shaderCullDistance                    = VK_TRUE;
      enabled.core.features.robustBufferAccess                    = VK_TRUE;
    }
    
    if (featureLevel >= D3D_FEATURE_LEVEL_9_2) {
      enabled.core.features.occlusionQueryPrecise                 = VK_TRUE;
    }
    
    if (featureLevel >= D3D_FEATURE_LEVEL_9_3) {
      enabled.core.features.multiViewport                         = VK_TRUE;
      enabled.core.features.independentBlend                      = VK_TRUE;
    }
    
    if (featureLevel >= D3D_FEATURE_LEVEL_10_0) {
      enabled.core.features.fullDrawIndexUint32                   = VK_TRUE;
      enabled.core.features.fragmentStoresAndAtomics              = VK_TRUE;
      enabled.core.features.geometryShader                        = VK_TRUE;
      enabled.core.features.logicOp                               = supported.core.features.logicOp;
      enabled.core.features.shaderImageGatherExtended             = VK_TRUE;
      enabled.core.features.textureCompressionBC                  = VK_TRUE;
    }
    
    if (featureLevel >= D3D_FEATURE_LEVEL_10_1) {
      enabled.core.features.dualSrcBlend                          = VK_TRUE;
      enabled.core.features.imageCubeArray                        = VK_TRUE;
    }
    
    if (featureLevel >= D3D_FEATURE_LEVEL_11_0) {
      enabled.core.features.drawIndirectFirstInstance             = VK_TRUE;
      enabled.core.features.shaderFloat64                         = supported.core.features.shaderFloat64;
      enabled.core.features.shaderInt64                           = supported.core.features.shaderInt64;
      enabled.core.features.tessellationShader                    = VK_TRUE;
      // TODO enable unconditionally once RADV gains support
      enabled.core.features.shaderStorageImageMultisample         = supported.core.features.shaderStorageImageMultisample;
      enabled.core.features.shaderStorageImageReadWithoutFormat   = supported.core.features.shaderStorageImageReadWithoutFormat;
      enabled.core.features.shaderStorageImageWriteWithoutFormat  = VK_TRUE;
    }
    
    if (featureLevel >= D3D_FEATURE_LEVEL_11_1) {
      enabled.core.features.logicOp                               = VK_TRUE;
      enabled.core.features.vertexPipelineStoresAndAtomics        = VK_TRUE;
    }
    
    return enabled;
  }
  
  
  Rc<D3D11CounterBuffer> D3D11Device::CreateUAVCounterBuffer() {
    // UAV counters are going to be used as raw storage buffers, so
    // we need to align them to the minimum SSBO offset alignment
    const auto& devInfo = m_dxvkAdapter->deviceProperties();

    VkDeviceSize uavCounterSliceLength = align<VkDeviceSize>(
      sizeof(D3D11UavCounter), devInfo.limits.minStorageBufferOffsetAlignment);

    DxvkBufferCreateInfo uavCounterInfo;
    uavCounterInfo.size   = 4096 * uavCounterSliceLength;
    uavCounterInfo.usage  = VK_BUFFER_USAGE_TRANSFER_DST_BIT
                          | VK_BUFFER_USAGE_TRANSFER_SRC_BIT
                          | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    uavCounterInfo.stages = VK_PIPELINE_STAGE_TRANSFER_BIT
                          | GetEnabledShaderStages();
    uavCounterInfo.access = VK_ACCESS_TRANSFER_READ_BIT
                          | VK_ACCESS_TRANSFER_WRITE_BIT
                          | VK_ACCESS_SHADER_READ_BIT
                          | VK_ACCESS_SHADER_WRITE_BIT;
    
    return new D3D11CounterBuffer(m_dxvkDevice,
      uavCounterInfo, uavCounterSliceLength);
  }
  
  
  HRESULT D3D11Device::CreateShaderModule(
          D3D11CommonShader*      pShaderModule,
    const void*                   pShaderBytecode,
          size_t                  BytecodeLength,
          ID3D11ClassLinkage*     pClassLinkage,
    const DxbcModuleInfo*         pModuleInfo,
          DxbcProgramType         ProgramType) {
    if (pClassLinkage != nullptr)
      Logger::warn("D3D11Device::CreateShaderModule: Class linkage not supported");
    
    try {
      *pShaderModule = m_shaderModules.GetShaderModule(this,
        pModuleInfo, pShaderBytecode, BytecodeLength, ProgramType);
      return S_OK;
    } catch (const DxvkError& e) {
      Logger::err(e.message());
      return E_FAIL;
    }
  }


  HRESULT D3D11Device::GetFormatSupportFlags(DXGI_FORMAT Format, UINT* pFlags1, UINT* pFlags2) const {
    // Query some general information from DXGI, DXVK and Vulkan about the format
    const DXGI_VK_FORMAT_INFO fmtMapping = m_dxgiAdapter->LookupFormat(Format, DXGI_VK_FORMAT_MODE_ANY);
    const VkFormatProperties  fmtSupport = m_dxvkAdapter->formatProperties(fmtMapping.Format);
    const DxvkFormatInfo*     fmtProperties = imageFormatInfo(fmtMapping.Format);
    
    // Reset output flags preemptively
    if (pFlags1 != nullptr) *pFlags1 = 0;
    if (pFlags2 != nullptr) *pFlags2 = 0;
    
    // Unsupported or invalid format
    if (fmtMapping.Format == VK_FORMAT_UNDEFINED)
      return E_INVALIDARG;
    
    UINT flags1 = 0;
    UINT flags2 = 0;
    
    // Format can be used for shader resource views with buffers
    if (fmtSupport.bufferFeatures & VK_FORMAT_FEATURE_UNIFORM_TEXEL_BUFFER_BIT)
      flags1 |= D3D11_FORMAT_SUPPORT_BUFFER;
    
    // Format can be used for vertex data
    if (fmtSupport.bufferFeatures & VK_FORMAT_FEATURE_VERTEX_BUFFER_BIT)
      flags1 |= D3D11_FORMAT_SUPPORT_IA_VERTEX_BUFFER;
    
    // Format can be used for index data. Only
    // these two formats are supported by D3D11.
    if (Format == DXGI_FORMAT_R16_UINT
     || Format == DXGI_FORMAT_R32_UINT)
      flags1 |= D3D11_FORMAT_SUPPORT_IA_INDEX_BUFFER;
    
    // TODO implement stream output
    // D3D11_FORMAT_SUPPORT_SO_BUFFER
    
    if (fmtSupport.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT
     || fmtSupport.optimalTilingFeatures & VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT) {
      const VkFormat depthFormat = m_dxgiAdapter->LookupFormat(Format, DXGI_VK_FORMAT_MODE_DEPTH).Format;
      
      if (GetImageTypeSupport(fmtMapping.Format, VK_IMAGE_TYPE_1D)) flags1 |= D3D11_FORMAT_SUPPORT_TEXTURE1D;
      if (GetImageTypeSupport(fmtMapping.Format, VK_IMAGE_TYPE_2D)) flags1 |= D3D11_FORMAT_SUPPORT_TEXTURE2D;
      if (GetImageTypeSupport(fmtMapping.Format, VK_IMAGE_TYPE_3D)) flags1 |= D3D11_FORMAT_SUPPORT_TEXTURE3D;
      
      flags1 |= D3D11_FORMAT_SUPPORT_MIP
             |  D3D11_FORMAT_SUPPORT_CPU_LOCKABLE
             |  D3D11_FORMAT_SUPPORT_CAST_WITHIN_BIT_LAYOUT;
    
      // Format can be read 
      if (fmtSupport.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT) {
        flags1 |= D3D11_FORMAT_SUPPORT_TEXTURECUBE
               |  D3D11_FORMAT_SUPPORT_SHADER_LOAD
               |  D3D11_FORMAT_SUPPORT_SHADER_GATHER
               |  D3D11_FORMAT_SUPPORT_SHADER_SAMPLE;
        
        if (depthFormat != VK_FORMAT_UNDEFINED) {
          flags1 |= D3D11_FORMAT_SUPPORT_SHADER_GATHER_COMPARISON
                 |  D3D11_FORMAT_SUPPORT_SHADER_SAMPLE_COMPARISON;
        }
      }
      
      // Format is a color format that can be used for rendering
      if (fmtSupport.optimalTilingFeatures & VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT) {
        flags1 |= D3D11_FORMAT_SUPPORT_RENDER_TARGET
               |  D3D11_FORMAT_SUPPORT_MIP_AUTOGEN;
        
        if (m_dxvkDevice->features().core.features.logicOp)
          flags2 |= D3D11_FORMAT_SUPPORT2_OUTPUT_MERGER_LOGIC_OP;
      }
      
      // Format supports blending when used for rendering
      if (fmtSupport.optimalTilingFeatures & VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BLEND_BIT)
        flags1 |= D3D11_FORMAT_SUPPORT_BLENDABLE;
      
      // Format is a depth-stencil format that can be used for rendering
      if (fmtSupport.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT)
        flags1 |= D3D11_FORMAT_SUPPORT_DEPTH_STENCIL;
      
      // FIXME implement properly. This would require a VkSurface.
      if (Format == DXGI_FORMAT_R8G8B8A8_UNORM
       || Format == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB
       || Format == DXGI_FORMAT_B8G8R8A8_UNORM
       || Format == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB
       || Format == DXGI_FORMAT_R16G16B16A16_FLOAT
       || Format == DXGI_FORMAT_R10G10B10A2_UNORM
       || Format == DXGI_FORMAT_R10G10B10_XR_BIAS_A2_UNORM)
        flags1 |= D3D11_FORMAT_SUPPORT_DISPLAY;
      
      // Query multisample support for this format
      VkImageFormatProperties imgFmtProperties;
      
      VkResult status = m_dxvkAdapter->imageFormatProperties(fmtMapping.Format,
        VK_IMAGE_TYPE_2D, VK_IMAGE_TILING_OPTIMAL,
        (fmtProperties->aspectMask & VK_IMAGE_ASPECT_COLOR_BIT)
          ? VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT
          : VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
        0, imgFmtProperties);
      
      if (status == VK_SUCCESS && imgFmtProperties.sampleCounts > VK_SAMPLE_COUNT_1_BIT) {
        flags1 |= D3D11_FORMAT_SUPPORT_MULTISAMPLE_RENDERTARGET
               |  D3D11_FORMAT_SUPPORT_MULTISAMPLE_RESOLVE
               |  D3D11_FORMAT_SUPPORT_MULTISAMPLE_LOAD;
      }
    }
    
    // Format can be used for storage images or storage texel buffers
    if ((fmtSupport.bufferFeatures & VK_FORMAT_FEATURE_STORAGE_TEXEL_BUFFER_BIT)
     && (fmtSupport.optimalTilingFeatures & VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT)) {
      flags1 |= D3D11_FORMAT_SUPPORT_TYPED_UNORDERED_ACCESS_VIEW;
      flags2 |= D3D11_FORMAT_SUPPORT2_UAV_TYPED_STORE;
      
      if (m_dxvkDevice->features().core.features.shaderStorageImageReadWithoutFormat
       || Format == DXGI_FORMAT_R32_UINT
       || Format == DXGI_FORMAT_R32_SINT
       || Format == DXGI_FORMAT_R32_FLOAT)
        flags2 |= D3D11_FORMAT_SUPPORT2_UAV_TYPED_LOAD;
      
      if (Format == DXGI_FORMAT_R32_UINT
       || Format == DXGI_FORMAT_R32_SINT) {
        flags2 |= D3D11_FORMAT_SUPPORT2_UAV_ATOMIC_ADD
               |  D3D11_FORMAT_SUPPORT2_UAV_ATOMIC_BITWISE_OPS
               |  D3D11_FORMAT_SUPPORT2_UAV_ATOMIC_COMPARE_STORE_OR_COMPARE_EXCHANGE
               |  D3D11_FORMAT_SUPPORT2_UAV_ATOMIC_EXCHANGE;
      }
      
      if (Format == DXGI_FORMAT_R32_SINT)
        flags2 |= D3D11_FORMAT_SUPPORT2_UAV_ATOMIC_SIGNED_MIN_OR_MAX;
      
      if (Format == DXGI_FORMAT_R32_UINT)
        flags2 |= D3D11_FORMAT_SUPPORT2_UAV_ATOMIC_UNSIGNED_MIN_OR_MAX;
    }
    
    // Write back format support flags
    if (pFlags1 != nullptr) *pFlags1 = flags1;
    if (pFlags2 != nullptr) *pFlags2 = flags2;
    return S_OK;
  }
  
  
  BOOL D3D11Device::GetImageTypeSupport(VkFormat Format, VkImageType Type) const {
    VkImageFormatProperties props;
    
    VkResult status = m_dxvkAdapter->imageFormatProperties(
      Format, Type, VK_IMAGE_TILING_OPTIMAL,
      VK_IMAGE_USAGE_SAMPLED_BIT, 0, props);
    
    return status == VK_SUCCESS;
  }
  
  
  D3D_FEATURE_LEVEL D3D11Device::GetMaxFeatureLevel(const Rc<DxvkAdapter>& Adapter) {
    static const std::array<std::pair<std::string, D3D_FEATURE_LEVEL>, 7> s_featureLevels = {{
      { "11_1", D3D_FEATURE_LEVEL_11_1 },
      { "11_0", D3D_FEATURE_LEVEL_11_0 },
      { "10_1", D3D_FEATURE_LEVEL_10_1 },
      { "10_0", D3D_FEATURE_LEVEL_10_0 },
      { "9_3",  D3D_FEATURE_LEVEL_9_3  },
      { "9_2",  D3D_FEATURE_LEVEL_9_2  },
      { "9_1",  D3D_FEATURE_LEVEL_9_1  },
    }};
    
    const std::string maxLevel = Adapter->instance()->config()
      .getOption<std::string>("d3d11.maxFeatureLevel");
    
    auto entry = std::find_if(s_featureLevels.begin(), s_featureLevels.end(),
      [&] (const std::pair<std::string, D3D_FEATURE_LEVEL>& pair) {
        return pair.first == maxLevel;
      });
    
    return entry != s_featureLevels.end()
      ? entry->second
      : D3D_FEATURE_LEVEL_11_0;
  }
  
}
