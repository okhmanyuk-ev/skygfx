#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <optional>
#include <glm/glm.hpp>
#include <glm/ext.hpp>
#include "vertex.h"
#include "shader_compiler.h"
#include "utils.h"

namespace skygfx
{
	enum class BackendType
	{
		D3D11,
		D3D12,
		OpenGL,
		Vulkan,
		Metal
	};

	using TextureHandle = struct TextureHandle;
	using RenderTargetHandle = struct RenderTargetHandle;
	using ShaderHandle = struct ShaderHandle;
	using VertexBufferHandle = struct VertexBufferHandle;
	using IndexBufferHandle = struct IndexBufferHandle;
	using UniformBufferHandle = struct UniformBufferHandle;

	class noncopyable
	{
	protected:
		noncopyable() = default;

	private:
		noncopyable(const noncopyable&) = delete;
		noncopyable& operator=(const noncopyable&) = delete;
	};

	class Texture : private noncopyable
	{
	public:
		Texture(uint32_t width, uint32_t height, uint32_t channels, void* memory, bool mipmap = false);
		virtual ~Texture();

		operator TextureHandle* () { return mTextureHandle; }

		auto getWidth() const { return mWidth; }
		auto getHeight() const { return mHeight; }

	private:
		TextureHandle* mTextureHandle = nullptr;
		uint32_t mWidth = 0;
		uint32_t mHeight = 0;
	};

	class RenderTarget : public Texture
	{
	public:
		RenderTarget(uint32_t width, uint32_t height);
		~RenderTarget();
		
		operator RenderTargetHandle* () { return mRenderTargetHandle; }

	private:
		RenderTargetHandle* mRenderTargetHandle = nullptr;
	};

	class Shader : private noncopyable
	{
	public:
		Shader(const Vertex::Layout& layout, const std::string& vertex_code, 
			const std::string& fragment_code, const std::vector<std::string>& defines = {});
		~Shader();

		operator ShaderHandle* () { return mShaderHandle; }

	private:
		ShaderHandle* mShaderHandle = nullptr;
	};

	class Buffer : private noncopyable
	{
	public:
		Buffer(size_t size);

		auto getSize() const { return mSize; }

	private:
		size_t mSize = 0;
	};

	class VertexBuffer : public Buffer
	{
	public:
		VertexBuffer(void* memory, size_t size, size_t stride);
		~VertexBuffer();

		template<class T> VertexBuffer(T* memory, size_t count) : VertexBuffer((void*)memory, count * sizeof(T), sizeof(T)) {}
		template<class T> VertexBuffer(const std::vector<T>& values) : VertexBuffer(values.data(), values.size()) {}

		void write(void* memory, size_t size, size_t stride);

		operator VertexBufferHandle* () { return mVertexBufferHandle; }

	private:
		VertexBufferHandle* mVertexBufferHandle = nullptr;
	};

	class IndexBuffer : public Buffer
	{
	public:
		IndexBuffer(void* memory, size_t size, size_t stride);
		~IndexBuffer();

		template<class T> IndexBuffer(T* memory, size_t count) : IndexBuffer((void*)memory, count * sizeof(T), sizeof(T)) {}
		template<class T> IndexBuffer(const std::vector<T>& values) : IndexBuffer(values.data(), values.size()) {}

		void write(void* memory, size_t size, size_t stride);

		operator IndexBufferHandle* () { return mIndexBufferHandle; }

	private:
		IndexBufferHandle* mIndexBufferHandle = nullptr;
	};

	class UniformBuffer : public Buffer
	{
	public:
		UniformBuffer(void* memory, size_t size);
		~UniformBuffer();

		template <class T> UniformBuffer(T value) : UniformBuffer(&value, sizeof(T)) {}

		void write(void* memory, size_t size);
		template <class T> void write(T value) { write(&value, sizeof(T)); }

		operator UniformBufferHandle* () { return mUniformBufferHandle; }

	private:
		UniformBufferHandle* mUniformBufferHandle = nullptr;
	};

	enum class Topology
	{
		PointList,
		LineList,
		LineStrip,
		TriangleList,
		TriangleStrip
	};

	struct Viewport
	{
		glm::vec2 position = { 0.0f, 0.0f };
		glm::vec2 size = { 0.0f, 0.0f };
		float min_depth = 0.0f;
		float max_depth = 1.0f;
	};

	inline bool operator==(const Viewport& left, const Viewport& right)
	{
		return
			left.position == right.position &&
			left.size == right.size &&
			left.min_depth == right.min_depth &&
			left.max_depth == right.max_depth;
	}

	inline bool operator!=(const Viewport& left, const Viewport& right)
	{
		return !(left == right);
	}

	enum class Blend
	{
		One, // Each component of the color is multiplied by {1, 1, 1, 1}.
		Zero, // Each component of the color is multiplied by {0, 0, 0, 0}.
		SrcColor, // Each component of the color is multiplied by the source color. {Rs, Gs, Bs, As}, where Rs, Gs, Bs, As are color source values.		
		InvSrcColor, // Each component of the color is multiplied by the inverse of the source color. {1 - Rs, 1 - Gs, 1 - Bs, 1 - As}, where Rs, Gs, Bs, As are color source values.
		SrcAlpha, // Each component of the color is multiplied by the alpha value of the source. {As, As, As, As}, where As is the source alpha value.		
		InvSrcAlpha, // Each component of the color is multiplied by the inverse of the alpha value of the source. {1 - As, 1 - As, 1 - As, 1 - As}, where As is the source alpha value.
		DstColor, // Each component color is multiplied by the destination color. {Rd, Gd, Bd, Ad}, where Rd, Gd, Bd, Ad are color destination values.
		InvDstColor, // Each component of the color is multiplied by the inversed destination color. {1 - Rd, 1 - Gd, 1 - Bd, 1 - Ad}, where Rd, Gd, Bd, Ad are color destination values.
		DstAlpha, // Each component of the color is multiplied by the alpha value of the destination. {Ad, Ad, Ad, Ad}, where Ad is the destination alpha value.
		InvDstAlpha, // Each component of the color is multiplied by the inversed alpha value of the destination. {1 - Ad, 1 - Ad, 1 - Ad, 1 - Ad}, where Ad is the destination alpha value.
	//	BlendFactor, // Each component of the color is multiplied by a constant in the BlendFactor
	//	InverseBlendFactor, // Each component of the color is multiplied by a inversed constant in the BlendFactor
	//	SourceAlphaSaturation // Each component of the color is multiplied by either the alpha of the source color, or the inverse of the alpha of the source color, whichever is greater. {f, f, f, 1}, where f = min(As, 1 - As), where As is the source alpha value.
	};

	enum class BlendFunction
	{
		Add, // The function will adds destination to the source. (srcColor * srcBlend) + (destColor * destBlend)	
		Subtract, // The function will subtracts destination from source. (srcColor * srcBlend) - (destColor * destBlend)
		ReverseSubtract, // The function will subtracts source from destination. (destColor * destBlend) - (srcColor * srcBlend) 
		Min, // The function will extracts minimum of the source and destination. min((srcColor * srcBlend),(destColor * destBlend))
		Max // The function will extracts maximum of the source and destination. max((srcColor * srcBlend),(destColor * destBlend))
	};

	struct ColorMask
	{
		bool red = true;
		bool green = true;
		bool blue = true;
		bool alpha = true;
	};

	struct BlendMode
	{
		BlendMode(Blend srcColorBlend, Blend dstColorBlend, Blend srcAlphaBlend, Blend dstAlphaBlend) :
			colorSrcBlend(srcColorBlend), colorDstBlend(dstColorBlend), alphaSrcBlend(srcAlphaBlend), alphaDstBlend(dstAlphaBlend)
		{}

		BlendMode(Blend srcBlend, Blend dstBlend) : BlendMode(srcBlend, dstBlend, srcBlend, dstBlend) { }

		BlendFunction colorBlendFunction = BlendFunction::Add;
		Blend colorSrcBlend;
		Blend colorDstBlend;

		BlendFunction alphaBlendFunction = BlendFunction::Add;
		Blend alphaSrcBlend;
		Blend alphaDstBlend;

		ColorMask colorMask;
	};

	inline bool operator==(const ColorMask& left, const ColorMask& right)
	{
		return
			left.red == right.red &&
			left.green == right.green &&
			left.blue == right.blue &&
			left.alpha == right.alpha;
	}

	inline bool operator!=(const ColorMask& left, const ColorMask& right)
	{
		return !(left == right);
	}

	inline bool operator==(const BlendMode& left, const BlendMode& right)
	{
		return
			left.colorBlendFunction == right.colorBlendFunction &&
			left.colorSrcBlend == right.colorSrcBlend &&
			left.colorDstBlend == right.colorDstBlend &&
			left.alphaBlendFunction == right.alphaBlendFunction &&
			left.alphaSrcBlend == right.alphaSrcBlend &&
			left.alphaDstBlend == right.alphaDstBlend &&
			left.colorMask == right.colorMask;
	}

	inline bool operator!=(const BlendMode& left, const BlendMode& right)
	{
		return !(left == right);
	}

	namespace BlendStates
	{
		inline const BlendMode Opaque = BlendMode(Blend::One, Blend::Zero);
		inline const BlendMode AlphaBlend = BlendMode(Blend::One, Blend::InvSrcAlpha);
		inline const BlendMode Additive = BlendMode(Blend::SrcAlpha, Blend::One);
		inline const BlendMode NonPremultiplied = BlendMode(Blend::SrcAlpha, Blend::InvSrcAlpha);
	}

	enum class ComparisonFunc
	{
		Always,         // comparison always succeeds
		Never,          // comparison always fails
		Less,           // passes if source is less than the destination
		Equal,          // passes if source is equal to the destination
		NotEqual,       // passes if source is not equal to the destination
		LessEqual,      // passes if source is less than or equal to the destination
		Greater,        // passes if source is greater than to the destination
		GreaterEqual,   // passes if source is greater than or equal to the destination
	};

	struct DepthMode
	{
		DepthMode() {}
		DepthMode(ComparisonFunc _func) : func(_func) {}

		ComparisonFunc func = ComparisonFunc::Always;
	};

	inline bool operator==(const DepthMode& left, const DepthMode& right)
	{
		return left.func == right.func;
	}

	inline bool operator!=(const DepthMode& left, const DepthMode& right)
	{
		return !(left == right);
	}

	enum class StencilOp
	{
		Keep, // Does not update the stencil buffer entry.
		Zero, // Sets the stencil buffer entry to 0.
		Replace, // Replaces the stencil buffer entry with a reference value.
		Increment, // Increments the stencil buffer entry, wrapping to 0 if the new value exceeds the maximum value.
		Decrement, // Decrements the stencil buffer entry, wrapping to the maximum value if the new value is less than 0.
		IncrementSaturation, // Increments the stencil buffer entry, clamping to the maximum value.
		DecrementSaturation, // Decrements the stencil buffer entry, clamping to 0.
		Invert // Inverts the bits in the stencil buffer entry.
	};

	struct StencilMode
	{
		uint8_t read_mask = 255;
		uint8_t write_mask = 255;

		StencilOp depth_fail_op = StencilOp::Keep;
		StencilOp fail_op = StencilOp::Keep;
		ComparisonFunc func = ComparisonFunc::Always;
		StencilOp pass_op = StencilOp::Keep;

		uint8_t reference = 1;
	};

	inline bool operator==(const StencilMode& left, const StencilMode& right)
	{
		return
			left.read_mask == right.read_mask &&
			left.write_mask == right.write_mask &&

			left.depth_fail_op == right.depth_fail_op &&
			left.fail_op == right.fail_op &&
			left.func == right.func &&
			left.pass_op == right.pass_op &&

			left.reference == right.reference;
	}

	inline bool operator!=(const StencilMode& left, const StencilMode& right)
	{
		return !(left == right);
	}

	struct Scissor
	{
		glm::vec2 position = { 0.0f, 0.0f };
		glm::vec2 size = { 0.0f, 0.0f };
	};

	inline bool operator==(const Scissor& left, const Scissor& right)
	{
		return left.position == right.position &&
			left.size == right.size;
	}

	inline bool operator!=(const Scissor& left, const Scissor& right)
	{
		return !(left == right);
	}

	enum class CullMode
	{
		None,   // No culling
		Front,  // Cull front-facing primitives
		Back,   // Cull back-facing primitives
	};

	enum class Sampler
	{
		Linear,
		Nearest
	};

	enum class TextureAddress
	{
		Wrap, // Texels outside range will form the tile at every integer junction.		
		Clamp, // Texels outside range will be set to color of 0.0 or 1.0 texel.
		MirrorWrap
	};

	class Device : private noncopyable
	{
	public:
		Device(void* window, uint32_t width, uint32_t height, std::optional<BackendType> type = std::nullopt);
		~Device();

		void resize(uint32_t width, uint32_t height);

		void setTopology(Topology topology);
		void setViewport(std::optional<Viewport> viewport);
		void setScissor(std::optional<Scissor> scissor);
		void setTexture(uint32_t binding, const Texture& texture);
		void setRenderTarget(const RenderTarget& value);
		void setRenderTarget(std::nullptr_t value);
		void setShader(const Shader& shader);
		void setVertexBuffer(const VertexBuffer& value);
		void setIndexBuffer(const IndexBuffer& value);
		void setUniformBuffer(uint32_t binding, const UniformBuffer& value);
		void setBlendMode(const BlendMode& value);
		void setDepthMode(std::optional<DepthMode> depth_mode);
		void setStencilMode(std::optional<StencilMode> stencil_mode);
		void setCullMode(CullMode cull_mode);
		void setSampler(Sampler value);
		void setTextureAddress(TextureAddress value);

		void clear(const std::optional<glm::vec4>& color = glm::vec4{ 0.0f, 0.0f, 0.0f, 0.0f },
			const std::optional<float>& depth = 1.0f, const std::optional<uint8_t>& stencil = 0);
		void draw(uint32_t vertex_count, uint32_t vertex_offset = 0);
		void drawIndexed(uint32_t index_count, uint32_t index_offset = 0);

		void readPixels(const glm::ivec2& pos, const glm::ivec2& size, Texture& dst_texture);

		void present();

		uint32_t getBackbufferWidth() const;
		uint32_t getBackbufferHeight() const;

	private:
		uint32_t mBackbufferWidth = 0;
		uint32_t mBackbufferHeight = 0;

	public:
		static BackendType GetBackendTypeBasedOnPlatform();
	};
}

SKYGFX_MAKE_HASHABLE(skygfx::BlendMode,
	t.alphaBlendFunction,
	t.alphaDstBlend,
	t.alphaSrcBlend,
	t.colorBlendFunction,
	t.colorDstBlend,
	t.colorSrcBlend);

SKYGFX_MAKE_HASHABLE(skygfx::DepthMode,
	t.func);

SKYGFX_MAKE_HASHABLE(skygfx::StencilMode,
	t.read_mask,
	t.write_mask,
	t.depth_fail_op,
	t.fail_op,
	t.func,
	t.pass_op);
