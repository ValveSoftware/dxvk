#include "dxbc_compiler.h"

namespace dxvk {

  constexpr uint32_t Icb_BindingSlotId   = 14;
  constexpr uint32_t Icb_MaxBakedDwords  = 16;
  
  constexpr uint32_t PerVertex_Position  = 0;
  constexpr uint32_t PerVertex_CullDist  = 1;
  constexpr uint32_t PerVertex_ClipDist  = 2;
  
  DxbcCompiler::DxbcCompiler(
    const std::string&        fileName,
    const DxbcModuleInfo&     moduleInfo,
    const DxbcProgramVersion& version,
    const Rc<DxbcIsgn>&       isgn,
    const Rc<DxbcIsgn>&       osgn,
    const DxbcAnalysisInfo&   analysis)
  : m_moduleInfo(moduleInfo),
    m_version   (version),
    m_isgn      (isgn),
    m_osgn      (osgn),
    m_analysis  (&analysis) {
    // Declare an entry point ID. We'll need it during the
    // initialization phase where the execution mode is set.
    m_entryPointId = m_module.allocateId();
    
    // Set the shader name so that we recognize it in renderdoc
    m_module.setDebugSource(
      spv::SourceLanguageUnknown, 0,
      m_module.addDebugString(fileName.c_str()),
      nullptr);
    
    // Set the memory model. This is the same for all shaders.
    m_module.setMemoryModel(
      spv::AddressingModelLogical,
      spv::MemoryModelGLSL450);
    
    // Make sure our interface registers are clear
    for (uint32_t i = 0; i < DxbcMaxInterfaceRegs; i++) {
      m_vRegs.at(i) = DxbcRegisterPointer { };
      m_oRegs.at(i) = DxbcRegisterPointer { };
    }
    
    // Clear spec constants
    for (uint32_t i = 0; i < m_specConstants.size(); i++) {
      m_specConstants.at(i) = DxbcRegisterValue {
        DxbcVectorType { DxbcScalarType::Uint32, 0 },
        0 };
    }
    
    this->emitInit();
  }
  
  
  DxbcCompiler::~DxbcCompiler() {
    
  }
  
  
  void DxbcCompiler::processInstruction(const DxbcShaderInstruction& ins) {
    switch (ins.opClass) {
      case DxbcInstClass::Declaration:
        return this->emitDcl(ins);
      
      case DxbcInstClass::CustomData:
        return this->emitCustomData(ins);
        
      case DxbcInstClass::Atomic:
        return this->emitAtomic(ins);
        
      case DxbcInstClass::AtomicCounter:
        return this->emitAtomicCounter(ins);
        
      case DxbcInstClass::Barrier:
        return this->emitBarrier(ins);
        
      case DxbcInstClass::BitExtract:
        return this->emitBitExtract(ins);
        
      case DxbcInstClass::BitInsert:
        return this->emitBitInsert(ins);
        
      case DxbcInstClass::BitScan:
        return this->emitBitScan(ins);
        
      case DxbcInstClass::BufferQuery:
        return this->emitBufferQuery(ins);
        
      case DxbcInstClass::BufferLoad:
        return this->emitBufferLoad(ins);
        
      case DxbcInstClass::BufferStore:
        return this->emitBufferStore(ins);
        
      case DxbcInstClass::ConvertFloat16:
        return this->emitConvertFloat16(ins);
        
      case DxbcInstClass::ConvertFloat64:
        return this->emitConvertFloat64(ins);
        
      case DxbcInstClass::ControlFlow:
        return this->emitControlFlow(ins);
        
      case DxbcInstClass::GeometryEmit:
        return this->emitGeometryEmit(ins);
      
      case DxbcInstClass::HullShaderPhase:
        return this->emitHullShaderPhase(ins);
      
      case DxbcInstClass::HullShaderInstCnt:
        return this->emitHullShaderInstCnt(ins);
      
      case DxbcInstClass::Interpolate:
        return this->emitInterpolate(ins);
      
      case DxbcInstClass::NoOperation:
        return;
      
      case DxbcInstClass::TextureQuery:
        return this->emitTextureQuery(ins);
        
      case DxbcInstClass::TextureQueryLod:
        return this->emitTextureQueryLod(ins);
        
      case DxbcInstClass::TextureQueryMs:
        return this->emitTextureQueryMs(ins);
        
      case DxbcInstClass::TextureQueryMsPos:
        return this->emitTextureQueryMsPos(ins);
        
      case DxbcInstClass::TextureFetch:
        return this->emitTextureFetch(ins);
        
      case DxbcInstClass::TextureGather:
        return this->emitTextureGather(ins);
        
      case DxbcInstClass::TextureSample:
        return this->emitTextureSample(ins);
        
      case DxbcInstClass::TypedUavLoad:
        return this->emitTypedUavLoad(ins);
        
      case DxbcInstClass::TypedUavStore:
        return this->emitTypedUavStore(ins);
        
      case DxbcInstClass::VectorAlu:
        return this->emitVectorAlu(ins);
        
      case DxbcInstClass::VectorCmov:
        return this->emitVectorCmov(ins);
        
      case DxbcInstClass::VectorCmp:
        return this->emitVectorCmp(ins);
        
      case DxbcInstClass::VectorDeriv:
        return this->emitVectorDeriv(ins);
        
      case DxbcInstClass::VectorDot:
        return this->emitVectorDot(ins);
        
      case DxbcInstClass::VectorIdiv:
        return this->emitVectorIdiv(ins);
        
      case DxbcInstClass::VectorImul:
        return this->emitVectorImul(ins);
        
      case DxbcInstClass::VectorShift:
        return this->emitVectorShift(ins);
        
      case DxbcInstClass::VectorSinCos:
        return this->emitVectorSinCos(ins);
        
      default:
        Logger::warn(
          str::format("DxbcCompiler: Unhandled opcode class: ",
          ins.op));
    }
  }
  
  
  Rc<DxvkShader> DxbcCompiler::finalize() {
    // Depending on the shader type, this will prepare
    // input registers, call various shader functions
    // and write back the output registers.
    switch (m_version.type()) {
      case DxbcProgramType::VertexShader:   this->emitVsFinalize(); break;
      case DxbcProgramType::HullShader:     this->emitHsFinalize(); break;
      case DxbcProgramType::DomainShader:   this->emitDsFinalize(); break;
      case DxbcProgramType::GeometryShader: this->emitGsFinalize(); break;
      case DxbcProgramType::PixelShader:    this->emitPsFinalize(); break;
      case DxbcProgramType::ComputeShader:  this->emitCsFinalize(); break;
    }
    
    // Declare the entry point, we now have all the
    // information we need, including the interfaces
    m_module.addEntryPoint(m_entryPointId,
      m_version.executionModel(), "main",
      m_entryPointInterfaces.size(),
      m_entryPointInterfaces.data());
    m_module.setDebugName(m_entryPointId, "main");
    
    // Create the shader module object
    return new DxvkShader(
      m_version.shaderStage(),
      m_resourceSlots.size(),
      m_resourceSlots.data(),
      m_interfaceSlots,
      m_module.compile(),
      std::move(m_immConstData));
  }
  
  
  void DxbcCompiler::emitDcl(const DxbcShaderInstruction& ins) {
    switch (ins.op) {
      case DxbcOpcode::DclGlobalFlags:
        return this->emitDclGlobalFlags(ins);
        
      case DxbcOpcode::DclIndexRange:
        return;  // not needed for anything
        
      case DxbcOpcode::DclTemps:
        return this->emitDclTemps(ins);
        
      case DxbcOpcode::DclIndexableTemp:
        return this->emitDclIndexableTemp(ins);
        
      case DxbcOpcode::DclInput:
      case DxbcOpcode::DclInputSgv:
      case DxbcOpcode::DclInputSiv:
      case DxbcOpcode::DclInputPs:
      case DxbcOpcode::DclInputPsSgv:
      case DxbcOpcode::DclInputPsSiv:
      case DxbcOpcode::DclOutput:
      case DxbcOpcode::DclOutputSgv:
      case DxbcOpcode::DclOutputSiv:
        return this->emitDclInterfaceReg(ins);
        
      case DxbcOpcode::DclConstantBuffer:
        return this->emitDclConstantBuffer(ins);
        
      case DxbcOpcode::DclSampler:
        return this->emitDclSampler(ins);
      
      case DxbcOpcode::DclStream:
        return this->emitDclStream(ins);
        
      case DxbcOpcode::DclUavTyped:
      case DxbcOpcode::DclResource:
        return this->emitDclResourceTyped(ins);
        
      case DxbcOpcode::DclUavRaw:
      case DxbcOpcode::DclResourceRaw:
      case DxbcOpcode::DclUavStructured:
      case DxbcOpcode::DclResourceStructured:
        return this->emitDclResourceRawStructured(ins);
      
      case DxbcOpcode::DclThreadGroupSharedMemoryRaw:
      case DxbcOpcode::DclThreadGroupSharedMemoryStructured:
        return this->emitDclThreadGroupSharedMemory(ins);
        
      case DxbcOpcode::DclGsInputPrimitive:
        return this->emitDclGsInputPrimitive(ins);
        
      case DxbcOpcode::DclGsOutputPrimitiveTopology:
        return this->emitDclGsOutputTopology(ins);
        
      case DxbcOpcode::DclMaxOutputVertexCount:
        return this->emitDclMaxOutputVertexCount(ins);
        
      case DxbcOpcode::DclInputControlPointCount:
        return this->emitDclInputControlPointCount(ins);
      
      case DxbcOpcode::DclOutputControlPointCount:
        return this->emitDclOutputControlPointCount(ins);
      
      case DxbcOpcode::DclHsMaxTessFactor:
        return this->emitDclHsMaxTessFactor(ins);
        
      case DxbcOpcode::DclTessDomain:
        return this->emitDclTessDomain(ins);
      
      case DxbcOpcode::DclTessPartitioning:
        return this->emitDclTessPartitioning(ins);
      
      case DxbcOpcode::DclTessOutputPrimitive:
        return this->emitDclTessOutputPrimitive(ins);
      
      case DxbcOpcode::DclThreadGroup:
        return this->emitDclThreadGroup(ins);
      
      case DxbcOpcode::DclGsInstanceCount:
        return this->emitDclGsInstanceCount(ins);
      
      default:
        Logger::warn(
          str::format("DxbcCompiler: Unhandled opcode: ",
          ins.op));
    }
  }
  
  
  void DxbcCompiler::emitDclGlobalFlags(const DxbcShaderInstruction& ins) {
    const DxbcGlobalFlags flags = ins.controls.globalFlags();
    
    if (flags.test(DxbcGlobalFlag::EarlyFragmentTests))
      m_module.setExecutionMode(m_entryPointId, spv::ExecutionModeEarlyFragmentTests);
  }
  
  
  void DxbcCompiler::emitDclTemps(const DxbcShaderInstruction& ins) {
    // dcl_temps has one operand:
    //    (imm0) Number of temp registers
    const uint32_t oldCount = m_rRegs.size();
    const uint32_t newCount = ins.imm[0].u32;
    
    if (newCount > oldCount) {
      m_rRegs.resize(newCount);
      
      DxbcRegisterInfo info;
      info.type.ctype   = DxbcScalarType::Float32;
      info.type.ccount  = 4;
      info.type.alength = 0;
      info.sclass       = spv::StorageClassPrivate;
      
      for (uint32_t i = oldCount; i < newCount; i++) {
        const uint32_t varId = this->emitNewVariable(info);
        m_module.setDebugName(varId, str::format("r", i).c_str());
        m_rRegs.at(i) = varId;
      }
    }
  }
  
  
  void DxbcCompiler::emitDclIndexableTemp(const DxbcShaderInstruction& ins) {
    // dcl_indexable_temps has three operands:
    //    (imm0) Array register index (x#)
    //    (imm1) Number of vectors stored in the array
    //    (imm2) Component count of each individual vector
    DxbcRegisterInfo info;
    info.type.ctype   = DxbcScalarType::Float32;
    info.type.ccount  = ins.imm[2].u32;
    info.type.alength = ins.imm[1].u32;
    info.sclass       = spv::StorageClassPrivate;
    
    const uint32_t regId = ins.imm[0].u32;
    
    if (regId >= m_xRegs.size())
      m_xRegs.resize(regId + 1);
    
    m_xRegs.at(regId).ccount = info.type.ccount;
    m_xRegs.at(regId).varId  = emitNewVariable(info);
    
    m_module.setDebugName(m_xRegs.at(regId).varId,
      str::format("x", regId).c_str());
  }
  
  
  void DxbcCompiler::emitDclInterfaceReg(const DxbcShaderInstruction& ins) {
    switch (ins.dst[0].type) {
      case DxbcOperandType::Input:
      case DxbcOperandType::Output: {
        // dcl_input and dcl_output instructions
        // have the following operands:
        //    (dst0) The register to declare
        //    (imm0) The system value (optional)
        uint32_t regDim = 0;
        uint32_t regIdx = 0;
        
        // In the vertex and fragment shader stage, the
        // operand indices will have the following format:
        //    (0) Register index
        // 
        // In other stages, the input and output registers
        // may be declared as arrays of a fixed size:
        //    (0) Array length
        //    (1) Register index
        if (ins.dst[0].idxDim == 2) {
          regDim = ins.dst[0].idx[0].offset;
          regIdx = ins.dst[0].idx[1].offset;
        } else if (ins.dst[0].idxDim == 1) {
          regIdx = ins.dst[0].idx[0].offset;
        } else {
          Logger::err(str::format(
            "DxbcCompiler: ", ins.op,
            ": Invalid index dimension"));
          return;
        }
        
        // This declaration may map an output register to a system
        // value. If that is the case, the system value type will
        // be stored in the second operand.
        const bool hasSv =
            ins.op == DxbcOpcode::DclInputSgv
         || ins.op == DxbcOpcode::DclInputSiv
         || ins.op == DxbcOpcode::DclInputPsSgv
         || ins.op == DxbcOpcode::DclInputPsSiv
         || ins.op == DxbcOpcode::DclOutputSgv
         || ins.op == DxbcOpcode::DclOutputSiv;
        
        DxbcSystemValue sv = DxbcSystemValue::None;
        
        if (hasSv)
          sv = static_cast<DxbcSystemValue>(ins.imm[0].u32);
        
        // In the pixel shader, inputs are declared with an
        // interpolation mode that is part of the op token.
        const bool hasInterpolationMode =
            ins.op == DxbcOpcode::DclInputPs
         || ins.op == DxbcOpcode::DclInputPsSiv;
        
        DxbcInterpolationMode im = DxbcInterpolationMode::Undefined;
        
        if (hasInterpolationMode)
          im = ins.controls.interpolation();
        
        // Declare the actual input/output variable
        switch (ins.op) {
          case DxbcOpcode::DclInput:
          case DxbcOpcode::DclInputSgv:
          case DxbcOpcode::DclInputSiv:
          case DxbcOpcode::DclInputPs:
          case DxbcOpcode::DclInputPsSgv:
          case DxbcOpcode::DclInputPsSiv:
            this->emitDclInput(regIdx, regDim, ins.dst[0].mask, sv, im);
            break;
          
          case DxbcOpcode::DclOutput:
          case DxbcOpcode::DclOutputSgv:
          case DxbcOpcode::DclOutputSiv:
            this->emitDclOutput(regIdx, regDim, ins.dst[0].mask, sv, im);
            break;
          
          default:
            Logger::err(str::format(
              "DxbcCompiler: Unexpected opcode: ",
              ins.op));
        }
      } break;
  
      case DxbcOperandType::InputThreadId: {
        m_cs.builtinGlobalInvocationId = emitNewBuiltinVariable({
          { DxbcScalarType::Uint32, 3, 0 },
          spv::StorageClassInput },
          spv::BuiltInGlobalInvocationId,
          "vThreadId");
      } break;
  
      case DxbcOperandType::InputThreadGroupId: {
        m_cs.builtinWorkgroupId = emitNewBuiltinVariable({
          { DxbcScalarType::Uint32, 3, 0 },
          spv::StorageClassInput },
          spv::BuiltInWorkgroupId,
          "vThreadGroupId");
      } break;
  
      case DxbcOperandType::InputThreadIdInGroup: {
        m_cs.builtinLocalInvocationId = emitNewBuiltinVariable({
          { DxbcScalarType::Uint32, 3, 0 },
          spv::StorageClassInput },
          spv::BuiltInLocalInvocationId,
          "vThreadIdInGroup");
      } break;
  
      case DxbcOperandType::InputThreadIndexInGroup: {
        m_cs.builtinLocalInvocationIndex = emitNewBuiltinVariable({
          { DxbcScalarType::Uint32, 1, 0 },
          spv::StorageClassInput },
          spv::BuiltInLocalInvocationIndex,
          "vThreadIndexInGroup");
      } break;
      
      case DxbcOperandType::InputCoverageMask: {
        m_ps.builtinSampleMaskIn = emitNewBuiltinVariable({
          { DxbcScalarType::Uint32, 1, 1 },
          spv::StorageClassInput },
          spv::BuiltInSampleMask,
          "vCoverage");
      } break;
      
      case DxbcOperandType::OutputCoverageMask: {
        m_ps.builtinSampleMaskOut = emitNewBuiltinVariable({
          { DxbcScalarType::Uint32, 1, 1 },
          spv::StorageClassOutput },
          spv::BuiltInSampleMask,
          "oMask");
      } break;
      
      case DxbcOperandType::OutputDepth: {
        m_module.setExecutionMode(m_entryPointId,
          spv::ExecutionModeDepthReplacing);
        m_ps.builtinDepth = emitNewBuiltinVariable({
          { DxbcScalarType::Float32, 1, 0 },
          spv::StorageClassOutput },
          spv::BuiltInFragDepth,
          "oDepth");
      } break;
      
      case DxbcOperandType::OutputDepthGe: {
        m_module.setExecutionMode(m_entryPointId, spv::ExecutionModeDepthReplacing);
        m_module.setExecutionMode(m_entryPointId, spv::ExecutionModeDepthGreater);
        m_ps.builtinDepth = emitNewBuiltinVariable({
          { DxbcScalarType::Float32, 1, 0 },
          spv::StorageClassOutput },
          spv::BuiltInFragDepth,
          "oDepthGe");
      } break;
      
      case DxbcOperandType::OutputDepthLe: {
        m_module.setExecutionMode(m_entryPointId, spv::ExecutionModeDepthReplacing);
        m_module.setExecutionMode(m_entryPointId, spv::ExecutionModeDepthLess);
        m_ps.builtinDepth = emitNewBuiltinVariable({
          { DxbcScalarType::Float32, 1, 0 },
          spv::StorageClassOutput },
          spv::BuiltInFragDepth,
          "oDepthLe");
      } break;
      
      case DxbcOperandType::InputPrimitiveId: {
        m_primitiveIdIn = emitNewBuiltinVariable({
          { DxbcScalarType::Uint32, 1, 0 },
          spv::StorageClassInput },
          spv::BuiltInPrimitiveId,
          "vPrim");
      } break;
      
      case DxbcOperandType::InputDomainPoint: {
        m_ds.builtinTessCoord = emitNewBuiltinVariable({
          { DxbcScalarType::Float32, 3, 0 },
          spv::StorageClassInput },
          spv::BuiltInTessCoord,
          "vDomain");
      } break;
      
      case DxbcOperandType::InputForkInstanceId:
      case DxbcOperandType::InputJoinInstanceId: {
        auto phase = this->getCurrentHsForkJoinPhase();
        
        phase->instanceIdPtr = m_module.newVar(
          m_module.defPointerType(
            m_module.defIntType(32, 0),
            spv::StorageClassFunction),
          spv::StorageClassFunction);
        
        m_module.opStore(phase->instanceIdPtr, phase->instanceId);
        m_module.setDebugName(phase->instanceIdPtr,
          ins.dst[0].type == DxbcOperandType::InputForkInstanceId
            ? "vForkInstanceId" : "vJoinInstanceId");
      } break;
      
      case DxbcOperandType::OutputControlPointId: {
        // This system value map to the invocation
        // ID, which has been declared already.
      } break;
      
      case DxbcOperandType::InputControlPoint:
      case DxbcOperandType::InputPatchConstant:
      case DxbcOperandType::OutputControlPoint: {
        // These have been declared as global input and
        // output arrays, so there's nothing left to do.
      } break;
      
      case DxbcOperandType::InputGsInstanceId: {
        m_gs.builtinInvocationId = emitNewBuiltinVariable({
          { DxbcScalarType::Uint32, 1, 0 },
          spv::StorageClassInput },
          spv::BuiltInInvocationId,
          "vInstanceID");
      } break;
      
      default:
        Logger::err(str::format(
          "DxbcCompiler: Unsupported operand type declaration: ",
          ins.dst[0].type));
        
    }
  }
  
  
  void DxbcCompiler::emitDclInput(
          uint32_t                regIdx,
          uint32_t                regDim,
          DxbcRegMask             regMask,
          DxbcSystemValue         sv,
          DxbcInterpolationMode   im) {
    // Avoid declaring the same variable multiple times.
    // This may happen when multiple system values are
    // mapped to different parts of the same register.
    if (m_vRegs.at(regIdx).id == 0 && sv == DxbcSystemValue::None) {
      const DxbcVectorType regType = getInputRegType(regIdx);
      
      DxbcRegisterInfo info;
      info.type.ctype   = regType.ctype;
      info.type.ccount  = regType.ccount;
      info.type.alength = regDim;
      info.sclass = spv::StorageClassInput;
      
      const uint32_t varId = emitNewVariable(info);
      
      m_module.decorateLocation(varId, regIdx);
      m_module.setDebugName(varId, str::format("v", regIdx).c_str());
      m_entryPointInterfaces.push_back(varId);
      
      m_vRegs.at(regIdx) = { regType, varId };
      
      // Interpolation mode, used in pixel shaders
      if (im == DxbcInterpolationMode::Constant)
        m_module.decorate(varId, spv::DecorationFlat);
      
      if (im == DxbcInterpolationMode::LinearCentroid
       || im == DxbcInterpolationMode::LinearNoPerspectiveCentroid)
        m_module.decorate(varId, spv::DecorationCentroid);
      
      if (im == DxbcInterpolationMode::LinearNoPerspective
       || im == DxbcInterpolationMode::LinearNoPerspectiveCentroid
       || im == DxbcInterpolationMode::LinearNoPerspectiveSample)
        m_module.decorate(varId, spv::DecorationNoPerspective);
      
      if (im == DxbcInterpolationMode::LinearSample
       || im == DxbcInterpolationMode::LinearNoPerspectiveSample) {
        m_module.enableCapability(spv::CapabilitySampleRateShading);
        m_module.decorate(varId, spv::DecorationSample);
      }
      
      // Declare the input slot as defined
      m_interfaceSlots.inputSlots |= 1u << regIdx;
    } else if (sv != DxbcSystemValue::None) {
      // Add a new system value mapping if needed
      bool skipSv = sv == DxbcSystemValue::ClipDistance
                 || sv == DxbcSystemValue::CullDistance;
      
      if (!skipSv)
        m_vMappings.push_back({ regIdx, regMask, sv });
    }
  }
  
  
  void DxbcCompiler::emitDclOutput(
          uint32_t                regIdx,
          uint32_t                regDim,
          DxbcRegMask             regMask,
          DxbcSystemValue         sv,
          DxbcInterpolationMode   im) {
    // Add a new system value mapping if needed. Clip
    // and cull distances are handled separately.
    if (sv != DxbcSystemValue::None
     && sv != DxbcSystemValue::ClipDistance
     && sv != DxbcSystemValue::CullDistance)
      m_oMappings.push_back({ regIdx, regMask, sv });
    
    // Hull shaders don't use standard outputs
    if (m_version.type() == DxbcProgramType::HullShader)
      return;
    
    // Avoid declaring the same variable multiple times.
    // This may happen when multiple system values are
    // mapped to different parts of the same register.
    if (m_oRegs.at(regIdx).id == 0) {
      const DxbcVectorType regType = getOutputRegType(regIdx);
      
      DxbcRegisterInfo info;
      info.type.ctype   = regType.ctype;
      info.type.ccount  = regType.ccount;
      info.type.alength = regDim;
      info.sclass = spv::StorageClassOutput;
      
      const uint32_t varId = this->emitNewVariable(info);
      
      m_module.decorateLocation(varId, regIdx);
      m_module.setDebugName(varId, str::format("o", regIdx).c_str());
      m_entryPointInterfaces.push_back(varId);
      
      m_oRegs.at(regIdx) = { regType, varId };
      
      // Declare the output slot as defined
      m_interfaceSlots.outputSlots |= 1u << regIdx;
    }
  }
  
  
  void DxbcCompiler::emitDclConstantBuffer(const DxbcShaderInstruction& ins) {
    // dcl_constant_buffer has one operand with two indices:
    //    (0) Constant buffer register ID (cb#)
    //    (1) Number of constants in the buffer
    const uint32_t bufferId     = ins.dst[0].idx[0].offset;
    const uint32_t elementCount = ins.dst[0].idx[1].offset;
    
    this->emitDclConstantBufferVar(bufferId, elementCount,
      str::format("cb", bufferId).c_str());
  }
  
  
  void DxbcCompiler::emitDclConstantBufferVar(
          uint32_t                regIdx,
          uint32_t                numConstants,
    const char*                   name) {
    // Uniform buffer data is stored as a fixed-size array
    // of 4x32-bit vectors. SPIR-V requires explicit strides.
    const uint32_t arrayType = m_module.defArrayTypeUnique(
      getVectorTypeId({ DxbcScalarType::Float32, 4 }),
      m_module.constu32(numConstants));
    m_module.decorateArrayStride(arrayType, 16);
    
    // SPIR-V requires us to put that array into a
    // struct and decorate that struct as a block.
    const uint32_t structType = m_module.defStructTypeUnique(1, &arrayType);
    
    m_module.decorateBlock       (structType);
    m_module.memberDecorateOffset(structType, 0, 0);
    
    m_module.setDebugName        (structType, str::format("struct_", name).c_str());
    m_module.setDebugMemberName  (structType, 0, "m");
    
    // Variable that we'll use to access the buffer
    const uint32_t varId = m_module.newVar(
      m_module.defPointerType(structType, spv::StorageClassUniform),
      spv::StorageClassUniform);
    
    m_module.setDebugName(varId, name);
    
    // Compute the DXVK binding slot index for the buffer.
    // D3D11 needs to bind the actual buffers to this slot.
    const uint32_t bindingId = computeResourceSlotId(
      m_version.type(), DxbcBindingType::ConstantBuffer,
      regIdx);
    
    m_module.decorateDescriptorSet(varId, 0);
    m_module.decorateBinding(varId, bindingId);
    
    // Declare a specialization constant which will
    // store whether or not the resource is bound.
    const uint32_t specConstId = m_module.specConstBool(true);
    m_module.decorateSpecId(specConstId, bindingId);
    m_module.setDebugName(specConstId,
      str::format(name, "_bound").c_str());
    
    DxbcConstantBuffer buf;
    buf.varId  = varId;
    buf.specId = specConstId;
    buf.size   = numConstants;
    m_constantBuffers.at(regIdx) = buf;
    
    // Store descriptor info for the shader interface
    DxvkResourceSlot resource;
    resource.slot = bindingId;
    resource.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    resource.view = VK_IMAGE_VIEW_TYPE_MAX_ENUM;
    m_resourceSlots.push_back(resource);
  }


  void DxbcCompiler::emitDclSampler(const DxbcShaderInstruction& ins) {
    // dclSampler takes one operand:
    //    (dst0) The sampler register to declare
    const uint32_t samplerId = ins.dst[0].idx[0].offset;
    
    // The sampler type is opaque, but we still have to
    // define a pointer and a variable in oder to use it
    const uint32_t samplerType = m_module.defSamplerType();
    const uint32_t samplerPtrType = m_module.defPointerType(
      samplerType, spv::StorageClassUniformConstant);
    
    // Define the sampler variable
    const uint32_t varId = m_module.newVar(samplerPtrType,
      spv::StorageClassUniformConstant);
    m_module.setDebugName(varId,
      str::format("s", samplerId).c_str());
    
    m_samplers.at(samplerId).varId  = varId;
    m_samplers.at(samplerId).typeId = samplerType;
    
    // Compute binding slot index for the sampler
    const uint32_t bindingId = computeResourceSlotId(
      m_version.type(), DxbcBindingType::ImageSampler, samplerId);
    
    m_module.decorateDescriptorSet(varId, 0);
    m_module.decorateBinding(varId, bindingId);
    
    // Store descriptor info for the shader interface
    DxvkResourceSlot resource;
    resource.slot = bindingId;
    resource.type = VK_DESCRIPTOR_TYPE_SAMPLER;
    resource.view = VK_IMAGE_VIEW_TYPE_MAX_ENUM;
    m_resourceSlots.push_back(resource);
  }
  
  
  void DxbcCompiler::emitDclStream(const DxbcShaderInstruction& ins) {
    if (ins.dst[0].idx[0].offset != 0)
      Logger::err("Dxbc: Multiple streams not supported");
  }
  
  
  void DxbcCompiler::emitDclResourceTyped(const DxbcShaderInstruction& ins) {
    // dclResource takes two operands:
    //    (dst0) The resource register ID
    //    (imm0) The resource return type
    const uint32_t registerId = ins.dst[0].idx[0].offset;
    
    // We also handle unordered access views here
    const bool isUav = ins.op == DxbcOpcode::DclUavTyped;
    
    if (isUav) {
      if (m_moduleInfo.options.test(DxbcOption::UseStorageImageReadWithoutFormat))
        m_module.enableCapability(spv::CapabilityStorageImageReadWithoutFormat);
      m_module.enableCapability(spv::CapabilityStorageImageWriteWithoutFormat);
    }
    
    // Defines the type of the resource (texture2D, ...)
    const DxbcResourceDim resourceType = ins.controls.resourceDim();
    
    // Defines the type of a read operation. DXBC has the ability
    // to define four different types whereas SPIR-V only allows
    // one, but in practice this should not be much of a problem.
    auto xType = static_cast<DxbcResourceReturnType>(
      bit::extract(ins.imm[0].u32, 0, 3));
    auto yType = static_cast<DxbcResourceReturnType>(
      bit::extract(ins.imm[0].u32, 4, 7));
    auto zType = static_cast<DxbcResourceReturnType>(
      bit::extract(ins.imm[0].u32, 8, 11));
    auto wType = static_cast<DxbcResourceReturnType>(
      bit::extract(ins.imm[0].u32, 12, 15));
    
    if ((xType != yType) || (xType != zType) || (xType != wType))
      Logger::warn("DxbcCompiler: dcl_resource: Ignoring resource return types");
    
    // Declare the actual sampled type
    const DxbcScalarType sampledType = [xType] {
      switch (xType) {
        // FIXME is this correct? There's no documentation about it
        case DxbcResourceReturnType::Mixed: return DxbcScalarType::Uint32;
        // FIXME do we have to manually clamp writes to SNORM/UNORM resources?
        case DxbcResourceReturnType::Snorm: return DxbcScalarType::Float32;
        case DxbcResourceReturnType::Unorm: return DxbcScalarType::Float32;
        case DxbcResourceReturnType::Float: return DxbcScalarType::Float32;
        case DxbcResourceReturnType::Sint:  return DxbcScalarType::Sint32;
        case DxbcResourceReturnType::Uint:  return DxbcScalarType::Uint32;
        default: throw DxvkError(str::format("DxbcCompiler: Invalid sampled type: ", xType));
      }
    }();
    
    // Declare the resource type
    const uint32_t sampledTypeId = getScalarTypeId(sampledType);
    const DxbcImageInfo typeInfo = getResourceType(resourceType, isUav);    
    
    // Declare additional capabilities if necessary
    switch (resourceType) {
      case DxbcResourceDim::Buffer:         m_module.enableCapability(spv::CapabilityImageBuffer);    break;
      case DxbcResourceDim::Texture1D:      m_module.enableCapability(spv::CapabilityImage1D);        break;
      case DxbcResourceDim::Texture1DArr:   m_module.enableCapability(spv::CapabilityImage1D);        break;
      case DxbcResourceDim::TextureCubeArr: m_module.enableCapability(spv::CapabilityImageCubeArray); break;
      case DxbcResourceDim::Texture2DMs:    m_module.enableCapability(spv::CapabilityImageMSArray);   break;
      case DxbcResourceDim::Texture2DMsArr: m_module.enableCapability(spv::CapabilityImageMSArray);   break;
      default: break; // No additional capabilities required
    }
    
    // If the read-without-format capability is not set and this
    // image is access via a typed load, or if atomic operations
    // are used,, we must define the image format explicitly.
    spv::ImageFormat imageFormat = spv::ImageFormatUnknown;
    
    if (isUav) {
      if ((m_analysis->uavInfos[registerId].accessAtomicOp)
       || (m_analysis->uavInfos[registerId].accessTypedLoad
        && !m_moduleInfo.options.test(DxbcOption::UseStorageImageReadWithoutFormat)))
        imageFormat = getScalarImageFormat(sampledType);
    }
    
    // We do not know whether the image is going to be used as
    // a color image or a depth image yet, but we can pick the
    // correct type when creating a sampled image object.
    const uint32_t imageTypeId = m_module.defImageType(sampledTypeId,
      typeInfo.dim, 0, typeInfo.array, typeInfo.ms, typeInfo.sampled,
      imageFormat);
    
    // We'll declare the texture variable with the color type
    // and decide which one to use when the texture is sampled.
    const uint32_t resourcePtrType = m_module.defPointerType(
      imageTypeId, spv::StorageClassUniformConstant);
    
    const uint32_t varId = m_module.newVar(resourcePtrType,
      spv::StorageClassUniformConstant);
    
    m_module.setDebugName(varId,
      str::format(isUav ? "u" : "t", registerId).c_str());
    
    // Compute the DXVK binding slot index for the resource.
    // D3D11 needs to bind the actual resource to this slot.
    const uint32_t bindingId = computeResourceSlotId(
      m_version.type(), isUav
        ? DxbcBindingType::UnorderedAccessView
        : DxbcBindingType::ShaderResource,
      registerId);
    
    m_module.decorateDescriptorSet(varId, 0);
    m_module.decorateBinding(varId, bindingId);
    
    if (ins.controls.uavFlags().test(DxbcUavFlag::GloballyCoherent))
      m_module.decorate(varId, spv::DecorationCoherent);
    
    // On GPUs which don't support storageImageReadWithoutFormat,
    // we have to decorate untyped UAVs as write-only
    if (isUav && imageFormat == spv::ImageFormatUnknown
     && !m_moduleInfo.options.test(DxbcOption::UseStorageImageReadWithoutFormat))
      m_module.decorate(varId, spv::DecorationNonReadable);
    
    // Declare a specialization constant which will
    // store whether or not the resource is bound.
    const uint32_t specConstId = m_module.specConstBool(true);
    m_module.decorateSpecId(specConstId, bindingId);
    m_module.setDebugName(specConstId,
      str::format(isUav ? "u" : "t", registerId, "_bound").c_str());
    
    if (isUav) {
      DxbcUav uav;
      uav.type          = DxbcResourceType::Typed;
      uav.imageInfo     = typeInfo;
      uav.varId         = varId;
      uav.ctrId         = 0;
      uav.specId        = specConstId;
      uav.sampledType   = sampledType;
      uav.sampledTypeId = sampledTypeId;
      uav.imageTypeId   = imageTypeId;
      uav.structStride  = 0;
      m_uavs.at(registerId) = uav;
    } else {
      DxbcShaderResource res;
      res.type          = DxbcResourceType::Typed;
      res.imageInfo     = typeInfo;
      res.varId         = varId;
      res.specId        = specConstId;
      res.sampledType   = sampledType;
      res.sampledTypeId = sampledTypeId;
      res.imageTypeId   = imageTypeId;
      res.colorTypeId   = imageTypeId;
      res.depthTypeId   = 0;
      res.structStride  = 0;
      
      if ((sampledType == DxbcScalarType::Float32)
       && (resourceType == DxbcResourceDim::Texture2D
        || resourceType == DxbcResourceDim::Texture2DArr
        || resourceType == DxbcResourceDim::TextureCube
        || resourceType == DxbcResourceDim::TextureCubeArr)) {
        res.depthTypeId = m_module.defImageType(sampledTypeId,
          typeInfo.dim, 1, typeInfo.array, typeInfo.ms, typeInfo.sampled,
          spv::ImageFormatUnknown);
      }
      
      m_textures.at(registerId) = res;
    }
    
    // Store descriptor info for the shader interface
    DxvkResourceSlot resource;
    resource.slot = bindingId;
    resource.view = typeInfo.vtype;
    
    if (isUav) {
      resource.type = resourceType == DxbcResourceDim::Buffer
        ? VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER
        : VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    } else {
      resource.type = resourceType == DxbcResourceDim::Buffer
        ? VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER
        : VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    }
    
    m_resourceSlots.push_back(resource);
  }
  
  
  void DxbcCompiler::emitDclResourceRawStructured(const DxbcShaderInstruction& ins) {
    // dcl_resource_raw and dcl_uav_raw take one argument:
    //    (dst0) The resource register ID
    // dcl_resource_structured and dcl_uav_structured take two arguments:
    //    (dst0) The resource register ID
    //    (imm0) Structure stride, in bytes
    const uint32_t registerId = ins.dst[0].idx[0].offset;
    
    const bool isUav = ins.op == DxbcOpcode::DclUavRaw
                    || ins.op == DxbcOpcode::DclUavStructured;
    
    const bool isStructured = ins.op == DxbcOpcode::DclUavStructured
                           || ins.op == DxbcOpcode::DclResourceStructured;
    
    // Structured and raw buffers are represented as
    // texel buffers consisting of 32-bit integers.
    m_module.enableCapability(spv::CapabilityImageBuffer);
    
    const DxbcScalarType sampledType = DxbcScalarType::Uint32;
    const uint32_t sampledTypeId = getScalarTypeId(sampledType);
    
    const DxbcImageInfo typeInfo = { spv::DimBuffer, 0, 0, isUav ? 2u : 1u, VK_IMAGE_VIEW_TYPE_MAX_ENUM };
    
    // Declare the resource type
    const uint32_t resTypeId = m_module.defImageType(sampledTypeId,
      typeInfo.dim, 0, typeInfo.array, typeInfo.ms, typeInfo.sampled,
      spv::ImageFormatR32ui);
    
    const uint32_t varId = m_module.newVar(
      m_module.defPointerType(resTypeId, spv::StorageClassUniformConstant),
      spv::StorageClassUniformConstant);
    
    m_module.setDebugName(varId,
      str::format(isUav ? "u" : "t", registerId).c_str());
    
    // Write back resource info
    const DxbcResourceType resType = isStructured
      ? DxbcResourceType::Structured
      : DxbcResourceType::Raw;
    
    const uint32_t resStride = isStructured
      ? ins.imm[0].u32
      : 0;
    
    // Compute the DXVK binding slot index for the resource.
    const uint32_t bindingId = computeResourceSlotId(
      m_version.type(), isUav
        ? DxbcBindingType::UnorderedAccessView
        : DxbcBindingType::ShaderResource,
      registerId);
    
    m_module.decorateDescriptorSet(varId, 0);
    m_module.decorateBinding(varId, bindingId);
    
    if (ins.controls.uavFlags().test(DxbcUavFlag::GloballyCoherent))
      m_module.decorate(varId, spv::DecorationCoherent);
    
    // Declare a specialization constant which will
    // store whether or not the resource is bound.
    const uint32_t specConstId = m_module.specConstBool(true);
    m_module.decorateSpecId(specConstId, bindingId);
    m_module.setDebugName(specConstId,
      str::format(isUav ? "u" : "t", registerId, "_bound").c_str());
    
    if (isUav) {
      DxbcUav uav;
      uav.type          = resType;
      uav.imageInfo     = typeInfo;
      uav.varId         = varId;
      uav.ctrId         = 0;
      uav.specId        = specConstId;
      uav.sampledType   = sampledType;
      uav.sampledTypeId = sampledTypeId;
      uav.imageTypeId   = resTypeId;
      uav.structStride  = resStride;
      m_uavs.at(registerId) = uav;
    } else {
      DxbcShaderResource res;
      res.type          = resType;
      res.imageInfo     = typeInfo;
      res.varId         = varId;
      res.specId        = specConstId;
      res.sampledType   = sampledType;
      res.sampledTypeId = sampledTypeId;
      res.imageTypeId   = resTypeId;
      res.colorTypeId   = resTypeId;
      res.depthTypeId   = 0;
      res.structStride  = resStride;
      m_textures.at(registerId) = res;
    }
    
    // Store descriptor info for the shader interface
    DxvkResourceSlot resource;
    resource.slot = bindingId;
    resource.type = isUav
      ? VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER
      : VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER;
    resource.view = VK_IMAGE_VIEW_TYPE_MAX_ENUM;
    m_resourceSlots.push_back(resource);
  }
  
  
  void DxbcCompiler::emitDclThreadGroupSharedMemory(const DxbcShaderInstruction& ins) {
    // dcl_tgsm_raw takes two arguments:
    //    (dst0) The resource register ID
    //    (imm0) Block size, in bytes
    // dcl_tgsm_structured takes three arguments:
    //    (dst0) The resource register ID
    //    (imm0) Structure stride, in bytes
    //    (imm1) Structure count
    const bool isStructured = ins.op == DxbcOpcode::DclThreadGroupSharedMemoryStructured;
    
    const uint32_t regId = ins.dst[0].idx[0].offset;
    
    if (regId >= m_gRegs.size())
      m_gRegs.resize(regId + 1);
    
    const uint32_t elementStride = isStructured ? ins.imm[0].u32 : 0;
    const uint32_t elementCount  = isStructured ? ins.imm[1].u32 : ins.imm[0].u32;
    
    DxbcRegisterInfo varInfo;
    varInfo.type.ctype   = DxbcScalarType::Uint32;
    varInfo.type.ccount  = 1;
    varInfo.type.alength = isStructured
      ? elementCount * elementStride / 4
      : elementCount / 4;
    varInfo.sclass = spv::StorageClassWorkgroup;
    
    m_gRegs[regId].type = isStructured
      ? DxbcResourceType::Structured
      : DxbcResourceType::Raw;
    m_gRegs[regId].elementStride = elementStride;
    m_gRegs[regId].elementCount  = elementCount;
    m_gRegs[regId].varId = emitNewVariable(varInfo);
    
    m_module.setDebugName(m_gRegs[regId].varId,
      str::format("g", regId).c_str());
  }
  
  
  void DxbcCompiler::emitDclGsInputPrimitive(const DxbcShaderInstruction& ins) {
    // The input primitive type is stored within in the
    // control bits of the opcode token. In SPIR-V, we
    // have to define an execution mode.
    const spv::ExecutionMode mode = [&] {
      switch (ins.controls.primitive()) {
        case DxbcPrimitive::Point:       return spv::ExecutionModeInputPoints;
        case DxbcPrimitive::Line:        return spv::ExecutionModeInputLines;
        case DxbcPrimitive::Triangle:    return spv::ExecutionModeTriangles;
        case DxbcPrimitive::LineAdj:     return spv::ExecutionModeInputLinesAdjacency;
        case DxbcPrimitive::TriangleAdj: return spv::ExecutionModeInputTrianglesAdjacency;
        default: throw DxvkError("DxbcCompiler: Unsupported primitive type");
      }
    }();
    
    m_gs.inputPrimitive = ins.controls.primitive();
    m_module.setExecutionMode(m_entryPointId, mode);
    
    const uint32_t vertexCount
      = primitiveVertexCount(m_gs.inputPrimitive);
    
    emitDclInputArray(vertexCount);
    emitDclInputPerVertex(vertexCount, "gs_vertex_in");
  }
  
  
  void DxbcCompiler::emitDclGsOutputTopology(const DxbcShaderInstruction& ins) {
    // The input primitive topology is stored within in the
    // control bits of the opcode token. In SPIR-V, we have
    // to define an execution mode.
    const spv::ExecutionMode mode = [&] {
      switch (ins.controls.primitiveTopology()) {
        case DxbcPrimitiveTopology::PointList:     return spv::ExecutionModeOutputPoints;
        case DxbcPrimitiveTopology::LineStrip:     return spv::ExecutionModeOutputLineStrip;
        case DxbcPrimitiveTopology::TriangleStrip: return spv::ExecutionModeOutputTriangleStrip;
        default: throw DxvkError("DxbcCompiler: Unsupported primitive topology");
      }
    }();
    
    m_module.setExecutionMode(m_entryPointId, mode);
  }
  
  
  void DxbcCompiler::emitDclMaxOutputVertexCount(const DxbcShaderInstruction& ins) {
    // dcl_max_output_vertex_count has one operand:
    //    (imm0) The maximum number of vertices
    m_gs.outputVertexCount = ins.imm[0].u32;
    
    m_module.setOutputVertices(m_entryPointId, m_gs.outputVertexCount);
  }
  
  
  void DxbcCompiler::emitDclInputControlPointCount(const DxbcShaderInstruction& ins) {
    // dcl_input_control_points has the control point
    // count embedded within the opcode token.
    if (m_version.type() == DxbcProgramType::HullShader) {
      m_hs.vertexCountIn = ins.controls.controlPointCount();
      
      emitDclInputArray(m_hs.vertexCountIn);    
    } else {
      m_ds.vertexCountIn = ins.controls.controlPointCount();
      
      m_ds.inputPerPatch  = emitTessInterfacePerPatch (spv::StorageClassInput);
      m_ds.inputPerVertex = emitTessInterfacePerVertex(spv::StorageClassInput, m_ds.vertexCountIn);
    }
  }
  
  
  void DxbcCompiler::emitDclOutputControlPointCount(const DxbcShaderInstruction& ins) {
    // dcl_output_control_points has the control point
    // count embedded within the opcode token.
    m_hs.vertexCountOut = ins.controls.controlPointCount();
    
    m_hs.outputPerPatch  = emitTessInterfacePerPatch (spv::StorageClassOutput);
    m_hs.outputPerVertex = emitTessInterfacePerVertex(spv::StorageClassOutput, m_hs.vertexCountOut);
    
    m_module.setOutputVertices(m_entryPointId, m_hs.vertexCountOut);
  }
  
  
  void DxbcCompiler::emitDclHsMaxTessFactor(const DxbcShaderInstruction& ins) {
    m_hs.maxTessFactor = ins.imm[0].f32;
  }
  
  
  void DxbcCompiler::emitDclTessDomain(const DxbcShaderInstruction& ins) {
    const spv::ExecutionMode executionMode = [&] {
      switch (ins.controls.tessDomain()) {
        case DxbcTessDomain::Isolines:  return spv::ExecutionModeIsolines;
        case DxbcTessDomain::Triangles: return spv::ExecutionModeTriangles;
        case DxbcTessDomain::Quads:     return spv::ExecutionModeQuads;
        default: throw DxvkError("Dxbc: Invalid tess domain");
      }
    }();
    
    m_module.setExecutionMode(m_entryPointId, executionMode);
  }
  
  
  void DxbcCompiler::emitDclTessPartitioning(const DxbcShaderInstruction& ins) {
    const spv::ExecutionMode executionMode = [&] {
      switch (ins.controls.tessPartitioning()) {
        case DxbcTessPartitioning::Pow2:
        case DxbcTessPartitioning::Integer:   return spv::ExecutionModeSpacingEqual;
        case DxbcTessPartitioning::FractOdd:  return spv::ExecutionModeSpacingFractionalOdd;
        case DxbcTessPartitioning::FractEven: return spv::ExecutionModeSpacingFractionalEven;
        default: throw DxvkError("Dxbc: Invalid tess partitioning");
      }
    }();
    
    m_module.setExecutionMode(m_entryPointId, executionMode);
  }
  
  
  void DxbcCompiler::emitDclTessOutputPrimitive(const DxbcShaderInstruction& ins) {
    switch (ins.controls.tessOutputPrimitive()) {
      case DxbcTessOutputPrimitive::Point:
        m_module.setExecutionMode(m_entryPointId, spv::ExecutionModePointMode);
        break;
        
      case DxbcTessOutputPrimitive::Line:
        break;
        
      case DxbcTessOutputPrimitive::TriangleCw:
        m_module.setExecutionMode(m_entryPointId, spv::ExecutionModeVertexOrderCw);
        break;
        
      case DxbcTessOutputPrimitive::TriangleCcw:
        m_module.setExecutionMode(m_entryPointId, spv::ExecutionModeVertexOrderCcw);
        break;
      
      default:
        throw DxvkError("Dxbc: Invalid tess output primitive");
    }
  }
  
  
  void DxbcCompiler::emitDclThreadGroup(const DxbcShaderInstruction& ins) {
    // dcl_thread_group has three operands:
    //    (imm0) Number of threads in X dimension
    //    (imm1) Number of threads in Y dimension
    //    (imm2) Number of threads in Z dimension
    m_module.setLocalSize(m_entryPointId,
      ins.imm[0].u32, ins.imm[1].u32, ins.imm[2].u32);
  }
  
  
  void DxbcCompiler::emitDclGsInstanceCount(const DxbcShaderInstruction& ins) {
    // dcl_gs_instance_count has one operand:
    //    (imm0) Number of geometry shader invocations
    m_module.setInvocations(m_entryPointId, ins.imm[0].u32);
  }
  
  
  uint32_t DxbcCompiler::emitDclUavCounter(uint32_t regId) {
    // Declare a structure type which holds the UAV counter
    if (m_uavCtrStructType == 0) {
      const uint32_t t_u32    = m_module.defIntType(32, 0);
      const uint32_t t_struct = m_module.defStructTypeUnique(1, &t_u32);
      
      m_module.decorate(t_struct, spv::DecorationBufferBlock);
      m_module.memberDecorateOffset(t_struct, 0, 0);
      
      m_module.setDebugName      (t_struct, "uav_meta");
      m_module.setDebugMemberName(t_struct, 0, "ctr");
      
      m_uavCtrStructType  = t_struct;
      m_uavCtrPointerType = m_module.defPointerType(
        t_struct, spv::StorageClassUniform);
    }
    
    // Declare the buffer variable
    const uint32_t varId = m_module.newVar(
      m_uavCtrPointerType, spv::StorageClassUniform);
    
    m_module.setDebugName(varId,
      str::format("u", regId, "_meta").c_str());
    
    const uint32_t bindingId = computeResourceSlotId(
      m_version.type(), DxbcBindingType::UavCounter,
      regId);
    
    m_module.decorateDescriptorSet(varId, 0);
    m_module.decorateBinding(varId, bindingId);
    
    // Declare the storage buffer binding
    DxvkResourceSlot resource;
    resource.slot = bindingId;
    resource.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    resource.view = VK_IMAGE_VIEW_TYPE_MAX_ENUM;
    m_resourceSlots.push_back(resource);
    
    return varId;
  }
  
  
  void DxbcCompiler::emitDclImmediateConstantBuffer(const DxbcShaderInstruction& ins) {
    if (m_immConstBuf != 0)
      throw DxvkError("DxbcCompiler: Immediate constant buffer already declared");
    
    if ((ins.customDataSize & 0x3) != 0)
      throw DxvkError("DxbcCompiler: Immediate constant buffer size not a multiple of four DWORDs");
    
    if (ins.customDataSize <= Icb_MaxBakedDwords) {
      this->emitDclImmediateConstantBufferBaked(
        ins.customDataSize, ins.customData);
    } else {
      this->emitDclImmediateConstantBufferUbo(
        ins.customDataSize, ins.customData);
    }
  }


  void DxbcCompiler::emitDclImmediateConstantBufferBaked(
          uint32_t                dwordCount,
    const uint32_t*               dwordArray) {
    // Declare individual vector constants as 4x32-bit vectors
    std::array<uint32_t, 4096> vectorIds;
    
    DxbcVectorType vecType;
    vecType.ctype  = DxbcScalarType::Uint32;
    vecType.ccount = 4;
    
    const uint32_t vectorTypeId = getVectorTypeId(vecType);
    const uint32_t vectorCount  = dwordCount / 4;
    
    for (uint32_t i = 0; i < vectorCount; i++) {
      std::array<uint32_t, 4> scalarIds = {
        m_module.constu32(dwordArray[4 * i + 0]),
        m_module.constu32(dwordArray[4 * i + 1]),
        m_module.constu32(dwordArray[4 * i + 2]),
        m_module.constu32(dwordArray[4 * i + 3]),
      };
      
      vectorIds.at(i) = m_module.constComposite(
        vectorTypeId, scalarIds.size(), scalarIds.data());
    }
    
    // Declare the array that contains all the vectors
    DxbcArrayType arrInfo;
    arrInfo.ctype   = DxbcScalarType::Uint32;
    arrInfo.ccount  = 4;
    arrInfo.alength = vectorCount;
    
    const uint32_t arrayTypeId = getArrayTypeId(arrInfo);
    const uint32_t arrayId = m_module.constComposite(
      arrayTypeId, vectorCount, vectorIds.data());
    
    // Declare the variable that will hold the constant
    // data and initialize it with the constant array.
    const uint32_t pointerTypeId = m_module.defPointerType(
      arrayTypeId, spv::StorageClassPrivate);
    
    m_immConstBuf = m_module.newVarInit(
      pointerTypeId, spv::StorageClassPrivate,
      arrayId);
    m_module.setDebugName(m_immConstBuf, "icb");
  }
  
  
  void DxbcCompiler::emitDclImmediateConstantBufferUbo(
          uint32_t                dwordCount,
    const uint32_t*               dwordArray) {
    this->emitDclConstantBufferVar(Icb_BindingSlotId, dwordCount / 4, "icb");
    m_immConstData = DxvkShaderConstData(dwordCount, dwordArray);
  }


  void DxbcCompiler::emitCustomData(const DxbcShaderInstruction& ins) {
    switch (ins.customDataType) {
      case DxbcCustomDataClass::ImmConstBuf:
        return emitDclImmediateConstantBuffer(ins);
      
      default:
        Logger::warn(str::format(
          "DxbcCompiler: Unsupported custom data block: ",
          ins.customDataType));
    }
  }
  
  
  void DxbcCompiler::emitVectorAlu(const DxbcShaderInstruction& ins) {
    std::array<DxbcRegisterValue, DxbcMaxOperandCount> src;
    
    for (uint32_t i = 0; i < ins.srcCount; i++)
      src.at(i) = emitRegisterLoad(ins.src[i], ins.dst[0].mask);
    
    DxbcRegisterValue dst;
    dst.type.ctype  = ins.dst[0].dataType;
    dst.type.ccount = ins.dst[0].mask.popCount();

    if (isDoubleType(ins.dst[0].dataType))
      dst.type.ccount /= 2;
    
    const uint32_t typeId = getVectorTypeId(dst.type);
    
    switch (ins.op) {
      /////////////////////
      // Move instructions
      case DxbcOpcode::Mov:
      case DxbcOpcode::DMov:
        dst.id = src.at(0).id;
        break;
        
      /////////////////////////////////////
      // ALU operations on float32 numbers
      case DxbcOpcode::Add:
      case DxbcOpcode::DAdd:
        dst.id = m_module.opFAdd(typeId,
          src.at(0).id, src.at(1).id);
        break;
        
      case DxbcOpcode::Div:
      case DxbcOpcode::DDiv:
        dst.id = m_module.opFDiv(typeId,
          src.at(0).id, src.at(1).id);
        break;
      
      case DxbcOpcode::Exp:
        dst.id = m_module.opExp2(
          typeId, src.at(0).id);
        break;
      
      case DxbcOpcode::Frc:
        dst.id = m_module.opFract(
          typeId, src.at(0).id);
        break;
      
      case DxbcOpcode::Log:
        dst.id = m_module.opLog2(
          typeId, src.at(0).id);
        break;
      
      case DxbcOpcode::Mad:
      case DxbcOpcode::DFma:
        dst.id = m_module.opFFma(typeId,
          src.at(0).id, src.at(1).id, src.at(2).id);
        break;
      
      case DxbcOpcode::Max:
      case DxbcOpcode::DMax:
        dst.id = m_module.opNMax(typeId,
          src.at(0).id, src.at(1).id);
        break;
      
      case DxbcOpcode::Min:
      case DxbcOpcode::DMin:
        dst.id = m_module.opNMin(typeId,
          src.at(0).id, src.at(1).id);
        break;
      
      case DxbcOpcode::Mul:
      case DxbcOpcode::DMul:
        dst.id = m_module.opFMul(typeId,
          src.at(0).id, src.at(1).id);
        break;
      
      case DxbcOpcode::Rcp:
        dst.id = m_module.opFDiv(typeId,
          emitBuildConstVecf32(
            1.0f, 1.0f, 1.0f, 1.0f,
            ins.dst[0].mask).id,
          src.at(0).id);
        break;
      
      case DxbcOpcode::DRcp:
        dst.id = m_module.opFDiv(typeId,
          emitBuildConstVecf64(1.0, 1.0,
            ins.dst[0].mask).id,
          src.at(0).id);
        break;
      
      case DxbcOpcode::RoundNe:
        dst.id = m_module.opRoundEven(
          typeId, src.at(0).id);
        break;
      
      case DxbcOpcode::RoundNi:
        dst.id = m_module.opFloor(
          typeId, src.at(0).id);
        break;
      
      case DxbcOpcode::RoundPi:
        dst.id = m_module.opCeil(
          typeId, src.at(0).id);
        break;
      
      case DxbcOpcode::RoundZ:
        dst.id = m_module.opTrunc(
          typeId, src.at(0).id);
        break;
      
      case DxbcOpcode::Rsq:
        dst.id = m_module.opInverseSqrt(
          typeId, src.at(0).id);
        break;
      
      case DxbcOpcode::Sqrt:
        dst.id = m_module.opSqrt(
          typeId, src.at(0).id);
        break;
      
      /////////////////////////////////////
      // ALU operations on signed integers
      case DxbcOpcode::IAdd:
        dst.id = m_module.opIAdd(typeId,
          src.at(0).id, src.at(1).id);
        break;
      
      case DxbcOpcode::IMad:
      case DxbcOpcode::UMad:
        dst.id = m_module.opIAdd(typeId,
          m_module.opIMul(typeId,
            src.at(0).id, src.at(1).id),
          src.at(2).id);
        break;
      
      case DxbcOpcode::IMax:
        dst.id = m_module.opSMax(typeId,
          src.at(0).id, src.at(1).id);
        break;
        
      case DxbcOpcode::IMin:
        dst.id = m_module.opSMin(typeId,
          src.at(0).id, src.at(1).id);
        break;
        
      case DxbcOpcode::INeg:
        dst.id = m_module.opSNegate(
          typeId, src.at(0).id);
        break;
      
      ///////////////////////////////////////
      // ALU operations on unsigned integers
      case DxbcOpcode::UMax:
        dst.id = m_module.opUMax(typeId,
          src.at(0).id, src.at(1).id);
        break;
        
      case DxbcOpcode::UMin:
        dst.id = m_module.opUMin(typeId,
          src.at(0).id, src.at(1).id);
        break;
      
      ///////////////////////////////////////
      // Bit operations on unsigned integers
      case DxbcOpcode::And:
        dst.id = m_module.opBitwiseAnd(typeId,
          src.at(0).id, src.at(1).id);
        break;
        
      case DxbcOpcode::Not:
        dst.id = m_module.opNot(
          typeId, src.at(0).id);
        break;
        
      case DxbcOpcode::Or:
        dst.id = m_module.opBitwiseOr(typeId,
          src.at(0).id, src.at(1).id);
        break;
      
      case DxbcOpcode::Xor:
        dst.id = m_module.opBitwiseXor(typeId,
          src.at(0).id, src.at(1).id);
        break;
      
      case DxbcOpcode::CountBits:
        dst.id = m_module.opBitCount(
          typeId, src.at(0).id);
        break;
      
      case DxbcOpcode::BfRev:
        dst.id = m_module.opBitReverse(
          typeId, src.at(0).id);
        break;
      
      ///////////////////////////
      // Conversion instructions
      case DxbcOpcode::ItoF:
        dst.id = m_module.opConvertStoF(
          typeId, src.at(0).id);
        break;
      
      case DxbcOpcode::UtoF:
        dst.id = m_module.opConvertUtoF(
          typeId, src.at(0).id);
        break;
      
      case DxbcOpcode::FtoI:
        dst.id = m_module.opConvertFtoS(
          typeId, src.at(0).id);
        break;
      
      case DxbcOpcode::FtoU:
        dst.id = m_module.opConvertFtoU(
          typeId, src.at(0).id);
        break;
      
      default:
        Logger::warn(str::format(
          "DxbcCompiler: Unhandled instruction: ",
          ins.op));
        return;
    }
    
    if (ins.controls.precise())
      m_module.decorate(dst.id, spv::DecorationNoContraction);
    
    // Store computed value
    dst = emitDstOperandModifiers(dst, ins.modifiers);
    emitRegisterStore(ins.dst[0], dst);
  }
  
  
  void DxbcCompiler::emitVectorCmov(const DxbcShaderInstruction& ins) {
    // movc and swapc have the following operands:
    //    (dst0) The first destination register
    //    (dst1) The second destination register (swapc only)
    //    (src0) The condition vector
    //    (src1) Vector to select from if the condition is not 0
    //    (src2) Vector to select from if the condition is 0
    const DxbcRegisterValue condition   = emitRegisterLoad(ins.src[0], ins.dst[0].mask);
    const DxbcRegisterValue selectTrue  = emitRegisterLoad(ins.src[1], ins.dst[0].mask);
    const DxbcRegisterValue selectFalse = emitRegisterLoad(ins.src[2], ins.dst[0].mask);
    
    const uint32_t componentCount = ins.dst[0].mask.popCount();
    
    // We'll compare against a vector of zeroes to generate a
    // boolean vector, which in turn will be used by OpSelect
    uint32_t zeroType = m_module.defIntType(32, 0);
    uint32_t boolType = m_module.defBoolType();
    
    uint32_t zero = m_module.constu32(0);
    
    if (componentCount > 1) {
      zeroType = m_module.defVectorType(zeroType, componentCount);
      boolType = m_module.defVectorType(boolType, componentCount);
      
      const std::array<uint32_t, 4> zeroVec = { zero, zero, zero, zero };
      zero = m_module.constComposite(zeroType, componentCount, zeroVec.data());
    }
    
    // In case of swapc, the second destination operand receives
    // the output that a cmov instruction would normally get
    const uint32_t trueIndex = ins.op == DxbcOpcode::Swapc ? 1 : 0;
    
    for (uint32_t i = 0; i < ins.dstCount; i++) {
      DxbcRegisterValue result;
      result.type.ctype  = ins.dst[i].dataType;
      result.type.ccount = componentCount;
      result.id = m_module.opSelect(
        getVectorTypeId(result.type),
        m_module.opINotEqual(boolType, condition.id, zero),
        i == trueIndex ? selectTrue.id : selectFalse.id,
        i != trueIndex ? selectTrue.id : selectFalse.id);
      
      result = emitDstOperandModifiers(result, ins.modifiers);
      emitRegisterStore(ins.dst[i], result);
    }
  }
  
  void DxbcCompiler::emitVectorCmp(const DxbcShaderInstruction& ins) {
    // Compare instructions have three operands:
    //    (dst0) The destination register
    //    (src0) The first vector to compare
    //    (src1) The second vector to compare
    uint32_t componentCount = ins.dst[0].mask.popCount();

    // For 64-bit operations, we'll return a 32-bit
    // vector, so we have to adjust the read mask
    DxbcRegMask srcMask = ins.dst[0].mask;

    if (isDoubleType(ins.src[0].dataType)) {
      srcMask = DxbcRegMask(
        componentCount > 0, componentCount > 0,
        componentCount > 1, componentCount > 1);
    }

    const std::array<DxbcRegisterValue, 2> src = {
      emitRegisterLoad(ins.src[0], srcMask),
      emitRegisterLoad(ins.src[1], srcMask),
    };
    
    // Condition, which is a boolean vector used
    // to select between the ~0u and 0u vectors.
    uint32_t condition     = 0;
    uint32_t conditionType = m_module.defBoolType();
    
    if (componentCount > 1)
      conditionType = m_module.defVectorType(conditionType, componentCount);
    
    switch (ins.op) {
      case DxbcOpcode::Eq:
      case DxbcOpcode::DEq:
        condition = m_module.opFOrdEqual(
          conditionType, src.at(0).id, src.at(1).id);
        break;
      
      case DxbcOpcode::Ge:
      case DxbcOpcode::DGe:
        condition = m_module.opFOrdGreaterThanEqual(
          conditionType, src.at(0).id, src.at(1).id);
        break;
      
      case DxbcOpcode::Lt:
      case DxbcOpcode::DLt:
        condition = m_module.opFOrdLessThan(
          conditionType, src.at(0).id, src.at(1).id);
        break;
      
      case DxbcOpcode::Ne:
      case DxbcOpcode::DNe:
        condition = m_module.opFOrdNotEqual(
          conditionType, src.at(0).id, src.at(1).id);
        break;
      
      case DxbcOpcode::IEq:
        condition = m_module.opIEqual(
          conditionType, src.at(0).id, src.at(1).id);
        break;
      
      case DxbcOpcode::IGe:
        condition = m_module.opSGreaterThanEqual(
          conditionType, src.at(0).id, src.at(1).id);
        break;
      
      case DxbcOpcode::ILt:
        condition = m_module.opSLessThan(
          conditionType, src.at(0).id, src.at(1).id);
        break;
      
      case DxbcOpcode::INe:
        condition = m_module.opINotEqual(
          conditionType, src.at(0).id, src.at(1).id);
        break;
      
      case DxbcOpcode::UGe:
        condition = m_module.opUGreaterThanEqual(
          conditionType, src.at(0).id, src.at(1).id);
        break;
      
      case DxbcOpcode::ULt:
        condition = m_module.opULessThan(
          conditionType, src.at(0).id, src.at(1).id);
        break;
      
      default:
        Logger::warn(str::format(
          "DxbcCompiler: Unhandled instruction: ",
          ins.op));
        return;
    }
    
    // Generate constant vectors for selection
    uint32_t sFalse = m_module.constu32( 0u);
    uint32_t sTrue  = m_module.constu32(~0u);
    
    DxbcRegisterValue result;
    result.type.ctype  = DxbcScalarType::Uint32;
    result.type.ccount = componentCount;
    
    const uint32_t typeId = getVectorTypeId(result.type);
    
    if (componentCount > 1) {
      const std::array<uint32_t, 4> vFalse = { sFalse, sFalse, sFalse, sFalse };
      const std::array<uint32_t, 4> vTrue  = { sTrue,  sTrue,  sTrue,  sTrue  };
      
      sFalse = m_module.constComposite(typeId, componentCount, vFalse.data());
      sTrue  = m_module.constComposite(typeId, componentCount, vTrue .data());
    }
    
    // Perform component-wise mask selection
    // based on the condition evaluated above.
    result.id = m_module.opSelect(
      typeId, condition, sTrue, sFalse);
    
    emitRegisterStore(ins.dst[0], result);
  }
  
  
  void DxbcCompiler::emitVectorDeriv(const DxbcShaderInstruction& ins) {
    // Derivative instructions have two operands:
    //    (dst0) Destination register for the derivative
    //    (src0) The operand to compute the derivative of
    DxbcRegisterValue value = emitRegisterLoad(ins.src[0], ins.dst[0].mask);
    const uint32_t typeId = getVectorTypeId(value.type);
    
    switch (ins.op) {
      case DxbcOpcode::DerivRtx:
        value.id = m_module.opDpdx(typeId, value.id);
        break;
        
      case DxbcOpcode::DerivRty:
        value.id = m_module.opDpdy(typeId, value.id);
        break;
        
      case DxbcOpcode::DerivRtxCoarse:
        value.id = m_module.opDpdxCoarse(typeId, value.id);
        break;
        
      case DxbcOpcode::DerivRtyCoarse:
        value.id = m_module.opDpdyCoarse(typeId, value.id);
        break;
        
      case DxbcOpcode::DerivRtxFine:
        value.id = m_module.opDpdxFine(typeId, value.id);
        break;
        
      case DxbcOpcode::DerivRtyFine:
        value.id = m_module.opDpdyFine(typeId, value.id);
        break;
      
      default:
        Logger::warn(str::format(
          "DxbcCompiler: Unhandled instruction: ",
          ins.op));
        return;
    }
    
    value = emitDstOperandModifiers(value, ins.modifiers);
    emitRegisterStore(ins.dst[0], value);
  }
  
  
  void DxbcCompiler::emitVectorDot(const DxbcShaderInstruction& ins) {
    const DxbcRegMask srcMask(true,
      ins.op >= DxbcOpcode::Dp2,
      ins.op >= DxbcOpcode::Dp3,
      ins.op >= DxbcOpcode::Dp4);
    
    const std::array<DxbcRegisterValue, 2> src = {
      emitRegisterLoad(ins.src[0], srcMask),
      emitRegisterLoad(ins.src[1], srcMask),
    };
    
    DxbcRegisterValue dst;
    dst.type.ctype  = ins.dst[0].dataType;
    dst.type.ccount = 1;
    
    dst.id = m_module.opDot(
      getVectorTypeId(dst.type),
      src.at(0).id,
      src.at(1).id);
    
    if (ins.controls.precise())
      m_module.decorate(dst.id, spv::DecorationNoContraction);
    
    dst = emitDstOperandModifiers(dst, ins.modifiers);
    emitRegisterStore(ins.dst[0], dst);
  }
  
  
  void DxbcCompiler::emitVectorIdiv(const DxbcShaderInstruction& ins) {
    // udiv has four operands:
    //    (dst0) Quotient destination register
    //    (dst1) Remainder destination register
    //    (src0) The first vector to compare
    //    (src1) The second vector to compare
    if (ins.dst[0].type == DxbcOperandType::Null
     && ins.dst[1].type == DxbcOperandType::Null)
      return;
    
    // FIXME support this if applications require it
    if (ins.dst[0].type != DxbcOperandType::Null
     && ins.dst[1].type != DxbcOperandType::Null
     && ins.dst[0].mask != ins.dst[1].mask) {
      Logger::warn("DxbcCompiler: Idiv with different destination masks not supported");
      return;
    }
    
    // Load source operands as integers with the
    // mask of one non-NULL destination operand
    const DxbcRegMask srcMask =
      ins.dst[0].type != DxbcOperandType::Null
        ? ins.dst[0].mask
        : ins.dst[1].mask;
    
    const std::array<DxbcRegisterValue, 2> src = {
      emitRegisterLoad(ins.src[0], srcMask),
      emitRegisterLoad(ins.src[1], srcMask),
    };
    
    // Compute results only if the destination
    // operands are not NULL.
    if (ins.dst[0].type != DxbcOperandType::Null) {
      DxbcRegisterValue quotient;
      quotient.type.ctype  = ins.dst[0].dataType;
      quotient.type.ccount = ins.dst[0].mask.popCount();
      
      quotient.id = m_module.opUDiv(
        getVectorTypeId(quotient.type),
        src.at(0).id, src.at(1).id);
      
      quotient = emitDstOperandModifiers(quotient, ins.modifiers);
      emitRegisterStore(ins.dst[0], quotient);
    }
    
    if (ins.dst[1].type != DxbcOperandType::Null) {
      DxbcRegisterValue remainder;
      remainder.type.ctype  = ins.dst[1].dataType;
      remainder.type.ccount = ins.dst[1].mask.popCount();
      
      remainder.id = m_module.opUMod(
        getVectorTypeId(remainder.type),
        src.at(0).id, src.at(1).id);
      
      remainder = emitDstOperandModifiers(remainder, ins.modifiers);
      emitRegisterStore(ins.dst[1], remainder);
    }
  }
  
  
  void DxbcCompiler::emitVectorImul(const DxbcShaderInstruction& ins) {
    // imul and umul have four operands:
    //    (dst0) High destination register
    //    (dst1) Low destination register
    //    (src0) The first vector to compare
    //    (src1) The second vector to compare
    if (ins.dst[0].type == DxbcOperandType::Null) {
      if (ins.dst[1].type == DxbcOperandType::Null)
        return;
      
      // If dst0 is NULL, this instruction behaves just
      // like any other three-operand ALU instruction
      const std::array<DxbcRegisterValue, 2> src = {
        emitRegisterLoad(ins.src[0], ins.dst[1].mask),
        emitRegisterLoad(ins.src[1], ins.dst[1].mask),
      };
      
      DxbcRegisterValue result;
      result.type.ctype  = ins.dst[1].dataType;
      result.type.ccount = ins.dst[1].mask.popCount();
      result.id = m_module.opIMul(
        getVectorTypeId(result.type),
        src.at(0).id, src.at(1).id);
      
      result = emitDstOperandModifiers(result, ins.modifiers);
      emitRegisterStore(ins.dst[1], result);
    } else {
      // TODO implement this
      Logger::warn("DxbcCompiler: Extended Imul not yet supported");
    }
  }
  
  
  void DxbcCompiler::emitVectorShift(const DxbcShaderInstruction& ins) {
    // Shift operations have three operands:
    //    (dst0) The destination register
    //    (src0) The register to shift
    //    (src1) The shift amount (scalar)
    DxbcRegisterValue shiftReg = emitRegisterLoad(ins.src[0], ins.dst[0].mask);
    DxbcRegisterValue countReg = emitRegisterLoad(ins.src[1], ins.dst[0].mask);
    
    if (countReg.type.ccount == 1)
      countReg = emitRegisterExtend(countReg, shiftReg.type.ccount);
    
    DxbcRegisterValue result;
    result.type.ctype  = ins.dst[0].dataType;
    result.type.ccount = ins.dst[0].mask.popCount();
    
    switch (ins.op) {
      case DxbcOpcode::IShl:
        result.id = m_module.opShiftLeftLogical(
          getVectorTypeId(result.type),
          shiftReg.id, countReg.id);
        break;
      
      case DxbcOpcode::IShr:
        result.id = m_module.opShiftRightArithmetic(
          getVectorTypeId(result.type),
          shiftReg.id, countReg.id);
        break;
      
      case DxbcOpcode::UShr:
        result.id = m_module.opShiftRightLogical(
          getVectorTypeId(result.type),
          shiftReg.id, countReg.id);
        break;
      
      default:
        Logger::warn(str::format(
          "DxbcCompiler: Unhandled instruction: ",
          ins.op));
        return;
    }
    
    result = emitDstOperandModifiers(result, ins.modifiers);
    emitRegisterStore(ins.dst[0], result);
  }
  
    
  void DxbcCompiler::emitVectorSinCos(const DxbcShaderInstruction& ins) {
    // sincos has three operands:
    //    (dst0) Destination register for sin(x)
    //    (dst1) Destination register for cos(x)
    //    (src0) Source operand x
    
    // Load source operand as 32-bit float vector.
    const DxbcRegisterValue srcValue = emitRegisterLoad(
      ins.src[0], DxbcRegMask(true, true, true, true));
    
    // Either output may be DxbcOperandType::Null, in
    // which case we don't have to generate any code.
    if (ins.dst[0].type != DxbcOperandType::Null) {
      const DxbcRegisterValue sinInput =
        emitRegisterExtract(srcValue, ins.dst[0].mask);
      
      DxbcRegisterValue sin;
      sin.type = sinInput.type;
      sin.id = m_module.opSin(
        getVectorTypeId(sin.type),
        sinInput.id);
      
      emitRegisterStore(ins.dst[0], sin);
    }
    
    if (ins.dst[1].type != DxbcOperandType::Null) {
      const DxbcRegisterValue cosInput =
        emitRegisterExtract(srcValue, ins.dst[1].mask);
      
      DxbcRegisterValue cos;
      cos.type = cosInput.type;
      cos.id = m_module.opCos(
        getVectorTypeId(cos.type),
        cosInput.id);
      
      emitRegisterStore(ins.dst[1], cos);
    }
  }
  
  
  void DxbcCompiler::emitGeometryEmit(const DxbcShaderInstruction& ins) {
    // Checking the negation is easier for EmitThenCut/EmitThenCutStream
    bool doEmit = ins.op != DxbcOpcode::Cut && ins.op != DxbcOpcode::CutStream;
    bool doCut = ins.op != DxbcOpcode::Emit && ins.op != DxbcOpcode::EmitStream;

    if (doEmit) {
      emitOutputSetup();
      emitClipCullStore(DxbcSystemValue::ClipDistance, m_clipDistances);
      emitClipCullStore(DxbcSystemValue::CullDistance, m_cullDistances);
      m_module.opEmitVertex();
    }

    if (doCut)
      m_module.opEndPrimitive();
  }
  
  
  void DxbcCompiler::emitAtomic(const DxbcShaderInstruction& ins) {
    // atomic_* operations have the following operands:
    //    (dst0) Destination u# or g# register
    //    (src0) Index into the texture or buffer
    //    (src1) The source value for the operation
    //    (src2) Second source operand (optional)
    // imm_atomic_* operations have the following operands:
    //    (dst0) Register that receives the result
    //    (dst1) Destination u# or g# register
    //    (srcX) As above
    const DxbcBufferInfo bufferInfo = getBufferInfo(ins.dst[ins.dstCount - 1]);
    
    const bool isImm = ins.dstCount == 2;
    const bool isUav = ins.dst[ins.dstCount - 1].type == DxbcOperandType::UnorderedAccessView;
    
    // Perform atomic operations on UAVs only if the UAV
    // is bound and if there is nothing else stopping us.
    DxbcConditional cond;
    
    if (isUav) {
      uint32_t writeTest = emitUavWriteTest(bufferInfo);
      
      cond.labelIf  = m_module.allocateId();
      cond.labelEnd = m_module.allocateId();
      
      m_module.opSelectionMerge(cond.labelEnd, spv::SelectionControlMaskNone);
      m_module.opBranchConditional(writeTest, cond.labelIf, cond.labelEnd);
      
      m_module.opLabel(cond.labelIf);
    }
    
    // Retrieve destination pointer for the atomic operation>
    const DxbcRegisterPointer pointer = emitGetAtomicPointer(
      ins.dst[ins.dstCount - 1], ins.src[0]);
    
    // Load source values
    std::array<DxbcRegisterValue, 2> src;
    
    for (uint32_t i = 1; i < ins.srcCount; i++) {
      src[i - 1] = emitRegisterBitcast(
        emitRegisterLoad(ins.src[i], DxbcRegMask(true, false, false, false)),
        pointer.type.ctype);
    }
    
    // Define memory scope and semantics based on the operands
    uint32_t scope     = 0;
    uint32_t semantics = 0;
    
    if (isUav) {
      scope     = spv::ScopeDevice;
      semantics = spv::MemorySemanticsImageMemoryMask
                | spv::MemorySemanticsAcquireReleaseMask;
    } else {
      scope     = spv::ScopeWorkgroup;
      semantics = spv::MemorySemanticsWorkgroupMemoryMask
                | spv::MemorySemanticsAcquireReleaseMask;
    }
    
    const uint32_t scopeId     = m_module.constu32(scope);
    const uint32_t semanticsId = m_module.constu32(semantics);
    
    // Perform the atomic operation on the given pointer
    DxbcRegisterValue value;
    value.type = pointer.type;
    value.id   = 0;
    
    // The result type, which is a scalar integer
    const uint32_t typeId = getVectorTypeId(value.type);
    
    switch (ins.op) {
      case DxbcOpcode::AtomicCmpStore:
      case DxbcOpcode::ImmAtomicCmpExch:
        value.id = m_module.opAtomicCompareExchange(
          typeId, pointer.id, scopeId, semanticsId,
          m_module.constu32(spv::MemorySemanticsMaskNone),
          src[1].id, src[0].id);
        break;
      
      case DxbcOpcode::ImmAtomicExch:
        value.id = m_module.opAtomicExchange(typeId,
          pointer.id, scopeId, semanticsId,
          src[0].id);
        break;
      
      case DxbcOpcode::AtomicIAdd:
      case DxbcOpcode::ImmAtomicIAdd:
        value.id = m_module.opAtomicIAdd(typeId,
          pointer.id, scopeId, semanticsId,
          src[0].id);
        break;
      
      case DxbcOpcode::AtomicAnd:
      case DxbcOpcode::ImmAtomicAnd:
        value.id = m_module.opAtomicAnd(typeId,
          pointer.id, scopeId, semanticsId,
          src[0].id);
        break;
      
      case DxbcOpcode::AtomicOr:
      case DxbcOpcode::ImmAtomicOr:
        value.id = m_module.opAtomicOr(typeId,
          pointer.id, scopeId, semanticsId,
          src[0].id);
        break;
      
      case DxbcOpcode::AtomicXor:
      case DxbcOpcode::ImmAtomicXor:
        value.id = m_module.opAtomicXor(typeId,
          pointer.id, scopeId, semanticsId,
          src[0].id);
        break;
      
      case DxbcOpcode::AtomicIMin:
      case DxbcOpcode::ImmAtomicIMin:
        value.id = m_module.opAtomicSMin(typeId,
          pointer.id, scopeId, semanticsId,
          src[0].id);
        break;
      
      case DxbcOpcode::AtomicIMax:
      case DxbcOpcode::ImmAtomicIMax:
        value.id = m_module.opAtomicSMax(typeId,
          pointer.id, scopeId, semanticsId,
          src[0].id);
        break;
      
      case DxbcOpcode::AtomicUMin:
      case DxbcOpcode::ImmAtomicUMin:
        value.id = m_module.opAtomicUMin(typeId,
          pointer.id, scopeId, semanticsId,
          src[0].id);
        break;
      
      case DxbcOpcode::AtomicUMax:
      case DxbcOpcode::ImmAtomicUMax:
        value.id = m_module.opAtomicUMax(typeId,
          pointer.id, scopeId, semanticsId,
          src[0].id);
        break;
      
      default:
        Logger::warn(str::format(
          "DxbcCompiler: Unhandled instruction: ",
          ins.op));
        return;
    }
    
    // Write back the result to the destination
    // register if this is an imm_atomic_* opcode.
    if (isImm)
      emitRegisterStore(ins.dst[0], value);
    
    // End conditional block
    if (isUav) {
      m_module.opBranch(cond.labelEnd);
      m_module.opLabel (cond.labelEnd);
    }
  }
  
  
  void DxbcCompiler::emitAtomicCounter(const DxbcShaderInstruction& ins) {
    // imm_atomic_alloc and imm_atomic_consume have the following operands:
    //    (dst0) The register that will hold the old counter value
    //    (dst1) The UAV whose counter is going to be modified
    const DxbcBufferInfo bufferInfo = getBufferInfo(ins.dst[1]);
    
    const uint32_t registerId = ins.dst[1].idx[0].offset;
    
    if (m_uavs.at(registerId).ctrId == 0)
      m_uavs.at(registerId).ctrId = emitDclUavCounter(registerId);
    
    // Only perform the operation if the UAV is bound
    uint32_t writeTest = emitUavWriteTest(bufferInfo);
    
    DxbcConditional cond;
    cond.labelIf  = m_module.allocateId();
    cond.labelEnd = m_module.allocateId();
    
    m_module.opSelectionMerge(cond.labelEnd, spv::SelectionControlMaskNone);
    m_module.opBranchConditional(writeTest, cond.labelIf, cond.labelEnd);
    
    m_module.opLabel(cond.labelIf);
    
    // Get a pointer to the atomic counter in question
    DxbcRegisterInfo ptrType;
    ptrType.type.ctype   = DxbcScalarType::Uint32;
    ptrType.type.ccount  = 1;
    ptrType.type.alength = 0;
    ptrType.sclass = spv::StorageClassUniform;
    
    const uint32_t zeroId = m_module.consti32(0);
    const uint32_t ptrId  = m_module.opAccessChain(
      getPointerTypeId(ptrType),
      m_uavs.at(registerId).ctrId,
      1, &zeroId);
    
    // Define memory scope and semantics based on the operands
    uint32_t scope     = spv::ScopeDevice;
    uint32_t semantics = spv::MemorySemanticsUniformMemoryMask
                       | spv::MemorySemanticsAcquireReleaseMask;
    
    const uint32_t scopeId     = m_module.constu32(scope);
    const uint32_t semanticsId = m_module.constu32(semantics);
    
    // Compute the result value
    DxbcRegisterValue value;
    value.type.ctype  = DxbcScalarType::Uint32;
    value.type.ccount = 1;
    
    const uint32_t typeId = getVectorTypeId(value.type);
    
    switch (ins.op) {
      case DxbcOpcode::ImmAtomicAlloc:
        value.id = m_module.opAtomicIAdd(typeId, ptrId,
          scopeId, semanticsId, m_module.constu32(1));
        break;
        
      case DxbcOpcode::ImmAtomicConsume:
        value.id = m_module.opAtomicISub(typeId, ptrId,
          scopeId, semanticsId, m_module.constu32(1));
        value.id = m_module.opISub(typeId, value.id,
          m_module.constu32(1));
        break;
      
      default:
        Logger::warn(str::format(
          "DxbcCompiler: Unhandled instruction: ",
          ins.op));
        return;
    }
    
    // Store the result
    emitRegisterStore(ins.dst[0], value);
    
    // End conditional block
    m_module.opBranch(cond.labelEnd);
    m_module.opLabel (cond.labelEnd);
  }
  
  
  void DxbcCompiler::emitBarrier(const DxbcShaderInstruction& ins) {
    // sync takes no operands. Instead, the synchronization
    // scope is defined by the operand control bits.
    const DxbcSyncFlags flags = ins.controls.syncFlags();
    
    uint32_t executionScope   = spv::ScopeInvocation;
    uint32_t memoryScope      = spv::ScopeInvocation;
    uint32_t memorySemantics  = 0;
    
    if (flags.test(DxbcSyncFlag::ThreadsInGroup))
      executionScope   = spv::ScopeWorkgroup;
    
    if (flags.test(DxbcSyncFlag::ThreadGroupSharedMemory)) {
      memoryScope      = spv::ScopeWorkgroup;
      memorySemantics |= spv::MemorySemanticsWorkgroupMemoryMask
                      |  spv::MemorySemanticsAcquireReleaseMask;
    }
    
    if (flags.test(DxbcSyncFlag::UavMemoryGroup)) {
      memoryScope      = spv::ScopeWorkgroup;
      memorySemantics |= spv::MemorySemanticsImageMemoryMask
                      |  spv::MemorySemanticsUniformMemoryMask
                      |  spv::MemorySemanticsAcquireReleaseMask;
    }
    
    if (flags.test(DxbcSyncFlag::UavMemoryGlobal)) {
      memoryScope      = spv::ScopeDevice;
      memorySemantics |= spv::MemorySemanticsImageMemoryMask
                      |  spv::MemorySemanticsUniformMemoryMask
                      |  spv::MemorySemanticsAcquireReleaseMask;
    }
    
    if (executionScope != spv::ScopeInvocation) {
      m_module.opControlBarrier(
        m_module.constu32(executionScope),
        m_module.constu32(memoryScope),
        m_module.constu32(memorySemantics));
    } else if (memoryScope != spv::ScopeInvocation) {
      m_module.opMemoryBarrier(
        m_module.constu32(memoryScope),
        m_module.constu32(memorySemantics));
    } else {
      Logger::warn("DxbcCompiler: sync instruction has no effect");
    }
  }
  
  
  void DxbcCompiler::emitBitExtract(const DxbcShaderInstruction& ins) {
    // ibfe and ubfe take the following arguments:
    //    (dst0) The destination register
    //    (src0) Number of bits to extact
    //    (src1) Offset of the bits to extract
    //    (src2) Register to extract bits from
    const bool isSigned = ins.op == DxbcOpcode::IBfe;
    
    const DxbcRegisterValue bitCnt = emitRegisterLoad(ins.src[0], ins.dst[0].mask);
    const DxbcRegisterValue bitOfs = emitRegisterLoad(ins.src[1], ins.dst[0].mask);
    
    const DxbcRegisterValue src = emitRegisterLoad(ins.src[2], ins.dst[0].mask);
    
    const uint32_t componentCount  = src.type.ccount;
    std::array<uint32_t, 4> componentIds = {{ 0, 0, 0, 0 }};
    
    for (uint32_t i = 0; i < componentCount; i++) {
      const DxbcRegisterValue currBitCnt = emitRegisterExtract(bitCnt, DxbcRegMask::select(i));
      const DxbcRegisterValue currBitOfs = emitRegisterExtract(bitOfs, DxbcRegMask::select(i));
      const DxbcRegisterValue currSrc    = emitRegisterExtract(src,    DxbcRegMask::select(i));
      
      const uint32_t typeId = getVectorTypeId(currSrc.type);
      
      componentIds[i] = isSigned
        ? m_module.opBitFieldSExtract(typeId, currSrc.id, currBitOfs.id, currBitCnt.id)
        : m_module.opBitFieldUExtract(typeId, currSrc.id, currBitOfs.id, currBitCnt.id);
    }
    
    DxbcRegisterValue result;
    result.type = src.type;
    result.id   = componentCount > 1
      ? m_module.opCompositeConstruct(
          getVectorTypeId(result.type),
          componentCount, componentIds.data())
      : componentIds[0];
    emitRegisterStore(ins.dst[0], result);
  }
  
  
  void DxbcCompiler::emitBitInsert(const DxbcShaderInstruction& ins) {
    // ibfe and ubfe take the following arguments:
    //    (dst0) The destination register
    //    (src0) Number of bits to extact
    //    (src1) Offset of the bits to extract
    //    (src2) Register to take bits from
    //    (src3) Register to replace bits in
    const DxbcRegisterValue bitCnt = emitRegisterLoad(ins.src[0], ins.dst[0].mask);
    const DxbcRegisterValue bitOfs = emitRegisterLoad(ins.src[1], ins.dst[0].mask);
    
    const DxbcRegisterValue insert = emitRegisterLoad(ins.src[2], ins.dst[0].mask);
    const DxbcRegisterValue base   = emitRegisterLoad(ins.src[3], ins.dst[0].mask);
    
    const uint32_t componentCount  = base.type.ccount;
    std::array<uint32_t, 4> componentIds = {{ 0, 0, 0, 0 }};
    
    for (uint32_t i = 0; i < componentCount; i++) {
      const DxbcRegisterValue currBitCnt = emitRegisterExtract(bitCnt, DxbcRegMask::select(i));
      const DxbcRegisterValue currBitOfs = emitRegisterExtract(bitOfs, DxbcRegMask::select(i));
      const DxbcRegisterValue currInsert = emitRegisterExtract(insert, DxbcRegMask::select(i));
      const DxbcRegisterValue currBase   = emitRegisterExtract(base,   DxbcRegMask::select(i));
      
      componentIds[i] = m_module.opBitFieldInsert(
        getVectorTypeId(currBase.type),
        currBase.id,   currInsert.id,
        currBitOfs.id, currBitCnt.id);
    }
    
    DxbcRegisterValue result;
    result.type = base.type;
    result.id   = componentCount > 1
      ? m_module.opCompositeConstruct(
          getVectorTypeId(result.type),
          componentCount, componentIds.data())
      : componentIds[0];
    emitRegisterStore(ins.dst[0], result);
  }
  
  
  void DxbcCompiler::emitBitScan(const DxbcShaderInstruction& ins) {
    // firstbit(lo|hi|shi) have two operands:
    //    (dst0) The destination operant
    //    (src0) Source operand to scan
    DxbcRegisterValue src = emitRegisterLoad(ins.src[0], ins.dst[0].mask);
    
    DxbcRegisterValue dst;
    dst.type.ctype  = ins.dst[0].dataType;
    dst.type.ccount = ins.dst[0].mask.popCount();
    
    // Result type, should be an unsigned integer
    const uint32_t typeId = getVectorTypeId(dst.type);
    
    switch (ins.op) {
      case DxbcOpcode::FirstBitLo:  dst.id = m_module.opFindILsb(typeId, src.id); break;
      case DxbcOpcode::FirstBitHi:  dst.id = m_module.opFindUMsb(typeId, src.id); break;
      case DxbcOpcode::FirstBitShi: dst.id = m_module.opFindSMsb(typeId, src.id); break;
      default: Logger::warn(str::format("DxbcCompiler: Unhandled instruction: ", ins.op)); return;
    }
    
    // The 'Hi' variants are counted from the MSB in DXBC
    // rather than the LSB, so we have to invert the number
    if (ins.op == DxbcOpcode::FirstBitHi
     || ins.op == DxbcOpcode::FirstBitShi) {
      dst.id = m_module.opSelect(typeId,
        m_module.opINotEqual(m_module.defBoolType(),
          dst.id, m_module.constu32(0xFFFFFFFF)),
        m_module.opISub(typeId, m_module.constu32(31), dst.id),
        m_module.constu32(0xFFFFFFFF));
    }
    
    // No modifiers are supported
    emitRegisterStore(ins.dst[0], dst);
  }
  
  
  void DxbcCompiler::emitBufferQuery(const DxbcShaderInstruction& ins) {
    // bufinfo takes two arguments
    //    (dst0) The destination register
    //    (src0) The buffer register to query
    // TODO Check if resource is bound
    const DxbcBufferInfo bufferInfo = getBufferInfo(ins.src[0]);
    
    // We'll store this as a scalar unsigned integer
    DxbcRegisterValue result = emitQueryTexelBufferSize(ins.src[0]);
    const uint32_t typeId = getVectorTypeId(result.type);
    
    // Adjust returned size if this is a raw or structured
    // buffer, as emitQueryTexelBufferSize only returns the
    // number of typed elements in the buffer.
    if (bufferInfo.type == DxbcResourceType::Raw) {
      result.id = m_module.opIMul(typeId,
        result.id, m_module.constu32(4));
    } else if (bufferInfo.type == DxbcResourceType::Structured) {
      result.id = m_module.opUDiv(typeId, result.id,
        m_module.constu32(bufferInfo.stride / 4));
    }
    
    // Store the result. The scalar will be extended to a
    // vector if the write mask consists of more than one
    // component, which is the desired behaviour.
    emitRegisterStore(ins.dst[0], result);
  }
  
  
  void DxbcCompiler::emitBufferLoad(const DxbcShaderInstruction& ins) {
    // ld_raw takes three arguments:
    //    (dst0) Destination register
    //    (src0) Byte offset
    //    (src1) Source register
    // ld_structured takes four arguments:
    //    (dst0) Destination register
    //    (src0) Structure index
    //    (src1) Byte offset
    //    (src2) Source register
    // TODO Check if resource is bound
    const bool isStructured = ins.op == DxbcOpcode::LdStructured;
    
    // Source register. The exact way we access
    // the data depends on the register type.
    const DxbcRegister& dstReg = ins.dst[0];
    const DxbcRegister& srcReg = isStructured ? ins.src[2] : ins.src[1];
    
    // Retrieve common info about the buffer
    const DxbcBufferInfo bufferInfo = getBufferInfo(srcReg);
    
    // Compute element index
    const DxbcRegisterValue elementIndex = isStructured
      ? emitCalcBufferIndexStructured(
          emitRegisterLoad(ins.src[0], DxbcRegMask(true, false, false, false)),
          emitRegisterLoad(ins.src[1], DxbcRegMask(true, false, false, false)),
          bufferInfo.stride)
      : emitCalcBufferIndexRaw(
          emitRegisterLoad(ins.src[0], DxbcRegMask(true, false, false, false)));
    
    emitRegisterStore(dstReg,
      emitRawBufferLoad(srcReg, elementIndex, dstReg.mask));
  }
  
  
  void DxbcCompiler::emitBufferStore(const DxbcShaderInstruction& ins) {
    // store_raw takes three arguments:
    //    (dst0) Destination register
    //    (src0) Byte offset
    //    (src1) Source register
    // store_structured takes four arguments:
    //    (dst0) Destination register
    //    (src0) Structure index
    //    (src1) Byte offset
    //    (src2) Source register
    // TODO Check if resource is bound
    const bool isStructured = ins.op == DxbcOpcode::StoreStructured;
    
    // Source register. The exact way we access
    // the data depends on the register type.
    const DxbcRegister& dstReg = ins.dst[0];
    const DxbcRegister& srcReg = isStructured ? ins.src[2] : ins.src[1];
    
    // Retrieve common info about the buffer
    const DxbcBufferInfo bufferInfo = getBufferInfo(dstReg);
    
    // Compute element index
    const DxbcRegisterValue elementIndex = isStructured
      ? emitCalcBufferIndexStructured(
          emitRegisterLoad(ins.src[0], DxbcRegMask(true, false, false, false)),
          emitRegisterLoad(ins.src[1], DxbcRegMask(true, false, false, false)),
          bufferInfo.stride)
      : emitCalcBufferIndexRaw(
          emitRegisterLoad(ins.src[0], DxbcRegMask(true, false, false, false)));
    
    emitRawBufferStore(dstReg, elementIndex,
      emitRegisterLoad(srcReg, dstReg.mask));
  }
  
  
  void DxbcCompiler::emitConvertFloat16(const DxbcShaderInstruction& ins) {
    // f32tof16 takes two operands:
    //    (dst0) Destination register as a uint32 vector
    //    (src0) Source register as a float32 vector
    // f16tof32 takes two operands:
    //    (dst0) Destination register as a float32 vector
    //    (src0) Source register as a uint32 vector
    const DxbcRegisterValue src = emitRegisterLoad(ins.src[0], ins.dst[0].mask);
    
    // We handle both packing and unpacking here
    const bool isPack = ins.op == DxbcOpcode::F32toF16;
    
    // The conversion instructions do not map very well to the
    // SPIR-V pack instructions, which operate on 2D vectors.
    std::array<uint32_t, 4> scalarIds  = {{ 0, 0, 0, 0 }};
    
    const uint32_t componentCount = src.type.ccount;
    
    // These types are used in both pack and unpack operations
    const uint32_t t_u32   = getVectorTypeId({ DxbcScalarType::Uint32,  1 });
    const uint32_t t_f32   = getVectorTypeId({ DxbcScalarType::Float32, 1 });
    const uint32_t t_f32v2 = getVectorTypeId({ DxbcScalarType::Float32, 2 });
    
    // Constant zero-bit pattern, used for packing
    const uint32_t zerof32 = isPack ? m_module.constf32(0.0f) : 0;
    
    for (uint32_t i = 0; i < componentCount; i++) {
      const DxbcRegisterValue componentValue
        = emitRegisterExtract(src, DxbcRegMask::select(i));
      
      if (isPack) {  // f32tof16
        const std::array<uint32_t, 2> packIds =
          {{ componentValue.id, zerof32 }};
        
        scalarIds[i] = m_module.opPackHalf2x16(t_u32,
          m_module.opCompositeConstruct(t_f32v2, packIds.size(), packIds.data()));
      } else {  // f16tof32
        const uint32_t zeroIndex = 0;
        
        scalarIds[i] = m_module.opCompositeExtract(t_f32,
          m_module.opUnpackHalf2x16(t_f32v2, componentValue.id),
          1, &zeroIndex);
      }
    }
    
    // Store result in the destination register
    DxbcRegisterValue result;
    result.type.ctype  = ins.dst[0].dataType;
    result.type.ccount = componentCount;
    result.id = componentCount > 1
      ? m_module.opCompositeConstruct(
          getVectorTypeId(result.type),
          componentCount, scalarIds.data())
      : scalarIds[0];
    emitRegisterStore(ins.dst[0], result);
  }


  void DxbcCompiler::emitConvertFloat64(const DxbcShaderInstruction& ins) {
    // ftod and dtof take the following operands:
    //  (dst0) Destination operand
    //  (src0) Number to convert
    uint32_t dstBits = ins.dst[0].mask.popCount();

    DxbcRegMask srcMask = isDoubleType(ins.dst[0].dataType)
      ? DxbcRegMask(dstBits >= 2, dstBits >= 4, false, false)
      : DxbcRegMask(dstBits >= 1, dstBits >= 1, dstBits >= 2, dstBits >= 2);

    // Perform actual conversion, destination modifiers are not applied
    DxbcRegisterValue val = emitRegisterLoad(ins.src[0], srcMask);

    DxbcRegisterValue result;
    result.type.ctype  = ins.dst[0].dataType;
    result.type.ccount = val.type.ccount;

    switch (ins.op) {
      case DxbcOpcode::DtoF:
      case DxbcOpcode::FtoD:
        result.id = m_module.opFConvert(
          getVectorTypeId(result.type), val.id);
        break;

      case DxbcOpcode::DtoI:
        result.id = m_module.opConvertFtoS(
          getVectorTypeId(result.type), val.id);
        break;

      case DxbcOpcode::DtoU:
        result.id = m_module.opConvertFtoU(
          getVectorTypeId(result.type), val.id);
        break;

      case DxbcOpcode::ItoD:
        result.id = m_module.opConvertStoF(
          getVectorTypeId(result.type), val.id);
        break;
      
      case DxbcOpcode::UtoD:
        result.id = m_module.opConvertUtoF(
          getVectorTypeId(result.type), val.id);
        break;
      
      default:
        Logger::warn(str::format("DxbcCompiler: Unhandled instruction: ", ins.op));
        return;
    }
    
    emitRegisterStore(ins.dst[0], result);
  }
  
  
  void DxbcCompiler::emitHullShaderInstCnt(const DxbcShaderInstruction& ins) {
    this->getCurrentHsForkJoinPhase()->instanceCount = ins.imm[0].u32;
  }
  
  
  void DxbcCompiler::emitHullShaderPhase(const DxbcShaderInstruction& ins) {
    switch (ins.op) {
      case DxbcOpcode::HsDecls: {
        if (m_hs.currPhaseType != DxbcCompilerHsPhase::None)
          Logger::err("DXBC: HsDecls not the first phase in hull shader");
        
        m_hs.currPhaseType = DxbcCompilerHsPhase::Decl;
      } break;
        
      case DxbcOpcode::HsControlPointPhase: {
        m_hs.cpPhase = this->emitNewHullShaderControlPointPhase();
        
        m_hs.currPhaseType = DxbcCompilerHsPhase::ControlPoint;
        m_hs.currPhaseId   = 0;
        
        m_module.setDebugName(m_hs.cpPhase.functionId, "hs_control_point");
      } break;
        
      case DxbcOpcode::HsForkPhase: {
        auto phase = this->emitNewHullShaderForkJoinPhase();
        m_hs.forkPhases.push_back(phase);
        
        m_hs.currPhaseType = DxbcCompilerHsPhase::Fork;
        m_hs.currPhaseId   = m_hs.forkPhases.size() - 1;
        
        m_module.setDebugName(phase.functionId,
          str::format("hs_fork_", m_hs.currPhaseId).c_str());
      } break;
        
      case DxbcOpcode::HsJoinPhase: {
        auto phase = this->emitNewHullShaderForkJoinPhase();
        m_hs.joinPhases.push_back(phase);
        
        m_hs.currPhaseType = DxbcCompilerHsPhase::Join;
        m_hs.currPhaseId   = m_hs.joinPhases.size() - 1;
        
        m_module.setDebugName(phase.functionId,
          str::format("hs_join_", m_hs.currPhaseId).c_str());
      } break;
        
      default:
        Logger::warn(str::format(
          "DxbcCompiler: Unhandled instruction: ",
          ins.op));
    }
  }
  
  
  void DxbcCompiler::emitInterpolate(const DxbcShaderInstruction& ins) {
    // The SPIR-V instructions operate on input variable pointers,
    // which are all declared as four-component float vectors.
    const uint32_t registerId = ins.src[0].idx[0].offset;
    
    DxbcRegisterValue result;
    result.type.ctype  = DxbcScalarType::Float32;
    result.type.ccount = 4;
    
    switch (ins.op) {
      case DxbcOpcode::EvalCentroid: {
        result.id = m_module.opInterpolateAtCentroid(
          getVectorTypeId(result.type),
          m_vRegs.at(registerId).id);
      } break;
      
      case DxbcOpcode::EvalSampleIndex: {
        const DxbcRegisterValue sampleIndex = emitRegisterLoad(
          ins.src[1], DxbcRegMask(true, false, false, false));
        
        result.id = m_module.opInterpolateAtSample(
          getVectorTypeId(result.type),
          m_vRegs.at(registerId).id,
          sampleIndex.id);
      } break;
      
      case DxbcOpcode::EvalSnapped: {
        const DxbcRegisterValue offset = emitRegisterLoad(
          ins.src[1], DxbcRegMask(true, true, false, false));
        
        result.id = m_module.opInterpolateAtOffset(
          getVectorTypeId(result.type),
          m_vRegs.at(registerId).id,
          offset.id);
      } break;
      
      default:
        Logger::warn(str::format(
          "DxbcCompiler: Unhandled instruction: ",
          ins.op));
        return;
    }
    
    result = emitRegisterSwizzle(result,
      ins.src[0].swizzle, ins.dst[0].mask);
    emitRegisterStore(ins.dst[0], result);
  }
  
  
  void DxbcCompiler::emitTextureQuery(const DxbcShaderInstruction& ins) {
    // resinfo has three operands:
    //    (dst0) The destination register
    //    (src0) Resource LOD to query
    //    (src1) Resource to query
    // TODO Check if resource is bound
    const DxbcBufferInfo resourceInfo = getBufferInfo(ins.src[1]);
    const DxbcResinfoType resinfoType = ins.controls.resinfoType();
    
    // Read the exact LOD for the image query
    const DxbcRegisterValue mipLod = emitRegisterLoad(
      ins.src[0], DxbcRegMask(true, false, false, false));
    
    const DxbcScalarType returnType = resinfoType == DxbcResinfoType::Uint
      ? DxbcScalarType::Uint32 : DxbcScalarType::Float32;
    
    // Query the size of the selected mip level, as well as the
    // total number of mip levels. We will have to combine the
    // result into a four-component vector later.
    DxbcRegisterValue imageSize   = emitQueryTextureSize(ins.src[1], mipLod);
    DxbcRegisterValue imageLevels = emitQueryTextureLods(ins.src[1]);
    
    // Convert intermediates to the requested type
    if (returnType == DxbcScalarType::Float32) {
      imageSize.type.ctype = DxbcScalarType::Float32;
      imageSize.id = m_module.opConvertUtoF(
        getVectorTypeId(imageSize.type),
        imageSize.id);
      
      imageLevels.type.ctype = DxbcScalarType::Float32;
      imageLevels.id = m_module.opConvertUtoF(
        getVectorTypeId(imageLevels.type),
        imageLevels.id);
    }
    
    // If the selected return type is rcpFloat, we need
    // to compute the reciprocal of the image dimensions,
    // but not the array size, so we need to separate it.
    const uint32_t imageCoordDim = imageSize.type.ccount;
    
    DxbcRegisterValue imageLayers;
    imageLayers.type = imageSize.type;
    imageLayers.id   = 0;
    
    if (resinfoType == DxbcResinfoType::RcpFloat && resourceInfo.image.array) {
      imageLayers = emitRegisterExtract(imageSize, DxbcRegMask::select(imageCoordDim - 1));
      imageSize   = emitRegisterExtract(imageSize, DxbcRegMask::firstN(imageCoordDim - 1));
    }
    
    if (resinfoType == DxbcResinfoType::RcpFloat) {
      imageSize.id = m_module.opFDiv(
        getVectorTypeId(imageSize.type),
        emitBuildConstVecf32(1.0f, 1.0f, 1.0f, 1.0f,
          DxbcRegMask::firstN(imageSize.type.ccount)).id,
        imageSize.id);
    }
    
    // Concatenate result vectors and scalars to form a
    // 4D vector. Unused components will be set to zero.
    std::array<uint32_t, 4> vectorIds = { imageSize.id, 0, 0, 0 };
    uint32_t numVectorIds = 1;
    
    if (imageLayers.id != 0)
      vectorIds[numVectorIds++] = imageLayers.id;
    
    if (imageCoordDim < 3) {
      const uint32_t zero = returnType == DxbcScalarType::Uint32
        ? m_module.constu32(0)
        : m_module.constf32(0.0f);
      
      for (uint32_t i = imageCoordDim; i < 3; i++)
        vectorIds[numVectorIds++] = zero;
    }
    
    vectorIds[numVectorIds++] = imageLevels.id;
    
    // Create the actual result vector
    DxbcRegisterValue result;
    result.type.ctype  = returnType;
    result.type.ccount = 4;
    result.id = m_module.opCompositeConstruct(
      getVectorTypeId(result.type),
      numVectorIds, vectorIds.data());
    
    // Swizzle components using the resource swizzle
    // and the destination operand's write mask
    result = emitRegisterSwizzle(result,
      ins.src[1].swizzle, ins.dst[0].mask);
    emitRegisterStore(ins.dst[0], result);
  }
  
  
  void DxbcCompiler::emitTextureQueryLod(const DxbcShaderInstruction& ins) {
    // All sample instructions have at least these operands:
    //    (dst0) The destination register
    //    (src0) Texture coordinates
    //    (src1) The texture itself
    //    (src2) The sampler object
    const DxbcRegister& texCoordReg = ins.src[0];
    const DxbcRegister& textureReg  = ins.src[1];
    const DxbcRegister& samplerReg  = ins.src[2];
    
    // Texture and sampler register IDs
    const uint32_t textureId = textureReg.idx[0].offset;
    const uint32_t samplerId = samplerReg.idx[0].offset;
    
    // Load texture coordinates
    const DxbcRegisterValue coord = emitRegisterLoad(texCoordReg,
      DxbcRegMask::firstN(getTexLayerDim(m_textures.at(textureId).imageInfo)));
    
    // Query the LOD. The result is a two-dimensional float32
    // vector containing the mip level and virtual LOD numbers.
    const uint32_t sampledImageId = emitLoadSampledImage(
      m_textures.at(textureId), m_samplers.at(samplerId), false);
    
    const uint32_t queriedLodId = m_module.opImageQueryLod(
      getVectorTypeId({ DxbcScalarType::Float32, 2 }),
      sampledImageId, coord.id);
    
    // Build the result array vector by filling up
    // the remaining two components with zeroes.
    const uint32_t zero = m_module.constf32(0.0f);
    const std::array<uint32_t, 3> resultIds
      = {{ queriedLodId, zero, zero }};
    
    DxbcRegisterValue result;
    result.type = DxbcVectorType { DxbcScalarType::Float32, 4 };
    result.id   = m_module.opCompositeConstruct(
      getVectorTypeId(result.type),
      resultIds.size(), resultIds.data());
    
    emitRegisterStore(ins.dst[0], result);
  }
  
  
  void DxbcCompiler::emitTextureQueryMs(const DxbcShaderInstruction& ins) {
    // sampleinfo has two operands:
    //    (dst0) The destination register
    //    (src0) Resource to query
    // TODO Check if resource is bound
    DxbcRegisterValue sampleCount = emitQueryTextureSamples(ins.src[0]);
    
    if (ins.controls.returnType() != DxbcInstructionReturnType::Uint) {
      sampleCount.type = { DxbcScalarType::Float32, 1 };
      sampleCount.id = m_module.opConvertUtoF(
        getVectorTypeId(sampleCount.type),
        sampleCount.id);
    }
    
    emitRegisterStore(ins.dst[0], sampleCount);
  }
  
  
  void DxbcCompiler::emitTextureQueryMsPos(const DxbcShaderInstruction& ins) {
    // samplepos has three operands:
    //    (dst0) The destination register
    //    (src0) Resource to query 
    //    (src1) Sample index
    // TODO Check if resource is bound
    if (m_samplePositions == 0)
      m_samplePositions = emitSamplePosArray();
    
    // The lookup index is qual to the sample count plus the
    // sample index, or 0 if the resource cannot be queried.
    DxbcRegisterValue sampleCount = emitQueryTextureSamples(ins.src[0]);
    DxbcRegisterValue sampleIndex = emitRegisterLoad(
      ins.src[1], DxbcRegMask(true, false, false, false));
    
    uint32_t lookupIndex = m_module.opIAdd(
      getVectorTypeId(sampleCount.type),
      sampleCount.id, sampleIndex.id);
    
    // Validate the parameters
    uint32_t sampleCountValid = m_module.opULessThanEqual(
      m_module.defBoolType(),
      sampleCount.id,
      m_module.constu32(16));
    
    uint32_t sampleIndexValid = m_module.opULessThan(
      m_module.defBoolType(),
      sampleIndex.id,
      sampleCount.id);
    
    // If the lookup cannot be performed, set the lookup
    // index to zero, which will return a zero vector.
    lookupIndex = m_module.opSelect(
      getVectorTypeId(sampleCount.type),
      m_module.opLogicalAnd(
        m_module.defBoolType(),
        sampleCountValid,
        sampleIndexValid),
      lookupIndex,
      m_module.constu32(0));
    
    // Load sample pos vector and write the masked
    // components to the destination register.
    DxbcRegisterPointer samplePos;
    samplePos.type.ctype  = DxbcScalarType::Float32;
    samplePos.type.ccount = 4;
    samplePos.id = m_module.opAccessChain(
      m_module.defPointerType(
        getVectorTypeId(samplePos.type),
        spv::StorageClassPrivate),
      m_samplePositions, 1, &lookupIndex);
    
    emitRegisterStore(ins.dst[0],
      emitRegisterSwizzle(
        emitValueLoad(samplePos),
        ins.src[0].swizzle,
        ins.dst[0].mask));
  }
  
  
  void DxbcCompiler::emitTextureFetch(const DxbcShaderInstruction& ins) {
    // ld has three operands:
    //    (dst0) The destination register
    //    (src0) Source address
    //    (src1) Source texture
    // ld2dms has four operands:
    //    (dst0) The destination register
    //    (src0) Source address
    //    (src1) Source texture
    //    (src2) Sample number
    const uint32_t textureId = ins.src[1].idx[0].offset;
    
    // Image type, which stores the image dimensions etc.
    const DxbcImageInfo imageType = m_textures.at(textureId).imageInfo;
    const uint32_t imageLayerDim  = getTexLayerDim(imageType);
    
    // Load the texture coordinates. The last component
    // contains the LOD if the resource is an image.
    const DxbcRegisterValue address = emitRegisterLoad(
      ins.src[0], DxbcRegMask(true, true, true, true));
    
    // Additional image operands. This will store
    // the LOD and the address offset if present.
    SpirvImageOperands imageOperands;
    
    if (ins.sampleControls.u != 0 || ins.sampleControls.v != 0 || ins.sampleControls.w != 0) {
      const std::array<uint32_t, 3> offsetIds = {
        imageLayerDim >= 1 ? m_module.consti32(ins.sampleControls.u) : 0,
        imageLayerDim >= 2 ? m_module.consti32(ins.sampleControls.v) : 0,
        imageLayerDim >= 3 ? m_module.consti32(ins.sampleControls.w) : 0,
      };
      
      imageOperands.flags |= spv::ImageOperandsConstOffsetMask;
      imageOperands.sConstOffset = m_module.constComposite(
        getVectorTypeId({ DxbcScalarType::Sint32, imageLayerDim }),
        imageLayerDim, offsetIds.data());
    }
    
    // The LOD is not present when reading from
    // a buffer or from a multisample texture.
    if (imageType.dim != spv::DimBuffer && imageType.ms == 0) {
      DxbcRegisterValue imageLod = emitRegisterExtract(
        address, DxbcRegMask(false, false, false, true));
      
      imageOperands.flags |= spv::ImageOperandsLodMask;
      imageOperands.sLod = imageLod.id;
    }
    
    // The ld2ms instruction has a sample index, but we
    // are only allowed to set it for multisample views
    if (ins.op == DxbcOpcode::LdMs && imageType.ms == 1) {
      DxbcRegisterValue sampleId = emitRegisterLoad(
        ins.src[2], DxbcRegMask(true, false, false, false));
      
      imageOperands.flags |= spv::ImageOperandsSampleMask;
      imageOperands.sSampleId = sampleId.id;
    }
    
    // Extract coordinates from address
    const DxbcRegisterValue coord = emitCalcTexCoord(address, imageType);
    
    // Fetch texels only if the resource is actually bound
    const uint32_t labelMerge     = m_module.allocateId();
    const uint32_t labelBound     = m_module.allocateId();
    const uint32_t labelUnbound   = m_module.allocateId();
    
    m_module.opSelectionMerge(labelMerge, spv::SelectionControlMaskNone);
    m_module.opBranchConditional(m_textures.at(textureId).specId, labelBound, labelUnbound);
    m_module.opLabel(labelBound);
    
    // Reading a typed image or buffer view
    // always returns a four-component vector.
    const uint32_t imageId = m_module.opLoad(
      m_textures.at(textureId).imageTypeId,
      m_textures.at(textureId).varId);
    
    DxbcRegisterValue result;
    result.type.ctype  = m_textures.at(textureId).sampledType;
    result.type.ccount = 4;
    result.id = m_module.opImageFetch(
      getVectorTypeId(result.type), imageId,
      coord.id, imageOperands);
    
    // Swizzle components using the texture swizzle
    // and the destination operand's write mask
    result = emitRegisterSwizzle(result,
      ins.src[1].swizzle, ins.dst[0].mask);
    
    // If the texture is not bound, return zeroes
    m_module.opBranch(labelMerge);
    m_module.opLabel(labelUnbound);
    
    DxbcRegisterValue zeroes = [&] {
      switch (result.type.ctype) {
        case DxbcScalarType::Float32: return emitBuildConstVecf32(0.0f, 0.0f, 0.0f, 0.0f, ins.dst[0].mask);
        case DxbcScalarType::Uint32:  return emitBuildConstVecu32(0u, 0u, 0u, 0u,         ins.dst[0].mask);
        case DxbcScalarType::Sint32:  return emitBuildConstVeci32(0, 0, 0, 0,             ins.dst[0].mask);
        default: throw DxvkError("DxbcCompiler: Invalid scalar type");
      }
    }();
    
    m_module.opBranch(labelMerge);
    m_module.opLabel(labelMerge);
    
    // Merge the result with a phi function
    const std::array<SpirvPhiLabel, 2> phiLabels = {{
      { result.id, labelBound   },
      { zeroes.id, labelUnbound },
    }};
    
    DxbcRegisterValue mergedResult;
    mergedResult.type = result.type;
    mergedResult.id = m_module.opPhi(
      getVectorTypeId(mergedResult.type),
      phiLabels.size(), phiLabels.data());
    
    emitRegisterStore(ins.dst[0], mergedResult);
  }
  
  
  void DxbcCompiler::emitTextureGather(const DxbcShaderInstruction& ins) {
    // Gather4 takes the following operands:
    //    (dst0) The destination register
    //    (src0) Texture coordinates
    //    (src1) The texture itself
    //    (src2) The sampler, with a component selector
    // Gather4C takes the following additional operand:
    //    (src3) The depth reference value
    // The Gather4Po variants take an additional operand
    // which defines an extended constant offset.
    // TODO reduce code duplication by moving some common code
    // in both sample() and gather() into separate methods
    const bool isExtendedGather = ins.op == DxbcOpcode::Gather4Po
                               || ins.op == DxbcOpcode::Gather4PoC;
    
    const DxbcRegister& texCoordReg = ins.src[0];
    const DxbcRegister& textureReg  = ins.src[1 + isExtendedGather];
    const DxbcRegister& samplerReg  = ins.src[2 + isExtendedGather];
    
    // Texture and sampler register IDs
    const uint32_t textureId = textureReg.idx[0].offset;
    const uint32_t samplerId = samplerReg.idx[0].offset;
    
    // Image type, which stores the image dimensions etc.
    const DxbcImageInfo imageType = m_textures.at(textureId).imageInfo;
    const uint32_t imageLayerDim = getTexLayerDim(imageType);
    
    // Load the texture coordinates. SPIR-V allows these
    // to be float4 even if not all components are used.
    DxbcRegisterValue coord = emitLoadTexCoord(texCoordReg, imageType);
    
    // Load reference value for depth-compare operations
    const bool isDepthCompare = ins.op == DxbcOpcode::Gather4C
                             || ins.op == DxbcOpcode::Gather4PoC;
    
    const DxbcRegisterValue referenceValue = isDepthCompare
      ? emitRegisterLoad(ins.src[3 + isExtendedGather],
          DxbcRegMask(true, false, false, false))
      : DxbcRegisterValue();
    
    // Determine the sampled image type based on the opcode.
    const uint32_t sampledImageType = isDepthCompare
      ? m_module.defSampledImageType(m_textures.at(textureId).depthTypeId)
      : m_module.defSampledImageType(m_textures.at(textureId).colorTypeId);
    
    // Accumulate additional image operands.
    SpirvImageOperands imageOperands;
    
    if (isExtendedGather) {
      m_module.enableCapability(spv::CapabilityImageGatherExtended);
      
      DxbcRegisterValue gatherOffset = emitRegisterLoad(
        ins.src[1], DxbcRegMask::firstN(imageLayerDim));
      
      imageOperands.flags |= spv::ImageOperandsOffsetMask;
      imageOperands.gOffset = gatherOffset.id;
    } else if (ins.sampleControls.u != 0 || ins.sampleControls.v != 0 || ins.sampleControls.w != 0) {
      const std::array<uint32_t, 3> offsetIds = {
        imageLayerDim >= 1 ? m_module.consti32(ins.sampleControls.u) : 0,
        imageLayerDim >= 2 ? m_module.consti32(ins.sampleControls.v) : 0,
        imageLayerDim >= 3 ? m_module.consti32(ins.sampleControls.w) : 0,
      };
      
      imageOperands.flags |= spv::ImageOperandsConstOffsetMask;
      imageOperands.sConstOffset = m_module.constComposite(
        getVectorTypeId({ DxbcScalarType::Sint32, imageLayerDim }),
        imageLayerDim, offsetIds.data());
    }
    
    // Combine the texture and the sampler into a sampled image
    const uint32_t sampledImageId = m_module.opSampledImage(
      sampledImageType,
      m_module.opLoad(
        m_textures.at(textureId).imageTypeId,
        m_textures.at(textureId).varId),
      m_module.opLoad(
        m_samplers.at(samplerId).typeId,
        m_samplers.at(samplerId).varId));
    
    // Gathering texels always returns a four-component
    // vector, even for the depth-compare variants.
    DxbcRegisterValue result;
    result.type.ctype  = m_textures.at(textureId).sampledType;
    result.type.ccount = 4;
    
    switch (ins.op) {
      // Simple image gather operation
      case DxbcOpcode::Gather4:
      case DxbcOpcode::Gather4Po: {
        result.id = m_module.opImageGather(
          getVectorTypeId(result.type), sampledImageId, coord.id,
          m_module.consti32(samplerReg.swizzle[0]),
          imageOperands);
      } break;
      
      // Depth-compare operation
      case DxbcOpcode::Gather4C:
      case DxbcOpcode::Gather4PoC: {
        result.id = m_module.opImageDrefGather(
          getVectorTypeId(result.type), sampledImageId, coord.id,
          referenceValue.id, imageOperands);
      } break;
      
      default:
        Logger::warn(str::format(
          "DxbcCompiler: Unhandled instruction: ",
          ins.op));
        return;
    }
    
    // Swizzle components using the texture swizzle
    // and the destination operand's write mask
    result = emitRegisterSwizzle(result,
      textureReg.swizzle, ins.dst[0].mask);
    
    emitRegisterStore(ins.dst[0], result);
  }
  
  
  void DxbcCompiler::emitTextureSample(const DxbcShaderInstruction& ins) {
    // All sample instructions have at least these operands:
    //    (dst0) The destination register
    //    (src0) Texture coordinates
    //    (src1) The texture itself
    //    (src2) The sampler object
    const DxbcRegister& texCoordReg = ins.src[0];
    const DxbcRegister& textureReg  = ins.src[1];
    const DxbcRegister& samplerReg  = ins.src[2];
    
    // Texture and sampler register IDs
    const uint32_t textureId = textureReg.idx[0].offset;
    const uint32_t samplerId = samplerReg.idx[0].offset;
    
    // Image type, which stores the image dimensions etc.
    const DxbcImageInfo imageType = m_textures.at(textureId).imageInfo;
    const uint32_t imageLayerDim  = getTexLayerDim(imageType);
    
    // Load the texture coordinates. SPIR-V allows these
    // to be float4 even if not all components are used.
    DxbcRegisterValue coord = emitLoadTexCoord(texCoordReg, imageType);
    
    // Load reference value for depth-compare operations
    const bool isDepthCompare = ins.op == DxbcOpcode::SampleC
                             || ins.op == DxbcOpcode::SampleClz;
    
    const DxbcRegisterValue referenceValue = isDepthCompare
      ? emitRegisterLoad(ins.src[3], DxbcRegMask(true, false, false, false))
      : DxbcRegisterValue();
    
    // Load explicit gradients for sample operations that require them
    const bool hasExplicitGradients = ins.op == DxbcOpcode::SampleD;
    
    const DxbcRegisterValue explicitGradientX = hasExplicitGradients
      ? emitRegisterLoad(ins.src[3], DxbcRegMask::firstN(imageLayerDim))
      : DxbcRegisterValue();
    
    const DxbcRegisterValue explicitGradientY = hasExplicitGradients
      ? emitRegisterLoad(ins.src[4], DxbcRegMask::firstN(imageLayerDim))
      : DxbcRegisterValue();
    
    // LOD for certain sample operations
    const bool hasLod = ins.op == DxbcOpcode::SampleL
                     || ins.op == DxbcOpcode::SampleB;
    
    const DxbcRegisterValue lod = hasLod
      ? emitRegisterLoad(ins.src[3], DxbcRegMask(true, false, false, false))
      : DxbcRegisterValue();
    
    // Accumulate additional image operands. These are
    // not part of the actual operand token in SPIR-V.
    SpirvImageOperands imageOperands;
    
    if (ins.sampleControls.u != 0 || ins.sampleControls.v != 0 || ins.sampleControls.w != 0) {
      const std::array<uint32_t, 3> offsetIds = {
        imageLayerDim >= 1 ? m_module.consti32(ins.sampleControls.u) : 0,
        imageLayerDim >= 2 ? m_module.consti32(ins.sampleControls.v) : 0,
        imageLayerDim >= 3 ? m_module.consti32(ins.sampleControls.w) : 0,
      };
      
      imageOperands.flags |= spv::ImageOperandsConstOffsetMask;
      imageOperands.sConstOffset = m_module.constComposite(
        getVectorTypeId({ DxbcScalarType::Sint32, imageLayerDim }),
        imageLayerDim, offsetIds.data());
    }
    
    // Combine the texture and the sampler into a sampled image
    const uint32_t sampledImageId = emitLoadSampledImage(
      m_textures.at(textureId), m_samplers.at(samplerId),
      isDepthCompare);
    
    // Sampling an image always returns a four-component
    // vector, whereas depth-compare ops return a scalar.
    DxbcRegisterValue result;
    result.type.ctype  = m_textures.at(textureId).sampledType;
    result.type.ccount = isDepthCompare ? 1 : 4;
    
    switch (ins.op) {
      // Simple image sample operation
      case DxbcOpcode::Sample: {
        result.id = m_module.opImageSampleImplicitLod(
          getVectorTypeId(result.type),
          sampledImageId, coord.id,
          imageOperands);
      } break;
      
      // Depth-compare operation
      case DxbcOpcode::SampleC: {
        result.id = m_module.opImageSampleDrefImplicitLod(
          getVectorTypeId(result.type), sampledImageId, coord.id,
          referenceValue.id, imageOperands);
      } break;
      
      // Depth-compare operation on mip level zero
      case DxbcOpcode::SampleClz: {
        imageOperands.flags |= spv::ImageOperandsLodMask;
        imageOperands.sLod = m_module.constf32(0.0f);
        
        result.id = m_module.opImageSampleDrefExplicitLod(
          getVectorTypeId(result.type), sampledImageId, coord.id,
          referenceValue.id, imageOperands);
      } break;
      
      // Sample operation with explicit gradients
      case DxbcOpcode::SampleD: {
        imageOperands.flags |= spv::ImageOperandsGradMask;
        imageOperands.sGradX = explicitGradientX.id;
        imageOperands.sGradY = explicitGradientY.id;
        
        result.id = m_module.opImageSampleExplicitLod(
          getVectorTypeId(result.type), sampledImageId, coord.id,
          imageOperands);
      } break;
      
      // Sample operation with explicit LOD
      case DxbcOpcode::SampleL: {
        imageOperands.flags |= spv::ImageOperandsLodMask;
        imageOperands.sLod = lod.id;
        
        result.id = m_module.opImageSampleExplicitLod(
          getVectorTypeId(result.type), sampledImageId, coord.id,
          imageOperands);
      } break;
      
      // Sample operation with LOD bias
      case DxbcOpcode::SampleB: {
        imageOperands.flags |= spv::ImageOperandsBiasMask;
        imageOperands.sLodBias = lod.id;
        
        result.id = m_module.opImageSampleImplicitLod(
          getVectorTypeId(result.type), sampledImageId, coord.id,
          imageOperands);
      } break;
      
      default:
        Logger::warn(str::format(
          "DxbcCompiler: Unhandled instruction: ",
          ins.op));
        return;
    }
    
    // Swizzle components using the texture swizzle
    // and the destination operand's write mask
    if (result.type.ccount != 1) {
      result = emitRegisterSwizzle(result,
        textureReg.swizzle, ins.dst[0].mask);
    }
    
    emitRegisterStore(ins.dst[0], result);
  }
  
  
  void DxbcCompiler::emitTypedUavLoad(const DxbcShaderInstruction& ins) {
    // load_uav_typed has three operands:
    //    (dst0) The destination register
    //    (src0) The texture or buffer coordinates
    //    (src1) The UAV to load from
    const uint32_t registerId = ins.src[1].idx[0].offset;
    const DxbcUav uavInfo = m_uavs.at(registerId);
    
    // Load texture coordinates
    DxbcRegisterValue texCoord = emitLoadTexCoord(
      ins.src[0], uavInfo.imageInfo);
    
    // Load source value from the UAV
    DxbcRegisterValue uavValue;
    uavValue.type.ctype  = uavInfo.sampledType;
    uavValue.type.ccount = 4;
    uavValue.id = m_module.opImageRead(
      getVectorTypeId(uavValue.type),
      m_module.opLoad(uavInfo.imageTypeId, uavInfo.varId),
      texCoord.id, SpirvImageOperands());
    
    // Apply component swizzle and mask
    uavValue = emitRegisterSwizzle(uavValue,
      ins.src[1].swizzle, ins.dst[0].mask);
    
    emitRegisterStore(ins.dst[0], uavValue);
  }
  
  
  void DxbcCompiler::emitTypedUavStore(const DxbcShaderInstruction& ins) {
    // store_uav_typed has three operands:
    //    (dst0) The destination UAV
    //    (src0) The texture or buffer coordinates
    //    (src1) The value to store
    const DxbcBufferInfo uavInfo = getBufferInfo(ins.dst[0]);
    
    // Execute write op only if the UAV is bound
    uint32_t writeTest = emitUavWriteTest(uavInfo);
    
    DxbcConditional cond;
    cond.labelIf  = m_module.allocateId();
    cond.labelEnd = m_module.allocateId();
    
    m_module.opSelectionMerge   (cond.labelEnd, spv::SelectionControlMaskNone);
    m_module.opBranchConditional(writeTest, cond.labelIf, cond.labelEnd);
    
    m_module.opLabel(cond.labelIf);
    
    // Load texture coordinates
    DxbcRegisterValue texCoord = emitLoadTexCoord(ins.src[0], uavInfo.image);
    
    // Load the value that will be written to the image. We'll
    // have to cast it to the component type of the image.
    const DxbcRegisterValue texValue = emitRegisterBitcast(
      emitRegisterLoad(ins.src[1], DxbcRegMask(true, true, true, true)),
      uavInfo.stype);
    
    // Write the given value to the image
    m_module.opImageWrite(
      m_module.opLoad(uavInfo.typeId, uavInfo.varId),
      texCoord.id, texValue.id, SpirvImageOperands());
    
    // End conditional block
    m_module.opBranch(cond.labelEnd);
    m_module.opLabel (cond.labelEnd);
  }
  
  
  void DxbcCompiler::emitControlFlowIf(const DxbcShaderInstruction& ins) {
    // Load the first component of the condition
    // operand and perform a zero test on it.
    const DxbcRegisterValue condition = emitRegisterLoad(
      ins.src[0], DxbcRegMask(true, false, false, false));
    
    // Declare the 'if' block. We do not know if there
    // will be an 'else' block or not, so we'll assume
    // that there is one and leave it empty otherwise.
    DxbcCfgBlock block;
    block.type = DxbcCfgBlockType::If;
    block.b_if.ztestId   = emitRegisterZeroTest(condition, ins.controls.zeroTest()).id;
    block.b_if.labelIf   = m_module.allocateId();
    block.b_if.labelElse = 0;
    block.b_if.labelEnd  = m_module.allocateId();
    block.b_if.headerPtr = m_module.getInsertionPtr();
    m_controlFlowBlocks.push_back(block);
    
    // We'll insert the branch instruction when closing
    // the block, since we don't know whether or not an
    // else block is needed right now.
    m_module.opLabel(block.b_if.labelIf);
  }
  
  
  void DxbcCompiler::emitControlFlowElse(const DxbcShaderInstruction& ins) {
    if (m_controlFlowBlocks.size() == 0
     || m_controlFlowBlocks.back().type != DxbcCfgBlockType::If
     || m_controlFlowBlocks.back().b_if.labelElse != 0)
      throw DxvkError("DxbcCompiler: 'Else' without 'If' found");
    
    // Set the 'Else' flag so that we do
    // not insert a dummy block on 'EndIf'
    DxbcCfgBlock& block = m_controlFlowBlocks.back();
    block.b_if.labelElse = m_module.allocateId();
    
    // Close the 'If' block by branching to
    // the merge block we declared earlier
    m_module.opBranch(block.b_if.labelEnd);
    m_module.opLabel (block.b_if.labelElse);
  }
  
  
  void DxbcCompiler::emitControlFlowEndIf(const DxbcShaderInstruction& ins) {
    if (m_controlFlowBlocks.size() == 0
     || m_controlFlowBlocks.back().type != DxbcCfgBlockType::If)
      throw DxvkError("DxbcCompiler: 'EndIf' without 'If' found");
    
    // Remove the block from the stack, it's closed
    DxbcCfgBlock block = m_controlFlowBlocks.back();
    m_controlFlowBlocks.pop_back();
    
    // Write out the 'if' header
    m_module.beginInsertion(block.b_if.headerPtr);
    
    m_module.opSelectionMerge(
      block.b_if.labelEnd,
      spv::SelectionControlMaskNone);
    
    m_module.opBranchConditional(
      block.b_if.ztestId,
      block.b_if.labelIf,
      block.b_if.labelElse != 0
        ? block.b_if.labelElse
        : block.b_if.labelEnd);
    
    m_module.endInsertion();
    
    // End the active 'if' or 'else' block
    m_module.opBranch(block.b_if.labelEnd);
    m_module.opLabel (block.b_if.labelEnd);
  }
  
  
  void DxbcCompiler::emitControlFlowSwitch(const DxbcShaderInstruction& ins) {
    // Load the selector as a scalar unsigned integer
    const DxbcRegisterValue selector = emitRegisterLoad(
      ins.src[0], DxbcRegMask(true, false, false, false));
    
    // Declare switch block. We cannot insert the switch
    // instruction itself yet because the number of case
    // statements and blocks is unknown at this point.
    DxbcCfgBlock block;
    block.type = DxbcCfgBlockType::Switch;
    block.b_switch.insertPtr    = m_module.getInsertionPtr();
    block.b_switch.selectorId   = selector.id;
    block.b_switch.labelBreak   = m_module.allocateId();
    block.b_switch.labelCase    = m_module.allocateId();
    block.b_switch.labelDefault = 0;
    block.b_switch.labelCases   = nullptr;
    m_controlFlowBlocks.push_back(block);
    
    // Define the first 'case' label
    m_module.opLabel(block.b_switch.labelCase);
  }
  
  
  void DxbcCompiler::emitControlFlowCase(const DxbcShaderInstruction& ins) {
    if (m_controlFlowBlocks.size() == 0
     || m_controlFlowBlocks.back().type != DxbcCfgBlockType::Switch)
      throw DxvkError("DxbcCompiler: 'Case' without 'Switch' found");
    
    // The source operand must be a 32-bit immediate.
    if (ins.src[0].type != DxbcOperandType::Imm32)
      throw DxvkError("DxbcCompiler: Invalid operand type for 'Case'");
    
    // Use the last label allocated for 'case'. The block starting
    // with that label is guaranteed to be empty unless a previous
    // 'case' block was not properly closed in the DXBC shader.
    DxbcCfgBlockSwitch* block = &m_controlFlowBlocks.back().b_switch;
    
    DxbcSwitchLabel label;
    label.desc.literal = ins.src[0].imm.u32_1;
    label.desc.labelId = block->labelCase;
    label.next         = block->labelCases;
    block->labelCases = new DxbcSwitchLabel(label);
  }
  
  
  void DxbcCompiler::emitControlFlowDefault(const DxbcShaderInstruction& ins) {
    if (m_controlFlowBlocks.size() == 0
     || m_controlFlowBlocks.back().type != DxbcCfgBlockType::Switch)
      throw DxvkError("DxbcCompiler: 'Default' without 'Switch' found");
    
    // Set the last label allocated for 'case' as the default label.
    m_controlFlowBlocks.back().b_switch.labelDefault
      = m_controlFlowBlocks.back().b_switch.labelCase;
  }
  
  
  void DxbcCompiler::emitControlFlowEndSwitch(const DxbcShaderInstruction& ins) {
    if (m_controlFlowBlocks.size() == 0
     || m_controlFlowBlocks.back().type != DxbcCfgBlockType::Switch)
      throw DxvkError("DxbcCompiler: 'EndSwitch' without 'Switch' found");
    
    // Remove the block from the stack, it's closed
    DxbcCfgBlock block = m_controlFlowBlocks.back();
    m_controlFlowBlocks.pop_back();
    
    // If no 'default' label was specified, use the last allocated
    // 'case' label. This is guaranteed to be an empty block unless
    // a previous 'case' block was not closed properly.
    if (block.b_switch.labelDefault == 0)
      block.b_switch.labelDefault = block.b_switch.labelCase;
    
    // Close the current 'case' block
    m_module.opBranch(block.b_switch.labelBreak);
    m_module.opLabel (block.b_switch.labelBreak);
    
    // Insert the 'switch' statement. For that, we need to
    // gather all the literal-label pairs for the construct.
    m_module.beginInsertion(block.b_switch.insertPtr);
    m_module.opSelectionMerge(
      block.b_switch.labelBreak,
      spv::SelectionControlMaskNone);
    
    // We'll restore the original order of the case labels here
    std::vector<SpirvSwitchCaseLabel> jumpTargets;
    for (auto i = block.b_switch.labelCases; i != nullptr; i = i->next)
      jumpTargets.insert(jumpTargets.begin(), i->desc);
    
    m_module.opSwitch(
      block.b_switch.selectorId,
      block.b_switch.labelDefault,
      jumpTargets.size(),
      jumpTargets.data());
    m_module.endInsertion();
    
    // Destroy the list of case labels
    // FIXME we're leaking memory if compilation fails.
    DxbcSwitchLabel* caseLabel = block.b_switch.labelCases;
    
    while (caseLabel != nullptr)
      delete std::exchange(caseLabel, caseLabel->next);
  }
  
    
  void DxbcCompiler::emitControlFlowLoop(const DxbcShaderInstruction& ins) {
    // Declare the 'loop' block
    DxbcCfgBlock block;
    block.type = DxbcCfgBlockType::Loop;
    block.b_loop.labelHeader   = m_module.allocateId();
    block.b_loop.labelBegin    = m_module.allocateId();
    block.b_loop.labelContinue = m_module.allocateId();
    block.b_loop.labelBreak    = m_module.allocateId();
    m_controlFlowBlocks.push_back(block);
    
    m_module.opBranch(block.b_loop.labelHeader);
    m_module.opLabel (block.b_loop.labelHeader);
    
    m_module.opLoopMerge(
      block.b_loop.labelBreak,
      block.b_loop.labelContinue,
      spv::LoopControlMaskNone);
    
    m_module.opBranch(block.b_loop.labelBegin);
    m_module.opLabel (block.b_loop.labelBegin);
  }
  
  
  void DxbcCompiler::emitControlFlowEndLoop(const DxbcShaderInstruction& ins) {
    if (m_controlFlowBlocks.size() == 0
     || m_controlFlowBlocks.back().type != DxbcCfgBlockType::Loop)
      throw DxvkError("DxbcCompiler: 'EndLoop' without 'Loop' found");
    
    // Remove the block from the stack, it's closed
    const DxbcCfgBlock block = m_controlFlowBlocks.back();
    m_controlFlowBlocks.pop_back();
    
    // Declare the continue block
    m_module.opBranch(block.b_loop.labelContinue);
    m_module.opLabel (block.b_loop.labelContinue);
    
    // Declare the merge block
    m_module.opBranch(block.b_loop.labelHeader);
    m_module.opLabel (block.b_loop.labelBreak);
  }
  
  
  void DxbcCompiler::emitControlFlowBreak(const DxbcShaderInstruction& ins) {
    const bool isBreak = ins.op == DxbcOpcode::Break;
    
    DxbcCfgBlock* cfgBlock = isBreak
      ? cfgFindBlock({ DxbcCfgBlockType::Loop, DxbcCfgBlockType::Switch })
      : cfgFindBlock({ DxbcCfgBlockType::Loop });
    
    if (cfgBlock == nullptr)
      throw DxvkError("DxbcCompiler: 'Break' or 'Continue' outside 'Loop' or 'Switch' found");
    
    if (cfgBlock->type == DxbcCfgBlockType::Loop) {
      m_module.opBranch(isBreak
        ? cfgBlock->b_loop.labelBreak
        : cfgBlock->b_loop.labelContinue);
    } else /* if (cfgBlock->type == DxbcCfgBlockType::Switch) */ {
      m_module.opBranch(cfgBlock->b_switch.labelBreak);
    }
    
    // Subsequent instructions assume that there is an open block
    const uint32_t labelId = m_module.allocateId();
    m_module.opLabel(labelId);
    
    // If this is on the same level as a switch-case construct,
    // rather than being nested inside an 'if' statement, close
    // the current 'case' block.
    if (m_controlFlowBlocks.back().type == DxbcCfgBlockType::Switch)
      cfgBlock->b_switch.labelCase = labelId;
  }
  
  
  void DxbcCompiler::emitControlFlowBreakc(const DxbcShaderInstruction& ins) {
    const bool isBreak = ins.op == DxbcOpcode::Breakc;
    
    DxbcCfgBlock* cfgBlock = isBreak
      ? cfgFindBlock({ DxbcCfgBlockType::Loop, DxbcCfgBlockType::Switch })
      : cfgFindBlock({ DxbcCfgBlockType::Loop });
    
    if (cfgBlock == nullptr)
      throw DxvkError("DxbcCompiler: 'Breakc' or 'Continuec' outside 'Loop' or 'Switch' found");
    
    // Perform zero test on the first component of the condition
    const DxbcRegisterValue condition = emitRegisterLoad(
      ins.src[0], DxbcRegMask(true, false, false, false));
    
    const DxbcRegisterValue zeroTest = emitRegisterZeroTest(
      condition, ins.controls.zeroTest());
    
    // We basically have to wrap this into an 'if' block
    const uint32_t breakBlock = m_module.allocateId();
    const uint32_t mergeBlock = m_module.allocateId();
    
    m_module.opSelectionMerge(mergeBlock,
      spv::SelectionControlMaskNone);
    
    m_module.opBranchConditional(
      zeroTest.id, breakBlock, mergeBlock);
    
    m_module.opLabel(breakBlock);
    
    if (cfgBlock->type == DxbcCfgBlockType::Loop) {
      m_module.opBranch(isBreak
        ? cfgBlock->b_loop.labelBreak
        : cfgBlock->b_loop.labelContinue);
    } else /* if (cfgBlock->type == DxbcCfgBlockType::Switch) */ {
      m_module.opBranch(cfgBlock->b_switch.labelBreak);
    }
    
    m_module.opLabel(mergeBlock);
  }
  
  
  void DxbcCompiler::emitControlFlowRet(const DxbcShaderInstruction& ins) {
    if (m_controlFlowBlocks.size() != 0) {
      uint32_t labelId = m_module.allocateId();
      
      m_module.opReturn();
      m_module.opLabel(labelId);

      // return can be used in place of break to terminate a case block
      if (m_controlFlowBlocks.back().type == DxbcCfgBlockType::Switch)
        m_controlFlowBlocks.back().b_switch.labelCase = labelId;
    } else {
      // Last instruction in the current function
      this->emitFunctionEnd();
    }
  }


  void DxbcCompiler::emitControlFlowRetc(const DxbcShaderInstruction& ins) {
    // Perform zero test on the first component of the condition
    const DxbcRegisterValue condition = emitRegisterLoad(
      ins.src[0], DxbcRegMask(true, false, false, false));
    
    const DxbcRegisterValue zeroTest = emitRegisterZeroTest(
      condition, ins.controls.zeroTest());
    
    // We basically have to wrap this into an 'if' block
    const uint32_t returnLabel = m_module.allocateId();
    const uint32_t continueLabel = m_module.allocateId();
    
    m_module.opSelectionMerge(continueLabel,
      spv::SelectionControlMaskNone);
    
    m_module.opBranchConditional(
      zeroTest.id, returnLabel, continueLabel);
    
    m_module.opLabel(returnLabel);
    m_module.opReturn();

    m_module.opLabel(continueLabel);
  }
  
  
  void DxbcCompiler::emitControlFlowDiscard(const DxbcShaderInstruction& ins) {
    // Discard actually has an operand that determines
    // whether or not the fragment should be discarded
    const DxbcRegisterValue condition = emitRegisterLoad(
      ins.src[0], DxbcRegMask(true, false, false, false));
    
    const DxbcRegisterValue zeroTest = emitRegisterZeroTest(
      condition, ins.controls.zeroTest());
    
    if (m_ps.killState == 0) {
      DxbcConditional cond;
      cond.labelIf  = m_module.allocateId();
      cond.labelEnd = m_module.allocateId();
      
      m_module.opSelectionMerge(cond.labelEnd, spv::SelectionControlMaskNone);
      m_module.opBranchConditional(zeroTest.id, cond.labelIf, cond.labelEnd);
      
      // OpKill terminates the block
      m_module.opLabel(cond.labelIf);
      m_module.opKill();
      
      m_module.opLabel(cond.labelEnd);
    } else {
      uint32_t typeId = m_module.defBoolType();
      
      uint32_t killState = m_module.opLoad     (typeId, m_ps.killState);
               killState = m_module.opLogicalOr(typeId, killState, zeroTest.id);
      m_module.opStore(m_ps.killState, killState);
    }
  }
  
  
  void DxbcCompiler::emitControlFlow(const DxbcShaderInstruction& ins) {
    switch (ins.op) {
      case DxbcOpcode::If:
        return this->emitControlFlowIf(ins);
        
      case DxbcOpcode::Else:
        return this->emitControlFlowElse(ins);
        
      case DxbcOpcode::EndIf:
        return this->emitControlFlowEndIf(ins);
        
      case DxbcOpcode::Switch:
        return this->emitControlFlowSwitch(ins);
        
      case DxbcOpcode::Case:
        return this->emitControlFlowCase(ins);
        
      case DxbcOpcode::Default:
        return this->emitControlFlowDefault(ins);
        
      case DxbcOpcode::EndSwitch:
        return this->emitControlFlowEndSwitch(ins);
        
      case DxbcOpcode::Loop:
        return this->emitControlFlowLoop(ins);
        
      case DxbcOpcode::EndLoop:
        return this->emitControlFlowEndLoop(ins);
        
      case DxbcOpcode::Break:
      case DxbcOpcode::Continue:
        return this->emitControlFlowBreak(ins);
        
      case DxbcOpcode::Breakc:
      case DxbcOpcode::Continuec:
        return this->emitControlFlowBreakc(ins);
        
      case DxbcOpcode::Ret:
        return this->emitControlFlowRet(ins);

      case DxbcOpcode::Retc:
        return this->emitControlFlowRetc(ins);
        
      case DxbcOpcode::Discard:
        return this->emitControlFlowDiscard(ins);
      
      default:
        Logger::warn(str::format(
          "DxbcCompiler: Unhandled instruction: ",
          ins.op));
    }
  }
  
  
  DxbcRegisterValue DxbcCompiler::emitBuildConstVecf32(
          float                   x,
          float                   y,
          float                   z,
          float                   w,
    const DxbcRegMask&            writeMask) {
    // TODO refactor these functions into one single template
    std::array<uint32_t, 4> ids = { 0, 0, 0, 0 };
    uint32_t componentIndex = 0;
    
    if (writeMask[0]) ids[componentIndex++] = m_module.constf32(x);
    if (writeMask[1]) ids[componentIndex++] = m_module.constf32(y);
    if (writeMask[2]) ids[componentIndex++] = m_module.constf32(z);
    if (writeMask[3]) ids[componentIndex++] = m_module.constf32(w);
    
    DxbcRegisterValue result;
    result.type.ctype  = DxbcScalarType::Float32;
    result.type.ccount = componentIndex;
    result.id = componentIndex > 1
      ? m_module.constComposite(
          getVectorTypeId(result.type),
          componentIndex, ids.data())
      : ids[0];
    return result;
  }
  
  
  DxbcRegisterValue DxbcCompiler::emitBuildConstVecu32(
          uint32_t                x,
          uint32_t                y,
          uint32_t                z,
          uint32_t                w,
    const DxbcRegMask&            writeMask) {
    std::array<uint32_t, 4> ids = { 0, 0, 0, 0 };
    uint32_t componentIndex = 0;
    
    if (writeMask[0]) ids[componentIndex++] = m_module.constu32(x);
    if (writeMask[1]) ids[componentIndex++] = m_module.constu32(y);
    if (writeMask[2]) ids[componentIndex++] = m_module.constu32(z);
    if (writeMask[3]) ids[componentIndex++] = m_module.constu32(w);
    
    DxbcRegisterValue result;
    result.type.ctype  = DxbcScalarType::Uint32;
    result.type.ccount = componentIndex;
    result.id = componentIndex > 1
      ? m_module.constComposite(
          getVectorTypeId(result.type),
          componentIndex, ids.data())
      : ids[0];
    return result;
  }
  
  
  DxbcRegisterValue DxbcCompiler::emitBuildConstVeci32(
          int32_t                 x,
          int32_t                 y,
          int32_t                 z,
          int32_t                 w,
    const DxbcRegMask&            writeMask) {
    std::array<uint32_t, 4> ids = { 0, 0, 0, 0 };
    uint32_t componentIndex = 0;
    
    if (writeMask[0]) ids[componentIndex++] = m_module.consti32(x);
    if (writeMask[1]) ids[componentIndex++] = m_module.consti32(y);
    if (writeMask[2]) ids[componentIndex++] = m_module.consti32(z);
    if (writeMask[3]) ids[componentIndex++] = m_module.consti32(w);
    
    DxbcRegisterValue result;
    result.type.ctype  = DxbcScalarType::Sint32;
    result.type.ccount = componentIndex;
    result.id = componentIndex > 1
      ? m_module.constComposite(
          getVectorTypeId(result.type),
          componentIndex, ids.data())
      : ids[0];
    return result;
  }
  
  
  DxbcRegisterValue DxbcCompiler::emitBuildConstVecf64(
          double                  xy,
          double                  zw,
    const DxbcRegMask&            writeMask) {
    std::array<uint32_t, 2> ids = { 0, 0 };
    uint32_t componentIndex = 0;
    
    if (writeMask[0] && writeMask[1]) ids[componentIndex++] = m_module.constf64(xy);
    if (writeMask[2] && writeMask[3]) ids[componentIndex++] = m_module.constf64(zw);
    
    DxbcRegisterValue result;
    result.type.ctype  = DxbcScalarType::Float64;
    result.type.ccount = componentIndex;
    result.id = componentIndex > 1
      ? m_module.constComposite(
          getVectorTypeId(result.type),
          componentIndex, ids.data())
      : ids[0];
    return result;
  }
  
  
  DxbcRegisterValue DxbcCompiler::emitRegisterBitcast(
          DxbcRegisterValue       srcValue,
          DxbcScalarType          dstType) {
    DxbcScalarType srcType = srcValue.type.ctype;

    if (srcType == dstType)
      return srcValue;
    
    DxbcRegisterValue result;
    result.type.ctype  = dstType;
    result.type.ccount = srcValue.type.ccount;

    if (isDoubleType(srcType)) result.type.ccount *= 2;
    if (isDoubleType(dstType)) result.type.ccount /= 2;

    result.id = m_module.opBitcast(
      getVectorTypeId(result.type),
      srcValue.id);
    return result;
  }
  
  
  DxbcRegisterValue DxbcCompiler::emitRegisterSwizzle(
          DxbcRegisterValue       value,
          DxbcRegSwizzle          swizzle,
          DxbcRegMask             writeMask) {
    if (value.type.ccount == 1)
      return emitRegisterExtend(value, writeMask.popCount());
    
    std::array<uint32_t, 4> indices;
    
    uint32_t dstIndex = 0;
    
    for (uint32_t i = 0; i < 4; i++) {
      if (writeMask[i])
        indices[dstIndex++] = swizzle[i];
    }
    
    // If the swizzle combined with the mask can be reduced
    // to a no-op, we don't need to insert any instructions.
    bool isIdentitySwizzle = dstIndex == value.type.ccount;
    
    for (uint32_t i = 0; i < dstIndex && isIdentitySwizzle; i++)
      isIdentitySwizzle &= indices[i] == i;
    
    if (isIdentitySwizzle)
      return value;
    
    // Use OpCompositeExtract if the resulting vector contains
    // only one component, and OpVectorShuffle if it is a vector.
    DxbcRegisterValue result;
    result.type.ctype  = value.type.ctype;
    result.type.ccount = dstIndex;
    
    const uint32_t typeId = getVectorTypeId(result.type);
    
    if (dstIndex == 1) {
      result.id = m_module.opCompositeExtract(
        typeId, value.id, 1, indices.data());
    } else {
      result.id = m_module.opVectorShuffle(
        typeId, value.id, value.id,
        dstIndex, indices.data());
    }
    
    return result;
  }
  
  
  DxbcRegisterValue DxbcCompiler::emitRegisterExtract(
          DxbcRegisterValue       value,
          DxbcRegMask             mask) {
    return emitRegisterSwizzle(value,
      DxbcRegSwizzle(0, 1, 2, 3), mask);
  }
  
  
  DxbcRegisterValue DxbcCompiler::emitRegisterInsert(
          DxbcRegisterValue       dstValue,
          DxbcRegisterValue       srcValue,
          DxbcRegMask             srcMask) {
    DxbcRegisterValue result;
    result.type = dstValue.type;
    
    const uint32_t typeId = getVectorTypeId(result.type);
    
    if (srcMask.popCount() == 0) {
      // Nothing to do if the insertion mask is empty
      result.id = dstValue.id;
    } else if (dstValue.type.ccount == 1) {
      // Both values are scalar, so the first component
      // of the write mask decides which one to take.
      result.id = srcMask[0] ? srcValue.id : dstValue.id;
    } else if (srcValue.type.ccount == 1) {
      // The source value is scalar. Since OpVectorShuffle
      // requires both arguments to be vectors, we have to
      // use OpCompositeInsert to modify the vector instead.
      const uint32_t componentId = srcMask.firstSet();
      
      result.id = m_module.opCompositeInsert(typeId,
        srcValue.id, dstValue.id, 1, &componentId);
    } else {
      // Both arguments are vectors. We can determine which
      // components to take from which vector and use the
      // OpVectorShuffle instruction.
      std::array<uint32_t, 4> components;
      uint32_t srcComponentId = dstValue.type.ccount;
      
      for (uint32_t i = 0; i < dstValue.type.ccount; i++)
        components.at(i) = srcMask[i] ? srcComponentId++ : i;
      
      result.id = m_module.opVectorShuffle(
        typeId, dstValue.id, srcValue.id,
        dstValue.type.ccount, components.data());
    }
    
    return result;
  }
  
  
  DxbcRegisterValue DxbcCompiler::emitRegisterConcat(
          DxbcRegisterValue       value1,
          DxbcRegisterValue       value2) {
    std::array<uint32_t, 2> ids =
      {{ value1.id, value2.id }};
    
    DxbcRegisterValue result;
    result.type.ctype  = value1.type.ctype;
    result.type.ccount = value1.type.ccount + value2.type.ccount;
    result.id = m_module.opCompositeConstruct(
      getVectorTypeId(result.type),
      ids.size(), ids.data());
    return result;
  }
  
  
  DxbcRegisterValue DxbcCompiler::emitRegisterExtend(
          DxbcRegisterValue       value,
          uint32_t                size) {
    if (size == 1)
      return value;
    
    std::array<uint32_t, 4> ids = {{
      value.id, value.id,
      value.id, value.id, 
    }};
    
    DxbcRegisterValue result;
    result.type.ctype  = value.type.ctype;
    result.type.ccount = size;
    result.id = m_module.opCompositeConstruct(
      getVectorTypeId(result.type),
      size, ids.data());
    return result;
  }
  
  
  DxbcRegisterValue DxbcCompiler::emitRegisterAbsolute(
          DxbcRegisterValue       value) {
    const uint32_t typeId = getVectorTypeId(value.type);
    
    switch (value.type.ctype) {
      case DxbcScalarType::Float32: value.id = m_module.opFAbs(typeId, value.id); break;
      case DxbcScalarType::Sint32:  value.id = m_module.opSAbs(typeId, value.id); break;
      default: Logger::warn("DxbcCompiler: Cannot get absolute value for given type");
    }
    
    return value;
  }
  
  
  DxbcRegisterValue DxbcCompiler::emitRegisterNegate(
          DxbcRegisterValue       value) {
    const uint32_t typeId = getVectorTypeId(value.type);
    
    switch (value.type.ctype) {
      case DxbcScalarType::Float32: value.id = m_module.opFNegate(typeId, value.id); break;
      case DxbcScalarType::Float64: value.id = m_module.opFNegate(typeId, value.id); break;
      case DxbcScalarType::Sint32:  value.id = m_module.opSNegate(typeId, value.id); break;
      case DxbcScalarType::Sint64:  value.id = m_module.opSNegate(typeId, value.id); break;
      default: Logger::warn("DxbcCompiler: Cannot negate given type");
    }
    
    return value;
  }
  
  
  DxbcRegisterValue DxbcCompiler::emitRegisterZeroTest(
          DxbcRegisterValue       value,
          DxbcZeroTest            test) {
    DxbcRegisterValue result;
    result.type.ctype  = DxbcScalarType::Bool;
    result.type.ccount = 1;
    
    const uint32_t zeroId = m_module.constu32(0u);
    const uint32_t typeId = getVectorTypeId(result.type);
    
    result.id = test == DxbcZeroTest::TestZ
      ? m_module.opIEqual   (typeId, value.id, zeroId)
      : m_module.opINotEqual(typeId, value.id, zeroId);
    return result;
  }
  
  
  DxbcRegisterValue DxbcCompiler::emitSrcOperandModifiers(
          DxbcRegisterValue       value,
          DxbcRegModifiers        modifiers) {
    if (modifiers.test(DxbcRegModifier::Abs))
      value = emitRegisterAbsolute(value);
    
    if (modifiers.test(DxbcRegModifier::Neg))
      value = emitRegisterNegate(value);
    return value;
  }
  
  
  DxbcRegisterValue DxbcCompiler::emitDstOperandModifiers(
          DxbcRegisterValue       value,
          DxbcOpModifiers         modifiers) {
    const uint32_t typeId = getVectorTypeId(value.type);
    
    if (value.type.ctype == DxbcScalarType::Float32) {
      // Saturating only makes sense on floats
      if (modifiers.saturate) {
        const DxbcRegMask       mask = DxbcRegMask::firstN(value.type.ccount);
        const DxbcRegisterValue vec0 = emitBuildConstVecf32(0.0f, 0.0f, 0.0f, 0.0f, mask);
        const DxbcRegisterValue vec1 = emitBuildConstVecf32(1.0f, 1.0f, 1.0f, 1.0f, mask);
        
        value.id = m_module.opNClamp(typeId, value.id, vec0.id, vec1.id);
      }
    }
    
    return value;
  }
  
  
  uint32_t DxbcCompiler::emitLoadSampledImage(
    const DxbcShaderResource&     textureResource,
    const DxbcSampler&            samplerResource,
          bool                    isDepthCompare) {
    const uint32_t sampledImageType = isDepthCompare
      ? m_module.defSampledImageType(textureResource.depthTypeId)
      : m_module.defSampledImageType(textureResource.colorTypeId);
    
    return m_module.opSampledImage(sampledImageType,
      m_module.opLoad(textureResource.imageTypeId, textureResource.varId),
      m_module.opLoad(samplerResource.typeId,      samplerResource.varId));
  }
  
  
  DxbcRegisterPointer DxbcCompiler::emitGetTempPtr(
    const DxbcRegister&           operand) {
    // r# regs are indexed as follows:
    //    (0) register index (immediate)
    DxbcRegisterPointer result;
    result.type.ctype  = DxbcScalarType::Float32;
    result.type.ccount = 4;
    result.id = m_rRegs.at(operand.idx[0].offset);
    return result;
  }
  
  
  DxbcRegisterPointer DxbcCompiler::emitGetIndexableTempPtr(
    const DxbcRegister&           operand) {
    // x# regs are indexed as follows:
    //    (0) register index (immediate)
    //    (1) element index (relative)
    const uint32_t regId = operand.idx[0].offset;
    
    const DxbcRegisterValue vectorId
      = emitIndexLoad(operand.idx[1]);
    
    DxbcRegisterInfo info;
    info.type.ctype   = DxbcScalarType::Float32;
    info.type.ccount  = m_xRegs[regId].ccount;
    info.type.alength = 0;
    info.sclass       = spv::StorageClassPrivate;
    
    DxbcRegisterPointer result;
    result.type.ctype  = info.type.ctype;
    result.type.ccount = info.type.ccount;
    result.id = m_module.opAccessChain(
      getPointerTypeId(info),
      m_xRegs.at(regId).varId,
      1, &vectorId.id);
    return result;
  }
  
  
  DxbcRegisterPointer DxbcCompiler::emitGetInputPtr(
    const DxbcRegister&           operand) {
    // In the vertex and pixel stages,
    // v# regs are indexed as follows:
    //    (0) register index (relative)
    // 
    // In the tessellation and geometry
    // stages, the index has two dimensions:
    //    (0) vertex index (relative)
    //    (1) register index (relative)
    DxbcRegisterPointer result;
    result.type.ctype  = DxbcScalarType::Float32;
    result.type.ccount = 4;
    
    std::array<uint32_t, 2> indices = {{ 0, 0 }};
    
    for (uint32_t i = 0; i < operand.idxDim; i++)
      indices.at(i) = emitIndexLoad(operand.idx[i]).id;
    
    // Pick the input array depending on
    // the program type and operand type
    struct InputArray {
      uint32_t          id;
      spv::StorageClass sclass;
    };
    
    const InputArray array = [&] () -> InputArray {
      switch (operand.type) {
        case DxbcOperandType::InputControlPoint:
          return m_version.type() == DxbcProgramType::HullShader
                  ? InputArray { m_vArray,             spv::StorageClassPrivate }
                  : InputArray { m_ds.inputPerVertex,  spv::StorageClassInput   };
        case DxbcOperandType::InputPatchConstant:
          return m_version.type() == DxbcProgramType::HullShader
                  ? InputArray { m_hs.outputPerPatch, spv::StorageClassOutput }
                  : InputArray { m_ds.inputPerPatch,  spv::StorageClassInput  };
        case DxbcOperandType::OutputControlPoint:
          return InputArray { m_hs.outputPerVertex, spv::StorageClassOutput };
        default:
          return { m_vArray, spv::StorageClassPrivate };
      }
    }();
      
    DxbcRegisterInfo info;
    info.type.ctype   = result.type.ctype;
    info.type.ccount  = result.type.ccount;
    info.type.alength = 0;
    info.sclass = array.sclass;
      
    result.id = m_module.opAccessChain(
      getPointerTypeId(info), array.id,
      operand.idxDim, indices.data());
    
    return result;
  }
  
  
  DxbcRegisterPointer DxbcCompiler::emitGetOutputPtr(
    const DxbcRegister&           operand) {
    if (m_version.type() == DxbcProgramType::HullShader) {
      // Hull shaders are special in that they have two sets of
      // output registers, one for per-patch values and one for
      // per-vertex values.
      DxbcRegisterPointer result;
      result.type.ctype  = DxbcScalarType::Float32;
      result.type.ccount = 4;
      
      uint32_t registerId = emitIndexLoad(operand.idx[0]).id;
      uint32_t ptrTypeId  = m_module.defPointerType(
          getVectorTypeId(result.type),
          spv::StorageClassOutput);
      
      if (m_hs.currPhaseType == DxbcCompilerHsPhase::ControlPoint) {
        std::array<uint32_t, 2> indices = {{
          m_module.opLoad(m_module.defIntType(32, 0), m_hs.builtinInvocationId),
          registerId,
        }};
        
        result.id = m_module.opAccessChain(
          ptrTypeId, m_hs.outputPerVertex,
          indices.size(), indices.data());
      } else {
        result.id = m_module.opAccessChain(
          ptrTypeId, m_hs.outputPerPatch,
          1, &registerId);
      }

      return result;
    } else {
      // Regular shaders have their output
      // registers set up at declaration time
      return m_oRegs.at(operand.idx[0].offset);
    }
  }
  
  
  DxbcRegisterPointer DxbcCompiler::emitGetConstBufPtr(
    const DxbcRegister&           operand) {
    // Constant buffers take a two-dimensional index:
    //    (0) register index (immediate)
    //    (1) constant offset (relative)
    DxbcRegisterInfo info;
    info.type.ctype   = DxbcScalarType::Float32;
    info.type.ccount  = 4;
    info.type.alength = 0;
    info.sclass = spv::StorageClassUniform;
    
    const uint32_t regId = operand.idx[0].offset;
    const DxbcRegisterValue constId = emitIndexLoad(operand.idx[1]);
    
    const uint32_t ptrTypeId = getPointerTypeId(info);
    
    const std::array<uint32_t, 2> indices =
      {{ m_module.consti32(0), constId.id }};
    
    DxbcRegisterPointer result;
    result.type.ctype  = info.type.ctype;
    result.type.ccount = info.type.ccount;
    result.id = m_module.opAccessChain(ptrTypeId,
      m_constantBuffers.at(regId).varId,
      indices.size(), indices.data());
    return result;
  }
  
  
  DxbcRegisterPointer DxbcCompiler::emitGetImmConstBufPtr(
    const DxbcRegister&           operand) {
    const DxbcRegisterValue constId
      = emitIndexLoad(operand.idx[0]);
    
    if (m_immConstBuf != 0) {
      DxbcRegisterInfo ptrInfo;
      ptrInfo.type.ctype   = DxbcScalarType::Uint32;
      ptrInfo.type.ccount  = 4;
      ptrInfo.type.alength = 0;
      ptrInfo.sclass = spv::StorageClassPrivate;

      DxbcRegisterPointer result;
      result.type.ctype  = ptrInfo.type.ctype;
      result.type.ccount = ptrInfo.type.ccount;
      result.id = m_module.opAccessChain(
        getPointerTypeId(ptrInfo),
        m_immConstBuf, 1, &constId.id);
      return result;
    } else if (m_constantBuffers.at(Icb_BindingSlotId).varId != 0) {
      const std::array<uint32_t, 2> indices =
        {{ m_module.consti32(0), constId.id }};
      
      DxbcRegisterInfo ptrInfo;
      ptrInfo.type.ctype   = DxbcScalarType::Float32;
      ptrInfo.type.ccount  = 4;
      ptrInfo.type.alength = 0;
      ptrInfo.sclass = spv::StorageClassUniform;

      DxbcRegisterPointer result;
      result.type.ctype  = ptrInfo.type.ctype;
      result.type.ccount = ptrInfo.type.ccount;
      result.id = m_module.opAccessChain(
        getPointerTypeId(ptrInfo),
        m_constantBuffers.at(Icb_BindingSlotId).varId,
        indices.size(), indices.data());
      return result;
    } else {
      throw DxvkError("DxbcCompiler: Immediate constant buffer not defined");
    }
  }
  
  
  DxbcRegisterPointer DxbcCompiler::emitGetOperandPtr(
    const DxbcRegister&           operand) {
    switch (operand.type) {
      case DxbcOperandType::Temp:
        return emitGetTempPtr(operand);
      
      case DxbcOperandType::IndexableTemp:
        return emitGetIndexableTempPtr(operand);
      
      case DxbcOperandType::Input:
      case DxbcOperandType::InputControlPoint:
      case DxbcOperandType::InputPatchConstant:
      case DxbcOperandType::OutputControlPoint:
        return emitGetInputPtr(operand);
      
      case DxbcOperandType::Output:
        return emitGetOutputPtr(operand);
      
      case DxbcOperandType::ConstantBuffer:
        return emitGetConstBufPtr(operand);
      
      case DxbcOperandType::ImmediateConstantBuffer:
        return emitGetImmConstBufPtr(operand);
      
      case DxbcOperandType::InputThreadId:
        return DxbcRegisterPointer {
          { DxbcScalarType::Uint32, 3 },
          m_cs.builtinGlobalInvocationId };
      
      case DxbcOperandType::InputThreadGroupId:
        return DxbcRegisterPointer {
          { DxbcScalarType::Uint32, 3 },
          m_cs.builtinWorkgroupId };
      
      case DxbcOperandType::InputThreadIdInGroup:
        return DxbcRegisterPointer {
          { DxbcScalarType::Uint32, 3 },
          m_cs.builtinLocalInvocationId };
      
      case DxbcOperandType::InputThreadIndexInGroup:
        return DxbcRegisterPointer {
          { DxbcScalarType::Uint32, 1 },
          m_cs.builtinLocalInvocationIndex };
      
      case DxbcOperandType::InputCoverageMask: {
        const std::array<uint32_t, 1> indices
          = {{ m_module.constu32(0) }};
        
        DxbcRegisterPointer result;
        result.type.ctype  = DxbcScalarType::Uint32;
        result.type.ccount = 1;
        result.id = m_module.opAccessChain(
          m_module.defPointerType(
            getVectorTypeId(result.type),
            spv::StorageClassInput),
          m_ps.builtinSampleMaskIn,
          indices.size(), indices.data());
        return result;
      }
        
      case DxbcOperandType::OutputCoverageMask: {
        const std::array<uint32_t, 1> indices
          = {{ m_module.constu32(0) }};
        
        DxbcRegisterPointer result;
        result.type.ctype  = DxbcScalarType::Uint32;
        result.type.ccount = 1;
        result.id = m_module.opAccessChain(
          m_module.defPointerType(
            getVectorTypeId(result.type),
            spv::StorageClassOutput),
          m_ps.builtinSampleMaskOut,
          indices.size(), indices.data());
        return result;
      }
        
      case DxbcOperandType::OutputDepth:
      case DxbcOperandType::OutputDepthGe:
      case DxbcOperandType::OutputDepthLe:
        return DxbcRegisterPointer {
          { DxbcScalarType::Float32, 1 },
          m_ps.builtinDepth };
      
      case DxbcOperandType::InputPrimitiveId:
        return DxbcRegisterPointer {
          { DxbcScalarType::Uint32, 1 },
          m_primitiveIdIn };
      
      case DxbcOperandType::InputDomainPoint:
        return DxbcRegisterPointer {
          { DxbcScalarType::Float32, 3 },
          m_ds.builtinTessCoord };
      
      case DxbcOperandType::OutputControlPointId:
        return DxbcRegisterPointer {
          { DxbcScalarType::Uint32, 1 },
          m_hs.builtinInvocationId };
      
      case DxbcOperandType::InputForkInstanceId:
      case DxbcOperandType::InputJoinInstanceId:
        return DxbcRegisterPointer {
          { DxbcScalarType::Uint32, 1 },
          getCurrentHsForkJoinPhase()->instanceIdPtr };
      
      case DxbcOperandType::InputGsInstanceId:
        return DxbcRegisterPointer {
          { DxbcScalarType::Uint32, 1 },
          m_gs.builtinInvocationId };
        
      default:
        throw DxvkError(str::format(
          "DxbcCompiler: Unhandled operand type: ",
          operand.type));
    }
  }
  
  
  DxbcRegisterPointer DxbcCompiler::emitGetAtomicPointer(
    const DxbcRegister&           operand,
    const DxbcRegister&           address) {
    // Query information about the resource itself
    const uint32_t registerId = operand.idx[0].offset;
    const DxbcBufferInfo resourceInfo = getBufferInfo(operand);
    
    // For UAVs and shared memory, different methods
    // of obtaining the final pointer are used.
    const bool isUav = operand.type == DxbcOperandType::UnorderedAccessView;
    
    // Compute the actual address into the resource
    const DxbcRegisterValue addressValue = [&] {
      switch (resourceInfo.type) {
        case DxbcResourceType::Raw:
          return emitCalcBufferIndexRaw(emitRegisterLoad(
            address, DxbcRegMask(true, false, false, false)));
          
        case DxbcResourceType::Structured: {
          const DxbcRegisterValue addressComponents = emitRegisterLoad(
            address, DxbcRegMask(true, true, false, false));
          
          return emitCalcBufferIndexStructured(
            emitRegisterExtract(addressComponents, DxbcRegMask(true, false, false, false)),
            emitRegisterExtract(addressComponents, DxbcRegMask(false, true, false, false)),
            resourceInfo.stride);
        };
        
        case DxbcResourceType::Typed: {
          if (!isUav)
            throw DxvkError("DxbcCompiler: TGSM cannot be typed");
          
          return emitLoadTexCoord(address,
            m_uavs.at(registerId).imageInfo);
        }
        
        default:
          throw DxvkError("DxbcCompiler: Unhandled resource type");
      }
    }();
    
    // Compute the actual pointer
    DxbcRegisterPointer result;
    result.type.ctype  = resourceInfo.stype;
    result.type.ccount = 1;
    result.id = isUav
      ? m_module.opImageTexelPointer(
          m_module.defPointerType(getVectorTypeId(result.type), spv::StorageClassImage),
          m_uavs.at(registerId).varId, addressValue.id, m_module.constu32(0))
      : m_module.opAccessChain(
          m_module.defPointerType(getVectorTypeId(result.type), spv::StorageClassWorkgroup),
          m_gRegs.at(registerId).varId, 1, &addressValue.id);
    return result;
  }
  
  
  DxbcRegisterValue DxbcCompiler::emitRawBufferLoad(
    const DxbcRegister&           operand,
          DxbcRegisterValue       elementIndex,
          DxbcRegMask             writeMask) {
    const DxbcBufferInfo bufferInfo = getBufferInfo(operand);
    
    // Shared memory is the only type of buffer that
    // is not accessed through a texel buffer view
    const bool isTgsm = operand.type == DxbcOperandType::ThreadGroupSharedMemory;
    
    const uint32_t bufferId = isTgsm
      ? 0 : m_module.opLoad(bufferInfo.typeId, bufferInfo.varId);
    
    // Since all data is represented as a sequence of 32-bit
    // integers, we have to load each component individually.
    std::array<uint32_t, 4> componentIds = { 0, 0, 0, 0 };
    std::array<uint32_t, 4> swizzleIds   = { 0, 0, 0, 0 };
    
    uint32_t componentIndex = 0;
    
    const uint32_t vectorTypeId = getVectorTypeId({ DxbcScalarType::Uint32, 4 });
    const uint32_t scalarTypeId = getVectorTypeId({ DxbcScalarType::Uint32, 1 });
    
    for (uint32_t i = 0; i < 4; i++) {
      // We'll apply both the write mask and the source operand swizzle
      // immediately. Unused components are not loaded, and the scalar
      // IDs are written to the array in the order they are requested.
      if (writeMask[i]) {
        const uint32_t swizzleIndex = operand.swizzle[i];
        
        if (componentIds[swizzleIndex] == 0) {
          // Add the component offset to the element index
          const uint32_t elementIndexAdjusted = m_module.opIAdd(
            getVectorTypeId(elementIndex.type), elementIndex.id,
            m_module.consti32(swizzleIndex));
          
          // Load requested component from the buffer
          componentIds[swizzleIndex] = [&] {
            const uint32_t zero = 0;
            
            switch (operand.type) {
              case DxbcOperandType::Resource:
                return m_module.opCompositeExtract(scalarTypeId,
                  m_module.opImageFetch(vectorTypeId,
                    bufferId, elementIndexAdjusted,
                    SpirvImageOperands()), 1, &zero);
              
              case DxbcOperandType::UnorderedAccessView:
                return m_module.opCompositeExtract(scalarTypeId,
                  m_module.opImageRead(vectorTypeId,
                    bufferId, elementIndexAdjusted,
                    SpirvImageOperands()), 1, &zero);
              
              case DxbcOperandType::ThreadGroupSharedMemory:
                return m_module.opLoad(scalarTypeId,
                  m_module.opAccessChain(bufferInfo.typeId,
                    bufferInfo.varId, 1, &elementIndexAdjusted));
                
              default:
                throw DxvkError("DxbcCompiler: Invalid operand type for strucured/raw load");
            }
          }();
        }
        
        // Append current component to the list of scalar IDs.
        // These will be used to construct the resulting vector.
        swizzleIds[componentIndex++] = componentIds[swizzleIndex];
      }
    }
    
    // Create result vector
    DxbcRegisterValue result;
    result.type.ctype  = DxbcScalarType::Uint32;
    result.type.ccount = writeMask.popCount();
    result.id = result.type.ccount > 1
      ? m_module.opCompositeConstruct(getVectorTypeId(result.type),
          result.type.ccount, swizzleIds.data())
      : swizzleIds[0];
    return result;
  }
  
  
  void DxbcCompiler::emitRawBufferStore(
    const DxbcRegister&           operand,
          DxbcRegisterValue       elementIndex,
          DxbcRegisterValue       value) {
    const DxbcBufferInfo bufferInfo = getBufferInfo(operand);
    
    // Cast source value to the expected data type
    value = emitRegisterBitcast(value, DxbcScalarType::Uint32);
    
    // Thread Group Shared Memory is not accessed through a texel buffer view
    const bool isUav = operand.type == DxbcOperandType::UnorderedAccessView;
    
    // Perform UAV writes only if the UAV is bound and if there
    // is nothing else preventing us from writing to it.
    DxbcConditional cond;
    
    if (isUav) {
      uint32_t writeTest = emitUavWriteTest(bufferInfo);
      
      cond.labelIf  = m_module.allocateId();
      cond.labelEnd = m_module.allocateId();
      
      m_module.opSelectionMerge(cond.labelEnd, spv::SelectionControlMaskNone);
      m_module.opBranchConditional(writeTest, cond.labelIf, cond.labelEnd);
      
      m_module.opLabel(cond.labelIf);
    }
    
    // Perform the actual write operation
    const uint32_t bufferId = isUav ? m_module.opLoad(bufferInfo.typeId, bufferInfo.varId) : 0;
    
    const uint32_t scalarTypeId = getVectorTypeId({ DxbcScalarType::Uint32, 1 });
    const uint32_t vectorTypeId = getVectorTypeId({ DxbcScalarType::Uint32, 4 });
    
    uint32_t srcComponentIndex = 0;
    
    for (uint32_t i = 0; i < 4; i++) {
      if (operand.mask[i]) {
        const uint32_t srcComponentId = value.type.ccount > 1
          ? m_module.opCompositeExtract(scalarTypeId,
              value.id, 1, &srcComponentIndex)
          : value.id;
        
        // Add the component offset to the element index
        const uint32_t elementIndexAdjusted = i != 0
          ? m_module.opIAdd(getVectorTypeId(elementIndex.type),
              elementIndex.id, m_module.consti32(i))
          : elementIndex.id;
        
        switch (operand.type) {
          case DxbcOperandType::UnorderedAccessView: {
            const std::array<uint32_t, 4> srcVectorIds = {
              srcComponentId, srcComponentId,
              srcComponentId, srcComponentId,
            };
        
            m_module.opImageWrite(
              bufferId, elementIndexAdjusted,
              m_module.opCompositeConstruct(vectorTypeId,
                4, srcVectorIds.data()),
              SpirvImageOperands());
          } break;
          
          case DxbcOperandType::ThreadGroupSharedMemory:
            m_module.opStore(
              m_module.opAccessChain(bufferInfo.typeId,
                bufferInfo.varId, 1, &elementIndexAdjusted),
              srcComponentId);
            break;
          
          default:
            throw DxvkError("DxbcCompiler: Invalid operand type for strucured/raw store");
        }
        
        // Write next component
        srcComponentIndex += 1;
      }
    }
    
    // End conditional block
    if (isUav) {
      m_module.opBranch(cond.labelEnd);
      m_module.opLabel (cond.labelEnd);
    }
  }
  
  
  DxbcRegisterValue DxbcCompiler::emitQueryTexelBufferSize(
    const DxbcRegister&           resource) {
    // Load the texel buffer object. This cannot be used with
    // constant buffers or any other type of resource.
    const DxbcBufferInfo bufferInfo = getBufferInfo(resource);
    
    const uint32_t bufferId = m_module.opLoad(
      bufferInfo.typeId, bufferInfo.varId);
    
    // We'll store this as a scalar unsigned integer
    DxbcRegisterValue result;
    result.type.ctype  = DxbcScalarType::Uint32;
    result.type.ccount = 1;
    result.id = m_module.opImageQuerySize(
      getVectorTypeId(result.type), bufferId);
    return result;
  }
  
  
  DxbcRegisterValue DxbcCompiler::emitQueryTextureLods(
    const DxbcRegister&           resource) {
    const DxbcBufferInfo info = getBufferInfo(resource);
    
    DxbcRegisterValue result;
    result.type.ctype  = DxbcScalarType::Uint32;
    result.type.ccount = 1;
    
    if (info.image.sampled == 1) {
      result.id = m_module.opImageQueryLevels(
        getVectorTypeId(result.type),
        m_module.opLoad(info.typeId, info.varId));
    } else {
      // Report one LOD in case of UAVs
      result.id = m_module.constu32(1);
    }
    return result;
  }
  
  
  DxbcRegisterValue DxbcCompiler::emitQueryTextureSamples(
    const DxbcRegister&           resource) {
    if (resource.type == DxbcOperandType::Rasterizer) {
      // SPIR-V has no gl_NumSamples equivalent, so we have
      // to work around it using a specialization constant
      return getSpecConstant(DxvkSpecConstantId::RasterizerSampleCount);
    } else {
      DxbcBufferInfo info = getBufferInfo(resource);
      
      DxbcRegisterValue result;
      result.type.ctype  = DxbcScalarType::Uint32;
      result.type.ccount = 1;
      result.id = m_module.opImageQuerySamples(
        getVectorTypeId(result.type),
        m_module.opLoad(info.typeId, info.varId));
      return result;
    }
  }
  
  
  DxbcRegisterValue DxbcCompiler::emitQueryTextureSize(
    const DxbcRegister&           resource,
          DxbcRegisterValue       lod) {
    const DxbcBufferInfo info = getBufferInfo(resource);
    
    DxbcRegisterValue result;
    result.type.ctype  = DxbcScalarType::Uint32;
    result.type.ccount = getTexSizeDim(info.image);
    
    if (info.image.ms == 0 && info.image.sampled == 1) {
      result.id = m_module.opImageQuerySizeLod(
        getVectorTypeId(result.type),
        m_module.opLoad(info.typeId, info.varId),
        lod.id);
    } else {
      result.id = m_module.opImageQuerySize(
        getVectorTypeId(result.type),
        m_module.opLoad(info.typeId, info.varId));
    }
    
    return result;
  }
  
  
  DxbcRegisterValue DxbcCompiler::emitCalcBufferIndexStructured(
          DxbcRegisterValue       structId,
          DxbcRegisterValue       structOffset,
          uint32_t                structStride) {
    DxbcRegisterValue result;
    result.type.ctype  = DxbcScalarType::Sint32;
    result.type.ccount = 1;
    
    const uint32_t typeId = getVectorTypeId(result.type);
    
    result.id = m_module.opIAdd(typeId,
      m_module.opIMul(typeId, structId.id, m_module.consti32(structStride / 4)),
      m_module.opSDiv(typeId, structOffset.id, m_module.consti32(4)));
    return result;
  }
  
  
  DxbcRegisterValue DxbcCompiler::emitCalcBufferIndexRaw(
          DxbcRegisterValue       byteOffset) {
    DxbcRegisterValue result;
    result.type.ctype  = DxbcScalarType::Sint32;
    result.type.ccount = 1;
    result.id = m_module.opSDiv(
      getVectorTypeId(result.type),
      byteOffset.id,
      m_module.consti32(4));
    return result;
  }
  
  
  DxbcRegisterValue DxbcCompiler::emitCalcTexCoord(
          DxbcRegisterValue       coordVector,
    const DxbcImageInfo&          imageInfo) {
    const uint32_t dim = getTexCoordDim(imageInfo);
    
    if (dim != coordVector.type.ccount) {
      coordVector = emitRegisterExtract(
        coordVector, DxbcRegMask::firstN(dim));      
    }
    
    return coordVector;
  }
  
  
  DxbcRegisterValue DxbcCompiler::emitLoadTexCoord(
    const DxbcRegister&           coordReg,
    const DxbcImageInfo&          imageInfo) {
    return emitCalcTexCoord(emitRegisterLoad(coordReg,
      DxbcRegMask(true, true, true, true)), imageInfo);
  }
  
  
  DxbcRegisterValue DxbcCompiler::emitIndexLoad(
          DxbcRegIndex            index) {
    if (index.relReg != nullptr) {
      DxbcRegisterValue result = emitRegisterLoad(
        *index.relReg, DxbcRegMask(true, false, false, false));
      
      if (index.offset != 0) {
        result.id = m_module.opIAdd(
          getVectorTypeId(result.type), result.id,
          m_module.consti32(index.offset));
      }
      
      return result;
    } else {
      DxbcRegisterValue result;
      result.type.ctype  = DxbcScalarType::Sint32;
      result.type.ccount = 1;
      result.id = m_module.consti32(index.offset);
      return result;
    }
  }
  
  
  DxbcRegisterValue DxbcCompiler::emitValueLoad(
          DxbcRegisterPointer     ptr) {
    DxbcRegisterValue result;
    result.type = ptr.type;
    result.id   = m_module.opLoad(
      getVectorTypeId(result.type),
      ptr.id);
    return result;
  }
  
  
  void DxbcCompiler::emitValueStore(
          DxbcRegisterPointer     ptr,
          DxbcRegisterValue       value,
          DxbcRegMask             writeMask) {
    // If the component types are not compatible,
    // we need to bit-cast the source variable.
    if (value.type.ctype != ptr.type.ctype)
      value = emitRegisterBitcast(value, ptr.type.ctype);
    
    // If the source value consists of only one component,
    // it is stored in all components of the destination.
    if (value.type.ccount == 1)
      value = emitRegisterExtend(value, writeMask.popCount());
    
    if (ptr.type.ccount == writeMask.popCount()) {
      // Simple case: We write to the entire register
      m_module.opStore(ptr.id, value.id);
    } else {
      // We only write to part of the destination
      // register, so we need to load and modify it
      DxbcRegisterValue tmp = emitValueLoad(ptr);
      tmp = emitRegisterInsert(tmp, value, writeMask);
      
      m_module.opStore(ptr.id, tmp.id);
    }
  }
  
  
  DxbcRegisterValue DxbcCompiler::emitRegisterLoadRaw(
    const DxbcRegister&           reg) {
    return emitValueLoad(emitGetOperandPtr(reg));
  }
  
  
  DxbcRegisterValue DxbcCompiler::emitRegisterLoad(
    const DxbcRegister&           reg,
          DxbcRegMask             writeMask) {
    if (reg.type == DxbcOperandType::Imm32
     || reg.type == DxbcOperandType::Imm64) {
      DxbcRegisterValue result;
      
      if (reg.componentCount == DxbcComponentCount::Component1) {
        // Create one single u32 constant
        result.type.ctype  = DxbcScalarType::Uint32;
        result.type.ccount = 1;
        result.id = m_module.constu32(reg.imm.u32_1);
      } else if (reg.componentCount == DxbcComponentCount::Component4) {
        // Create a u32 vector with as many components as needed
        std::array<uint32_t, 4> indices;
        uint32_t indexId = 0;
        
        for (uint32_t i = 0; i < indices.size(); i++) {
          if (writeMask[i]) {
            indices.at(indexId++) =
              m_module.constu32(reg.imm.u32_4[i]);
          }
        }
        
        result.type.ctype  = DxbcScalarType::Uint32;
        result.type.ccount = writeMask.popCount();
        result.id = indices.at(0);
        
        if (indexId > 1) {
          result.id = m_module.constComposite(
            getVectorTypeId(result.type),
            result.type.ccount, indices.data());
        }
        
      } else {
        // Something went horribly wrong in the decoder or the shader is broken
        throw DxvkError("DxbcCompiler: Invalid component count for immediate operand");
      }
      
      // Cast constants to the requested type
      return emitRegisterBitcast(result, reg.dataType);
    } else {
      // Load operand from the operand pointer
      DxbcRegisterValue result = emitRegisterLoadRaw(reg);
      
      // Apply operand swizzle to the operand value
      result = emitRegisterSwizzle(result, reg.swizzle, writeMask);
      
      // Cast it to the requested type. We need to do
      // this after the swizzling for 64-bit types.
      result = emitRegisterBitcast(result, reg.dataType);
      
      // Apply operand modifiers
      result = emitSrcOperandModifiers(result, reg.modifiers);
      return result;
    }
  }
  
  
  void DxbcCompiler::emitRegisterStore(
    const DxbcRegister&           reg,
          DxbcRegisterValue       value) {
    emitValueStore(emitGetOperandPtr(reg), value, reg.mask);
  }
  
  
  DxbcRegisterValue DxbcCompiler::getSpecConstant(DxvkSpecConstantId specId) {
    const uint32_t specIdOffset = uint32_t(specId) - uint32_t(DxvkSpecConstantId::SpecConstantIdMin);
    
    // Look up spec constant in the array
    DxbcRegisterValue value = m_specConstants.at(specIdOffset);
    
    if (value.id != 0)
      return value;
    
    // Declare a new specialization constant if needed
    DxbcSpecConstant info = getSpecConstantProperties(specId);
    
    value.type.ctype  = info.ctype;
    value.type.ccount = info.ccount;
    value.id = m_module.specConst32(
      getVectorTypeId(value.type),
      info.value);
    
    m_module.decorateSpecId(value.id, uint32_t(specId));
    m_module.setDebugName(value.id, info.name);
    
    m_specConstants.at(specIdOffset) = value;
    return value;
  }
  
  
  DxbcSpecConstant DxbcCompiler::getSpecConstantProperties(DxvkSpecConstantId specId) {
    static const std::array<DxbcSpecConstant,
      uint32_t(DxvkSpecConstantId::SpecConstantIdMax) -
      uint32_t(DxvkSpecConstantId::SpecConstantIdMin) + 1> s_specConstants = {{
        { DxbcScalarType::Uint32,   1,   1, "RasterizerSampleCount" },
    }};
    
    return s_specConstants.at(uint32_t(specId) - uint32_t(DxvkSpecConstantId::SpecConstantIdMin));
  }
  
  
  void DxbcCompiler::emitInputSetup() {
    // Copy all defined v# registers into the input array
    const uint32_t vecTypeId = m_module.defVectorType(m_module.defFloatType(32), 4);
    const uint32_t ptrTypeId = m_module.defPointerType(vecTypeId, spv::StorageClassPrivate);
    
    for (uint32_t i = 0; i < m_vRegs.size(); i++) {
      if (m_vRegs.at(i).id != 0) {
        const uint32_t registerId = m_module.consti32(i);

        DxbcRegisterPointer srcPtr = m_vRegs.at(i);
        DxbcRegisterValue srcValue = emitRegisterBitcast(
          emitValueLoad(srcPtr), DxbcScalarType::Float32);
        
        DxbcRegisterPointer dstPtr;
        dstPtr.type = { DxbcScalarType::Float32, 4 };
        dstPtr.id = m_module.opAccessChain(
          ptrTypeId, m_vArray, 1, &registerId);
        
        emitValueStore(dstPtr, srcValue, DxbcRegMask::firstN(srcValue.type.ccount));
      }
    }
    
    // Copy all system value registers into the array,
    // preserving any previously written contents.
    for (const DxbcSvMapping& map : m_vMappings) {
      const uint32_t registerId = m_module.consti32(map.regId);
      
      const DxbcRegisterValue value = [&] {
        switch (m_version.type()) {
          case DxbcProgramType::VertexShader:   return emitVsSystemValueLoad(map.sv, map.regMask);
          case DxbcProgramType::PixelShader:    return emitPsSystemValueLoad(map.sv, map.regMask);
          case DxbcProgramType::ComputeShader:  return emitCsSystemValueLoad(map.sv, map.regMask);
          default: throw DxvkError(str::format("DxbcCompiler: Unexpected stage: ", m_version.type()));
        }
      }();
      
      DxbcRegisterPointer inputReg;
      inputReg.type.ctype  = DxbcScalarType::Float32;
      inputReg.type.ccount = 4;
      inputReg.id = m_module.opAccessChain(
        ptrTypeId, m_vArray, 1, &registerId);
      emitValueStore(inputReg, value, map.regMask);
    }
  }
  
  
  void DxbcCompiler::emitInputSetup(uint32_t vertexCount) {
    // Copy all defined v# registers into the input array. Note
    // that the outer index of the array is the vertex index.
    const uint32_t vecTypeId    = m_module.defVectorType(m_module.defFloatType(32), 4);
    const uint32_t dstPtrTypeId = m_module.defPointerType(vecTypeId, spv::StorageClassPrivate);
    
    for (uint32_t i = 0; i < m_vRegs.size(); i++) {
      if (m_vRegs.at(i).id != 0) {
        const uint32_t registerId = m_module.consti32(i);
        
        for (uint32_t v = 0; v < vertexCount; v++) {
          std::array<uint32_t, 2> indices
            = {{ m_module.consti32(v), registerId }};
          
          DxbcRegisterPointer srcPtr;
          srcPtr.type = m_vRegs.at(i).type;
          srcPtr.id = m_module.opAccessChain(
            m_module.defPointerType(getVectorTypeId(srcPtr.type), spv::StorageClassInput),
            m_vRegs.at(i).id, 1, indices.data());
          
          DxbcRegisterValue srcValue = emitRegisterBitcast(
            emitValueLoad(srcPtr), DxbcScalarType::Float32);
          
          DxbcRegisterPointer dstPtr;
          dstPtr.type = { DxbcScalarType::Float32, 4 };
          dstPtr.id = m_module.opAccessChain(
            dstPtrTypeId, m_vArray, 2, indices.data());

          emitValueStore(dstPtr, srcValue, DxbcRegMask::firstN(srcValue.type.ccount));
        }
      }
    }
    
    // Copy all system value registers into the array,
    // preserving any previously written contents.
    for (const DxbcSvMapping& map : m_vMappings) {
      const uint32_t registerId = m_module.consti32(map.regId);
      
      for (uint32_t v = 0; v < vertexCount; v++) {
        const DxbcRegisterValue value = [&] {
          switch (m_version.type()) {
            case DxbcProgramType::GeometryShader: return emitGsSystemValueLoad(map.sv, map.regMask, v);
            default: throw DxvkError(str::format("DxbcCompiler: Unexpected stage: ", m_version.type()));
          }
        }();
        
        std::array<uint32_t, 2> indices = {
          m_module.consti32(v), registerId,
        };
        
        DxbcRegisterPointer inputReg;
        inputReg.type.ctype  = DxbcScalarType::Float32;
        inputReg.type.ccount = 4;
        inputReg.id = m_module.opAccessChain(dstPtrTypeId,
          m_vArray, indices.size(), indices.data());
        emitValueStore(inputReg, value, map.regMask);
      }
    }
  }
  
  
  void DxbcCompiler::emitOutputSetup() {
    for (const DxbcSvMapping& svMapping : m_oMappings) {
      DxbcRegisterPointer outputReg = m_oRegs.at(svMapping.regId);
      
      if (m_version.type() == DxbcProgramType::HullShader) {
        uint32_t registerIndex = m_module.constu32(svMapping.regId);
        
        outputReg.type = { DxbcScalarType::Float32, 4 };
        outputReg.id = m_module.opAccessChain(
          m_module.defPointerType(
            getVectorTypeId(outputReg.type),
            spv::StorageClassOutput),
          m_hs.outputPerPatch,
          1, &registerIndex);
      }
      
      auto sv    = svMapping.sv;
      auto mask  = svMapping.regMask;
      auto value = emitValueLoad(outputReg);
      
      switch (m_version.type()) {
        case DxbcProgramType::VertexShader:   emitVsSystemValueStore(sv, mask, value); break;
        case DxbcProgramType::GeometryShader: emitGsSystemValueStore(sv, mask, value); break;
        case DxbcProgramType::HullShader:     emitHsSystemValueStore(sv, mask, value); break;
        case DxbcProgramType::DomainShader:   emitDsSystemValueStore(sv, mask, value); break;
        case DxbcProgramType::PixelShader:    emitPsSystemValueStore(sv, mask, value); break;
        case DxbcProgramType::ComputeShader:  break;
      }
    }
  }


  void DxbcCompiler::emitOutputMapping() {
    // For pixel shaders, we need to swizzle the
    // output vectors using some spec constants.
    for (uint32_t i = 0; i < m_oRegs.size(); i++) {
      if (m_oRegs[i].id == 0 || m_oRegs[i].type.ccount < 2)
        continue;
      
      DxbcRegisterValue vector = emitValueLoad(m_oRegs[i]);

      uint32_t specTypeId = getScalarTypeId(DxbcScalarType::Uint32);
      uint32_t compTypeId = getScalarTypeId(vector.type.ctype);
      
      std::array<uint32_t, 4> scalars;

      for (uint32_t c = 0; c < vector.type.ccount; c++) {
        const char* components = "rgba";

        uint32_t specId = m_module.specConst32(specTypeId, c);
        m_module.decorateSpecId(specId, uint32_t(DxvkSpecConstantId::ColorComponentMappings) + 4 * i + c);
        m_module.setDebugName(specId, str::format("omap", i, ".", components[c]).c_str());

        scalars[c] = m_module.opVectorExtractDynamic(compTypeId, vector.id, specId);
      }

      vector.id = m_module.opCompositeConstruct(
        getVectorTypeId(vector.type),
        vector.type.ccount,
        scalars.data());
      
      emitValueStore(m_oRegs[i], vector,
        DxbcRegMask::firstN(vector.type.ccount));
    }
  }
  
  
  DxbcRegisterValue DxbcCompiler::emitVsSystemValueLoad(
          DxbcSystemValue         sv,
          DxbcRegMask             mask) {
    switch (sv) {
      case DxbcSystemValue::VertexId: {
        const uint32_t typeId = getScalarTypeId(DxbcScalarType::Uint32);
        
        if (m_vs.builtinVertexId == 0) {
          m_vs.builtinVertexId = emitNewBuiltinVariable({
            { DxbcScalarType::Uint32, 1, 0 },
            spv::StorageClassInput },
            spv::BuiltInVertexIndex,
            "vs_vertex_index");
        }
        
        if (m_vs.builtinBaseVertex == 0) {
          m_vs.builtinBaseVertex = emitNewBuiltinVariable({
            { DxbcScalarType::Uint32, 1, 0 },
            spv::StorageClassInput },
            spv::BuiltInBaseVertex,
            "vs_base_vertex");
        }
        
        DxbcRegisterValue result;
        result.type.ctype   = DxbcScalarType::Uint32;
        result.type.ccount  = 1;
        result.id = m_module.opISub(typeId,
          m_module.opLoad(typeId, m_vs.builtinVertexId),
          m_module.opLoad(typeId, m_vs.builtinBaseVertex));
        return result;
      } break;
      
      case DxbcSystemValue::InstanceId: {
        const uint32_t typeId = getScalarTypeId(DxbcScalarType::Uint32);
        
        if (m_vs.builtinInstanceId == 0) {
          m_vs.builtinInstanceId = emitNewBuiltinVariable({
            { DxbcScalarType::Uint32, 1, 0 },
            spv::StorageClassInput },
            spv::BuiltInInstanceIndex,
            "vs_instance_index");
        }
          
        if (m_vs.builtinBaseInstance == 0) {
          m_vs.builtinBaseInstance = emitNewBuiltinVariable({
            { DxbcScalarType::Uint32, 1, 0 },
            spv::StorageClassInput },
            spv::BuiltInBaseInstance,
            "vs_base_instance");
        }
        
        DxbcRegisterValue result;
        result.type.ctype   = DxbcScalarType::Uint32;
        result.type.ccount  = 1;
        result.id = m_module.opISub(typeId,
          m_module.opLoad(typeId, m_vs.builtinInstanceId),
          m_module.opLoad(typeId, m_vs.builtinBaseInstance));
        return result;
      } break;
      
      default:
        throw DxvkError(str::format(
          "DxbcCompiler: Unhandled VS SV input: ", sv));
    }
  }
  
  
  DxbcRegisterValue DxbcCompiler::emitGsSystemValueLoad(
          DxbcSystemValue         sv,
          DxbcRegMask             mask,
          uint32_t                vertexId) {
    switch (sv) {
      case DxbcSystemValue::Position: {
        const std::array<uint32_t, 2> indices = {
          m_module.consti32(vertexId),
          m_module.consti32(PerVertex_Position),
        };
        
        DxbcRegisterPointer ptrIn;
        ptrIn.type.ctype  = DxbcScalarType::Float32;
        ptrIn.type.ccount = 4;
        
        ptrIn.id = m_module.opAccessChain(
          m_module.defPointerType(
            getVectorTypeId(ptrIn.type),
            spv::StorageClassInput),
          m_perVertexIn,
          indices.size(),
          indices.data());
        
        return emitRegisterExtract(
          emitValueLoad(ptrIn), mask);
      } break;
      
      default:
        throw DxvkError(str::format(
          "DxbcCompiler: Unhandled GS SV input: ", sv));
    }
  }
  
  
  DxbcRegisterValue DxbcCompiler::emitPsSystemValueLoad(
          DxbcSystemValue         sv,
          DxbcRegMask             mask) {
    switch (sv) {
      case DxbcSystemValue::Position: {
        if (m_ps.builtinFragCoord == 0) {
          m_ps.builtinFragCoord = emitNewBuiltinVariable({
            { DxbcScalarType::Float32, 4, 0 },
            spv::StorageClassInput },
            spv::BuiltInFragCoord,
            "ps_frag_coord");
        }
        
        DxbcRegisterPointer ptrIn;
        ptrIn.type = { DxbcScalarType::Float32, 4 };
        ptrIn.id = m_ps.builtinFragCoord;
        
        // The X, Y and Z components of the SV_POSITION semantic
        // are identical to Vulkan's FragCoord builtin, but we
        // need to compute the reciprocal of the W component.
        DxbcRegisterValue fragCoord = emitValueLoad(ptrIn);
        
        uint32_t componentIndex = 3;
        uint32_t t_f32   = m_module.defFloatType(32);
        uint32_t v_wComp = m_module.opCompositeExtract(t_f32, fragCoord.id, 1, &componentIndex);
                 v_wComp = m_module.opFDiv(t_f32, m_module.constf32(1.0f), v_wComp);
        
        fragCoord.id = m_module.opCompositeInsert(
          getVectorTypeId(fragCoord.type),
          v_wComp, fragCoord.id,
          1, &componentIndex);
        
        return emitRegisterExtract(fragCoord, mask);
      } break;
      
      case DxbcSystemValue::IsFrontFace: {
        if (m_ps.builtinIsFrontFace == 0) {
          m_ps.builtinIsFrontFace = emitNewBuiltinVariable({
            { DxbcScalarType::Bool, 1, 0 },
            spv::StorageClassInput },
            spv::BuiltInFrontFacing,
            "ps_is_front_face");
        }
        
        DxbcRegisterValue result;
        result.type.ctype  = DxbcScalarType::Uint32;
        result.type.ccount = 1;
        result.id = m_module.opSelect(
          getVectorTypeId(result.type),
          m_module.opLoad(
            m_module.defBoolType(),
            m_ps.builtinIsFrontFace),
          m_module.constu32(0xFFFFFFFF),
          m_module.constu32(0x00000000));
        return result;
      } break;
      
      case DxbcSystemValue::PrimitiveId: {
        if (m_primitiveIdIn == 0) {
          m_module.enableCapability(spv::CapabilityGeometry);
          
          m_primitiveIdIn = emitNewBuiltinVariable({
            { DxbcScalarType::Uint32, 1, 0 },
            spv::StorageClassInput },
            spv::BuiltInPrimitiveId,
            "ps_primitive_id");
        }
        
        DxbcRegisterPointer ptrIn;
        ptrIn.type = { DxbcScalarType::Uint32, 1 };
        ptrIn.id   = m_primitiveIdIn;
        
        return emitValueLoad(ptrIn);
      } break;
      
      case DxbcSystemValue::SampleIndex: {
        if (m_ps.builtinSampleId == 0) {
          m_module.enableCapability(spv::CapabilitySampleRateShading);
          
          m_ps.builtinSampleId = emitNewBuiltinVariable({
            { DxbcScalarType::Uint32, 1, 0 },
            spv::StorageClassInput },
            spv::BuiltInSampleId,
            "ps_sample_id");
        }
        
        DxbcRegisterPointer ptrIn;
        ptrIn.type.ctype   = DxbcScalarType::Uint32;
        ptrIn.type.ccount  = 1;
        ptrIn.id = m_ps.builtinSampleId;
        
        return emitValueLoad(ptrIn);
      } break;
      
      case DxbcSystemValue::RenderTargetId: {
        if (m_ps.builtinLayer == 0) {
          m_module.enableCapability(spv::CapabilityGeometry);
          
          m_ps.builtinLayer = emitNewBuiltinVariable({
            { DxbcScalarType::Uint32, 1, 0 },
            spv::StorageClassInput },
            spv::BuiltInLayer,
            "v_layer");
        }
        
        DxbcRegisterPointer ptr;
        ptr.type.ctype   = DxbcScalarType::Uint32;
        ptr.type.ccount  = 1;
        ptr.id = m_ps.builtinLayer;
        
        return emitValueLoad(ptr);
      } break;
      
      case DxbcSystemValue::ViewportId: {
        if (m_ps.builtinViewportId == 0) {
          m_module.enableCapability(spv::CapabilityMultiViewport);
          
          m_ps.builtinViewportId = emitNewBuiltinVariable({
            { DxbcScalarType::Uint32, 1, 0 },
            spv::StorageClassInput },
            spv::BuiltInViewportIndex,
            "v_viewport");
        }
        
        DxbcRegisterPointer ptr;
        ptr.type.ctype   = DxbcScalarType::Uint32;
        ptr.type.ccount  = 1;
        ptr.id = m_ps.builtinViewportId;
        
        return emitValueLoad(ptr);
      } break;
      
      default:
        throw DxvkError(str::format(
          "DxbcCompiler: Unhandled PS SV input: ", sv));
    }
  }
  
  
  DxbcRegisterValue DxbcCompiler::emitCsSystemValueLoad(
          DxbcSystemValue         sv,
          DxbcRegMask             mask) {
    switch (sv) {
      default:
        throw DxvkError(str::format(
          "DxbcCompiler: Unhandled CS SV input: ", sv));
    }
  }
  
  
  void DxbcCompiler::emitVsSystemValueStore(
          DxbcSystemValue         sv,
          DxbcRegMask             mask,
    const DxbcRegisterValue&      value) {
    switch (sv) {
      case DxbcSystemValue::Position: {
        const uint32_t memberId = m_module.consti32(PerVertex_Position);
        
        DxbcRegisterPointer ptr;
        ptr.type.ctype  = DxbcScalarType::Float32;
        ptr.type.ccount = 4;
        
        ptr.id = m_module.opAccessChain(
          m_module.defPointerType(
            getVectorTypeId(ptr.type),
            spv::StorageClassOutput),
          m_perVertexOut, 1, &memberId);
        
        emitValueStore(ptr, value, mask);
      } break;
      
      case DxbcSystemValue::RenderTargetId: {
        if (m_version.type() != DxbcProgramType::GeometryShader)
          enableShaderViewportIndexLayer();

        if (m_gs.builtinLayer == 0) {
          m_module.enableCapability(spv::CapabilityGeometry);

          m_gs.builtinLayer = emitNewBuiltinVariable({
            { DxbcScalarType::Uint32, 1, 0 },
            spv::StorageClassOutput },
            spv::BuiltInLayer,
            "o_layer");
        }
        
        DxbcRegisterPointer ptr;
        ptr.type = { DxbcScalarType::Uint32, 1 };
        ptr.id   = m_gs.builtinLayer;
        
        emitValueStore(
          ptr, emitRegisterExtract(value, mask),
          DxbcRegMask(true, false, false, false));
      } break;
      
      case DxbcSystemValue::ViewportId: {
        if (m_version.type() != DxbcProgramType::GeometryShader)
          enableShaderViewportIndexLayer();

        if (m_gs.builtinViewportId == 0) {
          m_module.enableCapability(spv::CapabilityMultiViewport);
          
          m_gs.builtinViewportId = emitNewBuiltinVariable({
            { DxbcScalarType::Uint32, 1, 0 },
            spv::StorageClassOutput },
            spv::BuiltInViewportIndex,
            "o_viewport");
        }
        
        DxbcRegisterPointer ptr;
        ptr.type = { DxbcScalarType::Uint32, 1};
        ptr.id   = m_gs.builtinViewportId;
        
        emitValueStore(
          ptr, emitRegisterExtract(value, mask),
          DxbcRegMask(true, false, false, false));
      } break;
      
      default:
        Logger::warn(str::format(
          "DxbcCompiler: Unhandled VS SV output: ", sv));
    }
  }
  
  
  void DxbcCompiler::emitHsSystemValueStore(
          DxbcSystemValue         sv,
          DxbcRegMask             mask,
    const DxbcRegisterValue&      value) {
    if (sv >= DxbcSystemValue::FinalQuadUeq0EdgeTessFactor
     && sv <= DxbcSystemValue::FinalLineDensityTessFactor) {
      struct TessFactor {
        uint32_t array = 0;
        uint32_t index = 0;
      };
      
      static const std::array<TessFactor, 12> s_tessFactors = {{
        { m_hs.builtinTessLevelOuter, 0 },  // FinalQuadUeq0EdgeTessFactor
        { m_hs.builtinTessLevelOuter, 1 },  // FinalQuadVeq0EdgeTessFactor
        { m_hs.builtinTessLevelOuter, 2 },  // FinalQuadUeq1EdgeTessFactor
        { m_hs.builtinTessLevelOuter, 3 },  // FinalQuadVeq1EdgeTessFactor
        { m_hs.builtinTessLevelInner, 0 },  // FinalQuadUInsideTessFactor
        { m_hs.builtinTessLevelInner, 1 },  // FinalQuadVInsideTessFactor
        { m_hs.builtinTessLevelOuter, 0 },  // FinalTriUeq0EdgeTessFactor
        { m_hs.builtinTessLevelOuter, 1 },  // FinalTriVeq0EdgeTessFactor
        { m_hs.builtinTessLevelOuter, 2 },  // FinalTriWeq0EdgeTessFactor
        { m_hs.builtinTessLevelInner, 0 },  // FinalTriInsideTessFactor
        { m_hs.builtinTessLevelOuter, 0 },  // FinalLineDetailTessFactor
        { m_hs.builtinTessLevelOuter, 1 },  // FinalLineDensityTessFactor
      }};
      
      const TessFactor tessFactor = s_tessFactors.at(uint32_t(sv)
        - uint32_t(DxbcSystemValue::FinalQuadUeq0EdgeTessFactor));
      
      const uint32_t tessFactorArrayIndex
        = m_module.constu32(tessFactor.index);
      
      // Apply global tess factor limit
      DxbcRegisterValue tessValue = emitRegisterExtract(value, mask);
      tessValue.id = m_module.opFClamp(getVectorTypeId(tessValue.type),
        tessValue.id, m_module.constf32(0.0f),
        m_module.constf32(m_hs.maxTessFactor));
      
      DxbcRegisterPointer ptr;
      ptr.type.ctype  = DxbcScalarType::Float32;
      ptr.type.ccount = 1;
      ptr.id = m_module.opAccessChain(
        m_module.defPointerType(
          getVectorTypeId(ptr.type),
          spv::StorageClassOutput),
        tessFactor.array, 1,
        &tessFactorArrayIndex);
      
      emitValueStore(ptr, tessValue,
        DxbcRegMask(true, false, false, false));
    } else {
      Logger::warn(str::format(
        "DxbcCompiler: Unhandled HS SV output: ", sv));
    }
  }
  
  
  void DxbcCompiler::emitGsSystemValueStore(
          DxbcSystemValue         sv,
          DxbcRegMask             mask,
    const DxbcRegisterValue&      value) {
    switch (sv) {
      case DxbcSystemValue::Position:
      case DxbcSystemValue::CullDistance:
      case DxbcSystemValue::ClipDistance:
      case DxbcSystemValue::RenderTargetId:
      case DxbcSystemValue::ViewportId:
        emitVsSystemValueStore(sv, mask, value);
        break;
      
      case DxbcSystemValue::PrimitiveId: {
        if (m_primitiveIdOut == 0) {
          m_primitiveIdOut = emitNewBuiltinVariable({
            { DxbcScalarType::Uint32, 1, 0 },
            spv::StorageClassOutput },
            spv::BuiltInPrimitiveId,
            "gs_primitive_id");
        }
        
        DxbcRegisterPointer ptr;
        ptr.type = { DxbcScalarType::Uint32, 1};
        ptr.id   = m_primitiveIdOut;
        
        emitValueStore(
          ptr, emitRegisterExtract(value, mask),
          DxbcRegMask(true, false, false, false));
      } break;
      
      default:
        Logger::warn(str::format(
          "DxbcCompiler: Unhandled GS SV output: ", sv));
    }
  }
  
  
  void DxbcCompiler::emitPsSystemValueStore(
          DxbcSystemValue         sv,
          DxbcRegMask             mask,
    const DxbcRegisterValue&      value) {
    Logger::warn(str::format(
      "DxbcCompiler: Unhandled GS SV output: ", sv));
  }
  
  
  void DxbcCompiler::emitDsSystemValueStore(
          DxbcSystemValue         sv,
          DxbcRegMask             mask,
    const DxbcRegisterValue&      value) {
    switch (sv) {
      case DxbcSystemValue::Position:
      case DxbcSystemValue::CullDistance:
      case DxbcSystemValue::ClipDistance:
      case DxbcSystemValue::RenderTargetId:
      case DxbcSystemValue::ViewportId:
        emitVsSystemValueStore(sv, mask, value);
        break;
      
      default:
        Logger::warn(str::format(
          "DxbcCompiler: Unhandled DS SV output: ", sv));
    }
  }
  
  
  void DxbcCompiler::emitClipCullStore(
          DxbcSystemValue         sv,
          uint32_t                dstArray) {
    uint32_t offset = 0;
    
    if (dstArray == 0)
      return;
    
    for (auto e = m_osgn->begin(); e != m_osgn->end(); e++) {
      if (e->systemValue == sv) {
        DxbcRegisterPointer srcPtr = m_oRegs.at(e->registerId);
        DxbcRegisterValue srcValue = emitValueLoad(srcPtr);
        
        for (uint32_t i = 0; i < 4; i++) {
          if (e->componentMask[i]) {
            uint32_t offsetId = m_module.consti32(offset++);
            
            DxbcRegisterValue component = emitRegisterExtract(
              srcValue, DxbcRegMask::select(i));
            
            DxbcRegisterPointer dstPtr;
            dstPtr.type = { DxbcScalarType::Float32, 1 };
            dstPtr.id = m_module.opAccessChain(
              m_module.defPointerType(
                getVectorTypeId(dstPtr.type),
                spv::StorageClassOutput),
              dstArray, 1, &offsetId);
            
            emitValueStore(dstPtr, component,
              DxbcRegMask(true, false, false, false));
          }
        }
      }
    }
  }
  
  
  void DxbcCompiler::emitClipCullLoad(
          DxbcSystemValue         sv,
          uint32_t                srcArray) {
    uint32_t offset = 0;
    
    if (srcArray == 0)
      return;
    
    for (auto e = m_isgn->begin(); e != m_isgn->end(); e++) {
      if (e->systemValue == sv) {
        // Load individual components from the source array
        uint32_t                componentIndex = 0;
        std::array<uint32_t, 4> componentIds   = {{ 0, 0, 0, 0 }};
        
        for (uint32_t i = 0; i < 4; i++) {
          if (e->componentMask[i]) {
            uint32_t offsetId = m_module.consti32(offset++);
            
            DxbcRegisterPointer srcPtr;
            srcPtr.type = { DxbcScalarType::Float32, 1 };
            srcPtr.id = m_module.opAccessChain(
              m_module.defPointerType(
                getVectorTypeId(srcPtr.type),
                spv::StorageClassInput),
              srcArray, 1, &offsetId);
            
            componentIds[componentIndex++]
              = emitValueLoad(srcPtr).id;
          }
        }
        
        // Put everything into one vector
        DxbcRegisterValue dstValue;
        dstValue.type = { DxbcScalarType::Float32, componentIndex };
        dstValue.id = componentIds[0];
        
        if (componentIndex > 1) {
          dstValue.id = m_module.opCompositeConstruct(
            getVectorTypeId(dstValue.type),
            componentIndex, componentIds.data());
        }
        
        // Store vector to the input array
        uint32_t registerId = m_module.consti32(e->registerId);
        
        DxbcRegisterPointer dstInput;
        dstInput.type = { DxbcScalarType::Float32, 4 };
        dstInput.id = m_module.opAccessChain(
          m_module.defPointerType(
            getVectorTypeId(dstInput.type),
            spv::StorageClassPrivate),
          m_vArray, 1, &registerId);
        
        emitValueStore(dstInput, dstValue, e->componentMask);
      }
    }
  }
  
  
  uint32_t DxbcCompiler::emitUavWriteTest(const DxbcBufferInfo& uav) {
    uint32_t typeId = m_module.defBoolType();
    uint32_t testId = uav.specId;
    
    if (m_ps.killState != 0) {
      uint32_t killState = m_module.opLoad(typeId, m_ps.killState);
      
      testId = m_module.opLogicalAnd(typeId, testId,
        m_module.opLogicalNot(typeId, killState));
    }
    
    return testId;
  }
  
  
  void DxbcCompiler::emitInit() {
    // Set up common capabilities for all shaders
    m_module.enableCapability(spv::CapabilityShader);
    m_module.enableCapability(spv::CapabilityImageQuery);
    
    // Initialize the shader module with capabilities
    // etc. Each shader type has its own peculiarities.
    switch (m_version.type()) {
      case DxbcProgramType::VertexShader:   emitVsInit(); break;
      case DxbcProgramType::HullShader:     emitHsInit(); break;
      case DxbcProgramType::DomainShader:   emitDsInit(); break;
      case DxbcProgramType::GeometryShader: emitGsInit(); break;
      case DxbcProgramType::PixelShader:    emitPsInit(); break;
      case DxbcProgramType::ComputeShader:  emitCsInit(); break;
    }
  }
  
  
  void DxbcCompiler::emitFunctionBegin(
          uint32_t                entryPoint,
          uint32_t                returnType,
          uint32_t                funcType) {
    this->emitFunctionEnd();
      
    m_module.functionBegin(
      returnType, entryPoint, funcType,
      spv::FunctionControlMaskNone);
    
    m_insideFunction = true;
  }
  
  
  void DxbcCompiler::emitFunctionEnd() {
    if (m_insideFunction) {
      m_module.opReturn();
      m_module.functionEnd();
    }
    
    m_insideFunction = false;
  }
  
  
  void DxbcCompiler::emitFunctionLabel() {
    m_module.opLabel(m_module.allocateId());
  }
  
  
  void DxbcCompiler::emitMainFunctionBegin() {
    this->emitFunctionBegin(
      m_entryPointId,
      m_module.defVoidType(),
      m_module.defFunctionType(
        m_module.defVoidType(), 0, nullptr));
    this->emitFunctionLabel();
  }
  
  
  void DxbcCompiler::emitVsInit() {
    m_module.enableCapability(spv::CapabilityClipDistance);
    m_module.enableCapability(spv::CapabilityCullDistance);
    m_module.enableCapability(spv::CapabilityDrawParameters);
    
    m_module.enableExtension("SPV_KHR_shader_draw_parameters");
    
    // Declare the per-vertex output block. This is where
    // the vertex shader will write the vertex position.
    const uint32_t perVertexStruct = this->getPerVertexBlockId();
    const uint32_t perVertexPointer = m_module.defPointerType(
      perVertexStruct, spv::StorageClassOutput);
    
    m_perVertexOut = m_module.newVar(
      perVertexPointer, spv::StorageClassOutput);
    m_entryPointInterfaces.push_back(m_perVertexOut);
    m_module.setDebugName(m_perVertexOut, "vs_vertex_out");
    
    // Standard input array
    emitDclInputArray(0);
    
    // Cull/clip distances as outputs
    m_clipDistances = emitDclClipCullDistanceArray(
      m_analysis->clipCullOut.numClipPlanes,
      spv::BuiltInClipDistance,
      spv::StorageClassOutput);
    
    m_cullDistances = emitDclClipCullDistanceArray(
      m_analysis->clipCullOut.numCullPlanes,
      spv::BuiltInCullDistance,
      spv::StorageClassOutput);
    
    // Main function of the vertex shader
    m_vs.functionId = m_module.allocateId();
    m_module.setDebugName(m_vs.functionId, "vs_main");
    
    this->emitFunctionBegin(
      m_vs.functionId,
      m_module.defVoidType(),
      m_module.defFunctionType(
        m_module.defVoidType(), 0, nullptr));
    this->emitFunctionLabel();
  }
  
  
  void DxbcCompiler::emitHsInit() {
    m_module.enableCapability(spv::CapabilityTessellation);
    m_module.enableCapability(spv::CapabilityClipDistance);
    m_module.enableCapability(spv::CapabilityCullDistance);
    
    m_hs.builtinInvocationId = emitNewBuiltinVariable(
      DxbcRegisterInfo {
        { DxbcScalarType::Uint32, 1, 0 },
        spv::StorageClassInput },
      spv::BuiltInInvocationId,
      "vOutputControlPointId");
    
    m_hs.builtinTessLevelOuter = emitBuiltinTessLevelOuter(spv::StorageClassOutput);
    m_hs.builtinTessLevelInner = emitBuiltinTessLevelInner(spv::StorageClassOutput);
  }
  
  
  void DxbcCompiler::emitDsInit() {
    m_module.enableCapability(spv::CapabilityTessellation);
    m_module.enableCapability(spv::CapabilityClipDistance);
    m_module.enableCapability(spv::CapabilityCullDistance);
    
    m_ds.builtinTessLevelOuter = emitBuiltinTessLevelOuter(spv::StorageClassInput);
    m_ds.builtinTessLevelInner = emitBuiltinTessLevelInner(spv::StorageClassInput);
    
    // Declare the per-vertex output block
    const uint32_t perVertexStruct = this->getPerVertexBlockId();
    const uint32_t perVertexPointer = m_module.defPointerType(
      perVertexStruct, spv::StorageClassOutput);
    
    // Cull/clip distances as outputs
    m_clipDistances = emitDclClipCullDistanceArray(
      m_analysis->clipCullOut.numClipPlanes,
      spv::BuiltInClipDistance,
      spv::StorageClassOutput);
    
    m_cullDistances = emitDclClipCullDistanceArray(
      m_analysis->clipCullOut.numCullPlanes,
      spv::BuiltInCullDistance,
      spv::StorageClassOutput);
    
    m_perVertexOut = m_module.newVar(
      perVertexPointer, spv::StorageClassOutput);
    m_entryPointInterfaces.push_back(m_perVertexOut);
    m_module.setDebugName(m_perVertexOut, "ds_vertex_out");
    
    // Main function of the domain shader
    m_ds.functionId = m_module.allocateId();
    m_module.setDebugName(m_ds.functionId, "ds_main");
    
    this->emitFunctionBegin(
      m_ds.functionId,
      m_module.defVoidType(),
      m_module.defFunctionType(
        m_module.defVoidType(), 0, nullptr));
    this->emitFunctionLabel();
  }
  
  
  void DxbcCompiler::emitGsInit() {
    m_module.enableCapability(spv::CapabilityGeometry);
    m_module.enableCapability(spv::CapabilityClipDistance);
    m_module.enableCapability(spv::CapabilityCullDistance);
    
    // Declare the per-vertex output block. Outputs are not
    // declared as arrays, instead they will be flushed when
    // calling EmitVertex.
    const uint32_t perVertexStruct = this->getPerVertexBlockId();
    const uint32_t perVertexPointer = m_module.defPointerType(
      perVertexStruct, spv::StorageClassOutput);
    
    m_perVertexOut = m_module.newVar(
      perVertexPointer, spv::StorageClassOutput);
    m_entryPointInterfaces.push_back(m_perVertexOut);
    m_module.setDebugName(m_perVertexOut, "gs_vertex_out");
    
    // Cull/clip distances as outputs
    m_clipDistances = emitDclClipCullDistanceArray(
      m_analysis->clipCullOut.numClipPlanes,
      spv::BuiltInClipDistance,
      spv::StorageClassOutput);
    
    m_cullDistances = emitDclClipCullDistanceArray(
      m_analysis->clipCullOut.numCullPlanes,
      spv::BuiltInCullDistance,
      spv::StorageClassOutput);
    
    // Main function of the vertex shader
    m_gs.functionId = m_module.allocateId();
    m_module.setDebugName(m_gs.functionId, "gs_main");
    
    this->emitFunctionBegin(
      m_gs.functionId,
      m_module.defVoidType(),
      m_module.defFunctionType(
        m_module.defVoidType(), 0, nullptr));
    this->emitFunctionLabel();
  }
  
  
  void DxbcCompiler::emitPsInit() {
    m_module.enableCapability(spv::CapabilityDerivativeControl);
    m_module.enableCapability(spv::CapabilityInterpolationFunction);
    
    m_module.setExecutionMode(m_entryPointId,
      spv::ExecutionModeOriginUpperLeft);
    
    // Standard input array
    emitDclInputArray(0);
    
    // Cull/clip distances as inputs
    m_clipDistances = emitDclClipCullDistanceArray(
      m_analysis->clipCullIn.numClipPlanes,
      spv::BuiltInClipDistance,
      spv::StorageClassInput);
    
    m_cullDistances = emitDclClipCullDistanceArray(
      m_analysis->clipCullIn.numCullPlanes,
      spv::BuiltInCullDistance,
      spv::StorageClassInput);
    
    // We may have to defer kill operations to the end of
    // the shader in order to keep derivatives correct.
    if (m_analysis->usesKill && m_analysis->usesDerivatives && m_moduleInfo.options.test(DxbcOption::DeferKill)) {
      m_ps.killState = m_module.newVarInit(
        m_module.defPointerType(m_module.defBoolType(), spv::StorageClassPrivate),
        spv::StorageClassPrivate, m_module.constBool(false));
      
      m_module.setDebugName(m_ps.killState, "ps_kill");
    }
    
    // Main function of the pixel shader
    m_ps.functionId = m_module.allocateId();
    m_module.setDebugName(m_ps.functionId, "ps_main");
    
    this->emitFunctionBegin(
      m_ps.functionId,
      m_module.defVoidType(),
      m_module.defFunctionType(
        m_module.defVoidType(), 0, nullptr));
    this->emitFunctionLabel();
  }
  
  
  void DxbcCompiler::emitCsInit() {
    // Main function of the compute shader
    m_cs.functionId = m_module.allocateId();
    m_module.setDebugName(m_cs.functionId, "cs_main");
    
    this->emitFunctionBegin(
      m_cs.functionId,
      m_module.defVoidType(),
      m_module.defFunctionType(
        m_module.defVoidType(), 0, nullptr));
    this->emitFunctionLabel();
  }
  
  
  void DxbcCompiler::emitVsFinalize() {
    this->emitMainFunctionBegin();
    this->emitInputSetup();
    m_module.opFunctionCall(
      m_module.defVoidType(),
      m_vs.functionId, 0, nullptr);
    this->emitOutputSetup();
    this->emitClipCullStore(DxbcSystemValue::ClipDistance, m_clipDistances);
    this->emitClipCullStore(DxbcSystemValue::CullDistance, m_cullDistances);
    this->emitFunctionEnd();
  }
  
  
  void DxbcCompiler::emitHsFinalize() {
    if (m_hs.cpPhase.functionId == 0)
      m_hs.cpPhase = this->emitNewHullShaderPassthroughPhase();
    
    // Control point phase
    this->emitMainFunctionBegin();
    this->emitInputSetup(m_hs.vertexCountIn);
    this->emitHsControlPointPhase(m_hs.cpPhase);
    this->emitHsPhaseBarrier();
    
    // Fork-join phases and output setup
    this->emitHsInvocationBlockBegin(1);
    
    for (const auto& phase : m_hs.forkPhases)
      this->emitHsForkJoinPhase(phase);
    
    for (const auto& phase : m_hs.joinPhases)
      this->emitHsForkJoinPhase(phase);
    
    this->emitOutputSetup();
    this->emitHsInvocationBlockEnd();
    this->emitFunctionEnd();
  }
  
  
  void DxbcCompiler::emitDsFinalize() {
    this->emitMainFunctionBegin();
    m_module.opFunctionCall(
      m_module.defVoidType(),
      m_ds.functionId, 0, nullptr);
    this->emitOutputSetup();
    this->emitClipCullStore(DxbcSystemValue::ClipDistance, m_clipDistances);
    this->emitClipCullStore(DxbcSystemValue::CullDistance, m_cullDistances);
    this->emitFunctionEnd();
  }
  
  
  void DxbcCompiler::emitGsFinalize() {
    this->emitMainFunctionBegin();
    this->emitInputSetup(
      primitiveVertexCount(m_gs.inputPrimitive));
    m_module.opFunctionCall(
      m_module.defVoidType(),
      m_gs.functionId, 0, nullptr);
    // No output setup at this point as that was
    // already done during the EmitVertex step
    this->emitFunctionEnd();
  }
  
  
  void DxbcCompiler::emitPsFinalize() {
    this->emitMainFunctionBegin();
    this->emitInputSetup();
    this->emitClipCullLoad(DxbcSystemValue::ClipDistance, m_clipDistances);
    this->emitClipCullLoad(DxbcSystemValue::CullDistance, m_cullDistances);
    
    m_module.opFunctionCall(
      m_module.defVoidType(),
      m_ps.functionId, 0, nullptr);
    
    if (m_ps.killState != 0) {
      DxbcConditional cond;
      cond.labelIf  = m_module.allocateId();
      cond.labelEnd = m_module.allocateId();
      
      uint32_t killTest = m_module.opLoad(m_module.defBoolType(), m_ps.killState);
      
      m_module.opSelectionMerge(cond.labelEnd, spv::SelectionControlMaskNone);
      m_module.opBranchConditional(killTest, cond.labelIf, cond.labelEnd);
      
      m_module.opLabel(cond.labelIf);
      m_module.opKill();
      
      m_module.opLabel(cond.labelEnd);
    }
    
    this->emitOutputSetup();
    this->emitOutputMapping();
    this->emitFunctionEnd();
  }
  
  
  void DxbcCompiler::emitCsFinalize() {
    this->emitMainFunctionBegin();
    m_module.opFunctionCall(
      m_module.defVoidType(),
      m_cs.functionId, 0, nullptr);
    this->emitFunctionEnd();
  }
  
  
  void DxbcCompiler::emitHsControlPointPhase(
    const DxbcCompilerHsControlPointPhase&  phase) {
    m_module.opFunctionCall(
      m_module.defVoidType(),
      phase.functionId, 0, nullptr);
  }
  
  
  void DxbcCompiler::emitHsForkJoinPhase(
    const DxbcCompilerHsForkJoinPhase&      phase) {
    for (uint32_t i = 0; i < phase.instanceCount; i++) {
      uint32_t invocationId = m_module.constu32(i);
      
      m_module.opFunctionCall(
        m_module.defVoidType(),
        phase.functionId, 1,
        &invocationId);
    }
  }
  
  
  void DxbcCompiler::emitDclInputArray(uint32_t vertexCount) {
    DxbcArrayType info;
    info.ctype   = DxbcScalarType::Float32;
    info.ccount  = 4;
    info.alength = DxbcMaxInterfaceRegs;
    
    // Define the array type. This will be two-dimensional
    // in some shaders, with the outer index representing
    // the vertex ID within an invocation.
    uint32_t arrayTypeId = getArrayTypeId(info);
    
    if (vertexCount != 0) {
      arrayTypeId = m_module.defArrayType(
        arrayTypeId, m_module.constu32(vertexCount));
    }
    
    // Define the actual variable. Note that this is private
    // because we will copy input registers and some system
    // variables to the array during the setup phase.
    const uint32_t ptrTypeId = m_module.defPointerType(
      arrayTypeId, spv::StorageClassPrivate);
    
    const uint32_t varId = m_module.newVar(
      ptrTypeId, spv::StorageClassPrivate);
    
    m_module.setDebugName(varId, "shader_in");
    m_vArray = varId;
  }
  
  
  void DxbcCompiler::emitDclInputPerVertex(
          uint32_t          vertexCount,
    const char*             varName) {
    uint32_t typeId = getPerVertexBlockId();
    
    if (vertexCount != 0) {
      typeId = m_module.defArrayType(typeId,
        m_module.constu32(vertexCount));
    }
    
    const uint32_t ptrTypeId = m_module.defPointerType(
      typeId, spv::StorageClassInput);
    
    m_perVertexIn = m_module.newVar(
      ptrTypeId, spv::StorageClassInput);
    m_module.setDebugName(m_perVertexIn, varName);
    
    m_entryPointInterfaces.push_back(m_perVertexIn);
  }
  
  
  uint32_t DxbcCompiler::emitDclClipCullDistanceArray(
          uint32_t          length,
          spv::BuiltIn      builtIn,
          spv::StorageClass storageClass) {
    if (length == 0)
      return 0;
    
    uint32_t t_f32 = m_module.defFloatType(32);
    uint32_t t_arr = m_module.defArrayType(t_f32, m_module.constu32(length));
    uint32_t t_ptr = m_module.defPointerType(t_arr, storageClass);
    uint32_t varId = m_module.newVar(t_ptr, storageClass);
    
    m_module.decorateBuiltIn(varId, builtIn);
    m_module.setDebugName(varId,
      builtIn == spv::BuiltInClipDistance
        ? "clip_distances"
        : "cull_distances");
    
    m_entryPointInterfaces.push_back(varId);
    return varId;
  }
  
  
  DxbcCompilerHsControlPointPhase DxbcCompiler::emitNewHullShaderControlPointPhase() {
    uint32_t funTypeId = m_module.defFunctionType(
      m_module.defVoidType(), 0, nullptr);
    
    uint32_t funId = m_module.allocateId();
    
    this->emitFunctionBegin(funId,
      m_module.defVoidType(),
      funTypeId);
    this->emitFunctionLabel();
    
    DxbcCompilerHsControlPointPhase result;
    result.functionId = funId;
    return result;
  }
  
  
  DxbcCompilerHsControlPointPhase DxbcCompiler::emitNewHullShaderPassthroughPhase() {
    uint32_t funTypeId = m_module.defFunctionType(
      m_module.defVoidType(), 0, nullptr);
    
    // Begin passthrough function
    uint32_t funId = m_module.allocateId();
    m_module.setDebugName(funId, "hs_passthrough");
    
    this->emitFunctionBegin(funId,
      m_module.defVoidType(),
      funTypeId);
    this->emitFunctionLabel();
    
    // We'll basically copy each input variable to the corresponding
    // output, using the shader's invocation ID as the array index.
    const uint32_t invocationId = m_module.opLoad(
      getScalarTypeId(DxbcScalarType::Uint32),
      m_hs.builtinInvocationId);
    
    for (auto i = m_isgn->begin(); i != m_isgn->end(); i++) {
      this->emitDclInput(
        i->registerId, m_hs.vertexCountIn,
        i->componentMask,
        DxbcSystemValue::None,
        DxbcInterpolationMode::Undefined);
      
      // Vector type index
      const std::array<uint32_t, 2> dstIndices
        = {{ invocationId, m_module.constu32(i->registerId) }};
      
      DxbcRegisterPointer srcPtr;
      srcPtr.type = m_vRegs.at(i->registerId).type;
      srcPtr.id = m_module.opAccessChain(
        m_module.defPointerType(getVectorTypeId(srcPtr.type), spv::StorageClassInput),
        m_vRegs.at(i->registerId).id, 1, &invocationId);
      
      DxbcRegisterValue srcValue = emitRegisterBitcast(
        emitValueLoad(srcPtr), DxbcScalarType::Float32);

      DxbcRegisterPointer dstPtr;
      dstPtr.type = { DxbcScalarType::Float32, 4 };
      dstPtr.id = m_module.opAccessChain(
        m_module.defPointerType(getVectorTypeId(dstPtr.type), spv::StorageClassOutput),
        m_hs.outputPerVertex, dstIndices.size(), dstIndices.data());

      emitValueStore(dstPtr, srcValue, DxbcRegMask::firstN(srcValue.type.ccount));
    }
    
    // End function
    this->emitFunctionEnd();
    
    DxbcCompilerHsControlPointPhase result;
    result.functionId = funId;
    return result;
  }
  
  
  DxbcCompilerHsForkJoinPhase DxbcCompiler::emitNewHullShaderForkJoinPhase() {
    uint32_t argTypeId = m_module.defIntType(32, 0);
    uint32_t funTypeId = m_module.defFunctionType(
      m_module.defVoidType(), 1, &argTypeId);
    
    uint32_t funId = m_module.allocateId();
    
    this->emitFunctionBegin(funId,
      m_module.defVoidType(),
      funTypeId);
    
    uint32_t argId = m_module.functionParameter(argTypeId);
    this->emitFunctionLabel();
    
    DxbcCompilerHsForkJoinPhase result;
    result.functionId = funId;
    result.instanceId = argId;
    return result;
  }
  
  
  void DxbcCompiler::emitHsPhaseBarrier() {
    uint32_t exeScopeId = m_module.constu32(spv::ScopeWorkgroup);
    uint32_t memScopeId = m_module.constu32(spv::ScopeInvocation);
    uint32_t semanticId = m_module.constu32(spv::MemorySemanticsMaskNone);
    
    m_module.opControlBarrier(exeScopeId, memScopeId, semanticId);
  }
  
  
  void DxbcCompiler::emitHsInvocationBlockBegin(uint32_t count) {
    uint32_t invocationId = m_module.opLoad(
      getScalarTypeId(DxbcScalarType::Uint32),
      m_hs.builtinInvocationId);
    
    uint32_t condition = m_module.opULessThan(
      m_module.defBoolType(), invocationId,
      m_module.constu32(count));
    
    m_hs.invocationBlockBegin = m_module.allocateId();
    m_hs.invocationBlockEnd   = m_module.allocateId();
    
    m_module.opSelectionMerge(
      m_hs.invocationBlockEnd,
      spv::SelectionControlMaskNone);
    
    m_module.opBranchConditional(
      condition,
      m_hs.invocationBlockBegin,
      m_hs.invocationBlockEnd);
    
    m_module.opLabel(
      m_hs.invocationBlockBegin);
  }
  
  
  void DxbcCompiler::emitHsInvocationBlockEnd() {
    m_module.opBranch (m_hs.invocationBlockEnd);
    m_module.opLabel  (m_hs.invocationBlockEnd);
    
    m_hs.invocationBlockBegin = 0;
    m_hs.invocationBlockEnd   = 0;
  }
  
  
  uint32_t DxbcCompiler::emitTessInterfacePerPatch(spv::StorageClass storageClass) {
    const bool isInput = storageClass == spv::StorageClassInput;
    
    uint32_t vecType = m_module.defVectorType (m_module.defFloatType(32), 4);
    uint32_t arrType = m_module.defArrayType  (vecType, m_module.constu32(32));
    uint32_t ptrType = m_module.defPointerType(arrType, storageClass);
    uint32_t varId   = m_module.newVar        (ptrType, storageClass);
    
    m_module.setDebugName     (varId, isInput ? "vPatch" : "oPatch");
    m_module.decorate         (varId, spv::DecorationPatch);
    m_module.decorateLocation (varId, 0);
    
    m_entryPointInterfaces.push_back(varId);
    return varId;
  }
  
  
  uint32_t DxbcCompiler::emitTessInterfacePerVertex(spv::StorageClass storageClass, uint32_t vertexCount) {
    const bool isInput = storageClass == spv::StorageClassInput;
    
    uint32_t vecType      = m_module.defVectorType (m_module.defFloatType(32), 4);
    uint32_t arrTypeInner = m_module.defArrayType  (vecType,      m_module.constu32(32));
    uint32_t arrTypeOuter = m_module.defArrayType  (arrTypeInner, m_module.constu32(vertexCount));
    uint32_t ptrType      = m_module.defPointerType(arrTypeOuter, storageClass);
    uint32_t varId        = m_module.newVar        (ptrType,      storageClass);
    
    m_module.setDebugName     (varId, isInput ? "vVertex" : "oVertex");
    m_module.decorateLocation (varId, 0);
    
    m_entryPointInterfaces.push_back(varId);
    return varId;
  }
  
  
  uint32_t DxbcCompiler::emitSamplePosArray() {
    const std::array<uint32_t, 32> samplePosVectors = {{
      // Invalid sample count / unbound resource
      m_module.constvec4f32(0.0f, 0.0f, 0.0f, 0.0f),
      // VK_SAMPLE_COUNT_1_BIT
      m_module.constvec4f32(0.5f, 0.5f, 0.0f, 0.0f),
      // VK_SAMPLE_COUNT_2_BIT
      m_module.constvec4f32(0.75f, 0.75f, 0.0f, 0.0f),
      m_module.constvec4f32(0.25f, 0.25f, 0.0f, 0.0f),
      // VK_SAMPLE_COUNT_4_BIT
      m_module.constvec4f32(0.375f, 0.125f, 0.0f, 0.0f),
      m_module.constvec4f32(0.785f, 0.375f, 0.0f, 0.0f),
      m_module.constvec4f32(0.125f, 0.625f, 0.0f, 0.0f),
      m_module.constvec4f32(0.625f, 0.875f, 0.0f, 0.0f),
      // VK_SAMPLE_COUNT_8_BIT
      m_module.constvec4f32(0.5625f, 0.3125f, 0.0f, 0.0f),
      m_module.constvec4f32(0.4375f, 0.6875f, 0.0f, 0.0f),
      m_module.constvec4f32(0.8125f, 0.5625f, 0.0f, 0.0f),
      m_module.constvec4f32(0.3125f, 0.1875f, 0.0f, 0.0f),
      m_module.constvec4f32(0.1875f, 0.8125f, 0.0f, 0.0f),
      m_module.constvec4f32(0.0625f, 0.4375f, 0.0f, 0.0f),
      m_module.constvec4f32(0.6875f, 0.9375f, 0.0f, 0.0f),
      m_module.constvec4f32(0.9375f, 0.0625f, 0.0f, 0.0f),
      // VK_SAMPLE_COUNT_16_BIT
      m_module.constvec4f32(0.5625f, 0.5625f, 0.0f, 0.0f),
      m_module.constvec4f32(0.4375f, 0.3125f, 0.0f, 0.0f),
      m_module.constvec4f32(0.3125f, 0.6250f, 0.0f, 0.0f),
      m_module.constvec4f32(0.7500f, 0.4375f, 0.0f, 0.0f),
      m_module.constvec4f32(0.1875f, 0.3750f, 0.0f, 0.0f),
      m_module.constvec4f32(0.6250f, 0.8125f, 0.0f, 0.0f),
      m_module.constvec4f32(0.8125f, 0.6875f, 0.0f, 0.0f),
      m_module.constvec4f32(0.6875f, 0.1875f, 0.0f, 0.0f),
      m_module.constvec4f32(0.3750f, 0.8750f, 0.0f, 0.0f),
      m_module.constvec4f32(0.5000f, 0.0625f, 0.0f, 0.0f),
      m_module.constvec4f32(0.2500f, 0.1250f, 0.0f, 0.0f),
      m_module.constvec4f32(0.1250f, 0.7500f, 0.0f, 0.0f),
      m_module.constvec4f32(0.0000f, 0.5000f, 0.0f, 0.0f),
      m_module.constvec4f32(0.9375f, 0.2500f, 0.0f, 0.0f),
      m_module.constvec4f32(0.8750f, 0.9375f, 0.0f, 0.0f),
      m_module.constvec4f32(0.0625f, 0.0000f, 0.0f, 0.0f),
    }};
    
    uint32_t arrayTypeId = getArrayTypeId({
      DxbcScalarType::Float32, 4,
      static_cast<uint32_t>(samplePosVectors.size()) });
    
    uint32_t samplePosArray = m_module.constComposite(
      arrayTypeId,
      samplePosVectors.size(),
      samplePosVectors.data());
    
    uint32_t varId = m_module.newVarInit(
      m_module.defPointerType(arrayTypeId, spv::StorageClassPrivate),
      spv::StorageClassPrivate, samplePosArray);
    
    m_module.setDebugName(varId, "g_sample_pos");
    return varId;
  }
  
  
  uint32_t DxbcCompiler::emitNewVariable(const DxbcRegisterInfo& info) {
    const uint32_t ptrTypeId = this->getPointerTypeId(info);
    return m_module.newVar(ptrTypeId, info.sclass);
  }
  
  
  uint32_t DxbcCompiler::emitNewBuiltinVariable(
    const DxbcRegisterInfo& info,
          spv::BuiltIn      builtIn,
    const char*             name) {
    const uint32_t varId = emitNewVariable(info);
    
    m_module.decorateBuiltIn(varId, builtIn);
    m_module.setDebugName(varId, name);
    
    m_entryPointInterfaces.push_back(varId);
    return varId;
  }
  
  
  uint32_t DxbcCompiler::emitBuiltinTessLevelOuter(spv::StorageClass storageClass) {
    uint32_t id = emitNewBuiltinVariable(
      DxbcRegisterInfo {
        { DxbcScalarType::Float32, 0, 4 },
        storageClass },
      spv::BuiltInTessLevelOuter,
      "bTessLevelOuter");
    
    m_module.decorate(id, spv::DecorationPatch);
    return id;
  }
  
  
  uint32_t DxbcCompiler::emitBuiltinTessLevelInner(spv::StorageClass storageClass) {
    uint32_t id = emitNewBuiltinVariable(
      DxbcRegisterInfo {
        { DxbcScalarType::Float32, 0, 2 },
        storageClass },
      spv::BuiltInTessLevelInner,
      "bTessLevelInner");
    
    m_module.decorate(id, spv::DecorationPatch);
    return id;
  }
  
  
  void DxbcCompiler::enableShaderViewportIndexLayer() {
    if (!m_extensions.shaderViewportIndexLayer) {
      m_extensions.shaderViewportIndexLayer = true;
      
      m_module.enableExtension("SPV_EXT_shader_viewport_index_layer");
      m_module.enableCapability(spv::CapabilityShaderViewportIndexLayerEXT);
    }
  }


  DxbcCfgBlock* DxbcCompiler::cfgFindBlock(
    const std::initializer_list<DxbcCfgBlockType>& types) {
    for (auto cur =  m_controlFlowBlocks.rbegin();
              cur != m_controlFlowBlocks.rend(); cur++) {
      for (auto type : types) {
        if (cur->type == type)
          return &(*cur);
      }
    }
    
    return nullptr;
  }
  
  
  DxbcBufferInfo DxbcCompiler::getBufferInfo(const DxbcRegister& reg) {
    const uint32_t registerId = reg.idx[0].offset;
    
    switch (reg.type) {
      case DxbcOperandType::Resource: {
        DxbcBufferInfo result;
        result.image  = m_textures.at(registerId).imageInfo;
        result.stype  = m_textures.at(registerId).sampledType;
        result.type   = m_textures.at(registerId).type;
        result.typeId = m_textures.at(registerId).imageTypeId;
        result.varId  = m_textures.at(registerId).varId;
        result.specId = m_textures.at(registerId).specId;
        result.stride = m_textures.at(registerId).structStride;
        return result;
      } break;
        
      case DxbcOperandType::UnorderedAccessView: {
        DxbcBufferInfo result;
        result.image  = m_uavs.at(registerId).imageInfo;
        result.stype  = m_uavs.at(registerId).sampledType;
        result.type   = m_uavs.at(registerId).type;
        result.typeId = m_uavs.at(registerId).imageTypeId;
        result.varId  = m_uavs.at(registerId).varId;
        result.specId = m_uavs.at(registerId).specId;
        result.stride = m_uavs.at(registerId).structStride;
        return result;
      } break;
        
      case DxbcOperandType::ThreadGroupSharedMemory: {
        DxbcBufferInfo result;
        result.image  = { spv::DimBuffer, 0, 0, 0 };
        result.stype  = DxbcScalarType::Uint32;
        result.type   = m_gRegs.at(registerId).type;
        result.typeId = m_module.defPointerType(
          getScalarTypeId(DxbcScalarType::Uint32),
          spv::StorageClassWorkgroup);
        result.varId  = m_gRegs.at(registerId).varId;
        result.specId = 0;
        result.stride = m_gRegs.at(registerId).elementStride;
        return result;
      } break;
        
      default:
        throw DxvkError(str::format("DxbcCompiler: Invalid operand type for buffer: ", reg.type));
    }
  }
  
  
  uint32_t DxbcCompiler::getTexSizeDim(const DxbcImageInfo& imageType) const {
    switch (imageType.dim) {
      case spv::DimBuffer:  return 1 + imageType.array;
      case spv::Dim1D:      return 1 + imageType.array;
      case spv::Dim2D:      return 2 + imageType.array;
      case spv::Dim3D:      return 3 + imageType.array;
      case spv::DimCube:    return 2 + imageType.array;
      default: throw DxvkError("DxbcCompiler: getTexLayerDim: Unsupported image dimension");
    }
  }
  
  
  uint32_t DxbcCompiler::getTexLayerDim(const DxbcImageInfo& imageType) const {
    switch (imageType.dim) {
      case spv::DimBuffer:  return 1;
      case spv::Dim1D:      return 1;
      case spv::Dim2D:      return 2;
      case spv::Dim3D:      return 3;
      case spv::DimCube:    return 3;
      default: throw DxvkError("DxbcCompiler: getTexLayerDim: Unsupported image dimension");
    }
  }
  
  
  uint32_t DxbcCompiler::getTexCoordDim(const DxbcImageInfo& imageType) const {
    return getTexLayerDim(imageType) + imageType.array;
  }
  
  
  DxbcRegMask DxbcCompiler::getTexCoordMask(const DxbcImageInfo& imageType) const {
    return DxbcRegMask::firstN(getTexCoordDim(imageType));
  }
  
  
  DxbcVectorType DxbcCompiler::getInputRegType(uint32_t regIdx) const {
    switch (m_version.type()) {
      case DxbcProgramType::VertexShader: {
        const DxbcSgnEntry* entry = m_isgn->findByRegister(regIdx);
        
        DxbcVectorType result;
        result.ctype  = DxbcScalarType::Float32;
        result.ccount = 4;
        
        if (entry != nullptr) {
          result.ctype  = entry->componentType;
          result.ccount = entry->componentMask.popCount();
        }
        
        return result;
      }

      case DxbcProgramType::DomainShader: {
        DxbcVectorType result;
        result.ctype  = DxbcScalarType::Float32;
        result.ccount = 4;
        return result;
      }

      default: {
        DxbcVectorType result;
        result.ctype  = DxbcScalarType::Float32;
        result.ccount = m_isgn->regMask(regIdx).minComponents();
        return result;
      }
    }
  }
  
  
  DxbcVectorType DxbcCompiler::getOutputRegType(uint32_t regIdx) const {
    switch (m_version.type()) {
      case DxbcProgramType::PixelShader: {
        const DxbcSgnEntry* entry = m_osgn->findByRegister(regIdx);

        DxbcVectorType result;
        result.ctype  = DxbcScalarType::Float32;
        result.ccount = 4;
        
        if (entry != nullptr) {
          result.ctype  = entry->componentType;
          result.ccount = entry->componentMask.popCount();
        }

        return result;
      }

      case DxbcProgramType::HullShader: {
        DxbcVectorType result;
        result.ctype  = DxbcScalarType::Float32;
        result.ccount = 4;
        return result;
      }

      default: {
        DxbcVectorType result;
        result.ctype  = DxbcScalarType::Float32;
        result.ccount = m_osgn->regMask(regIdx).minComponents();
        return result;
      }
    }
  }
  
  
  DxbcImageInfo DxbcCompiler::getResourceType(
          DxbcResourceDim   resourceType,
          bool              isUav) const {
    DxbcImageInfo typeInfo = [resourceType, isUav] () -> DxbcImageInfo {
      switch (resourceType) {
        case DxbcResourceDim::Buffer:         return { spv::DimBuffer, 0, 0, isUav ? 2u : 1u, VK_IMAGE_VIEW_TYPE_MAX_ENUM   };
        case DxbcResourceDim::Texture1D:      return { spv::Dim1D,     0, 0, isUav ? 2u : 1u, VK_IMAGE_VIEW_TYPE_1D         };
        case DxbcResourceDim::Texture1DArr:   return { spv::Dim1D,     1, 0, isUav ? 2u : 1u, VK_IMAGE_VIEW_TYPE_1D_ARRAY   };
        case DxbcResourceDim::Texture2D:      return { spv::Dim2D,     0, 0, isUav ? 2u : 1u, VK_IMAGE_VIEW_TYPE_2D         };
        case DxbcResourceDim::Texture2DArr:   return { spv::Dim2D,     1, 0, isUav ? 2u : 1u, VK_IMAGE_VIEW_TYPE_2D_ARRAY   };
        case DxbcResourceDim::Texture2DMs:    return { spv::Dim2D,     0, 1, isUav ? 2u : 1u, VK_IMAGE_VIEW_TYPE_2D         };
        case DxbcResourceDim::Texture2DMsArr: return { spv::Dim2D,     1, 1, isUav ? 2u : 1u, VK_IMAGE_VIEW_TYPE_2D_ARRAY   };
        case DxbcResourceDim::Texture3D:      return { spv::Dim3D,     0, 0, isUav ? 2u : 1u, VK_IMAGE_VIEW_TYPE_3D         };
        case DxbcResourceDim::TextureCube:    return { spv::DimCube,   0, 0, isUav ? 2u : 1u, VK_IMAGE_VIEW_TYPE_CUBE       };
        case DxbcResourceDim::TextureCubeArr: return { spv::DimCube,   1, 0, isUav ? 2u : 1u, VK_IMAGE_VIEW_TYPE_CUBE_ARRAY };
        default: throw DxvkError(str::format("DxbcCompiler: Unsupported resource type: ", resourceType));
      }
    }();
    
    return typeInfo;
  }
  
  
  spv::ImageFormat DxbcCompiler::getScalarImageFormat(DxbcScalarType type) const {
    switch (type) {
      case DxbcScalarType::Float32: return spv::ImageFormatR32f;
      case DxbcScalarType::Sint32:  return spv::ImageFormatR32i;
      case DxbcScalarType::Uint32:  return spv::ImageFormatR32ui;
      default: throw DxvkError("DxbcCompiler: Unhandled scalar resource type");
    }
  }
  
  
  bool DxbcCompiler::isDoubleType(DxbcScalarType type) const {
    return type == DxbcScalarType::Sint64
        || type == DxbcScalarType::Uint64
        || type == DxbcScalarType::Float64;
  }


  uint32_t DxbcCompiler::getScalarTypeId(DxbcScalarType type) {
    if (type == DxbcScalarType::Float64)
      m_module.enableCapability(spv::CapabilityFloat64);
    
    if (type == DxbcScalarType::Sint64 || type == DxbcScalarType::Uint64)
      m_module.enableCapability(spv::CapabilityInt64);
    
    switch (type) {
      case DxbcScalarType::Uint32:  return m_module.defIntType(32, 0);
      case DxbcScalarType::Uint64:  return m_module.defIntType(64, 0);
      case DxbcScalarType::Sint32:  return m_module.defIntType(32, 1);
      case DxbcScalarType::Sint64:  return m_module.defIntType(64, 1);
      case DxbcScalarType::Float32: return m_module.defFloatType(32);
      case DxbcScalarType::Float64: return m_module.defFloatType(64);
      case DxbcScalarType::Bool:    return m_module.defBoolType();
    }

    throw DxvkError("DxbcCompiler: Invalid scalar type");
  }
  
  
  uint32_t DxbcCompiler::getVectorTypeId(const DxbcVectorType& type) {
    uint32_t typeId = this->getScalarTypeId(type.ctype);
    
    if (type.ccount > 1)
      typeId = m_module.defVectorType(typeId, type.ccount);
    
    return typeId;
  }
  
  
  uint32_t DxbcCompiler::getArrayTypeId(const DxbcArrayType& type) {
    DxbcVectorType vtype;
    vtype.ctype  = type.ctype;
    vtype.ccount = type.ccount;
    
    uint32_t typeId = this->getVectorTypeId(vtype);
    
    if (type.alength != 0) {
      typeId = m_module.defArrayType(typeId,
        m_module.constu32(type.alength));
    }
    
    return typeId;
  }
  
  
  uint32_t DxbcCompiler::getPointerTypeId(const DxbcRegisterInfo& type) {
    return m_module.defPointerType(
      this->getArrayTypeId(type.type),
      type.sclass);
  }
  
  
  uint32_t DxbcCompiler::getPerVertexBlockId() {
    uint32_t t_f32    = m_module.defFloatType(32);
    uint32_t t_f32_v4 = m_module.defVectorType(t_f32, 4);
//     uint32_t t_f32_a4 = m_module.defArrayType(t_f32, m_module.constu32(4));
    
    std::array<uint32_t, 1> members;
    members[PerVertex_Position] = t_f32_v4;
//     members[PerVertex_CullDist] = t_f32_a4;
//     members[PerVertex_ClipDist] = t_f32_a4;
    
    uint32_t typeId = m_module.defStructTypeUnique(
      members.size(), members.data());
    
    m_module.memberDecorateBuiltIn(typeId, PerVertex_Position, spv::BuiltInPosition);
//     m_module.memberDecorateBuiltIn(typeId, PerVertex_CullDist, spv::BuiltInCullDistance);
//     m_module.memberDecorateBuiltIn(typeId, PerVertex_ClipDist, spv::BuiltInClipDistance);
    m_module.decorateBlock(typeId);
    
    m_module.setDebugName(typeId, "s_per_vertex");
    m_module.setDebugMemberName(typeId, PerVertex_Position, "position");
//     m_module.setDebugMemberName(typeId, PerVertex_CullDist, "cull_dist");
//     m_module.setDebugMemberName(typeId, PerVertex_ClipDist, "clip_dist");
    return typeId;
  }
  
  
  DxbcCompilerHsForkJoinPhase* DxbcCompiler::getCurrentHsForkJoinPhase() {
    switch (m_hs.currPhaseType) {
      case DxbcCompilerHsPhase::Fork: return &m_hs.forkPhases.at(m_hs.currPhaseId);
      case DxbcCompilerHsPhase::Join: return &m_hs.joinPhases.at(m_hs.currPhaseId);
      default:                        return nullptr;
    }
  }
  
}