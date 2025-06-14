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
	SDL_LOG("Subdivision - Refining (uniform)");
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

	SDL_LOG("Subdivision - Refined vertices: " << nRefinedVerts);
	SDL_LOG("Subdivision - Refined triangles: " << nRefinedFaces);


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

	SDL_LOG("Subdivision - Stencil and patch tables created");

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

	SDL_LOG("Subdivision - Buffers built");

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
//
// - Subdivision is NOT tessellation
//   (https://graphics.pixar.com/opensubdiv/docs/subdivision_surfaces.html#subdivision-versus-tessellation)
//
// - Topology and Primvar are separate things
//   (https://graphics.pixar.com/opensubdiv/docs/far_overview.html#feature-adaptive-representation-far)
//
// (please read the docs)

namespace enhanced {


// Storage of local coordinates (coordinates in a given face) during
// tessellation.

struct LocalCoords {
	int face = -1;
	float x = 0.f;
	float y = 0.f;

	LocalCoords() {}

	LocalCoords(int p_face, float p_x, float p_y):
		face(p_face),
		x(p_x),
		y(p_y)
	{ }

	LocalCoords(int p_face, int p_i, int p_j, int N):
		face(p_face),
		x(float(p_i) / float(N)),
		y(float(p_j) / float(N))
	{ }

	// Avoid unexpected use of LocalCoords(int, float, float)
	// with implicit conversion
	LocalCoords(int, int, int) = delete;

	// Interpolating constructor
	LocalCoords(LocalCoords p0, LocalCoords p1, u_int weight, u_int total) {
		assert(p0.face == p1.face);
		assert(weight <= total);

		face = p0.face;

		float weight_f = float(weight);
		float total_f = float(total);
		x = (weight_f * p1.x + (total_f - weight_f) * p0.x) / total_f;
		y = (weight_f * p1.y + (total_f - weight_f) * p0.y) / total_f;
	}
};

typedef std::vector<LocalCoords> CoordVector;

TopologyRefinerPtr createTopologyAdaptiveRefiner(
		const ExtTriangleMesh * srcMesh,
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
	desc.numVertices = srcMesh->GetTotalVertexCount();
	desc.numFaces = srcMesh->GetTotalTriangleCount();
	std::vector<int> vertPerFace(desc.numFaces, 3);
	desc.numVertsPerFace = &vertPerFace[0];
	desc.vertIndicesPerFace = reinterpret_cast<const int *>(srcMesh->GetTriangles());

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
	const Point* basePositions;  // TODO
	const int numBasePositions;
	Point* localPositions;
	int numLocalPositions;
	int maxLevel;

	Surface(const ExtTriangleMesh * p_srcMesh, int p_maxLevel):
		basePositions(p_srcMesh->GetVertices()),
		numBasePositions(p_srcMesh->GetTotalVertexCount()),
		maxLevel(p_maxLevel)
	{
		SDL_LOG("Subdivision (enhanced) - Computing patches");

		// Initialize patch table options
		Far::PatchTableFactory::Options patchTableOptions(maxLevel);
		patchTableOptions.useInfSharpPatch = true;
		patchTableOptions.generateVaryingTables = false;

		// Note: ENDCAP_GREGORY_BASIS can generate null normals
		// (try with Suzanne, level=3...):
		// please avoid...
		patchTableOptions.endCapType =
			Far::PatchTableFactory::Options::ENDCAP_BSPLINE_BASIS;

		// Construct refiner
		refiner = std::move(createTopologyAdaptiveRefiner(p_srcMesh, patchTableOptions));

		// Apply adaptive refinement and construct the associated PatchTable to
		// evaluate the limit surface:
		refiner->RefineAdaptive(patchTableOptions.GetRefineAdaptiveOptions());

		// Construct the associated PatchTable to evaluate the limit surface:
		patchTable.reset(Far::PatchTableFactory::Create(*refiner, patchTableOptions));

		// Construct ptex indices table
		ptexIndices.reset(new Far::PtexIndices(*refiner));

		// Construct patch map
		patchMap.reset(new Far::PatchMap(*patchTable));

		// Set localPositions
		int nBaseVertices    = refiner->GetLevel(0).GetNumVertices();
		int nRefinedVertices = refiner->GetNumVerticesTotal() - nBaseVertices;
		int nLocalPoints     = patchTable->GetNumLocalPoints();

		numLocalPositions = nRefinedVertices + nLocalPoints;
		localPositions = TriangleMesh::AllocVerticesBuffer(numLocalPositions);

		if (nRefinedVertices) {
			Far::PrimvarRefiner primvarRefiner(*refiner);

			Point const * srcPos = basePositions;
			Point * dstPos = &localPositions[0];
			for (int level = 1; level < refiner->GetNumLevels(); ++level) {
				primvarRefiner.Interpolate(level, srcPos, dstPos);
				srcPos = dstPos;
				dstPos += refiner->GetLevel(level).GetNumVertices();
			}
		}
		if (nLocalPoints) {
			patchTable->GetLocalPointStencilTable()->UpdateValues(
				basePositions,
				nBaseVertices,
				&localPositions[0],
				&localPositions[nRefinedVertices]
			);
		}
	}

	// Get number of positions after N-tessellation
	int getTesselPosCount(int N) {
		const auto& topology = refiner->GetLevel(0);

		// Number of interior points in an edge
		const int numEdgeInteriorPoints = N - 1;

		// Number of interior points in a triangle
		const int numTriangleInteriorPoints = (N - 1) * (N - 2) / 2;

		// Number of coords to be computed
		const int numCoords =
			topology.GetNumVertices()
			+ numEdgeInteriorPoints * topology.GetNumEdges()
			+ numTriangleInteriorPoints * topology.GetNumFaces();

		return numCoords;
	}

	// Get number of triangles after N-tessellation
	int getTesselTriangleCount(int N) {
		const auto& topology = refiner->GetLevel(0);
		return topology.GetNumFaces() * N * N;
	}
};


void Tessellate (
	const Surface& surface,
	const size_t N,
	CoordVector& tessCoords,
	Triangle * tessTris
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
	SDL_LOG(
		"Subdivision (enhanced) - Tessellating "
		<< FACE_COUNT
		<< " faces / " << topology.GetNumVertices()
		<< " points with factor N="
		<< N
	);
	if (FACE_COUNT * N * N >= std::numeric_limits<int>::max()) {
		throw std::runtime_error(
			"FACE_COUNT * N * N > numeric limit ("
			+ ToString(std::numeric_limits<int>::max())
			+ ")"
		);
	}

	// Useful variables and constants
	int offset;

	// Number of coarse vertices
	const int numCoarseVertices = topology.GetNumVertices();

	// Number of interior points in an edge
	const int numEdgeInteriorPoints = N - 1;

	// Number of interior points in a triangle
	const int numTriangleInteriorPoints = (N - 1) * (N - 2) / 2;

	// Number of coords to be computed
	const int numCoords =
		topology.GetNumVertices()
		+ numEdgeInteriorPoints * topology.GetNumEdges()
		+ numTriangleInteriorPoints * topology.GetNumFaces();

	// Size tessCoords
	tessCoords.resize(numCoords);

	// Get vertex local coordinates in given face
	auto getVertexLocalCoords = [&topology](int face, int vertex) {
		auto faceVertices = topology.GetFaceVertices(face);
		if (vertex == faceVertices[0]) {
			return LocalCoords(face, 0.f, 0.f);
		} else if (vertex == faceVertices[1]) {
			return LocalCoords(face, 1.f, 0.f);
		} else if (vertex == faceVertices[2]) {
			return LocalCoords(face, 0.f, 1.f);
		}

		throw std::runtime_error("Error in getVertexLocalCoords");
	};

	// Step #1 - Initialize with initial (coarse) vertices
	#pragma omp parallel for
	for (int vertex = 0; vertex < numCoarseVertices; ++vertex) {
		int face = topology.GetVertexFaces(vertex)[0];  // Choose first face (arbitrary)
		tessCoords[vertex] = getVertexLocalCoords(face, vertex);
	}
	offset = numCoarseVertices;

	// Step #2 - Add edge vertices
	#pragma omp parallel for
	for (int edge = 0; edge < topology.GetNumEdges(); ++edge) {
		// Find face
		int face = topology.GetEdgeFaces(edge)[0];
		// Find edge vertices.
		auto edgeVerts = topology.GetEdgeVertices(edge);
		int v0 = edgeVerts[0];
		int v1 = edgeVerts[1];

		LocalCoords c0 = getVertexLocalCoords(face, v0);
		LocalCoords c1 = getVertexLocalCoords(face, v1);

		for (int i = 1; i < N; ++i) {  // Only edge interior vertices, not extremities
			LocalCoords coords(c0, c1, i, N);  // Interpolate from c0 and c1
			tessCoords[offset + edge * numEdgeInteriorPoints + i - 1 ] = coords;
		}
	}
	offset = numCoarseVertices + numEdgeInteriorPoints * topology.GetNumEdges();

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
					vertex = numCoarseVertices + edge * numEdgeInteriorPoints + (pos - 1);

				} else { // Interior point
					int idx = offset
						+ numTriangleInteriorPoints * f
						+ numTriangleInteriorPoints - (N - j) * (N - j - 1) / 2
						+ i - 1;
					// Create position
					tessCoords[idx] = LocalCoords(f, i, j, N);
					// Record point in map
					vertex = idx;
				}

				pointMap.push_back(vertex);
			}  // ~for j
		}  // ~for i

		// Create triangles

		// Lambda to get a point in the pointMap, given (i, j)
		auto pnt = [N, &pointMap](int i, int j) {
			int idx = (2 * N + 3 - j) * j / 2 + i;
			return pointMap[idx];
		};

		int idxTri = f * N * N;

		// Up triangles
		for (int j = 0; j < N; ++j) {
			for (int i = 0; i < N - j; ++i, ++idxTri) {
				tessTris[idxTri] = Triangle(pnt(i, j), pnt(i + 1, j), pnt(i, j + 1));
			}
		}
		// Down triangles
		for (int j = 0; j < N; ++j) {
			for (int i = 0; i < N - j - 1; ++i, ++idxTri) {
				tessTris[idxTri] = Triangle(pnt(i, j + 1), pnt(i + 1, j + 1), pnt(i + 1, j));
			}
		}

	}  // ~for face

}  // ~Tessellate


void Evaluate(
	const Surface& surface,
	const CoordVector& tessCoords,
	Point* tessPoints,
	Normal* tessNormals
) {
	SDL_LOG("Subdivision (enhanced) - Evaluating");

	const auto& topology = surface.refiner->GetLevel(0);
	const auto& patchTable = *surface.patchTable;
	const auto& ptexIndices = *surface.ptexIndices;

    int numBaseVerts = surface.numBasePositions;

	// Evaluate positions and derivatives
	#pragma omp parallel for
	for (int vertex = 0; vertex < tessCoords.size(); ++vertex) {
		// Init
		auto& coords = tessCoords[vertex];

		// Translate in ptex face
		int ptexFace = surface.ptexIndices->GetFaceId(coords.face);

		//  Locate the patch corresponding to the face ptex idx and (s,t)
		//  and evaluate:
		Far::PatchTable::PatchHandle const * handle =
			surface.patchMap->FindPatch(ptexFace, coords.x, coords.y);
		assert(handle);

		float pWeights[20];
		float duWeights[20];
		float dvWeights[20];
		patchTable.EvaluateBasis(
			*handle, coords.x, coords.y,
			pWeights, duWeights, dvWeights
		);

		//  Identify the patch cvs and combine with the evaluated weights --
		//  remember to distinguish cvs in the base level:
		Far::ConstIndexArray cvIndices = patchTable.GetPatchVertices(*handle);

		// Evaluate position and derivatives
		Point& pos = tessPoints[vertex];
		pos.Clear();

		Vector du;
		du.Clear();

		Vector dv;
		dv.Clear();

		for (int cv = 0; cv < cvIndices.size(); ++cv) {
			int cvIndex = cvIndices[cv];

			const Point& position =
				(cvIndex < numBaseVerts) ?
				surface.basePositions[cvIndex] :
				surface.localPositions[cvIndex - numBaseVerts];

			pos.AddWithWeight(position, pWeights[cv]);
			du.AddWithWeight(position, duWeights[cv]);
			dv.AddWithWeight(position, dvWeights[cv]);
		}

		// Update output (position and normal)
		tessNormals[vertex] = Normal(Cross(du, dv));

		// Check validity of normal
		auto t = tessNormals[vertex];
		if (t[0] == 0.f && t[1] == 0.f && t[2] == 0.f) {
			SDL_LOG(
				"Subdivision (enhanced) - Vertex #" << vertex << ", "
				<< "uv = (" << coords.x << ", " << coords.y << "), "
				<< "du = (" << du[0] << ", " << du[1] << ", " << du[2] << "), "
				<< "dv = (" << dv[0] << ", " << dv[1] << ", " << dv[2] << ")"
			);
			throw std::runtime_error("Null normal (vect(du, dv) == 0)");
		}
	}  // ~for vertex
}


ExtTriangleMesh *ApplySubdiv(
	ExtTriangleMesh *srcMesh,
	const u_int maxLevel
) {
	// Create limit surface (subdivided) from base geometry
	Surface surface(srcMesh, maxLevel);

	// Compute dimensions
	size_t tessellationRate = 1 << maxLevel;
	int pointCount = surface.getTesselPosCount(tessellationRate);
	int normCount = surface.getTesselPosCount(tessellationRate);
	int triCount = surface.getTesselTriangleCount(tessellationRate);

	// Output structures
	auto tessPositions = TriangleMesh::AllocVerticesBuffer(pointCount);
	auto tessNormals = new Normal[normCount];
	auto tessTriangles = TriangleMesh::AllocTrianglesBuffer(triCount);

	// Tessellate
	CoordVector tessCoords;  // We temporarily store new positions as (face, x, y)
	Tessellate(surface, tessellationRate, tessCoords, tessTriangles);

	// Evaluate
	Evaluate(surface, tessCoords, tessPositions, tessNormals);

	SDL_LOG(
		"Subdivision (enhanced) - Building new mesh: "
		<< pointCount << " points, "
		<< triCount << " triangles"
	);

	SDL_LOG("Subdivision (enhanced) - Allocating");

	// Allocate the new mesh
	ExtTriangleMesh *newMesh =  new ExtTriangleMesh(
		pointCount, triCount, tessPositions, tessTriangles, tessNormals
	);

	return newMesh;

}

}  // ~namespace enhanced

}  // ~namespace (local)


//////////////////////////
//        API           //
//////////////////////////


SubdivShape::SubdivShape(
	const Camera *camera,
	ExtTriangleMesh *srcMesh,
	const u_int maxLevel,
	const float maxEdgeScreenSize,
	const bool enhanced
) {
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
				ExtTriangleMesh *newMesh = ApplySubdiv(mesh, 1, enhanced);
				SDL_LOG("Subdivided shape step #" << i << " from " << mesh->GetTotalTriangleCount() << " to " << newMesh->GetTotalTriangleCount() << " faces");

				// Replace old mesh with new one
				delete mesh;
				mesh = newMesh;
			}
		} else {
			SDL_LOG("Subdividing shape " << srcMesh->GetName() << " at level: " << maxLevel);

			mesh = ApplySubdiv(srcMesh, maxLevel, enhanced);
		}
	} else {
		// Nothing to do, just make a copy
		SDL_LOG("Subdivision: level=0 - Skipping");
		mesh = srcMesh->Copy();
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
	ExtTriangleMesh *srcMesh,
	const u_int maxLevel,
	const bool enhanced
) {
	//--------------------------------------------------------------------------
	// Check data
	//--------------------------------------------------------------------------

	// OSD topology is indexed by int whereas Lux's one is indexed by uint...
	assert(srcMesh->GetTotalVertexCount() <= std::numeric_limits<int>::max());
	assert(srcMesh->GetTotalTriangleCount() <= std::numeric_limits<int>::max());

	if (enhanced) {
		SDL_LOG("Subdivision (enhanced) - Starting");

		return enhanced::ApplySubdiv(srcMesh, maxLevel);
	}
	else {
		SDL_LOG("Subdivision - Starting");

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
