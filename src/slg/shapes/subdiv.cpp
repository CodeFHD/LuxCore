/***************************************************************************
 * Copyright 1998-2020 by authors (see AUTHORS.txt)                        *
 *                                                                         *
 *   This file is part of LuxCoreRender.                                   *
 *                                                                         *
 * Licensed under the Apache License, Version 2.0 (the "License");         *
 * you may not use this file except in compliance with the License.        *
 * You may obtain a copy of the License at                                 *
 *                                                                         *
 *     http://www.apache.org/licenses/LICENSE-2.0                          *
 *                                                                         *
 * Unless required by applicable law or agreed to in writing, software     *
 * distributed under the License is distributed on an "AS IS" BASIS,       *
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.*
 * See the License for the specific language governing permissions and     *
 * limitations under the License.                                          *
 ***************************************************************************/

#include <unordered_map>
#include <format>

#include <opensubdiv/far/topologyDescriptor.h>
#include <opensubdiv/far/patchMap.h>
#include <opensubdiv/far/patchTable.h>
#include <opensubdiv/far/patchTableFactory.h>
#include <opensubdiv/far/stencilTableFactory.h>
#include <opensubdiv/far/topologyRefinerFactory.h>
#include <opensubdiv/far/primvarRefiner.h>
#include <opensubdiv/osd/cpuPatchTable.h>
#include <opensubdiv/osd/cpuVertexBuffer.h>
#include <opensubdiv/far/ptexIndices.h>
#include <opensubdiv/osd/tbbEvaluator.h>
#define OSD_EVALUATOR Osd::TbbEvaluator

#include <omp.h>

#include "luxrays/core/exttrianglemesh.h"
#include "slg/shapes/subdiv.h"
#include "slg/cameras/camera.h"
#include "slg/scene/scene.h"




namespace {
// Everything inside this namespace is local to this translation
// unit (behaves like static)

using namespace luxrays;
using namespace slg;
using namespace OpenSubdiv;

using BufferPtr = std::unique_ptr<Osd::CpuVertexBuffer>;
using StencilTablePtr = std::unique_ptr<const Far::StencilTable>;
using PatchTablePtr = std::unique_ptr<Far::PatchTable>;
using PatchMapPtr = std::unique_ptr<Far::PatchMap>;
using TopologyRefinerPtr = std::unique_ptr<Far::TopologyRefiner>;
using PtexIndicesPtr = std::unique_ptr<Far::PtexIndices>;

//////////////////////////
//  Simple subdivision  //
//////////////////////////

// This is a "simple" implementation (not so simple, however), with the
// following characteristics:
// - Subdivision is uniform (no special focus on singularities)
// - Tessellation is directly derivated from subdivision.
// - Implementation is partly mono-threaded

namespace simple {

struct Edge {
	Edge(const u_int v0Index, const u_int v1Index) {
		if (v0Index <= v1Index) {
			vIndex[0] = v0Index;
			vIndex[1] = v1Index;
		} else {
			vIndex[0] = v1Index;
			vIndex[1] = v0Index;
		}
	}

	bool operator==(const Edge &edge) const {
		return (vIndex[0] == edge.vIndex[0]) && (vIndex[1] == edge.vIndex[1]);
	}

	u_int vIndex[2];
};

class EdgeHashFunction {
public:
	size_t operator()(const Edge &edge) const {
		return (edge.vIndex[0] * 0x1f1f1f1fu) ^ edge.vIndex[1];
	}
};

template <int DIMENSIONS> static BufferPtr
BuildBuffer(
	StencilTablePtr& stencilTable,  // Stencil table
	const float *data,  // Primvar data
	const int nCoarse,  // Coarse vertex count
	const int nRefined  // Refined vertex count
)
{
    BufferPtr buffer(
		Osd::CpuVertexBuffer::Create(DIMENSIONS, nCoarse + nRefined)
	);

	// Pack the primvar data at the start of the vertex buffer
	// and update every time primvar data changes
	buffer->UpdateData(data, 0, nCoarse);

    Osd::BufferDescriptor desc(0, DIMENSIONS, DIMENSIONS);
    Osd::BufferDescriptor newDesc(nCoarse * DIMENSIONS, DIMENSIONS, DIMENSIONS);

	// Refine points (coarsePoints -> refinedPoints)
	OSD_EVALUATOR::EvalStencils(
		buffer.get(),
		desc,
		buffer.get(),
		newDesc,
		stencilTable.get()
	);

	return buffer;
}


static Far::TopologyRefiner * createFarTopologyRefiner(const ExtTriangleMesh*);

// TODO
static ExtTriangleMesh *ApplySubdivAdaptive(
	ExtTriangleMesh *srcMesh,
	const u_int maxLevel
);

static ExtTriangleMesh *ApplyAdaptiveSubdiv(
	ExtTriangleMesh *srcMesh,
	const u_int maxLevel
);



ExtTriangleMesh *ApplySubdiv(ExtTriangleMesh *srcMesh, const u_int maxLevel) {
	//--------------------------------------------------------------------------
	// Refine topology
	//--------------------------------------------------------------------------
	// https://graphics.pixar.com/opensubdiv/docs/osd_tutorial_0.html

	// Set-up refiner
	std::unique_ptr<Far::TopologyRefiner> refiner(createFarTopologyRefiner(srcMesh));

	// Refine the topology up to 'maxlevel'
	SDL_LOG("Subdiv - Refining (uniform)");
	Far::TopologyRefiner::UniformOptions refiner_options(maxLevel);
	refiner_options.fullTopologyInLastLevel = true;
	refiner->RefineUniform(refiner_options);

	Far::TopologyLevel const& refFirstLevel = refiner->GetLevel(0);
	Far::TopologyLevel const& refLastLevel = refiner->GetLevel(maxLevel);

	// Check validity
	if (refiner->GetMaxLevel() < maxLevel) {
		SDL_LOG("WARNING - SUBDIVISION FAILURE");
		SDL_LOG("'maxlevel' may be too high, please check. Continuing with input mesh...");
		return srcMesh;
	}

	// Get topology constants
	const int nCoarseVerts = refFirstLevel.GetNumVertices(); // Coarse vertex count
	const int nCoarseFaces = refFirstLevel.GetNumFaces(); // Coarse face count
	const int nRefinedVerts = refLastLevel.GetNumVertices(); // Refined vertex count
	const int nRefinedFaces = refLastLevel.GetNumFaces(); // Coarse face count
	const int totalVertsCount = nCoarseVerts + nRefinedVerts;  // Total vertex count

	SDL_LOG("Subdiv - Refined vertices: " << nRefinedVerts);
	SDL_LOG("Subdiv - Refined triangles: " << nRefinedFaces);


	//--------------------------------------------------------------------------
	// Create stencil and patch tables
	//--------------------------------------------------------------------------

	StencilTablePtr stencilTable;
	PatchTablePtr patchTable;

	{
		// Create stencil table
		Far::StencilTableFactory::Options stencilOptions;
		stencilOptions.generateOffsets = true;
		stencilOptions.generateIntermediateLevels = false;
		stencilOptions.maxLevel = maxLevel;

		stencilTable = StencilTablePtr(
			Far::StencilTableFactory::Create(*refiner, stencilOptions)
		);

		// Create patch table
		Far::PatchTableFactory::Options patchOptions;
		patchOptions.SetEndCapType(
				Far::PatchTableFactory::Options::ENDCAP_BSPLINE_BASIS
		);

		patchTable = PatchTablePtr(Far::PatchTableFactory::Create(*refiner, patchOptions));

		// Append local point stencils
		const auto *localPointStencilTable = patchTable->GetLocalPointStencilTable();
		if (localPointStencilTable) {
			SDL_LOG("Subdiv - Handling local points");
			StencilTablePtr combinedTable(
				Far::StencilTableFactory::AppendLocalPointStencilTable(
					*refiner, stencilTable.get(), localPointStencilTable
				)
			);
			if (combinedTable) {
				std::swap(stencilTable, combinedTable);
			}
		}
	}

	SDL_LOG("Subdiv - Stencil and patch tables created");

	//--------------------------------------------------------------------------
	// Set buffers and evaluate primvar from stencils
	//--------------------------------------------------------------------------


	// Vertices
	auto vertsBuffer = BuildBuffer<3>(
		stencilTable,
		(const float *)srcMesh->GetVertices(),
		nCoarseVerts,
		nRefinedVerts
	);

	// Normals
    BufferPtr normsBuffer;
	if (srcMesh->HasNormals()) {
        normsBuffer = BuildBuffer<3>(
			stencilTable,
			(const float *)srcMesh->GetNormals(),
			nCoarseVerts,
			nRefinedVerts
		);
	}

	// UVs
	std::vector<BufferPtr> uvsBuffers(EXTMESH_MAX_DATA_COUNT);
	for (u_int i = 0; i < EXTMESH_MAX_DATA_COUNT; i++) {
		if (srcMesh->HasUVs(i)) {
			uvsBuffers[i] = BuildBuffer<2>(
				stencilTable,
				(const float *)srcMesh->GetUVs(i),
				nCoarseVerts,
nRefinedVerts
			);
		}
	}

	// Colors
	std::vector<BufferPtr> colsBuffers(EXTMESH_MAX_DATA_COUNT);
	for (u_int i = 0; i < EXTMESH_MAX_DATA_COUNT; i++) {
		if (srcMesh->HasColors(i)) {
			colsBuffers[i] = BuildBuffer<3>(
				stencilTable,
				(const float *)srcMesh->GetColors(i),
				nCoarseVerts,
				nRefinedVerts
			);
		}
	}

	// Alphas
	std::vector<BufferPtr> alphasBuffers(EXTMESH_MAX_DATA_COUNT);
	if (srcMesh->HasAlphas(0)) {
		for (u_int i = 0; i < EXTMESH_MAX_DATA_COUNT; i++) {
			alphasBuffers[i] = BuildBuffer<1>(
				stencilTable,
				(const float *)srcMesh->GetAlphas(i),
				nCoarseVerts,
				nRefinedVerts
			);
		}
	}

	// VertAOVs
	std::vector<BufferPtr> vertAOVSsBuffers(EXTMESH_MAX_DATA_COUNT);
	for (u_int i = 0; i < EXTMESH_MAX_DATA_COUNT; i++) {
		if (srcMesh->HasVertexAOV(i)) {
			for (u_int i = 0; i < EXTMESH_MAX_DATA_COUNT; i++) {
				vertAOVSsBuffers[i] = BuildBuffer<1>(
					stencilTable,
					(const float *)srcMesh->GetVertexAOVs(i),
					nCoarseVerts,
					nRefinedVerts
				);
			}
		}
	}

	SDL_LOG("Subdiv - Buffers built");

	//--------------------------------------------------------------------------
	// Build the new mesh
	//--------------------------------------------------------------------------

	// New triangles
	Triangle *newTris = TriangleMesh::AllocTrianglesBuffer(nRefinedFaces);
	for (int face = 0; face < nRefinedFaces; ++face) {
		Vtr::ConstIndexArray faceVerts = refLastLevel.GetFaceVertices(face);
		for (u_int vertex = 0; vertex < 3; ++vertex) {
			newTris[face].v[vertex] = faceVerts[vertex];
		}
	}

	// New vertices
	Point *newVerts = TriangleMesh::AllocVerticesBuffer(nRefinedVerts);
	const float *refinedVerts = vertsBuffer->BindCpuBuffer() + 3 * nCoarseVerts;
	std::copy(refinedVerts, refinedVerts + 3 * nRefinedVerts, &newVerts->x);

	// New normals
	Normal *newNorms = nullptr;
	if (srcMesh->HasNormals()) {
		newNorms = new Normal[nRefinedVerts];
		const float *refinedNorms = normsBuffer->BindCpuBuffer() + 3 * nCoarseVerts;
		std::copy(refinedNorms, refinedNorms + 3 * nRefinedVerts, &newNorms->x);
	}

	// New UVs
	std::array<UV *, EXTMESH_MAX_DATA_COUNT> newUVs;
	for (u_int i = 0; i < EXTMESH_MAX_DATA_COUNT; i++) {
		if (srcMesh->HasUVs(i)) {
			newUVs[i] = new UV[nRefinedVerts];

			const float *refinedUVs = uvsBuffers[i]->BindCpuBuffer() + 2 * nCoarseVerts;
			std::copy(refinedUVs, refinedUVs + 2 * nRefinedVerts, &newUVs[i]->u);
		} else
			newUVs[i] = nullptr;
	}

	// New colors
	std::array<Spectrum *, EXTMESH_MAX_DATA_COUNT> newCols;
	for (u_int i = 0; i < EXTMESH_MAX_DATA_COUNT; i++) {
		if (srcMesh->HasColors(i)) {
			newCols[i] = new Spectrum[nRefinedVerts];

			const float *refinedCols = colsBuffers[i]->BindCpuBuffer() + 3 * nCoarseVerts;
			std::copy(refinedCols, refinedCols + 3 * nRefinedVerts, &newCols[i]->c[0]);
		} else
			newCols[i] = nullptr;
	}

	// New alphas
	std::array<float *, EXTMESH_MAX_DATA_COUNT> newAlphas;
	for (u_int i = 0; i < EXTMESH_MAX_DATA_COUNT; i++) {
		if (srcMesh->HasAlphas(i)) {
			newAlphas[i] = new float[nRefinedVerts];

			const float *refinedAlphas = alphasBuffers[i]->BindCpuBuffer() + 1 * nCoarseVerts;
			std::copy(refinedAlphas, refinedAlphas + 1 * nRefinedVerts, newAlphas[i]);
		} else
			newAlphas[i] = nullptr;
	}

	// New vertAOVs
	std::array<float *, EXTMESH_MAX_DATA_COUNT> newVertAOVs;
	for (u_int i = 0; i < EXTMESH_MAX_DATA_COUNT; i++) {
		if (srcMesh->HasVertexAOV(i)) {
			newVertAOVs[i] = new float[nRefinedVerts];

			const float *refinedVertAOVs = alphasBuffers[i]->BindCpuBuffer() + 1 * nCoarseVerts;
			std::copy(refinedVertAOVs, refinedVertAOVs + 1 * nRefinedVerts, newVertAOVs[i]);
		} else
			newVertAOVs[i] = nullptr;
	}

	// Allocate the new mesh
	ExtTriangleMesh *newMesh =  new ExtTriangleMesh(
		nRefinedVerts, nRefinedFaces,
		newVerts, newTris, newNorms,
		&newUVs, &newCols, &newAlphas
	);

	for (u_int i = 0; i < EXTMESH_MAX_DATA_COUNT; i++) {
		newMesh->SetVertexAOV(i, newVertAOVs[i]);
	}

	return newMesh;
}

static Far::TopologyRefiner* createFarTopologyRefiner(const ExtTriangleMesh* srcMesh)
{
	// Set topology descriptor
	Far::TopologyDescriptor desc;
	desc.numVertices = srcMesh->GetTotalVertexCount();
	desc.numFaces = srcMesh->GetTotalTriangleCount();
	std::vector<int> vertPerFace(desc.numFaces, 3);
	desc.numVertsPerFace = &vertPerFace[0];
	desc.vertIndicesPerFace = reinterpret_cast<const int *>(srcMesh->GetTriangles());

	// Look for mesh boundary edges
	std::unordered_map<Edge, u_int, EdgeHashFunction> edgesMap;
	const u_int triCount = srcMesh->GetTotalTriangleCount();
	const Triangle *tris = srcMesh->GetTriangles();

	// Count how many times an edge is shared
	for (u_int i = 0; i < triCount; ++i) {
		const Triangle &tri = tris[i];

		const Edge edge0(tri.v[0], tri.v[1]);
		if (edgesMap.find(edge0) != edgesMap.end())
			edgesMap[edge0] += 1;
		else
			edgesMap[edge0] = 1;

		const Edge edge1(tri.v[1], tri.v[2]);
		if (edgesMap.find(edge1) != edgesMap.end())
			edgesMap[edge1] += 1;
		else
		   edgesMap[edge1] = 1;

		const Edge edge2(tri.v[2], tri.v[0]);
		if (edgesMap.find(edge2) != edgesMap.end())
		   edgesMap[edge2] += 1;
		else
			edgesMap[edge2] = 1;
	}

	std::vector<bool> isBoundaryVertex(srcMesh->GetTotalVertexCount(), false);
	std::vector<Far::Index> cornerVertexIndices;
	std::vector<float> cornerWeights;
	for (auto em : edgesMap) {
		if (em.second == 1) {
			// It is a boundary edge

			const Edge &e = em.first;

			if (!isBoundaryVertex[e.vIndex[0]]) {
				cornerVertexIndices.push_back(e.vIndex[0]);
				cornerWeights.push_back(10.f);
				isBoundaryVertex[e.vIndex[0]] = true;
			}

			if (!isBoundaryVertex[e.vIndex[1]]) {
				cornerVertexIndices.push_back(e.vIndex[1]);
				cornerWeights.push_back(10.f);
				isBoundaryVertex[e.vIndex[1]] = true;
			}
		}
	}

	// Initialize TopologyDescriptor corners if I have some
	if (cornerVertexIndices.size() > 0) {
		assert(cornerVertexIndices.size() <= std::numeric_limits<int>::max());
		desc.numCorners = cornerVertexIndices.size();
		desc.cornerVertexIndices = &cornerVertexIndices[0];
		desc.cornerWeights = &cornerWeights[0];
	}

	// Set topology refiner factory's type & options
	Sdc::SchemeType type = Sdc::SCHEME_LOOP;
	Sdc::Options options;
	options.SetVtxBoundaryInterpolation(Sdc::Options::VTX_BOUNDARY_EDGE_AND_CORNER);

	// Instantiate a Far::TopologyRefiner from the descriptor
	using RefinerFactory = Far::TopologyRefinerFactory<Far::TopologyDescriptor>;
	auto *refiner = RefinerFactory::Create(
		desc,
		RefinerFactory::Options(type, options)
	);

	return refiner;
}

}  // ~namespace simple

////////////////////////////////////////////////////////////////////////////
//						Enhanced mode
//
// Enhanced mode characteristics:
// - Separate control on subdivision and tessellation
// - Adaptive subdivision (nota bene: in the sense of opensubdiv, not Blender)
// - Parallelized tessellation
// - Parallelized evaluation
//
// We use opensubdiv concepts and vocabulary:
// - Subdivision is NOT tessellation
// - Positions are NOT coordinates
// (please read the docs)

namespace enhanced {

template <size_t DIMENSION> struct SubdivVec: std::array<float, DIMENSION> {
	// Minimal required interface
	SubdivVec() {}

	SubdivVec(const float * src) {
		for (size_t i = 0; i < DIMENSION; ++i) {
			(*this)[i] = *(src + i);
		}
	}
	SubdivVec(std::initializer_list<float> src) {
		assert(src.size() == DIMENSION);
		for (int i=0; auto e: src) {
			(*this)[i++] = e;
		}
	}

	void Clear(void* = nullptr) {
		for (size_t i = 0; i < DIMENSION; ++i) {
			(*this)[i] = 0.0f;
		}
	}

	void AddWithWeight(const SubdivVec<DIMENSION>& src, float weight) {
		for (size_t i = 0; i < DIMENSION; ++i) {
			(*this)[i] += src[i] * weight;
		}
	}

};

using Pos = SubdivVec<3>;

// Vector product
Pos operator * (Pos u, Pos v) {
	Pos r;
	r[0] = u[1] * v[2] - u[2] * v[1];
	r[1] = u[2] * v[0] - u[0] * v[2];
	r[2] = u[0] * v[1] - u[1] * v[0];

	return r;
}




template<size_t DIMENSION>
struct SubdivBuffer: std::vector< SubdivVec<DIMENSION> > {
	SubdivBuffer() {};
	SubdivBuffer(const float * src, size_t count) {
		this->resize(count);
		for (size_t i = 0; i < count; ++i) {
			(*this)[i] = SubdivVec<3>(src + i * DIMENSION);
		}
	}
	SubdivBuffer(size_t count):
		std::vector< SubdivVec<DIMENSION> >(count)
	{}
	operator SubdivVec<DIMENSION>*() {
		return &(*this)[0];
	}
	operator const float * () {
		return reinterpret_cast<float *>(&(*this)[0]);
	}
	void pack_front(const float * src, size_t count) {
		assert(count <= this->size());
		for (size_t i = 0; i < count; ++i) {
			(*this)[i] = SubdivVec<3>(src + i * DIMENSION);
		}
	}
	void pack_front(const SubdivBuffer& src) {
		for (size_t i = 0; i < src.size(); ++i) {
			(*this)[i] = src[i];
		}
	}
};

using PosVector = SubdivBuffer<3>;


struct Tri: std::array<int, 3> {
	Tri() { }
	Tri(int a, int b, int c) { (*this)[0] = a, (*this)[1] = b, (*this)[2] = c; }

};
typedef std::vector<Tri> TriVector;


// Storage of local coordinates (coordinates in a given face) during
// tessellation.
// We temporarily store new positions as (face, i, j), with
// i <= N and j <= N (N being tessellation rate)

struct LocalCoords {
	int face;
	int i;
	int j;
	LocalCoords(int p_face=0, int p_i=0, int p_j=0):
		face(p_face), i(p_i), j(p_j)
	{ }

	static LocalCoords interpolate(LocalCoords p0, LocalCoords p1, u_int weight, u_int total) {
		assert(p0.face == p1.face);
		assert(weight <= total);

		LocalCoords res;
		res.face = p0.face;
		res.i = (weight * p1.i + (total - weight) * p0.i) / total;
		res.j = (weight * p1.j + (total - weight) * p0.j) / total;
		return res;
	}
};

typedef std::vector<LocalCoords> CoordVector;

// Storage of local coordinates in a given edge (for points belonging to an edge)
struct EdgeCoords {
	int edge;  // Edge number in opensubdiv topology
	int v0;  // First edge vertex in osd topology
	int v1;  // Second edge vertex in osd topology
	int x;  // Point coordinate on the segment
	EdgeCoords(int p_edge, int p_v0, int p_v1, int p_x):
		edge(p_edge), v0(p_v0), v1(p_v1), x(p_x)
	{ }
};

TopologyRefinerPtr createTopologyAdaptiveRefiner(
		const PosVector& positions,
		const TriVector& triangles,
		const Far::PatchTableFactory::Options& patchOptions
) {

	// Be sure patch options were intialized with the desired max level
	auto adaptiveOptions(patchOptions.GetRefineAdaptiveOptions());
	assert(adaptiveOptions.useInfSharpPatch == patchOptions.useInfSharpPatch);

	// Set topology refiner factory's type & options
	Sdc::SchemeType scheme_type = Sdc::SCHEME_LOOP;

	Sdc::Options sdc_options;
	sdc_options.SetVtxBoundaryInterpolation(Sdc::Options::VTX_BOUNDARY_EDGE_AND_CORNER);

	// Populate a topology descriptor with our raw data
	Far::TopologyDescriptor desc;
	desc.numVertices = positions.size(); // srcMesh->GetTotalVertexCount();
	desc.numFaces = triangles.size(); // srcMesh->GetTotalTriangleCount();
	std::vector<int> vertPerFace(desc.numFaces, 3);
	desc.numVertsPerFace = &vertPerFace[0];
	desc.vertIndicesPerFace = reinterpret_cast<const int *>(&triangles[0]);

	// Create refiner
	using RefinerFactory = Far::TopologyRefinerFactory<Far::TopologyDescriptor>;
    TopologyRefinerPtr refiner(
		RefinerFactory::Create(
			desc,
			RefinerFactory::Options(scheme_type, sdc_options)
		)
	);
	assert(refiner);


	return refiner;

}


// Subdivision limit surface
struct Surface {
	TopologyRefinerPtr refiner;
	PtexIndicesPtr ptexIndices;
	PatchTablePtr patchTable;
	PatchMapPtr patchMap;
	const PosVector& basePositions;
	const TriVector& baseTriangles;
	PosVector localPositions;
	int maxLevel;

	Surface(
		const PosVector& p_basePositions,
		const TriVector& p_baseTriangles,
		int p_maxLevel
	):
		basePositions(p_basePositions),
		baseTriangles(p_baseTriangles),
		maxLevel(p_maxLevel)
	{
		SDL_LOG("Subdiv - Adaptive - Computing limit surface");

		// Initialize patch table options
		Far::PatchTableFactory::Options patchOptions(maxLevel);
		//patchOptions.useInfSharpPatch = true;
		patchOptions.generateVaryingTables = false;
		patchOptions.shareEndCapPatchPoints = true;
		patchOptions.endCapType =
			Far::PatchTableFactory::Options::ENDCAP_GREGORY_BASIS;

		// Construct refiner and associated ptex
		refiner = std::move(
			createTopologyAdaptiveRefiner(basePositions, baseTriangles, patchOptions)
		);
		ptexIndices.reset(new Far::PtexIndices(*refiner));

		// Apply adaptive refinement and construct the associated PatchTable to
		// evaluate the limit surface:
		refiner->RefineAdaptive(patchOptions.GetRefineAdaptiveOptions());

		// Construct the associated PatchTable to evaluate the limit surface:
		patchTable.reset(Far::PatchTableFactory::Create(*refiner, patchOptions));

		// Construct patch map
		patchMap.reset(new Far::PatchMap(*patchTable));


		// Set localPositions
		int nBaseVertices    = refiner->GetLevel(0).GetNumVertices();
		int nRefinedVertices = refiner->GetNumVerticesTotal() - nBaseVertices;
		int nLocalPoints     = patchTable->GetNumLocalPoints();

		localPositions.resize(nRefinedVertices + nLocalPoints);

		if (nRefinedVertices) {
			Far::PrimvarRefiner primvarRefiner(*refiner);

			Pos const * src = &basePositions[0];
			Pos * dst = &localPositions[0];
			for (int level = 1; level < refiner->GetNumLevels(); ++level) {
				primvarRefiner.Interpolate(level, src, dst);
				src = dst;
				dst += refiner->GetLevel(level).GetNumVertices();
			}
		}
		if (nLocalPoints) {
			patchTable->GetLocalPointStencilTable()->UpdateValues(
					&basePositions[0], nBaseVertices, &localPositions[0],
					&localPositions[nRefinedVertices]);
		}
	}
};


void Tessellate (
	const Surface& surface,
	const size_t N,
	CoordVector& tessCoords,
	TriVector & tessTris
	) {
	// This is triforce tessellation.
	// This tessellation splits edges in N segments,
	// and generates N² resulting triangles by input triangle.
	//
	// Input:
	//           V2
	//          / \
	//		   /  \
	//        /   \
	//      V0----V1
	//
	//
	//	Output (N=4):
	//
	//          V2
	//         / \
	//        E7-E8
	//       / \/ \
	//      E5-I3-E6
	//     / \/ \/ \
	//    E3-I0-I1-E4
	//   / \/ \/ \/ \
	//	V0-E0-E1-E2-V1
	//
	//	Nota:
	//	Vx - initial vertex (to be referenced)
	//	Ex - edge vertex (to be created)
	//	Ix - internal vertex (to be created)

	// Some constants
	const auto& topology = surface.refiner->GetLevel(0);
	const size_t FACE_COUNT = topology.GetNumFaces();
	const int FACE_SIZE = 3;  // Of course (triangles)...


	// Check
	SDL_LOG("Subdiv - Apdative - Tessellating " << FACE_COUNT << " faces with factor N=" << N);
	if (FACE_COUNT * N * N >= std::numeric_limits<int>::max()) {
		throw std::runtime_error("FACE_COUNT * N * N > numeric limit (" + ToString(std::numeric_limits<int>::max()) + ")");
	}

	// Offset
	int offset = topology.GetNumVertices();
	int tessPosCount = offset + topology.GetNumEdges();

	// Resize tessPos and tessTris
	tessTris.clear();
	tessTris.resize(FACE_COUNT * N * N);

	// Get vertex local coordinates in given face
	auto getVertexLocalCoords = [N, &topology](int vertex, int face) {
		auto faceVertices = topology.GetFaceVertices(face);
		if (vertex == faceVertices[0]) {
			return LocalCoords(face, 0, 0);
		} else if (vertex == faceVertices[1]) {
			return LocalCoords(face, N, 0);
		} else if (vertex == faceVertices[2]) {
			return LocalCoords(face, 0, N);
		}

		throw std::runtime_error("Error in getVertexLocalCoords");
	};


	// Step #1 - Initialize with initial (coarse) vertices
	for (int vertex = 0; vertex < topology.GetNumVertices(); ++vertex) {
		int face = topology.GetVertexFaces(vertex)[0];  // Choose first face (arbitrary)
		auto faceVertices = topology.GetFaceVertices(face);
		LocalCoords coords = getVertexLocalCoords(vertex, face);
		tessCoords.push_back(coords);
	}
	int edge_offset = tessCoords.size();

	// Step #2 - Add edge vertices
	for (int edge = 0; edge < topology.GetNumEdges(); ++edge) {
		// Find face
		int face = topology.GetEdgeFaces(edge)[0];
		// Find edge vertices.
		auto edgeVerts = topology.GetEdgeVertices(edge);
		int v0 = edgeVerts[0];
		int v1 = edgeVerts[1];

		LocalCoords c0 = getVertexLocalCoords(v0, face);
		LocalCoords c1 = getVertexLocalCoords(v1, face);

		for (int i = 1; i < N; ++i) {  // Only edge interior vertices, not extremities
			// TODO Refactor constructor
			auto coords = LocalCoords::interpolate(c0, c1, i, N);
			tessCoords.push_back(coords);
		}
	}

	// Step #3 - Add interior points
	#pragma omp parallel for
	for (int f = 0; f < FACE_COUNT; ++f) {  // for each face
		const auto faceVertices = topology.GetFaceVertices(f);
		const auto faceEdges = topology.GetFaceEdges(f);
		assert(faceVertices.size() == 3);

		// If (i,j) are local coordinates, and edge length is N,
		// we have in the triangle: i >= 0, j >= 0, i + j <= N
		// We then consider k so that i + j + k = N.
		// If we are on an edge, i * j * k == 0.
		// If we are on a vertex, i == N or j == N or k == N.
		// In the face (triangle), after tessellation, we'll have N² points.
		// Some of them will be shared with other faces, we'll have to deal
		// with that.

		const int pointCount = (N + 1) * (N + 2) / 2;
		std::vector<int> pointMap;  // Map points between local numeration and global one

		for (int j = 0; j <= N ; ++j) {
			for (int i = 0; i <= N - j; ++i) {
				int k = N - i - j;
				int vertex;  // Output
				// Find point type: vertex, edge point, interior point
				if (i == N || j == N || k == N) {  // Vertex
					// Vertex point in initial triangle
					vertex = faceVertices[(i + 2 * j) / N];
				} else if (i == 0 || j == 0 || k == 0) {  // Edge
					// Find edge vertices
					int v0, v1, pos;
					// Find edge
					if (i == 0) {
						v0 = faceVertices[0];
						v1 = faceVertices[2];
						pos = j;
					} else if (j == 0) {
						v0 = faceVertices[0];
						v1 = faceVertices[1];
						pos = i;
					} else if (k == 0) {
						v0 = faceVertices[1];
						v1 = faceVertices[2];
						pos = j;
					}
					int edge = topology.FindEdge(v0, v1);
					auto edgeVertices = topology.GetEdgeVertices(edge);
					if (v0 != edgeVertices[0]) {
						pos = N - pos;
						std::swap(v0, v1);
					}
					vertex = edge_offset + edge * (N - 1) + (pos - 1);
				} else
				#pragma omp critical (LocalCoords)
				{  // Interior point
					// Create position
					tessCoords.emplace_back(f, i, j);
					// Record point in map
					vertex = tessCoords.size() - 1;
				}

				pointMap.push_back(vertex);
			}  // ~for j
		}  // ~for i

		// Create triangles

		// Lambda to get a point in the pointMap, given (i, j)
		auto pnt = [N, &pointMap](int i, int j) {
			int idx = (2 * N + 3 - j) * j / 2 + i; // Index of a point in the pointMap, given (i, j)
			return pointMap[idx];
		};

		// Up triangles
		int idxTri = f * N * N;
		for (int j = 0; j < N; ++j) {
			for (int i = 0; i < N - j; ++i, ++idxTri) {
				tessTris[idxTri] = Tri(pnt(i, j), pnt(i + 1, j), pnt(i, j + 1));
			}
		}
		// Down triangles
		for (int j = 0; j < N; ++j) {
			for (int i = 0; i < N - j - 1; ++i, ++idxTri) {
				tessTris[idxTri] = Tri(pnt(i, j + 1), pnt(i + 1, j + 1), pnt(i + 1, j));
			}
		}

	}  // ~for face

}  // ~Tessellate


void Evaluate(
	const Surface& surface,
	const size_t N,  // TODO Rename more explicitly
	const CoordVector& tessCoords,
	PosVector& tessPos,
	PosVector& tessNormals
) {
	SDL_LOG("Subdiv - Adaptive - Evaluating");

	const auto& topology = surface.refiner->GetLevel(0);

	tessPos.resize(tessCoords.size());
	tessNormals.resize(tessCoords.size());

    int numBaseVerts = (int) surface.basePositions.size();

	// Evaluate positions and derivatives
	#pragma omp parallel for
	for (int vertex = 0; vertex < tessCoords.size(); ++vertex) {
		// Init
		auto coords = tessCoords[vertex];

		// Translate local coords in global ones
		std::array<float, 2> st;
		st[0] = float(coords.i) / float(N);
		st[1] = float(coords.j) / float(N);
		int ptexFace = surface.ptexIndices->GetFaceId(coords.face);

		//  Locate the patch corresponding to the face ptex idx and (s,t)
		//  and evaluate:
		Far::PatchTable::PatchHandle const * handle =
			surface.patchMap->FindPatch(ptexFace, st[0], st[1]);
		assert(handle);

		float pWeights[20];
		float duWeights[20];
		float dvWeights[20];
		surface.patchTable->EvaluateBasis(*handle, st[0], st[1], pWeights, duWeights, dvWeights);

		//  Identify the patch cvs and combine with the evaluated weights --
		//  remember to distinguish cvs in the base level:
		Far::ConstIndexArray cvIndices = surface.patchTable->GetPatchVertices(*handle);

		// Evaluate position and derivatives
		Pos pos{0.0f, 0.0f, 0.0f};
		Pos du{0.0f, 0.0f, 0.0f};
		Pos dv{0.0f, 0.0f, 0.0f};
		for (int cv = 0; cv < cvIndices.size(); ++cv) {
			int cvIndex = cvIndices[cv];
			// TODO Simplify
			if (cvIndex < numBaseVerts) {
				pos.AddWithWeight(surface.basePositions[cvIndex], pWeights[cv]);
				du.AddWithWeight(surface.basePositions[cvIndex], duWeights[cv]);
				dv.AddWithWeight(surface.basePositions[cvIndex], dvWeights[cv]);
			} else {
				pos.AddWithWeight(surface.localPositions[cvIndex - numBaseVerts], pWeights[cv]);
				du.AddWithWeight(surface.localPositions[cvIndex - numBaseVerts], duWeights[cv]);
				dv.AddWithWeight(surface.localPositions[cvIndex - numBaseVerts], dvWeights[cv]);
			}
		}

		// Update output (position and normal)
		tessPos[vertex] = pos;
		tessNormals[vertex] = du * dv;
	}  // ~for coords
}


ExtTriangleMesh *ApplySubdiv(
	ExtTriangleMesh *srcMesh,
	const u_int maxLevel
) {
	SDL_LOG("Subdiv - Adaptive - Starting");

	// Initialize internal structures
	TriVector baseTriangles(srcMesh->GetTotalTriangleCount());
	Triangle* luxTriangles = srcMesh->GetTriangles();
	#pragma omp parallel for
	for (size_t t = 0; t < srcMesh->GetTotalTriangleCount(); ++t) {
		for (size_t v = 0; v < 3; ++v) {
			baseTriangles[t][v] = luxTriangles[t].v[v];
		}
	}

	PosVector basePositions;
	int numVertices = srcMesh->GetTotalVertexCount();
	basePositions.resize(numVertices);
	auto meshVertices = srcMesh->GetVertices();
	#pragma omp parallel for
	for (int i = 0; i < numVertices; ++i) {
		basePositions[i][0] = meshVertices[i].x;
		basePositions[i][1] = meshVertices[i].y;
		basePositions[i][2] = meshVertices[i].z;
	}

	// Output structures
	PosVector tessPositions;
	PosVector tessNormals;
	TriVector tessTriangles;

	// Create limit surface (subdivided) from base geometry
	Surface surface(basePositions, baseTriangles, maxLevel);

	// Tessellate
	CoordVector tessCoords;  // We temporarily store new positions as (face, i, j)
	size_t tessellationRate = 1 << maxLevel;
	Tessellate(surface, tessellationRate, tessCoords, tessTriangles);

	// Evaluate
	Evaluate(surface, tessellationRate, tessCoords, tessPositions, tessNormals);

	basePositions = tessPositions;
	baseTriangles = tessTriangles;

	SDL_LOG("Subdiv - Adaptive - Building new mesh");

	// New vertices
	size_t pointCount = tessPositions.size();
	SDL_LOG("Subdiv - Adaptive - " << pointCount << " points");
	Point *newVerts = TriangleMesh::AllocVerticesBuffer(pointCount);
	#pragma omp parallel for
	for (size_t i = 0; i < pointCount; ++i) {
		Point* vert = newVerts + i;
		vert->x = tessPositions[i][0];
		vert->y = tessPositions[i][1];
		vert->z = tessPositions[i][2];
	}

	// New normals
	size_t normCount = tessNormals.size();
	SDL_LOG("Subdiv - Adaptive - " << normCount << " normals");
	Normal *newNormals = new Normal[pointCount];
	#pragma omp parallel for
	for (size_t i = 0; i < normCount; ++i) {
		Normal* norm = newNormals + i;
		norm->x = tessNormals[i][0];
		norm->y = tessNormals[i][1];
		norm->z = tessNormals[i][2];
		*norm = Normalize(*norm);
	}

	// New triangles
	size_t triCount = tessTriangles.size();
	SDL_LOG("Subdiv - Adaptive - " << triCount << " triangles");
	Triangle *newTris = TriangleMesh::AllocTrianglesBuffer(triCount);
	#pragma omp parallel for
	for (size_t face = 0; face < triCount; ++face) {
		auto tri = tessTriangles[face];
		for (u_int vertex = 0; vertex < 3; ++vertex) {
			newTris[face].v[vertex] = tri[vertex];
			assert(tri[vertex] <= pointCount);
			assert(tri[vertex] >= 0);
		}
	}

	// Allocate the new mesh
	ExtTriangleMesh *newMesh =  new ExtTriangleMesh(
		pointCount, triCount, newVerts, newTris, newNormals
	);

	return newMesh;

}

}  // ~namespace enhanced

}  // ~namespace (local)


//////////////////////////
//        API           //
//////////////////////////


SubdivShape::SubdivShape(const Camera *camera, ExtTriangleMesh *srcMesh,
		const u_int maxLevel, const float maxEdgeScreenSize, const bool adaptive) {
	const double startTime = WallClockTime();

	if ((maxEdgeScreenSize > 0.f) && !camera) {
		throw std::runtime_error("The scene camera must be defined in order to enable subdiv maxedgescreensize option");
	}

	if (maxLevel > 0) {
		if (camera && (maxEdgeScreenSize > 0.f)) {
			SDL_LOG("Subdividing shape " << srcMesh->GetName() << " max. at level: " << maxLevel);

			mesh = srcMesh->Copy();

			for (u_int i = 0; i < maxLevel; ++i) {
				// Check the size of the longest mesh edge on film image plane
				const float edgeScreenSize = MaxEdgeScreenSize(camera, mesh);
				SDL_LOG("Subdividing shape current max. edge screen size: " << edgeScreenSize);

				if (edgeScreenSize <= maxEdgeScreenSize)
					break;

				// Subdivide by one level and re-try
				ExtTriangleMesh *newMesh = ApplySubdiv(mesh, 1, adaptive);
				SDL_LOG("Subdivided shape step #" << i << " from " << mesh->GetTotalTriangleCount() << " to " << newMesh->GetTotalTriangleCount() << " faces");

				// Replace old mesh with new one
				delete mesh;
				mesh = newMesh;
			}
		} else {
			SDL_LOG("Subdividing shape " << srcMesh->GetName() << " at level: " << maxLevel);

			mesh = ApplySubdiv(srcMesh, maxLevel, adaptive);
		}
	} else {
		// Nothing to do, just make a copy
		srcMesh = srcMesh->Copy();
	}

	if (maxLevel > 0) {
		SDL_LOG("Subdivided shape from " << srcMesh->GetTotalTriangleCount() << " to " << mesh->GetTotalTriangleCount() << " faces");
	}

	// For some debugging
	//mesh->Save("debug.ply");

	const double endTime = WallClockTime();
	SDL_LOG(std::format("Subdividing time: {:.3f} secs", endTime - startTime));
}



float SubdivShape::MaxEdgeScreenSize(const Camera *camera, ExtTriangleMesh *srcMesh) {
	const u_int triCount = srcMesh->GetTotalTriangleCount();
	const Point *verts = srcMesh->GetVertices();
	const Triangle *tris = srcMesh->GetTriangles();

	// Note VisualStudio doesn't support:
	//#pragma omp parallel for reduction(max:maxEdgeSize)

	const u_int threadCount =
#if defined(_OPENMP)
			omp_get_max_threads()
#else
			0
#endif
			;

	const Transform worldToScreen = Inverse(camera->GetScreenToWorld());

	std::vector<float> maxEdgeSizes(threadCount, 0.f);
	for(
			// Visual C++ 2013 supports only OpenMP 2.5
#if _OPENMP >= 200805
			unsigned
#endif
			int i = 0; i < triCount; ++i) {
		const int tid =
#if defined(_OPENMP)
			omp_get_thread_num()
#else
			0
#endif
			;

		const Triangle &tri = tris[i];
		const Point p0 = worldToScreen * verts[tri.v[0]];
		const Point p1 = worldToScreen * verts[tri.v[1]];
		const Point p2 = worldToScreen * verts[tri.v[2]];

		float maxEdgeSize = (p1 - p0).Length();
		maxEdgeSize = Max(maxEdgeSize, (p2 - p1).Length());
		maxEdgeSize = Max(maxEdgeSize, (p0 - p2).Length());

		maxEdgeSizes[tid] = Max(maxEdgeSizes[tid], maxEdgeSize);
	}

	float maxEdgeSize = 0.f;
	for (u_int i = 0; i < threadCount; ++i)
		maxEdgeSize = Max(maxEdgeSize, maxEdgeSizes[i]);

	return maxEdgeSize;
}


ExtTriangleMesh *SubdivShape::ApplySubdiv(
	ExtTriangleMesh *srcMesh, const u_int maxLevel, const bool adaptive
) {
	//--------------------------------------------------------------------------
	// Check data
	//--------------------------------------------------------------------------

	// OSD topology is indexed by int whereas Lux's one is indexed by uint...
	assert(srcMesh->GetTotalVertexCount() <= std::numeric_limits<int>::max());
	assert(srcMesh->GetTotalTriangleCount() <= std::numeric_limits<int>::max());


	// TODO
	if (adaptive) {
		SDL_LOG("Subdiv - Refining (adaptive)");

		return enhanced::ApplySubdiv(srcMesh, maxLevel);
	}
	else {
		SDL_LOG("Subdiv - Refining (adaptive)");

		return simple::ApplySubdiv(srcMesh, maxLevel);
	}
}


SubdivShape::~SubdivShape() {
	if (!refined)
		delete mesh;
}

ExtTriangleMesh *SubdivShape::RefineImpl(const Scene *scene) {
	return mesh;
}


// vim: autoindent noexpandtab tabstop=4 shiftwidth=4
