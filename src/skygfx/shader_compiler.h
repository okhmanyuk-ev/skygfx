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

	void AddShaderLocationDefines(const VertexLayout& vertex_layout, std::vector<std::string>& defines);

	struct ShaderReflection
	{
		struct Descriptor
		{
			enum class Type
			{
				CombinedImageSampler,
				UniformBuffer,
				StorageImage,
				AccelerationStructure
			};

			std::string name;
			std::string type_name;
			Type type;
		};

		std::unordered_map<uint32_t, Descriptor> descriptor_bindings;
		std::unordered_map<uint32_t/*set*/, std::unordered_set<uint32_t>/*bindings*/> descriptor_sets;
		ShaderStage stage;
	};

	ShaderReflection MakeSpirvReflection(const std::vector<uint32_t>& spirv);
}
