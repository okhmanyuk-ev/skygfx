#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <optional>
#include <variant>
#include <stdexcept>
#include <cassert>
#include <memory>
#include <algorithm>
#include <iterator>
#include <unordered_set>
#include <unordered_map>
#include <glm/glm.hpp>
#include <glm/ext.hpp>
#include "other.h"

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

	enum class Feature
	{
		Raytracing
	};

	enum class Format
	{
		Float1,
		Float2,
		Float3,
		Float4,
		Byte1,
		Byte2,
		Byte3,
		Byte4
	};

	enum class ShaderStage
	{
		Vertex,
		Fragment,
		Raygen,
		Miss,
		ClosestHit
	};

	struct VertexAttribute
	{
		std::optional<std::string> location_define;
		Format format;
		size_t offset;
	};

	struct VertexLayout
	{
		size_t stride;
		std::vector<VertexAttribute> attributes;
	};

	using TextureHandle = struct TextureHandle;
	using RenderTargetHandle = struct RenderTargetHandle;
	using ShaderHandle = struct ShaderHandle;
	using RaytracingShaderHandle = struct RaytracingShaderHandle;
	using VertexBufferHandle = struct VertexBufferHandle;
	using IndexBufferHandle = struct IndexBufferHandle;
	using UniformBufferHandle = struct UniformBufferHandle;
	using AccelerationStructureHandle = struct AccelerationStructureHandle;

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
		Texture(uint32_t width, uint32_t height, Format format, void* memory, bool mipmap = false);
		Texture(Texture&& other) noexcept;
		virtual ~Texture();

		void write(uint32_t width, uint32_t height, Format format, void* memory,
			uint32_t mip_level = 0, uint32_t offset_x = 0, uint32_t offset_y = 0);

		Texture& operator=(Texture&& other) noexcept;

		operator TextureHandle* () { return mTextureHandle; }

		auto getWidth() const { return mWidth; }
		auto getHeight() const { return mHeight; }
		auto getFormat() const { return mFormat; }

	private:
		TextureHandle* mTextureHandle = nullptr;
		uint32_t mWidth = 0;
		uint32_t mHeight = 0;
		Format mFormat;
	};

	class RenderTarget : public Texture
	{
	public:
		RenderTarget(uint32_t width, uint32_t height, Format format = Format::Float4);
		RenderTarget(RenderTarget&& other) noexcept;
		~RenderTarget();

		RenderTarget& operator=(RenderTarget&& other) noexcept;

		operator RenderTargetHandle* () { return mRenderTargetHandle; }

	private:
		RenderTargetHandle* mRenderTargetHandle = nullptr;
	};

	class Shader : private noncopyable
	{
	public:
		Shader(const VertexLayout& vertex_layout, const std::string& vertex_code, 
			const std::string& fragment_code, const std::vector<std::string>& defines = {});
		virtual ~Shader();

		operator ShaderHandle* () { return mShaderHandle; }

	private:
		ShaderHandle* mShaderHandle = nullptr;
	};

	class RaytracingShader : private noncopyable
	{
	public:
		RaytracingShader(const std::string& raygen_code, const std::string& miss_code,
			const std::string& closesthit_code, const std::vector<std::string>& defines = {});
		virtual ~RaytracingShader();

		operator RaytracingShaderHandle* () { return mRaytracingShaderHandle; }

	private:
		RaytracingShaderHandle* mRaytracingShaderHandle = nullptr;
	};

	class Buffer : private noncopyable
	{
	public:
		Buffer(size_t size);
		Buffer(Buffer&& other) noexcept;

		Buffer& operator=(Buffer&& other) noexcept;

		auto getSize() const { return mSize; }

	private:
		size_t mSize = 0;
	};

	class VertexBuffer : public Buffer
	{
	public:
		VertexBuffer(size_t size, size_t stride);
		VertexBuffer(void* memory, size_t size, size_t stride);
		VertexBuffer(VertexBuffer&& other) noexcept;
		~VertexBuffer();

		VertexBuffer& operator=(VertexBuffer&& other) noexcept;

		template<class T>
		explicit VertexBuffer(T* memory, size_t count) : VertexBuffer((void*)memory, count * sizeof(T), sizeof(T)) {}
		
		template<class T>
		explicit VertexBuffer(const std::vector<T>& values) : VertexBuffer(values.data(), values.size()) {}

		void write(void* memory, size_t size, size_t stride);

		template<class T>
		void write(T* memory, size_t count)
		{
			write((void*)memory, count * sizeof(T), sizeof(T));
		}

		template<class T>
		void write(const std::vector<T>& values)
		{
			write(values.data(), values.size());
		}

		operator VertexBufferHandle* () { return mVertexBufferHandle; }

	private:
		VertexBufferHandle* mVertexBufferHandle = nullptr;
	};

	class IndexBuffer : public Buffer
	{
	public:
		IndexBuffer(size_t size, size_t stride);
		IndexBuffer(void* memory, size_t size, size_t stride);
		IndexBuffer(IndexBuffer&& other) noexcept;
		~IndexBuffer();

		IndexBuffer& operator=(IndexBuffer&& other) noexcept;

		template<class T>
		explicit IndexBuffer(T* memory, size_t count) : IndexBuffer((void*)memory, count * sizeof(T), sizeof(T)) {}

		template<class T>
		explicit IndexBuffer(const std::vector<T>& values) : IndexBuffer(values.data(), values.size()) {}

		void write(void* memory, size_t size, size_t stride);

		template<class T>
		void write(T* memory, size_t count)
		{
			write((void*)memory, count * sizeof(T), sizeof(T));
		}

		template<class T>
		void write(const std::vector<T>& values)
		{
			write(values.data(), values.size());
		}

		operator IndexBufferHandle* () { return mIndexBufferHandle; }

	private:
		IndexBufferHandle* mIndexBufferHandle = nullptr;
	};

	class UniformBuffer : public Buffer
	{
	public:
		UniformBuffer(size_t size);
		UniformBuffer(void* memory, size_t size);
		UniformBuffer(UniformBuffer&& other) noexcept;
		~UniformBuffer();

		UniformBuffer& operator=(UniformBuffer&& other) noexcept;

		template <class T>
		explicit UniformBuffer(T value) : UniformBuffer(&value, sizeof(T)) {}

		void write(void* memory, size_t size);
		
		template <class T>
		void write(const T& value) { write(&const_cast<T&>(value), sizeof(T)); }

		operator UniformBufferHandle* () { return mUniformBufferHandle; }

	private:
		UniformBufferHandle* mUniformBufferHandle = nullptr;
	};

	class AccelerationStructure : public noncopyable
	{
	public:
		AccelerationStructure(const std::vector<glm::vec3>& vertices, const std::vector<uint32_t>& indices,
			const glm::mat4& transform = glm::mat4(1.0f));
		~AccelerationStructure();

		operator AccelerationStructureHandle* () { return mAccelerationStructureHandle; }

	private:
		AccelerationStructureHandle* mAccelerationStructureHandle = nullptr;
	};

	enum class Topology
	{
		PointList,
		LineList,
		LineStrip,
		TriangleList,
		TriangleStrip
	};

	enum class TopologyKind
	{
		Points,
		Lines,
		Triangles
	};

	inline TopologyKind GetTopologyKind(Topology topology)
	{
		static const std::unordered_map<Topology, TopologyKind> TopologyKindMap = {
			{ Topology::PointList, TopologyKind::Points },
			{ Topology::LineList, TopologyKind::Lines},
			{ Topology::LineStrip, TopologyKind::Lines },
			{ Topology::TriangleList, TopologyKind::Triangles },
			{ Topology::TriangleStrip, TopologyKind::Triangles }
		};
		return TopologyKindMap.at(topology);
	}

	inline uint32_t GetFormatChannelsCount(Format format)
	{
		static const std::unordered_map<Format, uint32_t> FormatChannelsMap = {
			{ Format::Float1, 1 },
			{ Format::Float2, 2 },
			{ Format::Float3, 3 },
			{ Format::Float4, 4 },
			{ Format::Byte1, 1 },
			{ Format::Byte2, 2 },
			{ Format::Byte3, 3 },
			{ Format::Byte4, 4 }
		};
		return FormatChannelsMap.at(format);
	}

	inline uint32_t GetFormatChannelSize(Format format)
	{
		static const std::unordered_map<Format, uint32_t> FormatChannelSizeMap = {
			{ Format::Float1, 4 },
			{ Format::Float2, 4 },
			{ Format::Float3, 4 },
			{ Format::Float4, 4 },
			{ Format::Byte1, 1 },
			{ Format::Byte2, 1 },
			{ Format::Byte3, 1 },
			{ Format::Byte4, 1 }
		};
		return FormatChannelSizeMap.at(format);
	}

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
		BlendMode(Blend _color_src_blend, Blend _color_dst_blend, Blend _alpha_src_blend, Blend _alpha_dst_blend) :
			color_src_blend(_color_src_blend), color_dst_blend(_color_dst_blend), alpha_src_blend(_alpha_src_blend), alpha_dst_blend(_alpha_dst_blend)
		{}

		BlendMode(Blend srcBlend, Blend dstBlend) : BlendMode(srcBlend, dstBlend, srcBlend, dstBlend) { }

		BlendFunction color_blend_func = BlendFunction::Add;
		Blend color_src_blend;
		Blend color_dst_blend;

		BlendFunction alpha_blend_func = BlendFunction::Add;
		Blend alpha_src_blend;
		Blend alpha_dst_blend;

		ColorMask color_mask;
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
			left.color_blend_func == right.color_blend_func &&
			left.color_src_blend == right.color_src_blend &&
			left.color_dst_blend == right.color_dst_blend &&
			left.alpha_blend_func == right.alpha_blend_func &&
			left.alpha_src_blend == right.alpha_src_blend &&
			left.alpha_dst_blend == right.alpha_dst_blend &&
			left.color_mask == right.color_mask;
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

	enum class FrontFace
	{
		Clockwise,
		CounterClockwise
	};

	void Initialize(void* window, uint32_t width, uint32_t height, std::optional<BackendType> type = std::nullopt,
		const std::unordered_set<Feature>& features = {});
	void Finalize();

	void Resize(uint32_t width, uint32_t height);

	void SetVsync(bool value);
	bool IsVsyncEnabled();

	void SetTopology(Topology topology);
	void SetViewport(const std::optional<Viewport>& viewport);
	void SetScissor(const std::optional<Scissor>& scissor);
	void SetTexture(uint32_t binding, const Texture& texture);
	void SetRenderTarget(const RenderTarget& value);
	void SetRenderTarget(std::nullopt_t value);
	void SetShader(const Shader& shader);
	void SetShader(const RaytracingShader& shader);
	void SetVertexBuffer(const VertexBuffer& value);
	void SetIndexBuffer(const IndexBuffer& value);
	void SetUniformBuffer(uint32_t binding, const UniformBuffer& value);
	void SetAccelerationStructure(uint32_t binding, const AccelerationStructure& value);
	void SetBlendMode(const std::optional<BlendMode>& blend_mode);
	void SetDepthMode(const std::optional<DepthMode>& depth_mode);
	void SetStencilMode(const std::optional<StencilMode>& stencil_mode);
	void SetCullMode(CullMode cull_mode);
	void SetSampler(Sampler value);
	void SetTextureAddress(TextureAddress value);
	void SetFrontFace(FrontFace value);

	void Clear(const std::optional<glm::vec4>& color = glm::vec4{ 0.0f, 0.0f, 0.0f, 1.0f },
		const std::optional<float>& depth = 1.0f, const std::optional<uint8_t>& stencil = 0);
	void Draw(uint32_t vertex_count, uint32_t vertex_offset = 0);
	void DrawIndexed(uint32_t index_count, uint32_t index_offset = 0);
	void ReadPixels(const glm::i32vec2& pos, const glm::i32vec2& size, Texture& dst_texture);
	std::vector<uint8_t> GetPixels();

	void DispatchRays(uint32_t width, uint32_t height, uint32_t depth);

	void Present();

	void SetVertexBuffer(void* memory, size_t size, size_t stride);

	template<class T>
	void SetVertexBuffer(T* memory, size_t count)
	{
		SetVertexBuffer((void*)memory, count * sizeof(T), sizeof(T));
	}

	template<class T>
	void SetVertexBuffer(const std::vector<T>& values)
	{
		SetVertexBuffer(values.data(), values.size());
	}

	void SetIndexBuffer(void* memory, size_t size, size_t stride);

	template<class T>
	void SetIndexBuffer(T* memory, size_t count)
	{
		SetIndexBuffer((void*)memory, count * sizeof(T), sizeof(T));
	}

	template<class T>
	void SetIndexBuffer(const std::vector<T>& values)
	{
		SetIndexBuffer(values.data(), values.size());
	}

	void SetUniformBuffer(uint32_t binding,	void* memory, size_t size);

	template <class T>
	void SetUniformBuffer(uint32_t binding, const T& value)
	{
		SetUniformBuffer(binding, &const_cast<T&>(value), sizeof(T));
	}

	uint32_t GetWidth();
	uint32_t GetHeight();

	uint32_t GetBackbufferWidth();
	uint32_t GetBackbufferHeight();
	Format GetBackbufferFormat();

	BackendType GetBackendType();

	std::unordered_set<BackendType> GetAvailableBackends(const std::unordered_set<Feature>& features = {});
	std::optional<BackendType> GetDefaultBackend(const std::unordered_set<Feature>& features = {});
}

SKYGFX_MAKE_HASHABLE(skygfx::ColorMask,
	t.red,
	t.green,
	t.blue,
	t.alpha);

SKYGFX_MAKE_HASHABLE(skygfx::BlendMode,
	t.alpha_blend_func,
	t.alpha_dst_blend,
	t.alpha_src_blend,
	t.color_blend_func,
	t.color_dst_blend,
	t.color_src_blend,
	t.color_mask);

SKYGFX_MAKE_HASHABLE(skygfx::DepthMode,
	t.func);

SKYGFX_MAKE_HASHABLE(skygfx::StencilMode,
	t.read_mask,
	t.write_mask,
	t.depth_fail_op,
	t.fail_op,
	t.func,
	t.pass_op);
