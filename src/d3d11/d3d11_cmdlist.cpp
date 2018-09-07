#include "d3d11_cmdlist.h"
#include "d3d11_device.h"

namespace dxvk {
    
  D3D11CommandList::D3D11CommandList(
          D3D11Device*  pDevice,
          UINT          ContextFlags)
  : m_device      (pDevice),
    m_contextFlags(ContextFlags) { }
  
  
  D3D11CommandList::~D3D11CommandList() {
    
  }
  
  
  HRESULT STDMETHODCALLTYPE D3D11CommandList::QueryInterface(REFIID riid, void** ppvObject) {
    *ppvObject = nullptr;
    
    if (riid == __uuidof(IUnknown)
     || riid == __uuidof(ID3D11DeviceChild)
     || riid == __uuidof(ID3D11CommandList)) {
      *ppvObject = ref(this);
      return S_OK;
    }
    
    Logger::warn("D3D11CommandList::QueryInterface: Unknown interface query");
    Logger::warn(str::format(riid));
    return E_NOINTERFACE;
  }
  
  
  void STDMETHODCALLTYPE D3D11CommandList::GetDevice(ID3D11Device **ppDevice) {
    *ppDevice = ref(m_device);
  }
  
  
  UINT STDMETHODCALLTYPE D3D11CommandList::GetContextFlags() {
    return m_contextFlags;
  }
  
  
  void D3D11CommandList::AddChunk(DxvkCsChunkRef&& Chunk) {
    m_chunks.push_back(std::move(Chunk));
  }
  
  
  void D3D11CommandList::EmitToCommandList(ID3D11CommandList* pCommandList) {
    auto cmdList = static_cast<D3D11CommandList*>(pCommandList);
    
    for (const auto& chunk : m_chunks)
      cmdList->m_chunks.push_back(chunk);
    
    MarkSubmitted();
  }
  
  
  void D3D11CommandList::EmitToCsThread(DxvkCsThread* CsThread) {
    for (const auto& chunk : m_chunks)
      CsThread->dispatchChunk(DxvkCsChunkRef(chunk));
    
    MarkSubmitted();
  }
  
  
  void D3D11CommandList::MarkSubmitted() {
    if (m_submitted.exchange(true) && !m_warned.exchange(true)) {
      Logger::warn(
        "D3D11: Command list submitted multiple times.\n"
        "       This is currently not supported.");
    }
  }
  
}