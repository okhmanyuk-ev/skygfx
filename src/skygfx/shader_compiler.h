#pragma once

#include <vector>
#include <string>
#include "vertex.h"

namespace skygfx
{
	enum class ShaderStage
	{
		Vertex,
		Fragment
	};

	enum class HlslVersion
	{
		v4_0,
		v5_0
	};

	std::vector<uint32_t> CompileGlslToSpirv(ShaderStage stage, const std::string& code, const std::vector<std::string>& defines = {});
	std::string CompileSpirvToHlsl(const std::vector<uint32_t>& spirv, HlslVersion hlsl_version);
	std::string CompileSpirvToGlsl(const std::vector<uint32_t>& spirv, bool es = false, uint32_t version = 450);
	std::string CompileSpirvToMsl(const std::vector<uint32_t>& spirv);

	void AddShaderLocationDefines(const Vertex::Layout& layout, std::vector<std::string>& defines);

	struct ShaderReflection
	{
		struct DescriptorSet
		{
			enum class Type
			{
				CombinedImageSampler,
				UniformBuffer
			};

			int binding;
			std::string name;
			std::string type_name;
			Type type;
		};

		std::vector<DescriptorSet> descriptor_sets;
		ShaderStage stage;
	};

	ShaderReflection MakeSpirvReflection(const std::vector<uint32_t>& spirv);
}
