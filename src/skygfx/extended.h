#pragma once

#include "skygfx.h"

namespace skygfx::extended
{
	class Mesh
	{
	public:
		using Vertex = Vertex::PositionColorTextureNormal;
		using Vertices = std::vector<Vertex>;
		using Indices = std::vector<uint32_t>;
		
		class VerticesBuilder;

		auto getTopology() const { return mTopology; }
		void setTopology(Topology value) { mTopology = value; }

		const auto& getVertices() const { return mVertices; }
		void setVertices(const Vertices& value) { mVertices = value; }

		const auto& getIndices() const { return mIndices; }
		void setIndices(const Indices& value) { mIndices = value; }

	private:
		Topology mTopology = Topology::TriangleList;
		Vertices mVertices;
		Indices mIndices;
	};
	
	struct alignas(16) Matrices
	{
		glm::mat4 projection = glm::mat4(1.0f);
		glm::mat4 view = glm::mat4(1.0f);
		glm::mat4 model = glm::mat4(1.0f);
	};

	void DrawMesh(const Mesh& mesh, const Matrices& matrices, const Texture& texture,
		const Scissor& scissor);
}
