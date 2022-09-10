#include "shader_compiler.h"
#include <glslang/SPIRV/GlslangToSpv.h>
#include <glslang/StandAlone/ResourceLimits.h>
#include <spirv_hlsl.hpp>
#include <spirv_reflect.h>
#include <spirv_msl.hpp>

using namespace skygfx;

const TBuiltInResource DefaultTBuiltInResource = {
	/* .MaxLights = */ 32,
	/* .MaxClipPlanes = */ 6,
	/* .MaxTextureUnits = */ 32,
	/* .MaxTextureCoords = */ 32,
	/* .MaxVertexAttribs = */ 64,
	/* .MaxVertexUniformComponents = */ 4096,
	/* .MaxVaryingFloats = */ 64,
	/* .MaxVertexTextureImageUnits = */ 32,
	/* .MaxCombinedTextureImageUnits = */ 80,
	/* .MaxTextureImageUnits = */ 32,
	/* .MaxFragmentUniformComponents = */ 4096,
	/* .MaxDrawBuffers = */ 32,
	/* .MaxVertexUniformVectors = */ 128,
	/* .MaxVaryingVectors = */ 8,
	/* .MaxFragmentUniformVectors = */ 16,
	/* .MaxVertexOutputVectors = */ 16,
	/* .MaxFragmentInputVectors = */ 15,
	/* .MinProgramTexelOffset = */ -8,
	/* .MaxProgramTexelOffset = */ 7,
	/* .MaxClipDistances = */ 8,
	/* .MaxComputeWorkGroupCountX = */ 65535,
	/* .MaxComputeWorkGroupCountY = */ 65535,
	/* .MaxComputeWorkGroupCountZ = */ 65535,
	/* .MaxComputeWorkGroupSizeX = */ 1024,
	/* .MaxComputeWorkGroupSizeY = */ 1024,
	/* .MaxComputeWorkGroupSizeZ = */ 64,
	/* .MaxComputeUniformComponents = */ 1024,
	/* .MaxComputeTextureImageUnits = */ 16,
	/* .MaxComputeImageUniforms = */ 8,
	/* .MaxComputeAtomicCounters = */ 8,
	/* .MaxComputeAtomicCounterBuffers = */ 1,
	/* .MaxVaryingComponents = */ 60,
	/* .MaxVertexOutputComponents = */ 64,
	/* .MaxGeometryInputComponents = */ 64,
	/* .MaxGeometryOutputComponents = */ 128,
	/* .MaxFragmentInputComponents = */ 128,
	/* .MaxImageUnits = */ 8,
	/* .MaxCombinedImageUnitsAndFragmentOutputs = */ 8,
	/* .MaxCombinedShaderOutputResources = */ 8,
	/* .MaxImageSamples = */ 0,
	/* .MaxVertexImageUniforms = */ 0,
	/* .MaxTessControlImageUniforms = */ 0,
	/* .MaxTessEvaluationImageUniforms = */ 0,
	/* .MaxGeometryImageUniforms = */ 0,
	/* .MaxFragmentImageUniforms = */ 8,
	/* .MaxCombinedImageUniforms = */ 8,
	/* .MaxGeometryTextureImageUnits = */ 16,
	/* .MaxGeometryOutputVertices = */ 256,
	/* .MaxGeometryTotalOutputComponents = */ 1024,
	/* .MaxGeometryUniformComponents = */ 1024,
	/* .MaxGeometryVaryingComponents = */ 64,
	/* .MaxTessControlInputComponents = */ 128,
	/* .MaxTessControlOutputComponents = */ 128,
	/* .MaxTessControlTextureImageUnits = */ 16,
	/* .MaxTessControlUniformComponents = */ 1024,
	/* .MaxTessControlTotalOutputComponents = */ 4096,
	/* .MaxTessEvaluationInputComponents = */ 128,
	/* .MaxTessEvaluationOutputComponents = */ 128,
	/* .MaxTessEvaluationTextureImageUnits = */ 16,
	/* .MaxTessEvaluationUniformComponents = */ 1024,
	/* .MaxTessPatchComponents = */ 120,
	/* .MaxPatchVertices = */ 32,
	/* .MaxTessGenLevel = */ 64,
	/* .MaxViewports = */ 16,
	/* .MaxVertexAtomicCounters = */ 0,
	/* .MaxTessControlAtomicCounters = */ 0,
	/* .MaxTessEvaluationAtomicCounters = */ 0,
	/* .MaxGeometryAtomicCounters = */ 0,
	/* .MaxFragmentAtomicCounters = */ 8,
	/* .MaxCombinedAtomicCounters = */ 8,
	/* .MaxAtomicCounterBindings = */ 1,
	/* .MaxVertexAtomicCounterBuffers = */ 0,
	/* .MaxTessControlAtomicCounterBuffers = */ 0,
	/* .MaxTessEvaluationAtomicCounterBuffers = */ 0,
	/* .MaxGeometryAtomicCounterBuffers = */ 0,
	/* .MaxFragmentAtomicCounterBuffers = */ 1,
	/* .MaxCombinedAtomicCounterBuffers = */ 1,
	/* .MaxAtomicCounterBufferSize = */ 16384,
	/* .MaxTransformFeedbackBuffers = */ 4,
	/* .MaxTransformFeedbackInterleavedComponents = */ 64,
	/* .MaxCullDistances = */ 8,
	/* .MaxCombinedClipAndCullDistances = */ 8,
	/* .MaxSamples = */ 4,
	/* .maxMeshOutputVerticesNV = */ 256,
	/* .maxMeshOutputPrimitivesNV = */ 512,
	/* .maxMeshWorkGroupSizeX_NV = */ 32,
	/* .maxMeshWorkGroupSizeY_NV = */ 1,
	/* .maxMeshWorkGroupSizeZ_NV = */ 1,
	/* .maxTaskWorkGroupSizeX_NV = */ 32,
	/* .maxTaskWorkGroupSizeY_NV = */ 1,
	/* .maxTaskWorkGroupSizeZ_NV = */ 1,
	/* .maxMeshViewCountNV = */ 4,
	/* .maxDualSourceDrawBuffersEXT = */ 1,

	/* .limits = */ {
		/* .nonInductiveForLoops = */ 1,
		/* .whileLoops = */ 1,
		/* .doWhileLoops = */ 1,
		/* .generalUniformIndexing = */ 1,
		/* .generalAttributeMatrixVectorIndexing = */ 1,
		/* .generalVaryingIndexing = */ 1,
		/* .generalSamplerIndexing = */ 1,
		/* .generalVariableIndexing = */ 1,
		/* .generalConstantMatrixVectorIndexing = */ 1,
	}
};

std::vector<uint32_t> skygfx::CompileGlslToSpirv(ShaderStage stage, const std::string& code, const std::vector<std::string>& defines)
{
	auto translateShaderStage = [](ShaderStage stage) {
		switch (stage)
		{
		case ShaderStage::Vertex: return EShLangVertex;
		case ShaderStage::Fragment: return EShLangFragment;
		default: throw std::runtime_error("Unknown shader stage"); return EShLangVertex;
		}
	};

	glslang::InitializeProcess();

	auto str = code.c_str();

	auto translated_stage = translateShaderStage(stage);

	glslang::TShader shader(translated_stage);
	shader.setStrings(&str, 1);

	std::string preamble;

	for (auto define : defines)
	{
		preamble += "#define " + define + "\n";
	}

	shader.setPreamble(preamble.c_str());

	auto messages = (EShMessages)(EShMsgSpvRules | EShMsgVulkanRules);

	if (!shader.parse(&DefaultTBuiltInResource, 100, false, messages))
	{
		auto info_log = shader.getInfoLog();
		throw std::runtime_error(info_log);
	}

	glslang::TProgram program;
	program.addShader(&shader);

	if (!program.link(messages))
	{
		auto info_log = shader.getInfoLog();
		throw std::runtime_error(info_log);
	}

	auto intermediate = program.getIntermediate(translated_stage);

	std::vector<uint32_t> result;
	glslang::GlslangToSpv(*intermediate, result);
	glslang::FinalizeProcess();

	return result;
}

std::string skygfx::CompileSpirvToHlsl(const std::vector<uint32_t>& spirv, uint32_t version)
{
	auto compiler = spirv_cross::CompilerHLSL(spirv);

	spirv_cross::CompilerHLSL::Options options;
	options.shader_model = version;
	compiler.set_hlsl_options(options);

	return compiler.compile();
}

std::string skygfx::CompileSpirvToGlsl(const std::vector<uint32_t>& spirv, bool es, uint32_t version)
{
	auto compiler = spirv_cross::CompilerGLSL(spirv);

	spirv_cross::CompilerGLSL::Options options;
	options.es = es;
	options.version = version;
	compiler.set_common_options(options);
	
	if (es && version <= 300)
	{
		// https://github.com/KhronosGroup/SPIRV-Cross/issues/1104
		
		auto stage = compiler.get_entry_points_and_stages()[0].execution_model;

		auto resources = compiler.get_shader_resources();
		
		if (stage == spv::ExecutionModelFragment)
		{
			for (const auto& input : resources.stage_inputs)
			{
				compiler.set_name(input.id, "varying");
			}
		}
		else if (stage == spv::ExecutionModelVertex)
		{
			for (const auto& output : resources.stage_outputs)
			{
				compiler.set_name(output.id, "varying");
			}
		}
	}
	
	return compiler.compile();
}

std::string skygfx::CompileSpirvToMsl(const std::vector<uint32_t>& spirv)
{
	class FixedCompilerMSL : public spirv_cross::CompilerMSL
	{
	public:
		using spirv_cross::CompilerMSL::CompilerMSL;
		
		void replace_illegal_names() override
		{
			spirv_cross::CompilerGLSL::replace_illegal_names({ "fragment" });
		}
	};
	
	auto compiler = FixedCompilerMSL(spirv);
	
	spirv_cross::CompilerMSL::Options options;
	options.enable_decoration_binding = true;
	compiler.set_msl_options(options);

	return compiler.compile();
}

void skygfx::AddShaderLocationDefines(const Vertex::Layout& layout, std::vector<std::string>& defines)
{
	const std::unordered_map<Vertex::Attribute::Type, std::string> Names = {
		{ Vertex::Attribute::Type::Position, "POSITION_LOCATION" },
		{ Vertex::Attribute::Type::Color, "COLOR_LOCATION" },
		{ Vertex::Attribute::Type::TexCoord, "TEXCOORD_LOCATION" },
		{ Vertex::Attribute::Type::Normal, "NORMAL_LOCATION" },
	};

	for (int i = 0; i < layout.attributes.size(); i++)
	{
		const auto& attrib = layout.attributes.at(i);
		assert(Names.contains(attrib.type));
		auto name = Names.at(attrib.type);
		defines.push_back(name + " " + std::to_string(i));
	}
}

ShaderReflection skygfx::MakeSpirvReflection(const std::vector<uint32_t>& spirv)
{
	static const std::unordered_map<SpvReflectDescriptorType, ShaderReflection::Descriptor::Type> DescriptorTypeMap = {
		{ SPV_REFLECT_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, ShaderReflection::Descriptor::Type::CombinedImageSampler },
		{ SPV_REFLECT_DESCRIPTOR_TYPE_UNIFORM_BUFFER, ShaderReflection::Descriptor::Type::UniformBuffer },
	};

	static const std::unordered_map<SpvReflectShaderStageFlagBits, ShaderStage> StageMap = {
		{ SPV_REFLECT_SHADER_STAGE_VERTEX_BIT, ShaderStage::Vertex },
		{ SPV_REFLECT_SHADER_STAGE_FRAGMENT_BIT, ShaderStage::Fragment }
	};

	auto refl = spv_reflect::ShaderModule(spirv);

	SpvReflectResult r;

	uint32_t descriptor_bindings_count = 0;
	r = refl.EnumerateDescriptorBindings(&descriptor_bindings_count, nullptr);
	assert(r == SPV_REFLECT_RESULT_SUCCESS);

	std::vector<SpvReflectDescriptorBinding*> descriptor_bindings(descriptor_bindings_count);
	r = refl.EnumerateDescriptorBindings(&descriptor_bindings_count, descriptor_bindings.data());
	assert(r == SPV_REFLECT_RESULT_SUCCESS);

	uint32_t descriptor_sets_count = 0;
	r = refl.EnumerateDescriptorSets(&descriptor_sets_count, nullptr);
	assert(r == SPV_REFLECT_RESULT_SUCCESS);

	std::vector<SpvReflectDescriptorSet*> descriptor_sets(descriptor_sets_count);
	r = refl.EnumerateDescriptorSets(&descriptor_sets_count, descriptor_sets.data());
	assert(r == SPV_REFLECT_RESULT_SUCCESS);

	ShaderReflection result;

	auto stage = refl.GetShaderStage();
	result.stage = StageMap.at(stage);

	for (const auto& descriptor_binding : descriptor_bindings)
	{
		auto binding = descriptor_binding->binding;
		auto type = DescriptorTypeMap.at(descriptor_binding->descriptor_type);

		assert(!result.descriptor_bindings.contains(binding));

		auto& descriptor = result.descriptor_bindings[binding];
		descriptor.type = type;
		descriptor.name = descriptor_binding->name;

		if (type == ShaderReflection::Descriptor::Type::UniformBuffer)
			descriptor.type_name = descriptor_binding->block.type_description->type_name;
	}

	for (const auto& descriptor_set : descriptor_sets)
	{
		auto set = descriptor_set->set;

		for (uint32_t i = 0; i < descriptor_set->binding_count; i++)
		{
			auto descriptor_binding = descriptor_set->bindings[i];
			auto binding = descriptor_binding->binding;

			result.descriptor_sets[set].insert(binding);
		}
	}

	return result;
}
