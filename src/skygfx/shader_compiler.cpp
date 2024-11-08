#include "shader_compiler.h"
#include <glslang/SPIRV/GlslangToSpv.h>
#include <glslang/Public/ResourceLimits.h>
#include <glslang/Public/ShaderLang.h>
#include <spirv_hlsl.hpp>
#include <spirv_reflect.h>
#include <spirv_msl.hpp>

using namespace skygfx;

std::vector<uint32_t> skygfx::CompileGlslToSpirv(ShaderStage stage, const std::string& code, const std::vector<std::string>& defines)
{
	static const std::unordered_map<ShaderStage, EShLanguage> StageMap = {
		{ ShaderStage::Vertex, EShLangVertex },
		{ ShaderStage::Fragment, EShLangFragment },
		{ ShaderStage::Raygen, EShLangRayGen },
		{ ShaderStage::Miss, EShLangMiss },
		{ ShaderStage::ClosestHit, EShLangClosestHit }
	};

	glslang::InitializeProcess();
	
	auto _stage = StageMap.at(stage);
	glslang::TShader shader(_stage);

	auto str = code.c_str();
	shader.setStrings(&str, 1);

	std::string preamble;

	for (const auto& define : defines)
	{
		preamble += "#define " + define + "\n";
	}

	shader.setPreamble(preamble.c_str());
	shader.setEnvTarget(glslang::EShTargetSpv, glslang::EShTargetSpv_1_6);

	auto messages = EShMessages(EShMsgSpvRules | EShMsgVulkanRules | EShMsgEnhanced);

	if (!shader.parse(GetDefaultResources(), 100, false, messages))
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

	auto intermediate = program.getIntermediate(_stage);

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
	options.flatten_matrix_vertex_input_semantics = true;
	compiler.set_hlsl_options(options);

	return compiler.compile();
}

std::string skygfx::CompileSpirvToGlsl(const std::vector<uint32_t>& spirv, bool es,
	uint32_t version, bool enable_420pack_extension, bool force_flattened_io_blocks)
{
	auto compiler = spirv_cross::CompilerGLSL(spirv);

	spirv_cross::CompilerGLSL::Options options;
	options.es = es;
	options.version = version;
	options.enable_420pack_extension = enable_420pack_extension;
	options.force_flattened_io_blocks = force_flattened_io_blocks;
	compiler.set_common_options(options);
	
	bool fix_varyings = (es && version <= 300) || force_flattened_io_blocks;

	if (fix_varyings)
	{
		// https://github.com/KhronosGroup/SPIRV-Cross/issues/1104
		
		auto stage = compiler.get_entry_points_and_stages()[0].execution_model;
		auto resources = compiler.get_shader_resources();
		
		if (stage == spv::ExecutionModelFragment)
		{
			for (const auto& input : resources.stage_inputs)
			{
				auto location = compiler.get_decoration(input.id, spv::DecorationLocation);
				compiler.set_name(input.id, "varying_" + std::to_string(location));
			}
		}
		else if (stage == spv::ExecutionModelVertex)
		{
			for (const auto& output : resources.stage_outputs)
			{
				auto location = compiler.get_decoration(output.id, spv::DecorationLocation);
				compiler.set_name(output.id, "varying_" + std::to_string(location));
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
	options.set_msl_version(2, 3);
	compiler.set_msl_options(options);

	return compiler.compile();
}

ShaderReflection skygfx::MakeSpirvReflection(const std::vector<uint32_t>& spirv)
{
	static const std::unordered_map<SpvReflectDescriptorType, ShaderReflection::DescriptorType> DescriptorTypeMap = {
		{ SPV_REFLECT_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, ShaderReflection::DescriptorType::CombinedImageSampler },
		{ SPV_REFLECT_DESCRIPTOR_TYPE_UNIFORM_BUFFER, ShaderReflection::DescriptorType::UniformBuffer },
		{ SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_IMAGE, ShaderReflection::DescriptorType::StorageImage },
		{ SPV_REFLECT_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, ShaderReflection::DescriptorType::AccelerationStructure },
		{ SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_BUFFER, ShaderReflection::DescriptorType::StorageBuffer }
	};

	static const std::unordered_map<SpvReflectShaderStageFlagBits, ShaderStage> StageMap = {
		{ SPV_REFLECT_SHADER_STAGE_VERTEX_BIT, ShaderStage::Vertex },
		{ SPV_REFLECT_SHADER_STAGE_FRAGMENT_BIT, ShaderStage::Fragment },
		{ SPV_REFLECT_SHADER_STAGE_RAYGEN_BIT_KHR, ShaderStage::Raygen },
		{ SPV_REFLECT_SHADER_STAGE_MISS_BIT_KHR, ShaderStage::Miss },
		{ SPV_REFLECT_SHADER_STAGE_CLOSEST_HIT_BIT_KHR, ShaderStage::ClosestHit }
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

		auto& typed_bindings = result.typed_descriptor_bindings[type];
		assert(!typed_bindings.contains(binding));

		auto& descriptor = typed_bindings[binding];
		descriptor.name = descriptor_binding->name;

		if (type == ShaderReflection::DescriptorType::UniformBuffer)
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
