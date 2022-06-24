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

	std::vector<uint32_t> CompileGlslToSpirv(ShaderStage stage, const std::string& code, const std::vector<std::string>& defines = {});
	std::string CompileSpirvToHlsl(const std::vector<uint32_t>& spirv);
	std::string CompileSpirvToGlsl(const std::vector<uint32_t>& spirv);
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
			Type type;
		};

		std::vector<DescriptorSet> descriptor_sets;
		ShaderStage stage;
	};

	ShaderReflection MakeSpirvReflection(const std::vector<uint32_t>& spirv);
}
