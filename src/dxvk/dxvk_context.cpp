#include <cstring>

#include "dxvk_device.h"
#include "dxvk_context.h"
#include "dxvk_main.h"

namespace dxvk {
  
  DxvkContext::DxvkContext(
    const Rc<DxvkDevice>&             device,
    const Rc<DxvkPipelineManager>&    pipelineManager,
    const Rc<DxvkMetaClearObjects>&   metaClearObjects,
    const Rc<DxvkMetaMipGenObjects>&  metaMipGenObjects,
    const Rc<DxvkMetaResolveObjects>& metaResolveObjects)
  : m_device      (device),
    m_pipeMgr     (pipelineManager),
    m_metaClear   (metaClearObjects),
    m_metaMipGen  (metaMipGenObjects),
    m_metaResolve (metaResolveObjects),
    m_queries     (device->vkd()) { }
  
  
  DxvkContext::~DxvkContext() {
    
  }
  
  
  void DxvkContext::beginRecording(const Rc<DxvkCommandList>& cmdList) {
    m_cmd = cmdList;
    m_cmd->beginRecording();
    
    // The current state of the internal command buffer is
    // undefined, so we have to bind and set up everything
    // before any draw or dispatch command is recorded.
    m_flags.clr(
      DxvkContextFlag::GpRenderPassBound,
      DxvkContextFlag::GpClearRenderTargets);
    
    m_flags.set(
      DxvkContextFlag::GpDirtyPipeline,
      DxvkContextFlag::GpDirtyPipelineState,
      DxvkContextFlag::GpDirtyResources,
      DxvkContextFlag::GpDirtyVertexBuffers,
      DxvkContextFlag::GpDirtyIndexBuffer,
      DxvkContextFlag::CpDirtyPipeline,
      DxvkContextFlag::CpDirtyPipelineState,
      DxvkContextFlag::CpDirtyResources);
  }
  
  
  Rc<DxvkCommandList> DxvkContext::endRecording() {
    this->spillRenderPass();
    
    m_queries.trackQueryPools(m_cmd);

    m_barriers.recordCommands(m_cmd);

    m_cmd->endRecording();
    return std::exchange(m_cmd, nullptr);
  }
  
  
  void DxvkContext::beginQuery(const DxvkQueryRevision& query) {
    query.query->beginRecording(query.revision);
    m_queries.enableQuery(m_cmd, query);
  }
  
  
  void DxvkContext::endQuery(const DxvkQueryRevision& query) {
    m_queries.disableQuery(m_cmd, query);
    query.query->endRecording(query.revision);
  }
  
  
  void DxvkContext::bindRenderTargets(
    const DxvkRenderTargets&    targets,
          bool                  spill) {
    m_state.om.renderTargets = targets;
    
    // If necessary, perform clears on the active render targets
    if (m_flags.test(DxvkContextFlag::GpClearRenderTargets))
      this->startRenderPass();
    
    // Set up default render pass ops
    this->resetRenderPassOps(
      m_state.om.renderTargets,
      m_state.om.renderPassOps);
    
    if (m_state.om.framebuffer == nullptr || !m_state.om.framebuffer->hasTargets(targets)) {
      // Create a new framebuffer object next
      // time we start rendering something
      m_flags.set(DxvkContextFlag::GpDirtyFramebuffer);
    } else {
      // Don't redundantly spill the render pass if
      // the same render targets are bound again
      m_flags.clr(DxvkContextFlag::GpDirtyFramebuffer);
    }

    if (spill)
      this->spillRenderPass();
  }
  
  
  void DxvkContext::bindIndexBuffer(
    const DxvkBufferSlice&      buffer,
          VkIndexType           indexType) {
    if (!m_state.vi.indexBuffer.matches(buffer)
     || (m_state.vi.indexType != indexType)) {
      m_state.vi.indexBuffer = buffer;
      m_state.vi.indexType   = indexType;
      
      m_flags.set(DxvkContextFlag::GpDirtyIndexBuffer);
    }
  }
  
  
  void DxvkContext::bindResourceBuffer(
          uint32_t              slot,
    const DxvkBufferSlice&      buffer) {
    if (!m_rc[slot].bufferSlice.matches(buffer)) {
      m_rc[slot].sampler     = nullptr;
      m_rc[slot].imageView   = nullptr;
      m_rc[slot].bufferView  = nullptr;
      m_rc[slot].bufferSlice = buffer;
      
      m_flags.set(
        DxvkContextFlag::CpDirtyResources,
        DxvkContextFlag::GpDirtyResources);
    }
  }
  
  
  void DxvkContext::bindResourceView(
          uint32_t              slot,
    const Rc<DxvkImageView>&    imageView,
    const Rc<DxvkBufferView>&   bufferView) {
    if (m_rc[slot].imageView  != imageView
     || m_rc[slot].bufferView != bufferView) {
      m_rc[slot].sampler     = nullptr;
      m_rc[slot].imageView   = imageView;
      m_rc[slot].bufferView  = bufferView;
      m_rc[slot].bufferSlice = DxvkBufferSlice();
      
      m_flags.set(
        DxvkContextFlag::CpDirtyResources,
        DxvkContextFlag::GpDirtyResources);
    }
  }
  
  
  void DxvkContext::bindResourceSampler(
          uint32_t              slot,
    const Rc<DxvkSampler>&      sampler) {
    if (m_rc[slot].sampler != sampler) {
      m_rc[slot].sampler     = sampler;
      m_rc[slot].imageView   = nullptr;
      m_rc[slot].bufferView  = nullptr;
      m_rc[slot].bufferSlice = DxvkBufferSlice();
      
      m_flags.set(
        DxvkContextFlag::CpDirtyResources,
        DxvkContextFlag::GpDirtyResources);
    }
  }
  
  
  void DxvkContext::bindShader(
          VkShaderStageFlagBits stage,
    const Rc<DxvkShader>&       shader) {
    DxvkShaderStage* shaderStage = nullptr;
    
    switch (stage) {
      case VK_SHADER_STAGE_VERTEX_BIT:                  shaderStage = &m_state.gp.vs;  break;
      case VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT:    shaderStage = &m_state.gp.tcs; break;
      case VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT: shaderStage = &m_state.gp.tes; break;
      case VK_SHADER_STAGE_GEOMETRY_BIT:                shaderStage = &m_state.gp.gs;  break;
      case VK_SHADER_STAGE_FRAGMENT_BIT:                shaderStage = &m_state.gp.fs;  break;
      case VK_SHADER_STAGE_COMPUTE_BIT:                 shaderStage = &m_state.cp.cs;  break;
      default: return;
    }
    
    if (shaderStage->shader != shader) {
      shaderStage->shader = shader;
      
      if (stage == VK_SHADER_STAGE_COMPUTE_BIT) {
        m_flags.set(
          DxvkContextFlag::CpDirtyPipeline,
          DxvkContextFlag::CpDirtyPipelineState,
          DxvkContextFlag::CpDirtyResources);
      } else {
        m_flags.set(
          DxvkContextFlag::GpDirtyPipeline,
          DxvkContextFlag::GpDirtyPipelineState,
          DxvkContextFlag::GpDirtyResources);
      }
    }
  }
  
  
  void DxvkContext::bindVertexBuffer(
          uint32_t              binding,
    const DxvkBufferSlice&      buffer,
          uint32_t              stride) {
    if (!m_state.vi.vertexBuffers[binding].matches(buffer)) {
      m_state.vi.vertexBuffers[binding] = buffer;
      m_flags.set(DxvkContextFlag::GpDirtyVertexBuffers);
    }
    
    if (m_state.vi.vertexStrides[binding] != stride) {
      m_state.vi.vertexStrides[binding] = stride;
      m_flags.set(DxvkContextFlag::GpDirtyPipelineState);
    }
  }
  
  
  void DxvkContext::clearBuffer(
    const Rc<DxvkBuffer>&       buffer,
          VkDeviceSize          offset,
          VkDeviceSize          length,
          uint32_t              value) {
    this->spillRenderPass();
    
    if (length == buffer->info().size)
      length = align(length, 4);
    
    auto slice = buffer->subSlice(offset, length);

    if (m_barriers.isBufferDirty(slice, DxvkAccess::Write))
      m_barriers.recordCommands(m_cmd);
    
    m_cmd->cmdFillBuffer(
      slice.handle(),
      slice.offset(),
      slice.length(),
      value);
    
    m_barriers.accessBuffer(slice,
      VK_PIPELINE_STAGE_TRANSFER_BIT,
      VK_ACCESS_TRANSFER_WRITE_BIT,
      buffer->info().stages,
      buffer->info().access);
    
    m_cmd->trackResource(slice.resource());
  }
  
  
  void DxvkContext::clearBufferView(
    const Rc<DxvkBufferView>&   bufferView,
          VkDeviceSize          offset,
          VkDeviceSize          length,
          VkClearColorValue     value) {
    this->spillRenderPass();
    this->unbindComputePipeline();

    auto bufferSlice = bufferView->physicalSlice();
    
    if (m_barriers.isBufferDirty(bufferSlice, DxvkAccess::Write))
      m_barriers.recordCommands(m_cmd);
    
    // Query pipeline objects to use for this clear operation
    DxvkMetaClearPipeline pipeInfo = m_metaClear->getClearBufferPipeline(
      imageFormatInfo(bufferView->info().format)->flags);
    
    // Create a descriptor set pointing to the view
    VkBufferView viewObject = bufferView->handle();
    
    VkDescriptorSet descriptorSet =
      m_cmd->allocateDescriptorSet(pipeInfo.dsetLayout);
    
    VkWriteDescriptorSet descriptorWrite;
    descriptorWrite.sType            = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrite.pNext            = nullptr;
    descriptorWrite.dstSet           = descriptorSet;
    descriptorWrite.dstBinding       = 0;
    descriptorWrite.dstArrayElement  = 0;
    descriptorWrite.descriptorCount  = 1;
    descriptorWrite.descriptorType   = VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER;
    descriptorWrite.pImageInfo       = nullptr;
    descriptorWrite.pBufferInfo      = nullptr;
    descriptorWrite.pTexelBufferView = &viewObject;
    m_cmd->updateDescriptorSets(1, &descriptorWrite);
    
    // Prepare shader arguments
    DxvkMetaClearArgs pushArgs;
    pushArgs.clearValue = value;
    pushArgs.offset = VkOffset3D {  int32_t(offset), 0, 0 };
    pushArgs.extent = VkExtent3D { uint32_t(length), 1, 1 };
    
    VkExtent3D workgroups = util::computeBlockCount(
      pushArgs.extent, pipeInfo.workgroupSize);
    
    m_cmd->cmdBindPipeline(
      VK_PIPELINE_BIND_POINT_COMPUTE,
      pipeInfo.pipeline);
    m_cmd->cmdBindDescriptorSet(
      VK_PIPELINE_BIND_POINT_COMPUTE,
      pipeInfo.pipeLayout, descriptorSet,
      0, nullptr);
    m_cmd->cmdPushConstants(
      pipeInfo.pipeLayout,
      VK_SHADER_STAGE_COMPUTE_BIT,
      0, sizeof(pushArgs), &pushArgs);
    m_cmd->cmdDispatch(
      workgroups.width,
      workgroups.height,
      workgroups.depth);
    
    m_barriers.accessBuffer(
      bufferSlice,
      VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
      VK_ACCESS_SHADER_WRITE_BIT,
      bufferView->bufferInfo().stages,
      bufferView->bufferInfo().access);
    
    m_cmd->trackResource(bufferView->viewResource());
    m_cmd->trackResource(bufferView->bufferResource());
  }
  
  
  void DxvkContext::clearColorImage(
    const Rc<DxvkImage>&            image,
    const VkClearColorValue&        value,
    const VkImageSubresourceRange&  subresources) {
    this->spillRenderPass();

    m_barriers.recordCommands(m_cmd);
    
    VkImageLayout imageLayoutClear = image->pickLayout(VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

    m_barriers.accessImage(image, subresources,
      VK_IMAGE_LAYOUT_UNDEFINED,
      image->info().stages,
      image->info().access,
      imageLayoutClear,
      VK_PIPELINE_STAGE_TRANSFER_BIT,
      VK_ACCESS_TRANSFER_WRITE_BIT);

    m_barriers.recordCommands(m_cmd);
    
    m_cmd->cmdClearColorImage(image->handle(),
      imageLayoutClear, &value, 1, &subresources);
    
    m_barriers.accessImage(image, subresources,
      imageLayoutClear,
      VK_PIPELINE_STAGE_TRANSFER_BIT,
      VK_ACCESS_TRANSFER_WRITE_BIT,
      image->info().layout,
      image->info().stages,
      image->info().access);
    
    m_cmd->trackResource(image);
  }
  
  
  void DxvkContext::clearDepthStencilImage(
    const Rc<DxvkImage>&            image,
    const VkClearDepthStencilValue& value,
    const VkImageSubresourceRange&  subresources) {
    this->spillRenderPass();
    
    m_barriers.recordCommands(m_cmd);

    VkImageLayout imageLayoutInitial = image->info().layout;
    VkImageLayout imageLayoutClear   = image->pickLayout(VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

    if (subresources.aspectMask == image->formatInfo()->aspectMask)
      imageLayoutInitial = VK_IMAGE_LAYOUT_UNDEFINED;

    m_barriers.accessImage(
      image, subresources,
      imageLayoutInitial,
      image->info().stages,
      image->info().access,
      imageLayoutClear,
      VK_PIPELINE_STAGE_TRANSFER_BIT,
      VK_ACCESS_TRANSFER_WRITE_BIT);

    m_barriers.recordCommands(m_cmd);
    
    m_cmd->cmdClearDepthStencilImage(image->handle(),
      imageLayoutClear, &value, 1, &subresources);
    
    m_barriers.accessImage(
      image, subresources,
      imageLayoutClear,
      VK_PIPELINE_STAGE_TRANSFER_BIT,
      VK_ACCESS_TRANSFER_WRITE_BIT,
      image->info().layout,
      image->info().stages,
      image->info().access);
    
    m_cmd->trackResource(image);
  }
  
  
  void DxvkContext::clearRenderTarget(
    const Rc<DxvkImageView>&    imageView,
          VkImageAspectFlags    clearAspects,
    const VkClearValue&         clearValue) {
    this->updateFramebuffer();

    // Prepare attachment ops
    DxvkColorAttachmentOps colorOp;
    colorOp.loadOp        = VK_ATTACHMENT_LOAD_OP_LOAD;
    colorOp.loadLayout    = imageView->imageInfo().layout;
    colorOp.storeOp       = VK_ATTACHMENT_STORE_OP_STORE;
    colorOp.storeLayout   = imageView->imageInfo().layout;
    
    DxvkDepthAttachmentOps depthOp;
    depthOp.loadOpD       = VK_ATTACHMENT_LOAD_OP_LOAD;
    depthOp.loadOpS       = VK_ATTACHMENT_LOAD_OP_LOAD;
    depthOp.loadLayout    = imageView->imageInfo().layout;
    depthOp.storeOpD      = VK_ATTACHMENT_STORE_OP_STORE;
    depthOp.storeOpS      = VK_ATTACHMENT_STORE_OP_STORE;
    depthOp.storeLayout   = imageView->imageInfo().layout;
    
    if (clearAspects & VK_IMAGE_ASPECT_COLOR_BIT)
      colorOp.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    
    if (clearAspects & VK_IMAGE_ASPECT_DEPTH_BIT)
      depthOp.loadOpD = VK_ATTACHMENT_LOAD_OP_CLEAR;
    
    if (clearAspects & VK_IMAGE_ASPECT_STENCIL_BIT)
      depthOp.loadOpS = VK_ATTACHMENT_LOAD_OP_CLEAR;
    
    if (clearAspects == imageView->info().aspect) {
      colorOp.loadLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
      depthOp.loadLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
    }
    
    // Check whether the render target view is an attachment
    // of the current framebuffer. If not, we need to create
    // a temporary framebuffer.
    int32_t attachmentIndex = -1;
    
    if (m_state.om.framebuffer != nullptr)
      attachmentIndex = m_state.om.framebuffer->findAttachment(imageView);
    
    if (attachmentIndex < 0) {
      this->spillRenderPass();
      
      // Set up and bind a temporary framebuffer
      DxvkRenderTargets attachments;
      DxvkRenderPassOps ops;
      
      if (clearAspects & VK_IMAGE_ASPECT_COLOR_BIT) {
        attachments.color[0].view   = imageView;
        attachments.color[0].layout = imageView->pickLayout(VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        ops.colorOps[0] = colorOp;
      } else {
        attachments.depth.view   = imageView;
        attachments.depth.layout = imageView->pickLayout(VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
        ops.depthOps = depthOp;
      }
      
      this->renderPassBindFramebuffer(
        m_device->createFramebuffer(attachments),
        ops, 1, &clearValue);
      this->renderPassUnbindFramebuffer();
    } else if (m_flags.test(DxvkContextFlag::GpRenderPassBound)) {
      // Clear the attachment in quesion. For color images,
      // the attachment index for the current subpass is
      // equal to the render pass attachment index.
      VkClearAttachment clearInfo;
      clearInfo.aspectMask      = clearAspects;
      clearInfo.colorAttachment = attachmentIndex;
      clearInfo.clearValue      = clearValue;
      
      VkClearRect clearRect;
      clearRect.rect.offset.x       = 0;
      clearRect.rect.offset.y       = 0;
      clearRect.rect.extent.width   = imageView->mipLevelExtent(0).width;
      clearRect.rect.extent.height  = imageView->mipLevelExtent(0).height;
      clearRect.baseArrayLayer      = 0;
      clearRect.layerCount          = imageView->info().numLayers;

      m_cmd->cmdClearAttachments(1, &clearInfo, 1, &clearRect);
    } else {
      // Perform the clear when starting the render pass
      if (clearAspects & VK_IMAGE_ASPECT_COLOR_BIT) {
        m_state.om.renderPassOps.colorOps[attachmentIndex] = colorOp;
        m_state.om.clearValues[attachmentIndex].color = clearValue.color;
      }
      
      if (clearAspects & VK_IMAGE_ASPECT_DEPTH_BIT) {
        m_state.om.renderPassOps.depthOps.loadOpD  = depthOp.loadOpD;
        m_state.om.renderPassOps.depthOps.storeOpD = depthOp.storeOpD;
        m_state.om.clearValues[attachmentIndex].depthStencil.depth = clearValue.depthStencil.depth;
      }
      
      if (clearAspects & VK_IMAGE_ASPECT_STENCIL_BIT) {
        m_state.om.renderPassOps.depthOps.loadOpS  = depthOp.loadOpS;
        m_state.om.renderPassOps.depthOps.storeOpS = depthOp.storeOpS;
        m_state.om.clearValues[attachmentIndex].depthStencil.stencil = clearValue.depthStencil.stencil;
      }

      if (clearAspects & (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT)) {
        m_state.om.renderPassOps.depthOps.loadLayout  = depthOp.loadLayout;
        m_state.om.renderPassOps.depthOps.storeLayout = depthOp.storeLayout;

        if (m_state.om.renderPassOps.depthOps.loadOpD == VK_ATTACHMENT_LOAD_OP_CLEAR
         && m_state.om.renderPassOps.depthOps.loadOpS == VK_ATTACHMENT_LOAD_OP_CLEAR)
          m_state.om.renderPassOps.depthOps.loadLayout = VK_IMAGE_LAYOUT_UNDEFINED;
      }
      
      m_flags.set(DxvkContextFlag::GpClearRenderTargets);
    }
  }
  
  
  void DxvkContext::clearImageView(
    const Rc<DxvkImageView>&    imageView,
          VkOffset3D            offset,
          VkExtent3D            extent,
          VkClearValue          value) {
    const VkImageUsageFlags viewUsage = imageView->info().usage;

    if (viewUsage & (VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT))
      this->clearImageViewFb(imageView, offset, extent, value);
    else if (viewUsage & VK_IMAGE_USAGE_STORAGE_BIT)
      this->clearImageViewCs(imageView, offset, extent, value);
  }
  
  
  void DxvkContext::copyBuffer(
    const Rc<DxvkBuffer>&       dstBuffer,
          VkDeviceSize          dstOffset,
    const Rc<DxvkBuffer>&       srcBuffer,
          VkDeviceSize          srcOffset,
          VkDeviceSize          numBytes) {
    if (numBytes == 0)
      return;
    
    this->spillRenderPass();
    
    auto dstSlice = dstBuffer->subSlice(dstOffset, numBytes);
    auto srcSlice = srcBuffer->subSlice(srcOffset, numBytes);

    if (m_barriers.isBufferDirty(srcSlice, DxvkAccess::Read)
     || m_barriers.isBufferDirty(dstSlice, DxvkAccess::Write))
      m_barriers.recordCommands(m_cmd);

    VkBufferCopy bufferRegion;
    bufferRegion.srcOffset = srcSlice.offset();
    bufferRegion.dstOffset = dstSlice.offset();
    bufferRegion.size      = dstSlice.length();

    m_cmd->cmdCopyBuffer(
      srcSlice.handle(),
      dstSlice.handle(),
      1, &bufferRegion);

    m_barriers.accessBuffer(srcSlice,
      VK_PIPELINE_STAGE_TRANSFER_BIT,
      VK_ACCESS_TRANSFER_READ_BIT,
      srcBuffer->info().stages,
      srcBuffer->info().access);

    m_barriers.accessBuffer(dstSlice,
      VK_PIPELINE_STAGE_TRANSFER_BIT,
      VK_ACCESS_TRANSFER_WRITE_BIT,
      dstBuffer->info().stages,
      dstBuffer->info().access);

    m_cmd->trackResource(dstBuffer->resource());
    m_cmd->trackResource(srcBuffer->resource());
  }
  
  
  void DxvkContext::copyBufferToImage(
    const Rc<DxvkImage>&        dstImage,
          VkImageSubresourceLayers dstSubresource,
          VkOffset3D            dstOffset,
          VkExtent3D            dstExtent,
    const Rc<DxvkBuffer>&       srcBuffer,
          VkDeviceSize          srcOffset,
          VkExtent2D            srcExtent) {
    this->spillRenderPass();

    auto srcSlice = srcBuffer->subSlice(srcOffset, 0);

    // We may copy to only one aspect of a depth-stencil image,
    // but pipeline barriers need to have all aspect bits set
    auto dstFormatInfo = dstImage->formatInfo();

    VkImageSubresourceRange dstSubresourceRange = {
      dstFormatInfo->aspectMask,
      dstSubresource.mipLevel, 1,
      dstSubresource.baseArrayLayer,
      dstSubresource.layerCount };
    
    if (m_barriers.isImageDirty(dstImage, dstSubresourceRange, DxvkAccess::Write)
     || m_barriers.isBufferDirty(srcSlice, DxvkAccess::Read))
      m_barriers.recordCommands(m_cmd);

    // Initialize the image if the entire subresource is covered
    VkImageLayout dstImageLayoutInitial  = dstImage->info().layout;
    VkImageLayout dstImageLayoutTransfer = dstImage->pickLayout(VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

    if (dstImage->isFullSubresource(dstSubresource, dstExtent))
      dstImageLayoutInitial = VK_IMAGE_LAYOUT_UNDEFINED;

    m_transitions.accessImage(
      dstImage, dstSubresourceRange,
      dstImageLayoutInitial, 0, 0,
      dstImageLayoutTransfer,
      VK_PIPELINE_STAGE_TRANSFER_BIT,
      VK_ACCESS_TRANSFER_WRITE_BIT);
      
    m_transitions.recordCommands(m_cmd);
    
    VkBufferImageCopy copyRegion;
    copyRegion.bufferOffset       = srcSlice.offset();
    copyRegion.bufferRowLength    = srcExtent.width;
    copyRegion.bufferImageHeight  = srcExtent.height;
    copyRegion.imageSubresource   = dstSubresource;
    copyRegion.imageOffset        = dstOffset;
    copyRegion.imageExtent        = dstExtent;
    
    m_cmd->cmdCopyBufferToImage(
      srcSlice.handle(),
      dstImage->handle(),
      dstImageLayoutTransfer,
      1, &copyRegion);
    
    m_barriers.accessImage(
      dstImage, dstSubresourceRange,
      dstImageLayoutTransfer,
      VK_PIPELINE_STAGE_TRANSFER_BIT,
      VK_ACCESS_TRANSFER_WRITE_BIT,
      dstImage->info().layout,
      dstImage->info().stages,
      dstImage->info().access);

    m_barriers.accessBuffer(srcSlice,
      VK_PIPELINE_STAGE_TRANSFER_BIT,
      VK_ACCESS_TRANSFER_READ_BIT,
      srcBuffer->info().stages,
      srcBuffer->info().access);
    
    m_cmd->trackResource(dstImage);
    m_cmd->trackResource(srcSlice.resource());
  }
  
  
  void DxvkContext::copyImage(
    const Rc<DxvkImage>&        dstImage,
          VkImageSubresourceLayers dstSubresource,
          VkOffset3D            dstOffset,
    const Rc<DxvkImage>&        srcImage,
          VkImageSubresourceLayers srcSubresource,
          VkOffset3D            srcOffset,
          VkExtent3D            extent) {
    this->spillRenderPass();
    
    VkImageSubresourceRange dstSubresourceRange = {
      dstSubresource.aspectMask,
      dstSubresource.mipLevel, 1,
      dstSubresource.baseArrayLayer,
      dstSubresource.layerCount };
    
    VkImageSubresourceRange srcSubresourceRange = {
      srcSubresource.aspectMask,
      srcSubresource.mipLevel, 1,
      srcSubresource.baseArrayLayer,
      srcSubresource.layerCount };
    
    if (m_barriers.isImageDirty(dstImage, dstSubresourceRange, DxvkAccess::Write)
     || m_barriers.isImageDirty(srcImage, srcSubresourceRange, DxvkAccess::Write))
      m_barriers.recordCommands(m_cmd);

    VkImageLayout dstImageLayout = dstImage->pickLayout(VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    VkImageLayout srcImageLayout = srcImage->pickLayout(VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

    VkImageLayout dstInitImageLayout = dstImage->info().layout;

    if (dstImage->isFullSubresource(dstSubresource, extent))
      dstInitImageLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    m_transitions.accessImage(
      dstImage, dstSubresourceRange,
      dstInitImageLayout, 0, 0,
      dstImageLayout,
      VK_PIPELINE_STAGE_TRANSFER_BIT,
      VK_ACCESS_TRANSFER_WRITE_BIT);

    m_transitions.accessImage(
      srcImage, srcSubresourceRange,
      srcImage->info().layout, 0, 0,
      srcImageLayout,
      VK_PIPELINE_STAGE_TRANSFER_BIT,
      VK_ACCESS_TRANSFER_READ_BIT);

    m_transitions.recordCommands(m_cmd);
      
    if (dstSubresource.aspectMask == srcSubresource.aspectMask) {
      VkImageCopy imageRegion;
      imageRegion.srcSubresource = srcSubresource;
      imageRegion.srcOffset      = srcOffset;
      imageRegion.dstSubresource = dstSubresource;
      imageRegion.dstOffset      = dstOffset;
      imageRegion.extent         = extent;
      
      m_cmd->cmdCopyImage(
        srcImage->handle(), srcImageLayout,
        dstImage->handle(), dstImageLayout,
        1, &imageRegion);
    } else {
      const VkDeviceSize transferBufferSize = std::max(
        util::computeImageDataSize(dstImage->info().format, extent),
        util::computeImageDataSize(srcImage->info().format, extent));
      
      // TODO optimize away buffer creation
      DxvkBufferCreateInfo tmpBufferInfo;
      tmpBufferInfo.size   = transferBufferSize;
      tmpBufferInfo.usage  = VK_BUFFER_USAGE_TRANSFER_SRC_BIT
                           | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
      tmpBufferInfo.stages = VK_PIPELINE_STAGE_TRANSFER_BIT;
      tmpBufferInfo.access = VK_ACCESS_TRANSFER_READ_BIT
                           | VK_ACCESS_TRANSFER_WRITE_BIT;
      
      Rc<DxvkBuffer> tmpBuffer = m_device->createBuffer(
        tmpBufferInfo, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
      
      DxvkPhysicalBufferSlice tmpSlice = tmpBuffer->slice();
      
      VkBufferImageCopy bufferImageCopy;
      bufferImageCopy.bufferOffset       = tmpSlice.offset();
      bufferImageCopy.bufferRowLength    = 0;
      bufferImageCopy.bufferImageHeight  = 0;
      bufferImageCopy.imageSubresource   = srcSubresource;
      bufferImageCopy.imageOffset        = srcOffset;
      bufferImageCopy.imageExtent        = extent;
      
      m_cmd->cmdCopyImageToBuffer(
        srcImage->handle(), srcImageLayout,
        tmpSlice.handle(), 1, &bufferImageCopy);
      
      m_barriers.accessBuffer(tmpSlice,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_ACCESS_TRANSFER_WRITE_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_ACCESS_TRANSFER_READ_BIT);

      m_barriers.recordCommands(m_cmd);
      
      bufferImageCopy.imageSubresource   = dstSubresource;
      bufferImageCopy.imageOffset        = dstOffset;
      
      m_cmd->cmdCopyBufferToImage(
        tmpSlice.handle(), dstImage->handle(),
        dstImageLayout, 1, &bufferImageCopy);
      
      m_barriers.accessBuffer(tmpSlice,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_ACCESS_TRANSFER_READ_BIT,
        tmpBuffer->info().stages,
        tmpBuffer->info().access);
      
      m_cmd->trackResource(tmpSlice.resource());
    }
      
    m_barriers.accessImage(
      dstImage, dstSubresourceRange,
      dstImageLayout,
      VK_PIPELINE_STAGE_TRANSFER_BIT,
      VK_ACCESS_TRANSFER_WRITE_BIT,
      dstImage->info().layout,
      dstImage->info().stages,
      dstImage->info().access);

    m_barriers.accessImage(
      srcImage, srcSubresourceRange,
      srcImageLayout,
      VK_PIPELINE_STAGE_TRANSFER_BIT,
      VK_ACCESS_TRANSFER_READ_BIT,
      srcImage->info().layout,
      srcImage->info().stages,
      srcImage->info().access);
    
    m_cmd->trackResource(dstImage);
    m_cmd->trackResource(srcImage);
  }
  
  
  void DxvkContext::copyImageToBuffer(
    const Rc<DxvkBuffer>&       dstBuffer,
          VkDeviceSize          dstOffset,
          VkExtent2D            dstExtent,
    const Rc<DxvkImage>&        srcImage,
          VkImageSubresourceLayers srcSubresource,
          VkOffset3D            srcOffset,
          VkExtent3D            srcExtent) {
    this->spillRenderPass();
    
    auto dstSlice = dstBuffer->subSlice(dstOffset, 0);

    // We may copy to only one aspect of a depth-stencil image,
    // but pipeline barriers need to have all aspect bits set
    auto srcFormatInfo = srcImage->formatInfo();

    VkImageSubresourceRange srcSubresourceRange = {
      srcFormatInfo->aspectMask,
      srcSubresource.mipLevel, 1,
      srcSubresource.baseArrayLayer,
      srcSubresource.layerCount };
    
    if (m_barriers.isImageDirty(srcImage, srcSubresourceRange, DxvkAccess::Write)
     || m_barriers.isBufferDirty(dstSlice, DxvkAccess::Write))
      m_barriers.recordCommands(m_cmd);

    // Select a suitable image layout for the transfer op
    VkImageLayout srcImageLayoutTransfer = srcImage->pickLayout(VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    
    m_transitions.accessImage(
      srcImage, srcSubresourceRange,
      srcImage->info().layout, 0, 0,
      srcImageLayoutTransfer,
      VK_PIPELINE_STAGE_TRANSFER_BIT,
      VK_ACCESS_TRANSFER_READ_BIT);

    m_transitions.recordCommands(m_cmd);
    
    VkBufferImageCopy copyRegion;
    copyRegion.bufferOffset       = dstSlice.offset();
    copyRegion.bufferRowLength    = dstExtent.width;
    copyRegion.bufferImageHeight  = dstExtent.height;
    copyRegion.imageSubresource   = srcSubresource;
    copyRegion.imageOffset        = srcOffset;
    copyRegion.imageExtent        = srcExtent;
    
    m_cmd->cmdCopyImageToBuffer(
      srcImage->handle(),
      srcImageLayoutTransfer,
      dstSlice.handle(),
      1, &copyRegion);
    
    m_barriers.accessImage(
      srcImage, srcSubresourceRange,
      srcImageLayoutTransfer,
      VK_PIPELINE_STAGE_TRANSFER_BIT,
      VK_ACCESS_TRANSFER_READ_BIT,
      srcImage->info().layout,
      srcImage->info().stages,
      srcImage->info().access);

    m_barriers.accessBuffer(dstSlice,
      VK_PIPELINE_STAGE_TRANSFER_BIT,
      VK_ACCESS_TRANSFER_WRITE_BIT,
      dstBuffer->info().stages,
      dstBuffer->info().access);
    
    m_cmd->trackResource(srcImage);
    m_cmd->trackResource(dstSlice.resource());
  }
  
  
  void DxvkContext::discardBuffer(
    const Rc<DxvkBuffer>&       buffer) {
    if (m_barriers.isBufferDirty(buffer->slice(), DxvkAccess::Write))
      this->invalidateBuffer(buffer, buffer->allocPhysicalSlice());
  }


  void DxvkContext::dispatch(
          uint32_t x,
          uint32_t y,
          uint32_t z) {
    this->commitComputeState();
    
    if (this->validateComputeState()) {
      this->commitComputeInitBarriers();

      m_queries.beginQueries(m_cmd,
        VK_QUERY_TYPE_PIPELINE_STATISTICS);
      
      m_cmd->cmdDispatch(x, y, z);
      
      m_queries.endQueries(m_cmd,
        VK_QUERY_TYPE_PIPELINE_STATISTICS);
      
      this->commitComputePostBarriers();
    }
    
    m_cmd->addStatCtr(DxvkStatCounter::CmdDispatchCalls, 1);
  }
  
  
  void DxvkContext::dispatchIndirect(
    const DxvkBufferSlice&  buffer) {
    this->commitComputeState();
    
    auto physicalSlice = buffer.physicalSlice();

    if (m_barriers.isBufferDirty(buffer.physicalSlice(), DxvkAccess::Read))
      m_barriers.recordCommands(m_cmd);
    
    if (this->validateComputeState()) {
      this->commitComputeInitBarriers();

      m_queries.beginQueries(m_cmd,
        VK_QUERY_TYPE_PIPELINE_STATISTICS);
      
      m_cmd->cmdDispatchIndirect(
        physicalSlice.handle(),
        physicalSlice.offset());
      
      m_queries.endQueries(m_cmd,
        VK_QUERY_TYPE_PIPELINE_STATISTICS);
      
      this->commitComputePostBarriers();
    }
    
    m_cmd->addStatCtr(DxvkStatCounter::CmdDispatchCalls, 1);
  }
  
  
  void DxvkContext::draw(
          uint32_t vertexCount,
          uint32_t instanceCount,
          uint32_t firstVertex,
          uint32_t firstInstance) {
    this->commitGraphicsState();
    
    if (this->validateGraphicsState()) {
      m_cmd->cmdDraw(
        vertexCount, instanceCount,
        firstVertex, firstInstance);
    }
    
    m_cmd->addStatCtr(DxvkStatCounter::CmdDrawCalls, 1);
  }
  
  
  void DxvkContext::drawIndirect(
    const DxvkBufferSlice&  buffer,
          uint32_t          count,
          uint32_t          stride) {
    this->commitGraphicsState();
    
    if (this->validateGraphicsState()) {
      auto physicalSlice = buffer.physicalSlice();
      
      m_cmd->cmdDrawIndirect(
        physicalSlice.handle(),
        physicalSlice.offset(),
        count, stride);
    }
    
    m_cmd->addStatCtr(DxvkStatCounter::CmdDrawCalls, 1);
  }
  
  
  void DxvkContext::drawIndexed(
          uint32_t indexCount,
          uint32_t instanceCount,
          uint32_t firstIndex,
          uint32_t vertexOffset,
          uint32_t firstInstance) {
    this->commitGraphicsState();
    
    if (this->validateGraphicsState()) {
      m_cmd->cmdDrawIndexed(
        indexCount, instanceCount,
        firstIndex, vertexOffset,
        firstInstance);
    }
    
    m_cmd->addStatCtr(DxvkStatCounter::CmdDrawCalls, 1);
  }
  
  
  void DxvkContext::drawIndexedIndirect(
    const DxvkBufferSlice&  buffer,
          uint32_t          count,
          uint32_t          stride) {
    this->commitGraphicsState();
    
    if (this->validateGraphicsState()) {
      auto physicalSlice = buffer.physicalSlice();
      
      m_cmd->cmdDrawIndexedIndirect(
        physicalSlice.handle(),
        physicalSlice.offset(),
        count, stride);
    }
    
    m_cmd->addStatCtr(DxvkStatCounter::CmdDrawCalls, 1);
  }
  
  
  void DxvkContext::initImage(
    const Rc<DxvkImage>&           image,
    const VkImageSubresourceRange& subresources) {
    m_barriers.accessImage(image, subresources,
      VK_IMAGE_LAYOUT_UNDEFINED, 0, 0,
      image->info().layout,
      image->info().stages,
      image->info().access);
    
    m_cmd->trackResource(image);
  }
  
  
  void DxvkContext::generateMipmaps(
    const Rc<DxvkImageView>&        imageView) {
    if (imageView->info().numLevels <= 1)
      return;
    
    this->spillRenderPass();
    this->unbindGraphicsPipeline();

    m_barriers.recordCommands(m_cmd);
    
    // Create the a set of framebuffers and image views
    const Rc<DxvkMetaMipGenRenderPass> mipGenerator
      = new DxvkMetaMipGenRenderPass(m_device->vkd(), imageView);
    
    // Common descriptor set properties that we use to
    // bind the source image view to the fragment shader
    VkDescriptorImageInfo descriptorImage;
    descriptorImage.sampler     = VK_NULL_HANDLE;
    descriptorImage.imageView   = VK_NULL_HANDLE;
    descriptorImage.imageLayout = imageView->imageInfo().layout;
    
    VkWriteDescriptorSet descriptorWrite;
    descriptorWrite.sType            = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrite.pNext            = nullptr;
    descriptorWrite.dstSet           = VK_NULL_HANDLE;
    descriptorWrite.dstBinding       = 0;
    descriptorWrite.dstArrayElement  = 0;
    descriptorWrite.descriptorCount  = 1;
    descriptorWrite.descriptorType   = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    descriptorWrite.pImageInfo       = &descriptorImage;
    descriptorWrite.pBufferInfo      = nullptr;
    descriptorWrite.pTexelBufferView = nullptr;
    
    // Common render pass info
    VkRenderPassBeginInfo passInfo;
    passInfo.sType            = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    passInfo.pNext            = nullptr;
    passInfo.renderPass       = mipGenerator->renderPass();
    passInfo.framebuffer      = VK_NULL_HANDLE;
    passInfo.renderArea       = VkRect2D { };
    passInfo.clearValueCount  = 0;
    passInfo.pClearValues     = nullptr;
    
    // Retrieve a compatible pipeline to use for rendering
    DxvkMetaMipGenPipeline pipeInfo = m_metaMipGen->getPipeline(
      mipGenerator->viewType(), imageView->info().format);
    
    for (uint32_t i = 0; i < mipGenerator->passCount(); i++) {
      DxvkMetaMipGenPass pass = mipGenerator->pass(i);
      
      // Width, height and layer count for the current pass
      VkExtent3D passExtent = mipGenerator->passExtent(i);
      
      // Create descriptor set with the current source view
      descriptorImage.imageView = pass.srcView;
      descriptorWrite.dstSet = m_cmd->allocateDescriptorSet(pipeInfo.dsetLayout);
      m_cmd->updateDescriptorSets(1, &descriptorWrite);
      
      // Set up viewport and scissor rect
      VkViewport viewport;
      viewport.x        = 0.0f;
      viewport.y        = 0.0f;
      viewport.width    = float(passExtent.width);
      viewport.height   = float(passExtent.height);
      viewport.minDepth = 0.0f;
      viewport.maxDepth = 1.0f;
      
      VkRect2D scissor;
      scissor.offset    = { 0, 0 };
      scissor.extent    = { passExtent.width, passExtent.height };
      
      // Set up render pass info
      passInfo.framebuffer = pass.framebuffer;
      passInfo.renderArea  = scissor;
      
      // Set up push constants
      DxvkMetaMipGenPushConstants pushConstants;
      pushConstants.layerCount = passExtent.depth;
      
      m_cmd->cmdBeginRenderPass(&passInfo, VK_SUBPASS_CONTENTS_INLINE);
      m_cmd->cmdBindPipeline(VK_PIPELINE_BIND_POINT_GRAPHICS, pipeInfo.pipeHandle);
      m_cmd->cmdBindDescriptorSet(VK_PIPELINE_BIND_POINT_GRAPHICS,
        pipeInfo.pipeLayout, descriptorWrite.dstSet, 0, nullptr);
      
      m_cmd->cmdSetViewport(0, 1, &viewport);
      m_cmd->cmdSetScissor (0, 1, &scissor);
      
      m_cmd->cmdPushConstants(
        pipeInfo.pipeLayout,
        VK_SHADER_STAGE_FRAGMENT_BIT,
        0, sizeof(pushConstants),
        &pushConstants);
      
      m_cmd->cmdDraw(1, passExtent.depth, 0, 0);
      m_cmd->cmdEndRenderPass();
    }
    
    m_cmd->trackResource(mipGenerator);
    m_cmd->trackResource(imageView->image());
  }
  
  
  void DxvkContext::invalidateBuffer(
    const Rc<DxvkBuffer>&           buffer,
    const DxvkPhysicalBufferSlice&  slice) {
    // Allocate new backing resource
    DxvkPhysicalBufferSlice prevSlice = buffer->rename(slice);
    m_cmd->freePhysicalBufferSlice(buffer, prevSlice);
    
    // We also need to update all bindings that the buffer
    // may be bound to either directly or through views.
    const VkBufferUsageFlags usage = buffer->info().usage;
    
    if (usage & VK_BUFFER_USAGE_INDEX_BUFFER_BIT)
      m_flags.set(DxvkContextFlag::GpDirtyIndexBuffer);
    
    if (usage & VK_BUFFER_USAGE_VERTEX_BUFFER_BIT)
      m_flags.set(DxvkContextFlag::GpDirtyVertexBuffers);
    
    if (usage & (VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT
               | VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT)) {
      m_flags.set(DxvkContextFlag::GpDirtyResources,
                  DxvkContextFlag::CpDirtyResources);
    }

    if (usage & (VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT
               | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT)) {
      if (prevSlice.handle() != slice.handle()) {
        m_flags.set(DxvkContextFlag::GpDirtyResources,
                    DxvkContextFlag::CpDirtyResources);
      } else {
        m_flags.set(DxvkContextFlag::GpDirtyDescriptorOffsets,
                    DxvkContextFlag::CpDirtyDescriptorOffsets);
      }
    }
  }
  
  
  void DxvkContext::resolveImage(
    const Rc<DxvkImage>&            dstImage,
    const VkImageSubresourceLayers& dstSubresources,
    const Rc<DxvkImage>&            srcImage,
    const VkImageSubresourceLayers& srcSubresources,
          VkFormat                  format) {
    this->spillRenderPass();
    this->unbindGraphicsPipeline();
    
    m_barriers.recordCommands(m_cmd);
    
    if (format == VK_FORMAT_UNDEFINED)
      format = srcImage->info().format;
    
    if (dstImage->info().format == format
     && srcImage->info().format == format) {
      VkImageSubresourceRange dstSubresourceRange = {
        dstSubresources.aspectMask,
        dstSubresources.mipLevel, 1,
        dstSubresources.baseArrayLayer,
        dstSubresources.layerCount };
      
      VkImageSubresourceRange srcSubresourceRange = {
        srcSubresources.aspectMask,
        srcSubresources.mipLevel, 1,
        srcSubresources.baseArrayLayer,
        srcSubresources.layerCount };
      
      // We only support resolving to the entire image
      // area, so we might as well discard its contents
      m_barriers.accessImage(
        dstImage, dstSubresourceRange,
        VK_IMAGE_LAYOUT_UNDEFINED,
        dstImage->info().stages,
        dstImage->info().access,
        dstImage->pickLayout(VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL),
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_ACCESS_TRANSFER_WRITE_BIT);

      m_barriers.accessImage(
        srcImage, srcSubresourceRange,
        srcImage->info().layout,
        srcImage->info().stages,
        srcImage->info().access,
        srcImage->pickLayout(VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL),
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_ACCESS_TRANSFER_READ_BIT);

      m_barriers.recordCommands(m_cmd);
      
      VkImageResolve imageRegion;
      imageRegion.srcSubresource = srcSubresources;
      imageRegion.srcOffset      = VkOffset3D { 0, 0, 0 };
      imageRegion.dstSubresource = dstSubresources;
      imageRegion.dstOffset      = VkOffset3D { 0, 0, 0 };
      imageRegion.extent         = srcImage->mipLevelExtent(srcSubresources.mipLevel);
      
      m_cmd->cmdResolveImage(
        srcImage->handle(), srcImage->pickLayout(VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL),
        dstImage->handle(), dstImage->pickLayout(VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL),
        1, &imageRegion);
    
      m_barriers.accessImage(
        dstImage, dstSubresourceRange,
        dstImage->pickLayout(VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL),
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_ACCESS_TRANSFER_WRITE_BIT,
        dstImage->info().layout,
        dstImage->info().stages,
        dstImage->info().access);

      m_barriers.accessImage(
        srcImage, srcSubresourceRange,
        srcImage->pickLayout(VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL),
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_ACCESS_TRANSFER_READ_BIT,
        srcImage->info().layout,
        srcImage->info().stages,
        srcImage->info().access);
    } else {
      // Create image views covering the requested subresourcs
      DxvkImageViewCreateInfo dstViewInfo;
      dstViewInfo.type      = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
      dstViewInfo.format    = format;
      dstViewInfo.usage     = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
      dstViewInfo.aspect    = dstSubresources.aspectMask;
      dstViewInfo.minLevel  = dstSubresources.mipLevel;
      dstViewInfo.numLevels = 1;
      dstViewInfo.minLayer  = dstSubresources.baseArrayLayer;
      dstViewInfo.numLayers = dstSubresources.layerCount;

      DxvkImageViewCreateInfo srcViewInfo;
      srcViewInfo.type      = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
      srcViewInfo.format    = format;
      srcViewInfo.usage     = VK_IMAGE_USAGE_SAMPLED_BIT;
      srcViewInfo.aspect    = srcSubresources.aspectMask;
      srcViewInfo.minLevel  = srcSubresources.mipLevel;
      srcViewInfo.numLevels = 1;
      srcViewInfo.minLayer  = srcSubresources.baseArrayLayer;
      srcViewInfo.numLayers = srcSubresources.layerCount;

      Rc<DxvkImageView> dstImageView = m_device->createImageView(dstImage, dstViewInfo);
      Rc<DxvkImageView> srcImageView = m_device->createImageView(srcImage, srcViewInfo);

      // Create a framebuffer and pipeline for the resolve op
      DxvkMetaResolvePipeline pipeInfo = m_metaResolve->getPipeline(format);

      Rc<DxvkMetaResolveRenderPass> fb = new DxvkMetaResolveRenderPass(
        m_device->vkd(), dstImageView, srcImageView);

      // Create descriptor set pointing to the source image
      VkDescriptorImageInfo descriptorImage;
      descriptorImage.sampler          = VK_NULL_HANDLE;
      descriptorImage.imageView        = srcImageView->handle();
      descriptorImage.imageLayout      = srcImageView->imageInfo().layout;

      VkWriteDescriptorSet descriptorWrite;
      descriptorWrite.sType            = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
      descriptorWrite.pNext            = nullptr;
      descriptorWrite.dstBinding       = 0;
      descriptorWrite.dstArrayElement  = 0;
      descriptorWrite.descriptorCount  = 1;
      descriptorWrite.descriptorType   = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
      descriptorWrite.pImageInfo       = &descriptorImage;
      descriptorWrite.pBufferInfo      = nullptr;
      descriptorWrite.pTexelBufferView = nullptr;
      
      descriptorWrite.dstSet = m_cmd->allocateDescriptorSet(pipeInfo.dsetLayout);
      m_cmd->updateDescriptorSets(1, &descriptorWrite);

      // Set up viewport and scissor rect
      VkExtent3D passExtent = dstImageView->mipLevelExtent(0);
      passExtent.depth = dstSubresources.layerCount;

      VkViewport viewport;
      viewport.x        = 0.0f;
      viewport.y        = 0.0f;
      viewport.width    = float(passExtent.width);
      viewport.height   = float(passExtent.height);
      viewport.minDepth = 0.0f;
      viewport.maxDepth = 1.0f;
      
      VkRect2D scissor;
      scissor.offset    = { 0, 0 };
      scissor.extent    = { passExtent.width, passExtent.height };

      // Render pass info
      VkRenderPassBeginInfo info;
      info.sType              = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
      info.pNext              = nullptr;
      info.renderPass         = fb->renderPass();
      info.framebuffer        = fb->framebuffer();
      info.renderArea.offset  = { 0, 0 };
      info.renderArea.extent  = { passExtent.width, passExtent.height };
      info.clearValueCount    = 0;
      info.pClearValues       = nullptr;
      
      // Perform the actual resolve operation
      m_cmd->cmdBeginRenderPass(&info, VK_SUBPASS_CONTENTS_INLINE);
      m_cmd->cmdBindPipeline(VK_PIPELINE_BIND_POINT_GRAPHICS, pipeInfo.pipeHandle);
      m_cmd->cmdBindDescriptorSet(VK_PIPELINE_BIND_POINT_GRAPHICS,
        pipeInfo.pipeLayout, descriptorWrite.dstSet, 0, nullptr);

      m_cmd->cmdSetViewport(0, 1, &viewport);
      m_cmd->cmdSetScissor (0, 1, &scissor);
      
      m_cmd->cmdDraw(1, passExtent.depth, 0, 0);
      m_cmd->cmdEndRenderPass();

      m_cmd->trackResource(fb);
    }
    
    m_cmd->trackResource(srcImage);
    m_cmd->trackResource(dstImage);
  }
  
  
  void DxvkContext::transformImage(
    const Rc<DxvkImage>&            dstImage,
    const VkImageSubresourceRange&  dstSubresources,
          VkImageLayout             srcLayout,
          VkImageLayout             dstLayout) {
    this->spillRenderPass();
    
    if (srcLayout != dstLayout) {
      m_barriers.recordCommands(m_cmd);

      m_barriers.accessImage(
        dstImage, dstSubresources,
        srcLayout,
        dstImage->info().stages,
        dstImage->info().access,
        dstLayout,
        dstImage->info().stages,
        dstImage->info().access);
      
      m_cmd->trackResource(dstImage);
    }
  }
  
  
  void DxvkContext::updateBuffer(
    const Rc<DxvkBuffer>&           buffer,
          VkDeviceSize              offset,
          VkDeviceSize              size,
    const void*                     data) {
    this->spillRenderPass();
    
    // Vulkan specifies that small amounts of data (up to 64kB) can
    // be copied to a buffer directly if the size is a multiple of
    // four. Anything else must be copied through a staging buffer.
    // We'll limit the size to 4kB in order to keep command buffers
    // reasonably small, we do not know how much data apps may upload.
    auto physicalSlice = buffer->subSlice(offset, size);

    if (m_barriers.isBufferDirty(physicalSlice, DxvkAccess::Write))
      m_barriers.recordCommands(m_cmd);
    
    if ((size <= 4096) && ((size & 0x3) == 0) && ((offset & 0x3) == 0)) {
      m_cmd->cmdUpdateBuffer(
        physicalSlice.handle(),
        physicalSlice.offset(),
        physicalSlice.length(),
        data);
    } else {
      auto slice = m_cmd->stagedAlloc(size);
      std::memcpy(slice.mapPtr, data, size);

      m_cmd->stagedBufferCopy(
        physicalSlice.handle(),
        physicalSlice.offset(),
        physicalSlice.length(),
        slice);
    }

    m_barriers.accessBuffer(
      physicalSlice,
      VK_PIPELINE_STAGE_TRANSFER_BIT,
      VK_ACCESS_TRANSFER_WRITE_BIT,
      buffer->info().stages,
      buffer->info().access);

    m_cmd->trackResource(buffer->resource());
  }
  
  
  void DxvkContext::updateImage(
    const Rc<DxvkImage>&            image,
    const VkImageSubresourceLayers& subresources,
          VkOffset3D                imageOffset,
          VkExtent3D                imageExtent,
    const void*                     data,
          VkDeviceSize              pitchPerRow,
          VkDeviceSize              pitchPerLayer) {
    this->spillRenderPass();
    
    // Upload data through a staging buffer. Special care needs to
    // be taken when dealing with compressed image formats: Rather
    // than copying pixels, we'll be copying blocks of pixels.
    const DxvkFormatInfo* formatInfo = image->formatInfo();
    
    // Align image extent to a full block. This is necessary in
    // case the image size is not a multiple of the block size.
    VkExtent3D elementCount = util::computeBlockCount(
      imageExtent, formatInfo->blockSize);
    elementCount.depth *= subresources.layerCount;
    
    // Allocate staging buffer memory for the image data. The
    // pixels or blocks will be tightly packed within the buffer.
    const DxvkStagingBufferSlice slice = m_cmd->stagedAlloc(
      formatInfo->elementSize * util::flattenImageExtent(elementCount));
    
    auto dstData = reinterpret_cast<char*>(slice.mapPtr);
    auto srcData = reinterpret_cast<const char*>(data);
    
    util::packImageData(dstData, srcData,
      elementCount, formatInfo->elementSize,
      pitchPerRow, pitchPerLayer);
    
    // Prepare the image layout. If the given extent covers
    // the entire image, we may discard its previous contents.
    VkImageSubresourceRange subresourceRange;
    subresourceRange.aspectMask     = formatInfo->aspectMask;
    subresourceRange.baseMipLevel   = subresources.mipLevel;
    subresourceRange.levelCount     = 1;
    subresourceRange.baseArrayLayer = subresources.baseArrayLayer;
    subresourceRange.layerCount     = subresources.layerCount;

    if (m_barriers.isImageDirty(image, subresourceRange, DxvkAccess::Write))
      m_barriers.recordCommands(m_cmd);

    // Initialize the image if the entire subresource is covered
    VkImageLayout imageLayoutInitial  = image->info().layout;
    VkImageLayout imageLayoutTransfer = image->pickLayout(VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

    if (image->isFullSubresource(subresources, imageExtent))
      imageLayoutInitial = VK_IMAGE_LAYOUT_UNDEFINED;

    m_transitions.accessImage(
      image, subresourceRange,
      imageLayoutInitial, 0, 0,
      imageLayoutTransfer,
      VK_PIPELINE_STAGE_TRANSFER_BIT,
      VK_ACCESS_TRANSFER_WRITE_BIT);

    m_transitions.recordCommands(m_cmd);
    
    // Copy contents of the staging buffer into the image.
    // Since our source data is tightly packed, we do not
    // need to specify any strides.
    VkBufferImageCopy region;
    region.bufferOffset       = slice.offset;
    region.bufferRowLength    = 0;
    region.bufferImageHeight  = 0;
    region.imageSubresource   = subresources;
    region.imageOffset        = imageOffset;
    region.imageExtent        = imageExtent;
    
    m_cmd->stagedBufferImageCopy(image->handle(),
      imageLayoutTransfer, region, slice);
    
    // Transition image back into its optimal layout
    m_barriers.accessImage(
      image, subresourceRange,
      imageLayoutTransfer,
      VK_PIPELINE_STAGE_TRANSFER_BIT,
      VK_ACCESS_TRANSFER_WRITE_BIT,
      image->info().layout,
      image->info().stages,
      image->info().access);
    
    m_cmd->trackResource(image);
  }
  
  
  void DxvkContext::setViewports(
          uint32_t            viewportCount,
    const VkViewport*         viewports,
    const VkRect2D*           scissorRects) {
    if (m_state.gp.state.rsViewportCount != viewportCount) {
      m_state.gp.state.rsViewportCount = viewportCount;
      m_flags.set(DxvkContextFlag::GpDirtyPipelineState);
    }
    
    for (uint32_t i = 0; i < viewportCount; i++) {
      m_state.vp.viewports[i]    = viewports[i];
      m_state.vp.scissorRects[i] = scissorRects[i];
      
      // Vulkan viewports are not allowed to have a width or
      // height of zero, so we fall back to a dummy viewport.
      if (viewports[i].width == 0.0f || viewports[i].height == 0.0f) {
        m_state.vp.viewports[i] = VkViewport {
          0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f };
      }
    }
    
    m_flags.set(DxvkContextFlag::GpDirtyViewport);
  }
  
  
  void DxvkContext::setBlendConstants(
    const DxvkBlendConstants&   blendConstants) {
    m_state.om.blendConstants = blendConstants;
    m_flags.set(DxvkContextFlag::GpDirtyBlendConstants);
  }
  
  
  void DxvkContext::setStencilReference(
    const uint32_t            reference) {
    m_state.om.stencilReference = reference;
    m_flags.set(DxvkContextFlag::GpDirtyStencilRef);
  }
  
  
  void DxvkContext::setInputAssemblyState(const DxvkInputAssemblyState& ia) {
    m_state.gp.state.iaPrimitiveTopology = ia.primitiveTopology;
    m_state.gp.state.iaPrimitiveRestart  = ia.primitiveRestart;
    m_state.gp.state.iaPatchVertexCount  = ia.patchVertexCount;
    
    m_flags.set(DxvkContextFlag::GpDirtyPipelineState);
  }
  
  
  void DxvkContext::setInputLayout(
          uint32_t             attributeCount,
    const DxvkVertexAttribute* attributes,
          uint32_t             bindingCount,
    const DxvkVertexBinding*   bindings) {
    m_flags.set(
      DxvkContextFlag::GpDirtyPipelineState,
      DxvkContextFlag::GpDirtyVertexBuffers);
    
    for (uint32_t i = 0; i < attributeCount; i++) {
      m_state.gp.state.ilAttributes[i].location = attributes[i].location;
      m_state.gp.state.ilAttributes[i].binding  = attributes[i].binding;
      m_state.gp.state.ilAttributes[i].format   = attributes[i].format;
      m_state.gp.state.ilAttributes[i].offset   = attributes[i].offset;
    }
    
    for (uint32_t i = attributeCount; i < m_state.gp.state.ilAttributeCount; i++)
      m_state.gp.state.ilAttributes[i] = VkVertexInputAttributeDescription();
    
    for (uint32_t i = 0; i < bindingCount; i++) {
      m_state.gp.state.ilBindings[i].binding    = bindings[i].binding;
      m_state.gp.state.ilBindings[i].inputRate  = bindings[i].inputRate;
      m_state.gp.state.ilDivisors[i]            = bindings[i].fetchRate;
    }
    
    for (uint32_t i = bindingCount; i < m_state.gp.state.ilBindingCount; i++)
      m_state.gp.state.ilBindings[i] = VkVertexInputBindingDescription();
    
    m_state.gp.state.ilAttributeCount = attributeCount;
    m_state.gp.state.ilBindingCount   = bindingCount;
  }
  
  
  void DxvkContext::setRasterizerState(const DxvkRasterizerState& rs) {
    m_state.gp.state.rsDepthClampEnable  = rs.depthClampEnable;
    m_state.gp.state.rsDepthBiasEnable   = rs.depthBiasEnable;
    m_state.gp.state.rsPolygonMode       = rs.polygonMode;
    m_state.gp.state.rsCullMode          = rs.cullMode;
    m_state.gp.state.rsFrontFace         = rs.frontFace;

    m_state.ds.depthBiasConstant = rs.depthBiasConstant;
    m_state.ds.depthBiasClamp    = rs.depthBiasClamp;
    m_state.ds.depthBiasSlope    = rs.depthBiasSlope;

    m_flags.set(
      DxvkContextFlag::GpDirtyPipelineState,
      DxvkContextFlag::GpDirtyDepthBias);
  }
  
  
  void DxvkContext::setMultisampleState(const DxvkMultisampleState& ms) {
    m_state.gp.state.msSampleMask            = ms.sampleMask;
    m_state.gp.state.msEnableAlphaToCoverage = ms.enableAlphaToCoverage;
    m_state.gp.state.msEnableAlphaToOne      = ms.enableAlphaToOne;
    
    m_flags.set(DxvkContextFlag::GpDirtyPipelineState);
  }
  
  
  void DxvkContext::setDepthStencilState(const DxvkDepthStencilState& ds) {
    m_state.gp.state.dsEnableDepthTest   = ds.enableDepthTest;
    m_state.gp.state.dsEnableDepthWrite  = ds.enableDepthWrite;
    m_state.gp.state.dsEnableStencilTest = ds.enableStencilTest;
    m_state.gp.state.dsDepthCompareOp    = ds.depthCompareOp;
    m_state.gp.state.dsStencilOpFront    = ds.stencilOpFront;
    m_state.gp.state.dsStencilOpBack     = ds.stencilOpBack;
    
    m_flags.set(DxvkContextFlag::GpDirtyPipelineState);
  }
  
  
  void DxvkContext::setLogicOpState(const DxvkLogicOpState& lo) {
    m_state.gp.state.omEnableLogicOp = lo.enableLogicOp;
    m_state.gp.state.omLogicOp       = lo.logicOp;
    
    m_flags.set(DxvkContextFlag::GpDirtyPipelineState);
  }
  
  
  void DxvkContext::setBlendMode(
          uint32_t            attachment,
    const DxvkBlendMode&      blendMode) {
    m_state.gp.state.omBlendAttachments[attachment].blendEnable         = blendMode.enableBlending;
    m_state.gp.state.omBlendAttachments[attachment].srcColorBlendFactor = blendMode.colorSrcFactor;
    m_state.gp.state.omBlendAttachments[attachment].dstColorBlendFactor = blendMode.colorDstFactor;
    m_state.gp.state.omBlendAttachments[attachment].colorBlendOp        = blendMode.colorBlendOp;
    m_state.gp.state.omBlendAttachments[attachment].srcAlphaBlendFactor = blendMode.alphaSrcFactor;
    m_state.gp.state.omBlendAttachments[attachment].dstAlphaBlendFactor = blendMode.alphaDstFactor;
    m_state.gp.state.omBlendAttachments[attachment].alphaBlendOp        = blendMode.alphaBlendOp;
    m_state.gp.state.omBlendAttachments[attachment].colorWriteMask      = blendMode.writeMask;
    
    m_flags.set(DxvkContextFlag::GpDirtyPipelineState);
  }
  
  
  void DxvkContext::signalEvent(const DxvkEventRevision& event) {
    m_cmd->trackEvent(event);
  }
  
  
  void DxvkContext::writeTimestamp(const DxvkQueryRevision& query) {
    DxvkQueryHandle handle = m_queries.allocQuery(m_cmd, query);
    
    m_cmd->cmdWriteTimestamp(
      VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
      handle.queryPool, handle.queryId);
    
    query.query->endRecording(query.revision);
  }
  
  
  void DxvkContext::clearImageViewFb(
    const Rc<DxvkImageView>&    imageView,
          VkOffset3D            offset,
          VkExtent3D            extent,
          VkClearValue          value) {
    this->updateFramebuffer();

    // Find out if the render target view is currently bound,
    // so that we can avoid spilling the render pass if it is.
    int32_t attachmentIndex = -1;
    
    if (m_state.om.framebuffer != nullptr)
      attachmentIndex = m_state.om.framebuffer->findAttachment(imageView);

    if (attachmentIndex < 0) {
      this->spillRenderPass();

      // Set up a temporary framebuffer
      DxvkRenderTargets attachments;
      DxvkRenderPassOps ops;
      
      if (imageView->info().aspect & VK_IMAGE_ASPECT_COLOR_BIT) {
        attachments.color[0].view   = imageView;
        attachments.color[0].layout = imageView->pickLayout(VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
      } else {
        attachments.depth.view   = imageView;
        attachments.depth.layout = imageView->pickLayout(VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
      }

      // We cannot leverage render pass clears
      // because we clear only part of the view
      this->renderPassBindFramebuffer(
        m_device->createFramebuffer(attachments),
        ops, 0, nullptr);
    }

    // Perform the actual clear operation
    VkClearAttachment clearInfo;
    clearInfo.aspectMask          = imageView->info().aspect;
    clearInfo.colorAttachment     = attachmentIndex;
    clearInfo.clearValue          = value;

    if (attachmentIndex < 0)
      clearInfo.colorAttachment   = 0;

    VkClearRect clearRect;
    clearRect.rect.offset.x       = offset.x;
    clearRect.rect.offset.y       = offset.y;
    clearRect.rect.extent.width   = extent.width;
    clearRect.rect.extent.height  = extent.height;
    clearRect.baseArrayLayer      = 0;
    clearRect.layerCount          = imageView->info().numLayers;

    m_cmd->cmdClearAttachments(1, &clearInfo, 1, &clearRect);

    // Unbind temporary framebuffer
    if (attachmentIndex < 0)
      this->renderPassUnbindFramebuffer();
  }

  
  void DxvkContext::clearImageViewCs(
    const Rc<DxvkImageView>&    imageView,
          VkOffset3D            offset,
          VkExtent3D            extent,
          VkClearValue          value) {
    this->spillRenderPass();
    this->unbindComputePipeline();
    
    m_barriers.recordCommands(m_cmd);
    
    // Query pipeline objects to use for this clear operation
    DxvkMetaClearPipeline pipeInfo = m_metaClear->getClearImagePipeline(
      imageView->type(), imageFormatInfo(imageView->info().format)->flags);
    
    // Create a descriptor set pointing to the view
    VkDescriptorSet descriptorSet =
      m_cmd->allocateDescriptorSet(pipeInfo.dsetLayout);
    
    VkDescriptorImageInfo viewInfo;
    viewInfo.sampler      = VK_NULL_HANDLE;
    viewInfo.imageView    = imageView->handle();
    viewInfo.imageLayout  = imageView->imageInfo().layout;
    
    VkWriteDescriptorSet descriptorWrite;
    descriptorWrite.sType            = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrite.pNext            = nullptr;
    descriptorWrite.dstSet           = descriptorSet;
    descriptorWrite.dstBinding       = 0;
    descriptorWrite.dstArrayElement  = 0;
    descriptorWrite.descriptorCount  = 1;
    descriptorWrite.descriptorType   = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    descriptorWrite.pImageInfo       = &viewInfo;
    descriptorWrite.pBufferInfo      = nullptr;
    descriptorWrite.pTexelBufferView = nullptr;
    m_cmd->updateDescriptorSets(1, &descriptorWrite);
    
    // Prepare shader arguments
    DxvkMetaClearArgs pushArgs;
    pushArgs.clearValue = value.color;
    pushArgs.offset = offset;
    pushArgs.extent = extent;
    
    VkExtent3D workgroups = util::computeBlockCount(
      pushArgs.extent, pipeInfo.workgroupSize);
    
    if (imageView->type() == VK_IMAGE_VIEW_TYPE_1D_ARRAY)
      workgroups.height = imageView->subresources().layerCount;
    else if (imageView->type() == VK_IMAGE_VIEW_TYPE_2D_ARRAY)
      workgroups.depth = imageView->subresources().layerCount;
    
    m_cmd->cmdBindPipeline(
      VK_PIPELINE_BIND_POINT_COMPUTE,
      pipeInfo.pipeline);
    m_cmd->cmdBindDescriptorSet(
      VK_PIPELINE_BIND_POINT_COMPUTE,
      pipeInfo.pipeLayout, descriptorSet,
      0, nullptr);
    m_cmd->cmdPushConstants(
      pipeInfo.pipeLayout,
      VK_SHADER_STAGE_COMPUTE_BIT,
      0, sizeof(pushArgs), &pushArgs);
    m_cmd->cmdDispatch(
      workgroups.width,
      workgroups.height,
      workgroups.depth);
    
    m_barriers.accessImage(
      imageView->image(),
      imageView->subresources(),
      imageView->imageInfo().layout,
      VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
      VK_ACCESS_SHADER_WRITE_BIT,
      imageView->imageInfo().layout,
      imageView->imageInfo().stages,
      imageView->imageInfo().access);
    
    m_cmd->trackResource(imageView);
    m_cmd->trackResource(imageView->image());
  }

  
  void DxvkContext::startRenderPass() {
    if (!m_flags.test(DxvkContextFlag::GpRenderPassBound)
     && (m_state.om.framebuffer != nullptr)) {
      m_flags.set(DxvkContextFlag::GpRenderPassBound);
      m_flags.clr(DxvkContextFlag::GpClearRenderTargets);

      m_barriers.recordCommands(m_cmd);

      this->renderPassBindFramebuffer(
        m_state.om.framebuffer,
        m_state.om.renderPassOps,
        m_state.om.clearValues.size(),
        m_state.om.clearValues.data());
      
      // Don't discard image contents if we have
      // to spill the current render pass
      this->resetRenderPassOps(
        m_state.om.renderTargets,
        m_state.om.renderPassOps);
      
      // Begin occlusion queries
      m_queries.beginQueries(m_cmd, {
        VK_QUERY_TYPE_OCCLUSION,
        VK_QUERY_TYPE_PIPELINE_STATISTICS });
    }
  }
  
  
  void DxvkContext::spillRenderPass() {
    if (m_flags.test(DxvkContextFlag::GpClearRenderTargets))
      this->startRenderPass();
    
    if (m_flags.test(DxvkContextFlag::GpRenderPassBound)) {
      m_flags.clr(DxvkContextFlag::GpRenderPassBound);

      m_queries.endQueries(m_cmd, {
        VK_QUERY_TYPE_OCCLUSION,
        VK_QUERY_TYPE_PIPELINE_STATISTICS });
      
      this->renderPassUnbindFramebuffer();
    }
  }
  
  
  void DxvkContext::renderPassBindFramebuffer(
    const Rc<DxvkFramebuffer>&  framebuffer,
    const DxvkRenderPassOps&    ops,
          uint32_t              clearValueCount,
    const VkClearValue*         clearValues) {
    const DxvkFramebufferSize fbSize = framebuffer->size();
    
    VkRect2D renderArea;
    renderArea.offset = VkOffset2D { 0, 0 };
    renderArea.extent = VkExtent2D { fbSize.width, fbSize.height };
    
    VkRenderPassBeginInfo info;
    info.sType                = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    info.pNext                = nullptr;
    info.renderPass           = framebuffer->getRenderPassHandle(ops);
    info.framebuffer          = framebuffer->handle();
    info.renderArea           = renderArea;
    info.clearValueCount      = clearValueCount;
    info.pClearValues         = clearValues;
    
    m_cmd->cmdBeginRenderPass(&info,
      VK_SUBPASS_CONTENTS_INLINE);
    
    m_cmd->trackResource(framebuffer);

    for (uint32_t i = 0; i < framebuffer->numAttachments(); i++) {
      m_cmd->trackResource(framebuffer->getAttachment(i).view);
      m_cmd->trackResource(framebuffer->getAttachment(i).view->image());
    }

    m_cmd->addStatCtr(DxvkStatCounter::CmdRenderPassCount, 1);
  }
  
  
  void DxvkContext::renderPassUnbindFramebuffer() {
    m_cmd->cmdEndRenderPass();
  }
  
  
  void DxvkContext::resetRenderPassOps(
    const DxvkRenderTargets&    renderTargets,
          DxvkRenderPassOps&    renderPassOps) {
    renderPassOps.depthOps = renderTargets.depth.view != nullptr
      ? DxvkDepthAttachmentOps {
          VK_ATTACHMENT_LOAD_OP_LOAD,
          VK_ATTACHMENT_LOAD_OP_LOAD,
          renderTargets.depth.view->imageInfo().layout,
          VK_ATTACHMENT_STORE_OP_STORE,
          VK_ATTACHMENT_STORE_OP_STORE,
          renderTargets.depth.view->imageInfo().layout }
      : DxvkDepthAttachmentOps { };
    
    for (uint32_t i = 0; i < MaxNumRenderTargets; i++) {
      renderPassOps.colorOps[i] = renderTargets.color[i].view != nullptr
        ? DxvkColorAttachmentOps {
            VK_ATTACHMENT_LOAD_OP_LOAD,
            renderTargets.color[i].view->imageInfo().layout,
            VK_ATTACHMENT_STORE_OP_STORE,
            renderTargets.color[i].view->imageInfo().layout }
        : DxvkColorAttachmentOps { };
    }
    
    // TODO provide a sane alternative for this
    if (renderPassOps.colorOps[0].loadLayout == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR) {
      renderPassOps.colorOps[0].loadOp     = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
      renderPassOps.colorOps[0].loadLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    }
  }
  
  
  void DxvkContext::unbindComputePipeline() {
    m_flags.set(
      DxvkContextFlag::CpDirtyPipeline,
      DxvkContextFlag::CpDirtyPipelineState,
      DxvkContextFlag::CpDirtyResources);
    
    m_cpActivePipeline = VK_NULL_HANDLE;
  }
  
  
  void DxvkContext::updateComputePipeline() {
    if (m_flags.test(DxvkContextFlag::CpDirtyPipeline)) {
      m_flags.clr(DxvkContextFlag::CpDirtyPipeline);
      
      m_state.cp.state.bsBindingState.clear();
      m_state.cp.pipeline = m_pipeMgr->createComputePipeline(m_state.cp.cs.shader);
      
      if (m_state.cp.pipeline != nullptr)
        m_cmd->trackResource(m_state.cp.pipeline);
    }
  }
  
  
  void DxvkContext::updateComputePipelineState() {
    if (m_flags.test(DxvkContextFlag::CpDirtyPipelineState)) {
      m_flags.clr(DxvkContextFlag::CpDirtyPipelineState);
      
      m_cpActivePipeline = m_state.cp.pipeline != nullptr
        ? m_state.cp.pipeline->getPipelineHandle(m_state.cp.state, m_cmd->statCounters())
        : VK_NULL_HANDLE;
      
      if (m_cpActivePipeline != VK_NULL_HANDLE) {
        m_cmd->cmdBindPipeline(
          VK_PIPELINE_BIND_POINT_COMPUTE,
          m_cpActivePipeline);
      }
    }
  }
  
  
  void DxvkContext::unbindGraphicsPipeline() {
    m_flags.set(
      DxvkContextFlag::GpDirtyPipeline,
      DxvkContextFlag::GpDirtyPipelineState,
      DxvkContextFlag::GpDirtyResources,
      DxvkContextFlag::GpDirtyVertexBuffers,
      DxvkContextFlag::GpDirtyIndexBuffer);
    
    m_gpActivePipeline = VK_NULL_HANDLE;
  }
  
  
  void DxvkContext::updateGraphicsPipeline() {
    if (m_flags.test(DxvkContextFlag::GpDirtyPipeline)) {
      m_flags.clr(DxvkContextFlag::GpDirtyPipeline);
      
      m_state.gp.state.bsBindingState.clear();
      m_state.gp.pipeline = m_pipeMgr->createGraphicsPipeline(
        m_state.gp.vs.shader,
        m_state.gp.tcs.shader, m_state.gp.tes.shader,
        m_state.gp.gs.shader, m_state.gp.fs.shader);
      
      if (m_state.gp.pipeline != nullptr)
        m_cmd->trackResource(m_state.gp.pipeline);
    }
  }
  
  
  void DxvkContext::updateGraphicsPipelineState() {
    if (m_flags.test(DxvkContextFlag::GpDirtyPipelineState)) {
      m_flags.clr(DxvkContextFlag::GpDirtyPipelineState);
      
      for (uint32_t i = 0; i < m_state.gp.state.ilBindingCount; i++) {
        const uint32_t binding = m_state.gp.state.ilBindings[i].binding;
        
        m_state.gp.state.ilBindings[i].stride
          = (m_state.vi.bindingMask & (1u << binding)) != 0
            ? m_state.vi.vertexStrides[binding]
            : 0;
      }
      
      for (uint32_t i = m_state.gp.state.ilBindingCount; i < MaxNumVertexBindings; i++)
        m_state.gp.state.ilBindings[i].stride = 0;
      
      m_gpActivePipeline = m_state.gp.pipeline != nullptr && m_state.om.framebuffer != nullptr
        ? m_state.gp.pipeline->getPipelineHandle(m_state.gp.state,
            m_state.om.framebuffer->getRenderPass(), m_cmd->statCounters())
        : VK_NULL_HANDLE;
      
      if (m_gpActivePipeline != VK_NULL_HANDLE) {
        m_cmd->cmdBindPipeline(
          VK_PIPELINE_BIND_POINT_GRAPHICS,
          m_gpActivePipeline);
      }

      m_flags.set(
        DxvkContextFlag::GpDirtyBlendConstants,
        DxvkContextFlag::GpDirtyStencilRef,
        DxvkContextFlag::GpDirtyViewport,
        DxvkContextFlag::GpDirtyDepthBias);
    }
  }
  
  
  void DxvkContext::updateComputeShaderResources() {
    if (m_state.cp.pipeline == nullptr)
      return;

    if ((m_flags.test(DxvkContextFlag::CpDirtyResources))
     || (m_flags.test(DxvkContextFlag::CpDirtyDescriptorOffsets)
      && m_state.cp.pipeline->layout()->hasStaticBufferBindings())) {
      m_flags.clr(DxvkContextFlag::CpDirtyResources);

      this->updateShaderResources(
        VK_PIPELINE_BIND_POINT_COMPUTE,
        m_state.cp.pipeline->layout());

      m_flags.set(
        DxvkContextFlag::CpDirtyDescriptorSet,
        DxvkContextFlag::CpDirtyDescriptorOffsets);
    }
  }
  
  
  void DxvkContext::updateComputeShaderDescriptors() {
    if (m_state.cp.pipeline == nullptr)
      return;

    if (m_flags.test(DxvkContextFlag::CpDirtyDescriptorSet)) {
      m_cpSet = this->updateShaderDescriptors(
        VK_PIPELINE_BIND_POINT_COMPUTE,
        m_state.cp.state.bsBindingState,
        m_state.cp.pipeline->layout());
    }

    if (m_flags.test(DxvkContextFlag::CpDirtyDescriptorOffsets)) {
      this->updateShaderDescriptorSetBinding(
        VK_PIPELINE_BIND_POINT_COMPUTE, m_cpSet,
        m_state.cp.pipeline->layout());
    }

    m_flags.clr(
      DxvkContextFlag::CpDirtyDescriptorOffsets,
      DxvkContextFlag::CpDirtyDescriptorSet);
  }
  
  
  void DxvkContext::updateGraphicsShaderResources() {
    if (m_state.gp.pipeline == nullptr)
      return;
    
    if ((m_flags.test(DxvkContextFlag::GpDirtyResources))
     || (m_flags.test(DxvkContextFlag::GpDirtyDescriptorOffsets)
      && m_state.gp.pipeline->layout()->hasStaticBufferBindings())) {
      m_flags.clr(DxvkContextFlag::GpDirtyResources);

      this->updateShaderResources(
        VK_PIPELINE_BIND_POINT_GRAPHICS,
        m_state.gp.pipeline->layout());

      m_flags.set(
        DxvkContextFlag::GpDirtyDescriptorSet,
        DxvkContextFlag::GpDirtyDescriptorOffsets);
    }
  }
  
  
  void DxvkContext::updateGraphicsShaderDescriptors() {
    if (m_state.gp.pipeline == nullptr)
      return;

    if (m_flags.test(DxvkContextFlag::GpDirtyDescriptorSet)) {
      m_gpSet = this->updateShaderDescriptors(
        VK_PIPELINE_BIND_POINT_GRAPHICS,
        m_state.gp.state.bsBindingState,
        m_state.gp.pipeline->layout());
    }

    if (m_flags.test(DxvkContextFlag::GpDirtyDescriptorOffsets)) {
      this->updateShaderDescriptorSetBinding(
        VK_PIPELINE_BIND_POINT_GRAPHICS, m_gpSet,
        m_state.gp.pipeline->layout());
    }

    m_flags.clr(
      DxvkContextFlag::GpDirtyDescriptorOffsets,
      DxvkContextFlag::GpDirtyDescriptorSet);
  }
  
  
  void DxvkContext::updateShaderResources(
          VkPipelineBindPoint     bindPoint,
    const DxvkPipelineLayout*     layout) {
    DxvkBindingState& bindingState =
      bindPoint == VK_PIPELINE_BIND_POINT_GRAPHICS
        ? m_state.gp.state.bsBindingState
        : m_state.cp.state.bsBindingState;
    
    bool updatePipelineState = false;
    
    DxvkAttachment depthAttachment;
    
    if (bindPoint == VK_PIPELINE_BIND_POINT_GRAPHICS && m_state.om.framebuffer != nullptr)
      depthAttachment = m_state.om.framebuffer->getDepthTarget();
    
    for (uint32_t i = 0; i < layout->bindingCount(); i++) {
      const auto& binding = layout->binding(i);
      const auto& res     = m_rc[binding.slot];
      
      switch (binding.type) {
        case VK_DESCRIPTOR_TYPE_SAMPLER:
          if (res.sampler != nullptr) {
            updatePipelineState |= bindingState.setBound(i);
            
            m_descInfos[i].image.sampler     = res.sampler->handle();
            m_descInfos[i].image.imageView   = VK_NULL_HANDLE;
            m_descInfos[i].image.imageLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            
            m_cmd->trackResource(res.sampler);
          } else {
            updatePipelineState |= bindingState.setUnbound(i);
            m_descInfos[i].image = m_device->dummySamplerDescriptor();
          } break;
        
        case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
        case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
          if (res.imageView != nullptr && res.imageView->handle(binding.view) != VK_NULL_HANDLE) {
            updatePipelineState |= bindingState.setBound(i);
            
            m_descInfos[i].image.sampler     = VK_NULL_HANDLE;
            m_descInfos[i].image.imageView   = res.imageView->handle(binding.view);
            m_descInfos[i].image.imageLayout = res.imageView->imageInfo().layout;
            
            if (depthAttachment.view != nullptr
             && depthAttachment.view->image() == res.imageView->image())
              m_descInfos[i].image.imageLayout = depthAttachment.layout;
            
            m_cmd->trackResource(res.imageView);
            m_cmd->trackResource(res.imageView->image());
          } else {
            updatePipelineState |= bindingState.setUnbound(i);
            m_descInfos[i].image = m_device->dummyImageViewDescriptor(binding.view);
          } break;
        
        case VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:
        case VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER:
          if (res.bufferView != nullptr) {
            updatePipelineState |= bindingState.setBound(i);
            
            res.bufferView->updateView();
            m_descInfos[i].texelBuffer = res.bufferView->handle();
            
            m_cmd->trackResource(res.bufferView->viewResource());
            m_cmd->trackResource(res.bufferView->bufferResource());
          } else {
            updatePipelineState |= bindingState.setUnbound(i);
            m_descInfos[i].texelBuffer = m_device->dummyBufferViewDescriptor();
          } break;
        
        case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
        case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
          if (res.bufferSlice.defined()) {
            updatePipelineState |= bindingState.setBound(i);
            
            auto physicalSlice = res.bufferSlice.physicalSlice();
            m_descInfos[i].buffer.buffer = physicalSlice.handle();
            m_descInfos[i].buffer.offset = physicalSlice.offset();
            m_descInfos[i].buffer.range  = physicalSlice.length();
            
            m_cmd->trackResource(physicalSlice.resource());
          } else {
            updatePipelineState |= bindingState.setUnbound(i);
            m_descInfos[i].buffer = m_device->dummyBufferDescriptor();
          } break;
        
        case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:
        case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC:
          if (res.bufferSlice.defined()) {
            updatePipelineState |= bindingState.setBound(i);
            
            auto physicalSlice = res.bufferSlice.physicalSlice();
            m_descInfos[i].buffer.buffer = physicalSlice.handle();
            m_descInfos[i].buffer.offset = 0; /* dynamic */
            m_descInfos[i].buffer.range  = physicalSlice.length();
            
            m_cmd->trackResource(physicalSlice.resource());
          } else {
            updatePipelineState |= bindingState.setUnbound(i);
            m_descInfos[i].buffer = m_device->dummyBufferDescriptor();
          } break;
        
        default:
          Logger::err(str::format("DxvkContext: Unhandled descriptor type: ", binding.type));
      }
    }

    if (updatePipelineState) {
      m_flags.set(bindPoint == VK_PIPELINE_BIND_POINT_GRAPHICS
        ? DxvkContextFlag::GpDirtyPipelineState
        : DxvkContextFlag::CpDirtyPipelineState);
    }
  }
  
  
  VkDescriptorSet DxvkContext::updateShaderDescriptors(
          VkPipelineBindPoint     bindPoint,
    const DxvkBindingState&       bindingState,
    const DxvkPipelineLayout*     layout) {
    VkDescriptorSet descriptorSet = VK_NULL_HANDLE;

    if (layout->bindingCount() != 0) {
      descriptorSet = m_cmd->allocateDescriptorSet(
        layout->descriptorSetLayout());
      
      m_cmd->updateDescriptorSetWithTemplate(
        descriptorSet, layout->descriptorTemplate(),
        m_descInfos.data());
    }

    return descriptorSet;
  }


  void DxvkContext::updateShaderDescriptorSetBinding(
          VkPipelineBindPoint     bindPoint,
          VkDescriptorSet         set,
    const DxvkPipelineLayout*     layout) {
    if (set != VK_NULL_HANDLE) {
      for (uint32_t i = 0; i < layout->dynamicBindingCount(); i++) {
        const auto& binding = layout->dynamicBinding(i);
        const auto& res     = m_rc[binding.slot];

        m_descOffsets[i] = res.bufferSlice.defined()
          ? res.bufferSlice.physicalSlice().offset()
          : 0;
      }
      
      m_cmd->cmdBindDescriptorSet(bindPoint,
        layout->pipelineLayout(), set,
        layout->dynamicBindingCount(),
        m_descOffsets.data());
    }
  }
  
  
  void DxvkContext::updateFramebuffer() {
    if (m_flags.test(DxvkContextFlag::GpDirtyFramebuffer)) {
      m_flags.clr(DxvkContextFlag::GpDirtyFramebuffer);
      
      this->spillRenderPass();
      
      auto fb = m_device->createFramebuffer(m_state.om.renderTargets);
      
      m_state.gp.state.msSampleCount = fb->getSampleCount();
      m_state.om.framebuffer = fb;

      for (uint32_t i = 0; i < MaxNumRenderTargets; i++) {
        Rc<DxvkImageView> attachment = fb->getColorTarget(i).view;

        m_state.gp.state.omComponentMapping[i] = attachment != nullptr
          ? util::invertComponentMapping(attachment->info().swizzle)
          : VkComponentMapping();
      }
      
      m_flags.set(DxvkContextFlag::GpDirtyPipelineState);
    }
  }
  
  
  void DxvkContext::updateIndexBufferBinding() {
    if (m_flags.test(DxvkContextFlag::GpDirtyIndexBuffer)) {
      m_flags.clr(DxvkContextFlag::GpDirtyIndexBuffer);
      
      if (m_state.vi.indexBuffer.defined()) {
        auto physicalSlice = m_state.vi.indexBuffer.physicalSlice();
        
        m_cmd->cmdBindIndexBuffer(
          physicalSlice.handle(),
          physicalSlice.offset(),
          m_state.vi.indexType);
        m_cmd->trackResource(
          physicalSlice.resource());
      } else {
        m_cmd->cmdBindIndexBuffer(
          m_device->dummyBufferHandle(),
          0, VK_INDEX_TYPE_UINT32);
      }
    }
  }
  
  
  void DxvkContext::updateVertexBufferBindings() {
    if (m_flags.test(DxvkContextFlag::GpDirtyVertexBuffers)) {
      m_flags.clr(DxvkContextFlag::GpDirtyVertexBuffers);
      
      std::array<VkBuffer,     MaxNumVertexBindings> buffers;
      std::array<VkDeviceSize, MaxNumVertexBindings> offsets;
      
      // Set buffer handles and offsets for active bindings
      uint32_t bindingCount = 0;
      uint32_t bindingMask  = 0;
      
      for (uint32_t i = 0; i < m_state.gp.state.ilBindingCount; i++) {
        const uint32_t binding = m_state.gp.state.ilBindings[i].binding;
        bindingCount = std::max(bindingCount, binding + 1);
        
        if (m_state.vi.vertexBuffers[binding].defined()) {
          auto vbo = m_state.vi.vertexBuffers[binding].physicalSlice();
          
          buffers[binding] = vbo.handle();
          offsets[binding] = vbo.offset();
          
          bindingMask |= 1u << binding;
          
          m_cmd->trackResource(vbo.resource());
        }
      }
      
      // Bind a dummy buffer to the remaining bindings
      uint32_t bindingsUsed = (1u << bindingCount) - 1u;
      uint32_t bindingsSet  = bindingMask;
      
      while (bindingsSet != bindingsUsed) {
        uint32_t binding = bit::tzcnt(~bindingsSet);
        
        buffers[binding] = m_device->dummyBufferHandle();
        offsets[binding] = 0;
        
        bindingsSet |= 1u << binding;
      }
      
      // Bind all vertex buffers at once
      if (bindingCount != 0) {
        m_cmd->cmdBindVertexBuffers(0, bindingCount,
          buffers.data(), offsets.data());
      }
      
      // If the set of active bindings has changed, we'll
      // need to adjust the strides of the inactive ones
      // and compile a new pipeline
      if (m_state.vi.bindingMask != bindingMask) {
        m_flags.set(DxvkContextFlag::GpDirtyPipelineState);
        m_state.vi.bindingMask = bindingMask;
      }
    }
  }

  
  void DxvkContext::updateDynamicState() {
    if (m_gpActivePipeline == VK_NULL_HANDLE)
      return;
    
    if (m_flags.test(DxvkContextFlag::GpDirtyViewport)) {
      uint32_t viewportCount = m_state.gp.state.rsViewportCount;
      m_cmd->cmdSetViewport(0, viewportCount, m_state.vp.viewports.data());
      m_cmd->cmdSetScissor (0, viewportCount, m_state.vp.scissorRects.data());
    }

    if (m_flags.test(DxvkContextFlag::GpDirtyBlendConstants))
      m_cmd->cmdSetBlendConstants(&m_state.om.blendConstants.r);

    if (m_flags.test(DxvkContextFlag::GpDirtyStencilRef))
      m_cmd->cmdSetStencilReference(VK_STENCIL_FRONT_AND_BACK, m_state.om.stencilReference);
    
    if (m_flags.test(DxvkContextFlag::GpDirtyDepthBias)) {
      m_cmd->cmdSetDepthBias(
        m_state.ds.depthBiasConstant,
        m_state.ds.depthBiasClamp,
        m_state.ds.depthBiasSlope);
    }
    
    m_flags.clr(
      DxvkContextFlag::GpDirtyBlendConstants,
      DxvkContextFlag::GpDirtyStencilRef,
      DxvkContextFlag::GpDirtyViewport,
      DxvkContextFlag::GpDirtyDepthBias);
  }
  
  
  bool DxvkContext::validateComputeState() {
    return m_cpActivePipeline != VK_NULL_HANDLE;
  }
  
  
  bool DxvkContext::validateGraphicsState() {
    if (m_gpActivePipeline == VK_NULL_HANDLE)
      return false;
    
    if (!m_flags.test(DxvkContextFlag::GpRenderPassBound))
      return false;
    
    return true;
  }
  
  
  void DxvkContext::commitComputeState() {
    this->spillRenderPass();
    this->updateComputePipeline();
    this->updateComputeShaderResources();
    this->updateComputePipelineState();
    this->updateComputeShaderDescriptors();
  }
  
  
  void DxvkContext::commitGraphicsState() {
    this->updateFramebuffer();
    this->startRenderPass();
    this->updateGraphicsPipeline();
    this->updateIndexBufferBinding();
    this->updateVertexBufferBindings();
    this->updateGraphicsShaderResources();
    this->updateGraphicsPipelineState();
    this->updateGraphicsShaderDescriptors();
    this->updateDynamicState();
  }
  
  
  void DxvkContext::commitComputeInitBarriers() {
    auto layout = m_state.cp.pipeline->layout();

    bool requiresBarrier = false;

    for (uint32_t i = 0; i < layout->bindingCount() && !requiresBarrier; i++) {
      if (m_state.cp.state.bsBindingState.isBound(i)) {
        const DxvkDescriptorSlot binding = layout->binding(i);
        const DxvkShaderResourceSlot& slot = m_rc[binding.slot];

        DxvkAccessFlags access = DxvkAccess::Read;
        
        switch (binding.type) {
          case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
          case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC:
            access.set(DxvkAccess::Write);
            /* fall through */
          
          case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
          case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:
            requiresBarrier = m_barriers.isBufferDirty(
              slot.bufferSlice.physicalSlice(), access);
            break;
        
          case VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER:
            access.set(DxvkAccess::Write);
            /* fall through */

          case VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:
            requiresBarrier = m_barriers.isBufferDirty(
              slot.bufferView->physicalSlice(), access);
            break;
          
          case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
            access.set(DxvkAccess::Write);
            /* fall through */

          case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
            requiresBarrier = m_barriers.isImageDirty(
              slot.imageView->image(),
              slot.imageView->subresources(),
              access);
            break;

          default:
            /* nothing to do */;
        }
      }
    }

    if (requiresBarrier)
      m_barriers.recordCommands(m_cmd);
  }
  

  void DxvkContext::commitComputePostBarriers() {
    auto layout = m_state.cp.pipeline->layout();
    
    for (uint32_t i = 0; i < layout->bindingCount(); i++) {
      if (m_state.cp.state.bsBindingState.isBound(i)) {
        const DxvkDescriptorSlot binding = layout->binding(i);
        const DxvkShaderResourceSlot& slot = m_rc[binding.slot];

        VkPipelineStageFlags stages = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
        VkAccessFlags        access = VK_ACCESS_SHADER_READ_BIT;
        
        switch (binding.type) {
          case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
          case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC:
            access |= VK_ACCESS_SHADER_WRITE_BIT;
            /* fall through */
          
          case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
          case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:
            m_barriers.accessBuffer(
              slot.bufferSlice.physicalSlice(),
              stages, access,
              slot.bufferSlice.bufferInfo().stages,
              slot.bufferSlice.bufferInfo().access);
            break;
        
          case VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER:
            access |= VK_ACCESS_SHADER_WRITE_BIT;
            /* fall through */

          case VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:
            m_barriers.accessBuffer(
              slot.bufferView->physicalSlice(),
              stages, access,
              slot.bufferView->bufferInfo().stages,
              slot.bufferView->bufferInfo().access);
            break;
          
          case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
            access |= VK_ACCESS_SHADER_WRITE_BIT;
            /* fall through */

          case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
            m_barriers.accessImage(
              slot.imageView->image(),
              slot.imageView->subresources(),
              slot.imageView->imageInfo().layout,
              stages, access,
              slot.imageView->imageInfo().layout,
              slot.imageView->imageInfo().stages,
              slot.imageView->imageInfo().access);
            break;

          default:
            /* nothing to do */;
        }
      }
    }
  }
  
}