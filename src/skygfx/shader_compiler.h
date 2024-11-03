#pragma once

#include <vector>
#include <string>
#include <map>
#include <set>
#include "skygfx.h"

namespace skygfx
{
	std::vector<uint32_t> CompileGlslToSpirv(ShaderStage stage, const std::string& code,
		const std::vector<std::string>& defines = {});
	std::string CompileSpirvToHlsl(const std::vector<uint32_t>& spirv, uint32_t version);
	std::string CompileSpirvToGlsl(const std::vector<uint32_t>& spirv, bool es = false,
		uint32_t version = 450, bool enable_420pack_extension = true,
		bool force_flattened_io_blocks = false);
	std::string CompileSpirvToMsl(const std::vector<uint32_t>& spirv);

	struct ShaderReflection
	{
		enum class DescriptorType
		{
			CombinedImageSampler,
			UniformBuffer,
			StorageImage,
			AccelerationStructure,
			StorageBuffer
		};

		struct Descriptor
		{
			std::string name;
			std::string type_name;
		};

		std::unordered_map<DescriptorType, std::unordered_map<uint32_t, Descriptor>> typed_descriptor_bindings;
		std::unordered_map<uint32_t/*set*/, std::unordered_set<uint32_t>/*bindings*/> descriptor_sets;
		ShaderStage stage;
	};

	ShaderReflection MakeSpirvReflection(const std::vector<uint32_t>& spirv);
}
