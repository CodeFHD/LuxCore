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
#include <boost/format.hpp>

#include <opensubdiv/far/topologyDescriptor.h>
#include <opensubdiv/far/patchMap.h>
#include <opensubdiv/far/patchTable.h>
#include <opensubdiv/far/patchTableFactory.h>
#include <opensubdiv/far/stencilTableFactory.h>
#include <opensubdiv/far/topologyRefinerFactory.h>
#include <opensubdiv/osd/cpuPatchTable.h>
#include <opensubdiv/osd/cpuVertexBuffer.h>

#include <omp.h>
#include <opensubdiv/osd/tbbEvaluator.h>
#define OSD_EVALUATOR Osd::TbbEvaluator

#include "luxrays/core/exttrianglemesh.h"
#include "slg/shapes/subdiv.h"
#include "slg/cameras/camera.h"
#include "slg/scene/scene.h"

using namespace std;
using namespace luxrays;
using namespace slg;
using namespace OpenSubdiv;

using BufferPtr = std::unique_ptr<Osd::CpuVertexBuffer>;
using StencilTablePtr = std::unique_ptr<const Far::StencilTable>;
using PatchTablePtr = std::unique_ptr<Far::PatchTable>;

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

	vector<float> maxEdgeSizes(threadCount, 0.f);
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

static Far::TopologyRefiner * createFarTopologyRefiner(const ExtTriangleMesh*);

ExtTriangleMesh *SubdivShape::ApplySubdiv(ExtTriangleMesh *srcMesh, const u_int maxLevel) {
	//--------------------------------------------------------------------------
	// Check data
	//--------------------------------------------------------------------------

	// OSD topology is indexed by int whereas Lux's one is indexed by uint...
	assert(srcMesh->GetTotalVertexCount() <= std::numeric_limits<int>::max());
	assert(srcMesh->GetTotalTriangleCount() <= std::numeric_limits<int>::max());


	//--------------------------------------------------------------------------
	// Refine topology
	//--------------------------------------------------------------------------
	// https://graphics.pixar.com/opensubdiv/docs/osd_tutorial_0.html

	// Set-up refiner
	std::unique_ptr<Far::TopologyRefiner> refiner(createFarTopologyRefiner(srcMesh));

	// Uniformly refine the topology up to 'maxlevel'
	Far::TopologyRefiner::UniformOptions refiner_options(maxLevel);
	refiner_options.fullTopologyInLastLevel = true;
	refiner->RefineUniform(refiner_options);
	if (refiner->GetMaxLevel() < maxLevel) {
		SDL_LOG("WARNING - SUBDIVISION FAILURE");
		SDL_LOG("'maxlevel' may be too high, please check. Continuing with input mesh...");
		return srcMesh;
	}

	//--------------------------------------------------------------------------
	// Create stencil and patch tables
	//--------------------------------------------------------------------------

	StencilTablePtr stencilTable;
	PatchTablePtr patchTable;

	{
		// Create stencil table
		Far::StencilTableFactory::Options stencilOptions;
		stencilOptions.generateOffsets = true;
		stencilOptions.generateIntermediateLevels = false;  // TODO

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
			const auto *combinedTable =
				Far::StencilTableFactory::AppendLocalPointStencilTable(
					*refiner, stencilTable.get(), localPointStencilTable
				);
			if (combinedTable) {
				stencilTable.reset(combinedTable);
			}
		}
	}

	//--------------------------------------------------------------------------
	// Set buffers and evaluate primvar from stencils
	//--------------------------------------------------------------------------

	// Setup buffers for vertex primvar data
	Far::TopologyLevel const& refFirstLevel = refiner->GetLevel(0);
	Far::TopologyLevel const& refLastLevel = refiner->GetLevel(maxLevel);

	const int nCoarseVerts = refFirstLevel.GetNumVertices(); // Coarse vertex count
	const int nCoarseFaces = refFirstLevel.GetNumFaces(); // Coarse face count
	const int nRefinedVerts = refLastLevel.GetNumVertices(); // Refined vertex count
	const int nRefinedFaces = refLastLevel.GetNumFaces(); // Coarse face count
	const int totalVertsCount = nCoarseVerts + nRefinedVerts;  // Total vertex count
	SDL_LOG("Subdiv - Refined vertices: " << nRefinedVerts);
	SDL_LOG("Subdiv - Refined triangles: " << nRefinedFaces);

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
					totalVertsCount
				);
			}
		}
	}

	//--------------------------------------------------------------------------
	// Build the new mesh
	//--------------------------------------------------------------------------

	// New triangles
	Triangle *newTris = TriangleMesh::AllocTrianglesBuffer(nRefinedFaces);
	for (int face = 0; face < nRefinedFaces; ++face) {
		auto faceVerts = refLastLevel.GetFaceVertices(face);
		for (u_int vertex = 0; vertex < 3; ++vertex) {
			newTris[face].v[vertex] = faceVerts[vertex];
		}
	}

	// New vertices
	Point *newVerts = TriangleMesh::AllocVerticesBuffer(nRefinedVerts);
	const float *refinedVerts = vertsBuffer->BindCpuBuffer() + 3 * nCoarseVerts;
	copy(refinedVerts, refinedVerts + 3 * nRefinedVerts, &newVerts->x);


	// New normals
	Normal *newNorms = nullptr;
	if (srcMesh->HasNormals()) {
		newNorms = new Normal[nRefinedVerts];

		const float *refinedNorms = normsBuffer->BindCpuBuffer() + 3 * nCoarseVerts;
		copy(refinedNorms, refinedNorms + 3 * nRefinedVerts, &newNorms->x);
	}

	// New UVs
	array<UV *, EXTMESH_MAX_DATA_COUNT> newUVs;
	for (u_int i = 0; i < EXTMESH_MAX_DATA_COUNT; i++) {
		if (srcMesh->HasUVs(i)) {
			newUVs[i] = new UV[nRefinedVerts];

			const float *refinedUVs = uvsBuffers[i]->BindCpuBuffer() + 2 * nCoarseVerts;
			copy(refinedUVs, refinedUVs + 2 * nRefinedVerts, &newUVs[i]->u);
		} else
			newUVs[i] = nullptr;
	}

	// New colors
	array<Spectrum *, EXTMESH_MAX_DATA_COUNT> newCols;
	for (u_int i = 0; i < EXTMESH_MAX_DATA_COUNT; i++) {
		if (srcMesh->HasColors(i)) {
			newCols[i] = new Spectrum[nRefinedVerts];

			const float *refinedCols = colsBuffers[i]->BindCpuBuffer() + 3 * nCoarseVerts;
			copy(refinedCols, refinedCols + 3 * nRefinedVerts, &newCols[i]->c[0]);
		} else
			newCols[i] = nullptr;
	}

	// New alphas
	array<float *, EXTMESH_MAX_DATA_COUNT> newAlphas;
	for (u_int i = 0; i < EXTMESH_MAX_DATA_COUNT; i++) {
		if (srcMesh->HasAlphas(i)) {
			newAlphas[i] = new float[nRefinedVerts];

			const float *refinedAlphas = alphasBuffers[i]->BindCpuBuffer() + 1 * nCoarseVerts;
			copy(refinedAlphas, refinedAlphas + 1 * nRefinedVerts, newAlphas[i]);
		} else
			newAlphas[i] = nullptr;
	}

	// New vertAOVs
	array<float *, EXTMESH_MAX_DATA_COUNT> newVertAOVs;
	for (u_int i = 0; i < EXTMESH_MAX_DATA_COUNT; i++) {
		if (srcMesh->HasVertexAOV(i)) {
			newVertAOVs[i] = new float[nRefinedVerts];

			const float *refinedVertAOVs = alphasBuffers[i]->BindCpuBuffer() + 1 * nCoarseVerts;
			copy(refinedVertAOVs, refinedVertAOVs + 1 * nRefinedVerts, newVertAOVs[i]);
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

SubdivShape::SubdivShape(const Camera *camera, ExtTriangleMesh *srcMesh,
		const u_int maxLevel, const float maxEdgeScreenSize) {
	const double startTime = WallClockTime();

	if ((maxEdgeScreenSize > 0.f) && !camera)
		throw runtime_error("The scene camera must be defined in order to enable subdiv maxedgescreensize option");

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
				ExtTriangleMesh *newMesh = ApplySubdiv(mesh, 1);
				SDL_LOG("Subdivided shape step #" << i << " from " << mesh->GetTotalTriangleCount() << " to " << newMesh->GetTotalTriangleCount() << " faces");

				// Replace old mesh with new one
				delete mesh;
				mesh = newMesh;
			}
		} else {
			SDL_LOG("Subdividing shape " << srcMesh->GetName() << " at level: " << maxLevel);

			mesh = ApplySubdiv(srcMesh, maxLevel);
		}
	} else {
		// Nothing to do, just make a copy
		srcMesh = srcMesh->Copy();
	}

	SDL_LOG("Subdivided shape from " << srcMesh->GetTotalTriangleCount() << " to " << mesh->GetTotalTriangleCount() << " faces");

	// For some debugging
	//mesh->Save("debug.ply");

	const double endTime = WallClockTime();
	SDL_LOG("Subdividing time: " << (boost::format("%.3f") % (endTime - startTime)) << "secs");
}

SubdivShape::~SubdivShape() {
	if (!refined)
		delete mesh;
}

ExtTriangleMesh *SubdivShape::RefineImpl(const Scene *scene) {
	return mesh;
}

static Far::TopologyRefiner* createFarTopologyRefiner(const ExtTriangleMesh* srcMesh)
{
	// Set topology descriptor
	Far::TopologyDescriptor desc;
	desc.numVertices = srcMesh->GetTotalVertexCount();
	desc.numFaces = srcMesh->GetTotalTriangleCount();
	vector<int> vertPerFace(desc.numFaces, 3);
	desc.numVertsPerFace = &vertPerFace[0];
	desc.vertIndicesPerFace = reinterpret_cast<const int *>(srcMesh->GetTriangles());

	// Look for mesh boundary edges
	unordered_map<Edge, u_int, EdgeHashFunction> edgesMap;
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

	vector<bool> isBoundaryVertex(srcMesh->GetTotalVertexCount(), false);
	vector<Far::Index> cornerVertexIndices;
	vector<float> cornerWeights;
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


// vim: autoindent noexpandtab tabstop=4 shiftwidth=4
