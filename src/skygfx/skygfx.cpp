#include "skygfx.h"
#include "backend.h"
#include "backend_d3d11.h"
#include "backend_d3d12.h"
#include "backend_gl.h"
#include "backend_vk.h"
#include "backend_mtl.h"

using namespace skygfx;

static Backend* gBackend = nullptr;
static RaytracingBackend* gRaytracingBackend = nullptr;
static glm::u32vec2 gSize = { 0, 0 };
static bool gVsync = false;
static std::optional<glm::u32vec2> gRenderTargetSize;
static Format gBackbufferFormat;
static BackendType gBackendType = BackendType::OpenGL;
static std::optional<VertexBuffer> gVertexBuffer;
static std::optional<IndexBuffer> gIndexBuffer;
static std::unordered_map<uint32_t, UniformBuffer> gUniformBuffers;
static std::unordered_map<uint32_t, StorageBuffer> gStorageBuffers;

// texture

Texture::Texture(uint32_t width, uint32_t height, Format format, uint32_t mip_count) :
	mWidth(width),
	mHeight(height),
	mFormat(format),
	mMipCount(mip_count)
{
	assert(width > 0);
	assert(height > 0);
	assert(mip_count > 0);
	mTextureHandle = gBackend->createTexture(width, height, format, mip_count);
}

Texture::Texture(uint32_t width, uint32_t height, Format format, void* memory, bool generate_mips) :
	Texture(width, height, format, generate_mips ? GetMipCount(width, height) : 1)
{
	write(width, height, format, memory);

	if (generate_mips)
		generateMips();
}

Texture::Texture(Texture&& other) noexcept
{
	mTextureHandle = std::exchange(other.mTextureHandle, nullptr);
	mWidth = std::exchange(other.mWidth, 0);
	mHeight = std::exchange(other.mHeight, 0);
	mFormat = std::exchange(other.mFormat, {});
	mMipCount = std::exchange(other.mMipCount, 0);
}

Texture::~Texture()
{
	if (gBackend)
		gBackend->destroyTexture(mTextureHandle);
}

void Texture::write(uint32_t width, uint32_t height, Format format, void* memory,
	uint32_t mip_level, uint32_t offset_x, uint32_t offset_y)
{
	assert(width > 0);
	assert(height > 0);
	assert(offset_x + width <= GetMipWidth(mWidth, mip_level));
	assert(offset_y + height <= GetMipHeight(mHeight, mip_level));
	assert(mip_level < mMipCount);
	assert(memory != nullptr);
	gBackend->writeTexturePixels(mTextureHandle, width, height, format, memory, mip_level, offset_x, offset_y);
}

void Texture::read(uint32_t pos_x, uint32_t pos_y, uint32_t width, uint32_t height,
	uint32_t mip_level, void* dst_memory)
{
	assert(width > 0);
	assert(height > 0);
	assert(pos_x + width <= GetMipWidth(mWidth, mip_level));
	assert(pos_y + height <= GetMipHeight(mHeight, mip_level));
	assert(mip_level < mMipCount);
	gBackend->readTexturePixels(mTextureHandle, pos_x, pos_y, width, height, mip_level, dst_memory);
}

std::vector<uint8_t> Texture::read(uint32_t pos_x, uint32_t pos_y, uint32_t width, uint32_t height,
	uint32_t mip_level)
{
	auto channels_count = GetFormatChannelsCount(mFormat);
	auto channel_size = GetFormatChannelSize(mFormat);
	auto result = std::vector<uint8_t>(width * height * channels_count * channel_size);
	read(pos_x, pos_y, width, height, mip_level, result.data());
	return result;
}

void Texture::generateMips()
{
	gBackend->generateMips(mTextureHandle);
}

Texture& Texture::operator=(Texture&& other) noexcept
{
	if (this == &other)
		return *this;

	if (mTextureHandle)
		gBackend->destroyTexture(mTextureHandle);

	mTextureHandle = std::exchange(other.mTextureHandle, nullptr);
	mWidth = std::exchange(other.mWidth, 0);
	mHeight = std::exchange(other.mHeight, 0);
	mFormat = std::exchange(other.mFormat, {});
	mMipCount = std::exchange(other.mMipCount, 0);

	return *this;
}

RenderTarget::RenderTarget(uint32_t width, uint32_t height, Format format) : Texture(width, height, format, 1)
{
	mRenderTargetHandle = gBackend->createRenderTarget(width, height, *this);
}

RenderTarget::RenderTarget(RenderTarget&& other) noexcept : Texture(std::move(other))
{
	mRenderTargetHandle = std::exchange(other.mRenderTargetHandle, nullptr);
}

RenderTarget::~RenderTarget()
{
	if (gBackend)
		gBackend->destroyRenderTarget(mRenderTargetHandle);
}

RenderTarget& RenderTarget::operator=(RenderTarget&& other) noexcept
{
	Texture::operator=(std::move(other));

	if (this == &other)
		return *this;

	if (mRenderTargetHandle)
		gBackend->destroyRenderTarget(mRenderTargetHandle);

	mRenderTargetHandle = std::exchange(other.mRenderTargetHandle, nullptr);

	return *this;
}

// shader

Shader::Shader(const std::string& vertex_code, const std::string& fragment_code, const std::vector<std::string>& defines)
{
	mShaderHandle = gBackend->createShader(vertex_code, fragment_code, defines);
}

Shader::Shader(Shader&& other) noexcept
{
	if (mShaderHandle)
		gBackend->destroyShader(mShaderHandle);

	mShaderHandle = std::exchange(other.mShaderHandle, nullptr);
}

Shader::~Shader()
{
	if (gBackend)
		gBackend->destroyShader(mShaderHandle);
}

Shader& Shader::operator=(Shader&& other) noexcept
{
	if (this == &other)
		return *this;

	if (mShaderHandle)
		gBackend->destroyShader(mShaderHandle);

	mShaderHandle = std::exchange(other.mShaderHandle, nullptr);
	return *this;
}

// raytracing shader

RaytracingShader::RaytracingShader(const std::string& raygen_code, const std::vector<std::string>& miss_code,
	const std::string& closesthit_code, const std::vector<std::string>& defines)
{
	mRaytracingShaderHandle = gRaytracingBackend->createRaytracingShader(raygen_code, miss_code, closesthit_code, defines);
}

RaytracingShader::~RaytracingShader()
{
	if (gRaytracingBackend)
		gRaytracingBackend->destroyRaytracingShader(mRaytracingShaderHandle);
}

// buffer

Buffer::Buffer(size_t size) : mSize(size)
{
	assert(size > 0);
}

Buffer::Buffer(Buffer&& other) noexcept
{
	mSize = std::exchange(other.mSize, 0);
}

Buffer& Buffer::operator=(Buffer&& other) noexcept
{
	if (this == &other)
		return *this;

	mSize = std::exchange(other.mSize, 0);
	return *this;
}

// vertex buffer

VertexBuffer::VertexBuffer(size_t size, size_t stride) : Buffer(size)
{
	mVertexBufferHandle = gBackend->createVertexBuffer(size, stride);
}

VertexBuffer::VertexBuffer(void* memory, size_t size, size_t stride) : VertexBuffer(size, stride)
{
	write(memory, size, stride);
}

VertexBuffer::VertexBuffer(VertexBuffer&& other) noexcept : Buffer(std::move(other))
{
	if (mVertexBufferHandle)
		gBackend->destroyVertexBuffer(mVertexBufferHandle);

	mVertexBufferHandle = std::exchange(other.mVertexBufferHandle, nullptr);
}

VertexBuffer::~VertexBuffer()
{
	if (gBackend && mVertexBufferHandle)
		gBackend->destroyVertexBuffer(mVertexBufferHandle);
}

VertexBuffer& VertexBuffer::operator=(VertexBuffer&& other) noexcept
{
	Buffer::operator=(std::move(other));

	if (this == &other)
		return *this;

	if (mVertexBufferHandle)
		gBackend->destroyVertexBuffer(mVertexBufferHandle);

	mVertexBufferHandle = std::exchange(other.mVertexBufferHandle, nullptr);
	return *this;
}

void VertexBuffer::write(void* memory, size_t size, size_t stride)
{
	gBackend->writeVertexBufferMemory(mVertexBufferHandle, memory, size, stride);
}

// index buffer

IndexBuffer::IndexBuffer(size_t size, size_t stride) : Buffer(size)
{
	mIndexBufferHandle = gBackend->createIndexBuffer(size, stride);
}

IndexBuffer::IndexBuffer(void* memory, size_t size, size_t stride) : IndexBuffer(size, stride)
{
	write(memory, size, stride);
}

IndexBuffer::IndexBuffer(IndexBuffer&& other) noexcept : Buffer(std::move(other))
{
	mIndexBufferHandle = std::exchange(other.mIndexBufferHandle, nullptr);
}

IndexBuffer::~IndexBuffer()
{
	if (gBackend && mIndexBufferHandle)
		gBackend->destroyIndexBuffer(mIndexBufferHandle);
}

IndexBuffer& IndexBuffer::operator=(IndexBuffer&& other) noexcept
{
	Buffer::operator=(std::move(other));

	if (this == &other)
		return *this;

	if (mIndexBufferHandle)
		gBackend->destroyIndexBuffer(mIndexBufferHandle);

	mIndexBufferHandle = std::exchange(other.mIndexBufferHandle, nullptr);
	return *this;
}

void IndexBuffer::write(void* memory, size_t size, size_t stride)
{
	gBackend->writeIndexBufferMemory(mIndexBufferHandle, memory, size, stride);
}

// uniform buffer

UniformBuffer::UniformBuffer(size_t size) : Buffer(size)
{
	mUniformBufferHandle = gBackend->createUniformBuffer(size);
}

UniformBuffer::UniformBuffer(void* memory, size_t size) : UniformBuffer(size)
{
	write(memory, size);
}

UniformBuffer::UniformBuffer(UniformBuffer&& other) noexcept : Buffer(std::move(other))
{
	mUniformBufferHandle = std::exchange(other.mUniformBufferHandle, nullptr);
}

UniformBuffer::~UniformBuffer()
{
	if (gBackend && mUniformBufferHandle)
		gBackend->destroyUniformBuffer(mUniformBufferHandle);
}

UniformBuffer& UniformBuffer::operator=(UniformBuffer&& other) noexcept
{
	Buffer::operator=(std::move(other));

	if (this == &other)
		return *this;

	if (mUniformBufferHandle)
		gBackend->destroyUniformBuffer(mUniformBufferHandle);

	mUniformBufferHandle = std::exchange(other.mUniformBufferHandle, nullptr);
	return *this;
}

void UniformBuffer::write(void* memory, size_t size)
{
	gBackend->writeUniformBufferMemory(mUniformBufferHandle, memory, size);
}

// storage buffer

StorageBuffer::StorageBuffer(size_t size) : Buffer(size)
{
	mStorageBufferHandle = gRaytracingBackend->createStorageBuffer(size);
}

StorageBuffer::StorageBuffer(void* memory, size_t size) : StorageBuffer(size)
{
	write(memory, size);
}

StorageBuffer::StorageBuffer(StorageBuffer&& other) noexcept : Buffer(std::move(other))
{
	mStorageBufferHandle = std::exchange(other.mStorageBufferHandle, nullptr);
}

StorageBuffer::~StorageBuffer()
{
	if (gRaytracingBackend && mStorageBufferHandle)
		gRaytracingBackend->destroyStorageBuffer(mStorageBufferHandle);
}

StorageBuffer& StorageBuffer::operator=(StorageBuffer&& other) noexcept
{
	Buffer::operator=(std::move(other));

	if (this == &other)
		return *this;

	if (mStorageBufferHandle)
		gRaytracingBackend->destroyStorageBuffer(mStorageBufferHandle);

	mStorageBufferHandle = std::exchange(other.mStorageBufferHandle, nullptr);
	return *this;
}

void StorageBuffer::write(void* memory, size_t size)
{
	gRaytracingBackend->writeStorageBufferMemory(mStorageBufferHandle, memory, size);
}

// bottom level acceleration structure

BottomLevelAccelerationStructure::BottomLevelAccelerationStructure(void* vertex_memory, uint32_t vertex_count, uint32_t vertex_offset,
	uint32_t vertex_stride, void* index_memory, uint32_t index_count, uint32_t index_offset, uint32_t index_stride,
	const glm::mat4& transform)
{
	auto vertex_memory_with_offset = (void*)((size_t)vertex_memory + vertex_offset);
	auto index_memory_with_offset = (void*)((size_t)index_memory + index_offset);

	mBottomLevelAccelerationStructureHandle = gRaytracingBackend->createBottomLevelAccelerationStructure(vertex_memory_with_offset,
		vertex_count, vertex_stride, index_memory_with_offset, index_count, index_stride, transform);
}

BottomLevelAccelerationStructure::~BottomLevelAccelerationStructure()
{
	if (gRaytracingBackend && mBottomLevelAccelerationStructureHandle)
		gRaytracingBackend->destroyBottomLevelAccelerationStructure(mBottomLevelAccelerationStructureHandle);
}

// top level acceleration structure

TopLevelAccelerationStructure::TopLevelAccelerationStructure(
	const std::vector<std::tuple<uint32_t, BottomLevelAccelerationStructureHandle*>>& bottom_level_acceleration_structures)
{
	mTopLevelAccelerationStructureHandle = gRaytracingBackend->createTopLevelAccelerationStructure(bottom_level_acceleration_structures);
}

TopLevelAccelerationStructure::TopLevelAccelerationStructure(
	const std::vector<BottomLevelAccelerationStructureHandle*>& bottom_level_acceleration_structures) :
	TopLevelAccelerationStructure(CreateIndexedBlases(bottom_level_acceleration_structures))
{
}

TopLevelAccelerationStructure::~TopLevelAccelerationStructure()
{
	if (gRaytracingBackend && mTopLevelAccelerationStructureHandle)
		gRaytracingBackend->destroyTopLevelAccelerationStructure(mTopLevelAccelerationStructureHandle);
}

std::vector<std::tuple<uint32_t, BottomLevelAccelerationStructureHandle*>>
	TopLevelAccelerationStructure::CreateIndexedBlases(const std::vector<BottomLevelAccelerationStructureHandle*>& blases)
{
	std::vector<std::tuple<uint32_t, BottomLevelAccelerationStructureHandle*>> result;
	for (auto blas : blases)
	{
		result.push_back({ (uint32_t)result.size(), blas });
	}
	return result;
}

// helper functions

TopologyKind skygfx::GetTopologyKind(Topology topology)
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

uint32_t skygfx::GetFormatChannelsCount(Format format)
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

uint32_t skygfx::GetFormatChannelSize(Format format)
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

uint32_t skygfx::GetMipCount(uint32_t width, uint32_t height)
{
	return static_cast<uint32_t>(glm::floor(glm::log2(glm::max(width, height)))) + 1;
}

uint32_t skygfx::GetMipWidth(uint32_t base_width, uint32_t mip_level)
{
	return glm::max<uint32_t>(1, static_cast<uint32_t>(glm::floor<uint32_t>(base_width >> mip_level)));
}

uint32_t skygfx::GetMipHeight(uint32_t base_height, uint32_t mip_level)
{
	return glm::max<uint32_t>(1, static_cast<uint32_t>(glm::floor<uint32_t>(base_height >> mip_level)));
}

// input layout

InputLayout::Attribute::Attribute(Format _format, size_t _offset) :
	format(_format),
	offset(_offset)
{
}

InputLayout::InputLayout(Rate _rate, std::unordered_map<uint32_t, Attribute> _attributes) :
	rate(_rate),
	attributes(std::move(_attributes))
{
}

InputLayout::InputLayout(Rate _rate, const std::vector<InputLayout::Attribute>& _attributes) :
	rate(_rate)
{
	for (size_t i = 0; i < _attributes.size(); i++)
	{
		attributes.insert({ (uint32_t)i, _attributes.at(i) });
	}
}

// blend mode

BlendMode::BlendMode(Blend _color_src, Blend _color_dst, Blend _alpha_src, Blend _alpha_dst) :
	color_src(_color_src), color_dst(_color_dst), alpha_src(_alpha_src), alpha_dst(_alpha_dst)
{
}

BlendMode::BlendMode(Blend src, Blend dst) : BlendMode(src, dst, src, dst)
{
}

// depth bias

DepthBias::DepthBias(float _factor, float _units) :
	factor(_factor), units(_units)
{
}

// transient render targets

struct TransientRenderTargetDesc
{
	TransientRenderTargetDesc(uint32_t _width, uint32_t _height, Format _format) :
		width(_width), height(_height), format(_format)
	{
	}

	uint32_t width;
	uint32_t height;
	Format format;

	bool operator==(const TransientRenderTargetDesc& other) const = default;
};

SKYGFX_MAKE_HASHABLE(TransientRenderTargetDesc,
	t.width,
	t.height,
	t.format
);

class TransientRenderTarget : public RenderTarget
{
public:
	enum class State
	{
		Active,
		Inactive,
		Destroy
	};

public:
	using RenderTarget::RenderTarget;

public:
	auto getState() const { return mState; }
	void setState(State value) { mState = value; }

private:
	State mState = State::Active;
};

static std::unordered_map<TransientRenderTargetDesc, std::unordered_set<std::shared_ptr<TransientRenderTarget>>> gTransientRenderTargets;

RenderTarget* skygfx::AcquireTransientRenderTarget(uint32_t width, uint32_t height, Format format)
{
	auto desc = TransientRenderTargetDesc(width, height, format);

	for (auto& [_desc, transient_rts] : gTransientRenderTargets)
	{
		if (desc != _desc)
			continue;

		for (auto& transient_rt : transient_rts)
		{
			if (transient_rt->getState() == TransientRenderTarget::State::Active)
				continue;

			transient_rt->setState(TransientRenderTarget::State::Active);
			return transient_rt.get();
		}
	}

	auto transient_rt = std::make_shared<TransientRenderTarget>(width, height, format);
	gTransientRenderTargets[desc].insert(transient_rt);
	return transient_rt.get();
}

void skygfx::ReleaseTransientRenderTarget(RenderTarget* target)
{
	for (auto& [desc, transient_rts] : gTransientRenderTargets)
	{
		for (auto& transient_rt : transient_rts)
		{
			if (transient_rt.get() != target)
				continue;

			transient_rt->setState(TransientRenderTarget::State::Inactive);
			return;
		}
	}
	assert(false);
}

static void DestroyTransientRenderTargets()
{	
	for (auto& [desc, transient_rts] : gTransientRenderTargets)
	{
		std::erase_if(transient_rts, [](const std::shared_ptr<TransientRenderTarget>& transient_rt) {
			return transient_rt->getState() == TransientRenderTarget::State::Destroy;
		});

		for (auto& transient_rt : transient_rts)
		{
			transient_rt->setState(TransientRenderTarget::State::Destroy);
		}
	}

	std::erase_if(gTransientRenderTargets, [](const auto& pair) {
		return pair.second.empty();
	});
}

// device

void skygfx::Initialize(void* window, uint32_t width, uint32_t height, std::optional<BackendType> _type,
	Adapter adapter, const std::unordered_set<Feature>& features)
{
	assert(gBackend == nullptr);

	auto type = _type.value_or(GetDefaultBackend().value());

#ifdef SKYGFX_HAS_D3D11
	if (type == BackendType::D3D11)
		gBackend = new BackendD3D11(window, width, height, adapter);
#endif
#ifdef SKYGFX_HAS_D3D12
	if (type == BackendType::D3D12)
		gBackend = new BackendD3D12(window, width, height, adapter);
#endif
#ifdef SKYGFX_HAS_OPENGL
	if (type == BackendType::OpenGL)
		gBackend = new BackendGL(window, width, height, adapter);
#endif
#ifdef SKYGFX_HAS_VULKAN
	if (type == BackendType::Vulkan)
		gBackend = new BackendVK(window, width, height, adapter, features);
#endif
#ifdef SKYGFX_HAS_METAL
	if (type == BackendType::Metal)
		gBackend = new BackendMetal(window, width, height);
#endif

	if (gBackend == nullptr)
		throw std::runtime_error("backend not implemented");

	SetVsync(false);

	gSize = { width, height };
	gRenderTargetSize.reset();
	gBackendType = type;
	gBackbufferFormat = Format::Byte4;

	if (features.contains(Feature::Raytracing))
	{
		gRaytracingBackend = dynamic_cast<RaytracingBackend*>(gBackend);

		if (gRaytracingBackend == nullptr)
			throw std::runtime_error("this backend does not support raytracing");
	}
}

void skygfx::Finalize()
{
	assert(gBackend != nullptr);

	gIndexBuffer.reset();
	gVertexBuffer.reset();
	gUniformBuffers.clear();
	gStorageBuffers.clear();

	delete gBackend;
	gBackend = nullptr;

	if (gRaytracingBackend)
	{
		gRaytracingBackend = nullptr;
	}
}

void skygfx::Resize(uint32_t width, uint32_t height)
{
	gBackend->resize(width, height);
	gSize = { width, height };
}

void skygfx::SetVsync(bool value)
{
	gBackend->setVsync(value);
	gVsync = value;
}

bool skygfx::IsVsyncEnabled()
{
	return gVsync;
}

void skygfx::SetTopology(Topology topology)
{
	gBackend->setTopology(topology);
}

void skygfx::SetViewport(const std::optional<Viewport>& viewport)
{
	gBackend->setViewport(viewport);
}

void skygfx::SetScissor(const std::optional<Scissor>& scissor)
{
	gBackend->setScissor(scissor);
}

void skygfx::SetTexture(uint32_t binding, const Texture& texture)
{
	gBackend->setTexture(binding, const_cast<Texture&>(texture));
}

void skygfx::SetRenderTarget(const RenderTarget& value)
{
	SetRenderTarget({ (RenderTarget*)&value });
}

void skygfx::SetRenderTarget(const std::vector<RenderTarget*>& value)
{
	std::vector<RenderTargetHandle*> handles;
	for (auto target : value)
	{
		handles.push_back(*target);
	}
	gBackend->setRenderTarget(handles);
	gRenderTargetSize = { value.at(0)->getWidth(), value.at(0)->getHeight()};
	gBackbufferFormat = value.at(0)->getFormat(); // TODO: wtf when mrt
}

void skygfx::SetRenderTarget(std::nullopt_t value)
{
	gBackend->setRenderTarget(value);
	gRenderTargetSize.reset();
	gBackbufferFormat = Format::Byte4;
}

void skygfx::SetShader(const Shader& shader)
{
	gBackend->setShader(const_cast<Shader&>(shader));
}

void skygfx::SetShader(const RaytracingShader& shader)
{
	gRaytracingBackend->setRaytracingShader(const_cast<RaytracingShader&>(shader));
}

void skygfx::SetInputLayout(const InputLayout& value)
{
	gBackend->setInputLayout({ value });
}

void skygfx::SetInputLayout(const std::vector<InputLayout>& value)
{
	gBackend->setInputLayout(value);
}

void skygfx::SetVertexBuffer(const VertexBuffer& value)
{
	SetVertexBuffer({ (VertexBuffer*)&value });
}

void skygfx::SetVertexBuffer(const std::vector<VertexBuffer*>& value)
{
	std::vector<VertexBufferHandle*> handles;
	for (auto target : value)
	{
		handles.push_back(*target);
	}
	gBackend->setVertexBuffer(handles);
}

void skygfx::SetIndexBuffer(const IndexBuffer& value)
{
	gBackend->setIndexBuffer(const_cast<IndexBuffer&>(value));
}

void skygfx::SetUniformBuffer(uint32_t binding, const UniformBuffer& value)
{
	gBackend->setUniformBuffer(binding, const_cast<UniformBuffer&>(value));
}

void skygfx::SetStorageBuffer(uint32_t binding, const StorageBuffer& value)
{
	gRaytracingBackend->setStorageBuffer(binding, const_cast<StorageBuffer&>(value));
}

void skygfx::SetAccelerationStructure(uint32_t binding, const TopLevelAccelerationStructure& value)
{
	gRaytracingBackend->setAccelerationStructure(binding, const_cast<TopLevelAccelerationStructure&>(value));
}

void skygfx::SetBlendMode(const std::optional<BlendMode>& blend_mode)
{
	gBackend->setBlendMode(blend_mode);
}

void skygfx::SetDepthMode(const std::optional<DepthMode>& depth_mode)
{
	gBackend->setDepthMode(depth_mode);
}

void skygfx::SetStencilMode(const std::optional<StencilMode>& stencil_mode)
{
	gBackend->setStencilMode(stencil_mode);
}

void skygfx::SetCullMode(CullMode cull_mode)
{
	gBackend->setCullMode(cull_mode);
}

void skygfx::SetSampler(Sampler value)
{
	gBackend->setSampler(value);
}

void skygfx::SetTextureAddress(TextureAddress value)
{
	gBackend->setTextureAddress(value);
}

void skygfx::SetFrontFace(FrontFace value)
{
	gBackend->setFrontFace(value);
}

void skygfx::SetDepthBias(const std::optional<DepthBias> depth_bias)
{
	gBackend->setDepthBias(depth_bias);
}

void skygfx::Clear(const std::optional<glm::vec4>& color, const std::optional<float>& depth,
	const std::optional<uint8_t>& stencil)
{
	gBackend->clear(color, depth, stencil);
}

void skygfx::Draw(uint32_t vertex_count, uint32_t vertex_offset, uint32_t instance_count)
{
	gBackend->draw(vertex_count, vertex_offset, instance_count);
}

void skygfx::DrawIndexed(uint32_t index_count, uint32_t index_offset, uint32_t instance_count)
{
	gBackend->drawIndexed(index_count, index_offset, instance_count);
}

void skygfx::ReadPixels(const glm::i32vec2& pos, const glm::i32vec2& size, Texture& dst_texture)
{
	gBackend->readPixels(pos, size, dst_texture);
}

void skygfx::DispatchRays(uint32_t width, uint32_t height, uint32_t depth)
{
	gRaytracingBackend->dispatchRays(width, height, depth);
}

void skygfx::Present()
{
	gBackend->present();
	DestroyTransientRenderTargets();
}

void skygfx::SetVertexBuffer(void* memory, size_t size, size_t stride)
{
	assert(size > 0);

	size_t vertex_buffer_size = 0;

	if (gVertexBuffer.has_value())
		vertex_buffer_size = gVertexBuffer->getSize();

	if (vertex_buffer_size < size)
		gVertexBuffer.emplace(memory, size, stride);
	else
		gVertexBuffer.value().write(memory, size, stride);

	SetVertexBuffer(gVertexBuffer.value());
}

void skygfx::SetIndexBuffer(void* memory, size_t size, size_t stride)
{
	assert(size > 0);

	size_t index_buffer_size = 0;

	if (gIndexBuffer.has_value())
		index_buffer_size = gIndexBuffer->getSize();

	if (index_buffer_size < size)
		gIndexBuffer.emplace(memory, size, stride);
	else
		gIndexBuffer.value().write(memory, size, stride);

	SetIndexBuffer(gIndexBuffer.value());
}

void skygfx::SetUniformBuffer(uint32_t binding, void* memory, size_t size)
{
	assert(size > 0);

	if (!gUniformBuffers.contains(binding))
		gUniformBuffers.emplace(binding, size);

	auto& buffer = gUniformBuffers.at(binding);

	if (buffer.getSize() < size)
		buffer = UniformBuffer(size);

	buffer.write(memory, size);

	SetUniformBuffer(binding, buffer);
}

void skygfx::SetStorageBuffer(uint32_t binding, void* memory, size_t size)
{
	assert(size > 0);

	if (!gStorageBuffers.contains(binding))
		gStorageBuffers.emplace(binding, size);

	auto& buffer = gStorageBuffers.at(binding);

	if (buffer.getSize() < size)
		buffer = StorageBuffer(size);

	buffer.write(memory, size);

	SetStorageBuffer(binding, buffer);
}

uint32_t skygfx::GetWidth()
{
	return gSize.x;
}

uint32_t skygfx::GetHeight()
{
	return gSize.y;
}

uint32_t skygfx::GetBackbufferWidth()
{
	return gRenderTargetSize.value_or(gSize).x;
}

uint32_t skygfx::GetBackbufferHeight()
{
	return gRenderTargetSize.value_or(gSize).y;
}

Format skygfx::GetBackbufferFormat()
{
	return gBackbufferFormat;
}

std::vector<uint8_t> skygfx::GetBackbufferPixels()
{
	auto width = GetBackbufferWidth();
	auto height = GetBackbufferHeight();
	auto format = GetBackbufferFormat();
	auto texture = Texture(width, height, format, 1);
	ReadPixels({ 0, 0 }, { width, height }, texture);
	return texture.read(0, 0, width, height, 0);
}

BackendType skygfx::GetBackendType()
{
	return gBackendType;
}

std::unordered_set<BackendType> skygfx::GetAvailableBackends(const std::unordered_set<Feature>& features)
{
	static const std::unordered_map<Feature, std::unordered_set<BackendType>> FeatureCoverageMap = {
		{ Feature::Raytracing, { BackendType::Vulkan } }
	};

	static const std::unordered_set<BackendType> AvailableBackendsForPlatform = {
#ifdef SKYGFX_HAS_D3D11
		BackendType::D3D11,
#endif
#ifdef SKYGFX_HAS_D3D12
		BackendType::D3D12,
#endif
#ifdef SKYGFX_HAS_OPENGL
		BackendType::OpenGL,
#endif
#ifdef SKYGFX_HAS_VULKAN
		BackendType::Vulkan,
#endif
#ifdef SKYGFX_HAS_METAL
		BackendType::Metal,
#endif
	};

	if (features.empty())
		return AvailableBackendsForPlatform;

	std::unordered_set<BackendType> result;

	std::copy_if(AvailableBackendsForPlatform.begin(), AvailableBackendsForPlatform.end(), std::inserter(result, result.end()), [&](auto backend) {
		return std::all_of(features.begin(), features.end(), [&](auto feature) {
			return FeatureCoverageMap.contains(feature) && FeatureCoverageMap.at(feature).contains(backend);
		});
	});

	return result;
}

std::optional<BackendType> skygfx::GetDefaultBackend(const std::unordered_set<Feature>& features)
{
	static const auto backends_by_priority = {
		BackendType::D3D11,
		BackendType::OpenGL,
		BackendType::Metal,
		BackendType::D3D12,
		BackendType::Vulkan
	};

	auto backends = GetAvailableBackends(features);
	
	for (auto backend : backends_by_priority)
	{
		if (!backends.contains(backend))
			continue;
			
		return backend;
	}

	return std::nullopt;
}
