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

using namespace std;
using namespace luxrays;
using namespace slg;
using namespace OpenSubdiv;

using BufferPtr = std::unique_ptr<Osd::CpuVertexBuffer>;
using StencilTablePtr = std::unique_ptr<const Far::StencilTable>;
using PatchTablePtr = std::unique_ptr<Far::PatchTable>;
using PatchMapPtr = std::unique_ptr<Far::PatchMap>;
using TopologyRefinerPtr = std::unique_ptr<Far::TopologyRefiner>;

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
static ExtTriangleMesh *ApplySubdivAdaptive(
	ExtTriangleMesh *srcMesh,
	const u_int maxLevel
);

ExtTriangleMesh *SubdivShape::ApplySubdiv(
	ExtTriangleMesh *srcMesh, const u_int maxLevel, const bool adaptive
) {

	if (adaptive) {
		SDL_LOG("Subdiv - Refining (adaptive)");
		return ApplySubdivAdaptive(srcMesh, 2);
	}

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

	// Refine the topology up to 'maxlevel'
	SDL_LOG("Subdiv - Refining (uniform)");
	Far::TopologyRefiner::UniformOptions refiner_options(maxLevel);
	refiner_options.fullTopologyInLastLevel = true;
	refiner->RefineUniform(refiner_options);

	Far::TopologyLevel const& refFirstLevel = refiner->GetLevel(0);
	Far::TopologyLevel const& refLastLevel = refiner->GetLevel(maxLevel);

	// Check validity
	if (refiner->GetMaxLevel() < maxLevel && !adaptive) {
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

	// TODO
	//const int nRefinedVerts = stencilTable->GetNumStencils();
	//const int nRefinedFaces = refLastLevel.GetNumFaces(); // Refined face count
	//const int totalVertsCount = nCoarseVerts + nRefinedVerts;  // Total vertex count

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
		const u_int maxLevel, const float maxEdgeScreenSize, const bool adaptive) {
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

	SDL_LOG("Subdivided shape from " << srcMesh->GetTotalTriangleCount() << " to " << mesh->GetTotalTriangleCount() << " faces");

	// For some debugging
	//mesh->Save("debug.ply");

	const double endTime = WallClockTime();
	SDL_LOG(std::format("Subdividing time: {:.3f} secs", endTime - startTime));
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

////////////////////////////////////////////////////////////////////////////
// Adaptive


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




static void TessellateBaseFace(
	const Far::Index face,
	const TopologyRefinerPtr& refiner,
	const PatchTablePtr& patchTable,
	const PatchMapPtr& patchMap,
	const SubdivBuffer<3>& basePositions,
	const SubdivBuffer<3>& localPositions,
	SubdivBuffer<3>& tessPoints,  // Output
	TriVector& tessTris  // Output
);


//  The PatchGroup bundles objects used to create and evaluate a sparse set
//  of patches.  Its construction creates a PatchTable and all other objects
//  necessary to evaluate patches associated with the specified subset of
//  faces provided.  A simple method to tessellate a specified face is
//  provided.
//
//  Note that, since the data buffers for the base level and refined levels
//  are separate (we want to avoid copying primvar data for the base level
//  of a potentially large mesh), that patch evaluation needs to account
//  for the separation when combining control points.
//
struct PatchGroup {
    PatchGroup(Far::PatchTableFactory::Options patchOptions,
               TopologyRefinerPtr const &      baseRefinerArg,
               Far::PtexIndices const &        basePtexIndicesArg,
               PosVector const &               basePositionsArg,
               std::vector<Far::Index> const &      baseFacesArg);

    void Tessellate(
		PosVector & tessPoints,
        TriVector & tessTris
	) const;

    //  Const reference members:
    TopologyRefinerPtr const &   baseRefiner;
    Far::PtexIndices const &     basePtexIndices;
    std::vector<Pos> const &     basePositions;
    std::vector<Far::Index> const &   baseFaces;

    //  Members constructed to evaluate patches:
    PatchTablePtr       patchTable;
    PatchMapPtr         patchMap;
    int                 patchFaceSize;
    PosVector           localPositions;
};

PatchGroup::PatchGroup(Far::PatchTableFactory::Options patchOptions,
                       TopologyRefinerPtr const &      baseRefinerArg,
                       Far::PtexIndices const &        basePtexIndicesArg,
                       PosVector const &               basePositionsArg,
                       std::vector<Far::Index> const &      baseFacesArg) :
	baseRefiner(baseRefinerArg),
	basePtexIndices(basePtexIndicesArg),
	basePositions(basePositionsArg),
	baseFaces(baseFacesArg) {


    //  Create a local refiner (sharing the base level), apply adaptive
    //  refinement to the given subset of base faces, and construct a patch
    //  table (and its associated map) for the same set of faces:
    //
    Far::ConstIndexArray groupFaces(&baseFaces[0], (int)baseFaces.size());

    TopologyRefinerPtr localRefiner(
        Far::TopologyRefinerFactory<Far::TopologyDescriptor>::Create(*baseRefiner)
	);

    localRefiner->RefineAdaptive(
        patchOptions.GetRefineAdaptiveOptions(), groupFaces);

    patchTable.reset(
		Far::PatchTableFactory::Create(*localRefiner, patchOptions, groupFaces)
	);

    patchMap.reset(new Far::PatchMap(*patchTable));

    patchFaceSize =
        Sdc::SchemeTypeTraits::GetRegularFaceSize(localRefiner->GetSchemeType());

    //  Compute the number of refined and local points needed to evaluate the
    //  patches, allocate and interpolate.  This varies from tutorial_5_1 in
    //  that the primvar buffer for the base vertices is separate from the
    //  refined vertices and local patch points (which must also be accounted
    //  for when evaluating the patches).
    //
    int nBaseVertices    = localRefiner->GetLevel(0).GetNumVertices();
    int nRefinedVertices = localRefiner->GetNumVerticesTotal() - nBaseVertices;
    int nLocalPoints     = patchTable->GetNumLocalPoints();

    localPositions.resize(nRefinedVertices + nLocalPoints);

    if (nRefinedVertices) {
        Far::PrimvarRefiner primvarRefiner(*localRefiner);

        Pos const * src = &basePositions[0];
        Pos * dst = &localPositions[0];
        for (int level = 1; level < localRefiner->GetNumLevels(); ++level) {
            primvarRefiner.Interpolate(level, src, dst);
            src = dst;
            dst += localRefiner->GetLevel(level).GetNumVertices();
        }
    }
    if (nLocalPoints) {
        patchTable->GetLocalPointStencilTable()->UpdateValues(
                &basePositions[0], nBaseVertices, &localPositions[0],
                &localPositions[nRefinedVertices]);
    }

}

#if 0
void
PatchGroup::TessellateBaseFace(int face, PosVector & tessPoints,
                                         TriVector & tessTris) const {

    //  Tesselate the face with points at the midpoint of the face and at
    //  each corner, and triangles connecting the midpoint to each edge.
    //  Irregular faces require an aribrary number of corners points, but
    //  all are at the origin of the child face of the irregular base face:
    //
    float const quadPoints[5][2] = { { 0.5f, 0.5f },
                                     { 0.0f, 0.0f },
                                     { 1.0f, 0.0f },
                                     { 1.0f, 1.0f },
                                     { 0.0f, 1.0f } };

    float const triPoints[4][2] = { { 0.5f, 0.5f },
                                    { 0.0f, 0.0f },
                                    { 1.0f, 0.0f },
                                    { 0.0f, 1.0f } };

    float const irregPoints[4][2] = { { 1.0f, 1.0f },
                                      { 0.0f, 0.0f } };

    //  Determine the topology of the given base face and the resulting
    //  tessellation points and faces to generate:
    //
    int baseFace = baseFaces[face];
    int faceSize = baseRefiner->GetLevel(0).GetFaceVertices(baseFace).size();

    bool faceIsIrregular = (faceSize != patchFaceSize);

    int nTessPoints = faceSize + 1;
    int nTessFaces  = faceSize;

    tessPoints.resize(nTessPoints);
    tessTris.resize(nTessFaces);



    //  Compute the mid and corner points -- remember that for an irregular
    //  face, we must reference the individual ptex faces for each corner:
    //
    int ptexFace = basePtexIndices.GetFaceId(baseFace);

    int numBaseVerts = (int) basePositions.size();

	// Butterfly tessellation (triangles)
	auto& topology = refiner->GetLevel(0);
	for (auto edge: topology.GetFaceEdges(baseFace)) {
		// TODO Boundary edge: take midpoint


		// Gather vertices of the edge

		// v0, v1: endpoints of the edge
		auto v0_v1 = topology.GetEdgeVertices(edge);
		int v0 = v0_v1[0], v1 = v0_v1[1];

		// v2, v3: "opposite" vertices in triangles sharing the edge
		std::vector<int> v2_v3;
		for (int f: topology.GetEdgeFaces(edge)) {
			for (int v: topolevel.GetFaceVertices(f) {
				if (v != v0 && vert != v1) {
					v2_v3.push_back(v);
				}
			}
		}
		assert(v2_v3.size() == 2);
		int v2 = v2_v3[0], v3 = v2_v3[1];

		// v4, v5, v6, v7: vertices opposite v0 and v1 in adjacent triangles to v2 and v3
		std::vector<int> v4_v5_v6_v7;
		for (int v: v0_v1) {
			for (int w: v2_v3) {
				int e = topology.FindEdge(v, w);
				for (int f: topology.GetEdgeFaces(e)) {
					for (int x: topology.GetFaceVertices(f) {
						if ( x != v && x != w) {
							v4_v5_v6_v7.push_back(x);
						}
					}
				}
			}
		}
		int v4 = v4_v5_v6_v7[0], v5 = v4_v5_v6_v7[1], v6 = v4_v5_v6_v7[2], v7 = v4_v5_v6_v7[3];

		// v_new = 0.5*(v0 + v1) + 0.125*(v2 + v3) - 0.0625*(v4 + v5 + v6 + v7)

#if 0
    for (int i = 0; i < nTessPoints; ++i) {
        //  Choose the (s,t) coordinate from the fixed tessellation:
        float const * st = faceIsIrregular ? irregPoints[i != 0]
                         : ((faceSize == 4) ? quadPoints[i] : triPoints[i]);

        //  Locate the patch corresponding to the face ptex idx and (s,t)
        //  and evaluate:
        int patchFace = ptexFace;
        if (faceIsIrregular && (i > 0)) {
            patchFace += i - 1;
        }
        Far::PatchTable::PatchHandle const * handle =
            patchMap->FindPatch(patchFace, st[0], st[1]);
        assert(handle);

        float pWeights[20];
        patchTable->EvaluateBasis(*handle, st[0], st[1], pWeights);

        //  Identify the patch cvs and combine with the evaluated weights --
        //  remember to distinguish cvs in the base level:
        Far::ConstIndexArray cvIndices = patchTable->GetPatchVertices(*handle);

        Pos & pos = tessPoints[i];
        pos.Clear();
        for (int cv = 0; cv < cvIndices.size(); ++cv) {
            int cvIndex = cvIndices[cv];
            if (cvIndex < numBaseVerts) {
                pos.AddWithWeight(basePositions[cvIndex],
                    pWeights[cv]);
            } else {
                pos.AddWithWeight(localPositions[cvIndex - numBaseVerts],
                    pWeights[cv]);
            }
        }
    }

    //  Assign triangles connecting the midpoint of the base face to the
    //  points computed at the ends of each of its edges:
    //
    for (int i = 0; i < nTessFaces; ++i) {
        tessTris[i] = Tri(0, 1 + i, 1 + ((i + 1) % faceSize));
    }
#endif
}
#endif

void PatchGroup::Tessellate(
	PosVector & tessPos,
	TriVector & tessTris) const {

	// This tessellation splits edges at their midpoints
	// and generates 4 resulting triangles per input triangle.
	//
	// Input:
	//           V2
	//          / \
	//		   /  \
	//        /   \
	//      V0----V1
	//
	//
	//           V2
	//          / \
	//        M1--M0
	//       / \ / \
	//      V0-M2-V1

	// Some constants
	const auto& topology = baseRefiner->GetLevel(0);
	const int FACE_COUNT = topology.GetNumFaces();
	const int FACE_SIZE = 3;  // Of course (triangles)...
	const int MIDPOINT_COUNT = 3;  // We split each edge of a triangle
	const int RESULTING_TRIANGLES = 4;  // This gives four resulting triangles

	const char* trianglePatterns[RESULTING_TRIANGLES][FACE_SIZE] = {
		{ "V0", "M2", "M1"},
		{ "V1", "M0", "M2"},
		{ "V2", "M1", "M0"},
		{ "M0", "M1", "M2"},
	};

	// Coordinates of each symbolic point in face local (u,v)
	std::map<const char*, std::array<float, 2> > stMap;
	stMap["V0"] = {0.0f, 0.0f};
	stMap["V1"] = {1.0f, 0.0f};
	stMap["V2"] = {0.0f, 1.0f};
	stMap["M0"] = {0.5f, 0.5f};
	stMap["M1"] = {0.0f, 0.5f};
	stMap["M2"] = {0.5f, 0.0f};

	// Local indices of symbolic points, when they are vertex
	std::map<const char*, int> liMap;
	liMap["V0"] = 0;
	liMap["V1"] = 1;
	liMap["V2"] = 2;

	// Extremities of the edge where the midpoint is set
	std::map<const char*, std::array<int, 2> > eeMap;
	eeMap["M0"] = {1, 2};
	eeMap["M1"] = {2, 0};
	eeMap["M2"] = {0, 1};

	// Offset
	int offset = topology.GetNumVertices();

	// Resize tessPos and tessTris
	tessTris.clear();
	tessPos.resize(offset + topology.GetNumEdges());

	const auto& ptex = basePtexIndices;
    int numBaseVerts = (int) basePositions.size();

	// Tessellate faces
	for (int f = 0; f < FACE_COUNT; ++f) {  // for each face
		const auto faceVertices = topology.GetFaceVertices(f);

		int ptexFace = basePtexIndices.GetFaceId(f);

		for (int t = 0; t < RESULTING_TRIANGLES; ++t) {  // for each triangle to build
			auto pattern = trianglePatterns[t];

			Tri topotriangle;  // Resulting triangle, to be built
			for (int p = 0; p < FACE_SIZE; ++p) {  // for each point in the triangle
				auto sname = pattern[p];  // Point symbolic name

				// Topology
				int vertex;
				if (sname[0] == 'V') {
					// Vertex
					vertex = faceVertices[liMap[sname]];
				} else {
					// Edge midpoint (reminder: indexed with edge number)
					std::array<int, 2> edgeExtremities(eeMap[sname]);

					vertex = topology.FindEdge(
						faceVertices[edgeExtremities[0]],
						faceVertices[edgeExtremities[1]]
					) + offset;  // First elements are reserved for existing vertices
				}
				// Update output
				topotriangle[p] = vertex;

				// Coordinates
				std::array<float, 2> st(stMap[sname]);
				//  Locate the patch corresponding to the face ptex idx and (s,t)
				//  and evaluate:
				Far::PatchTable::PatchHandle const * handle =
					patchMap->FindPatch(ptexFace, st[0], st[1]);
				assert(handle);

				float pWeights[20];
				patchTable->EvaluateBasis(*handle, st[0], st[1], pWeights);

				//  Identify the patch cvs and combine with the evaluated weights --
				//  remember to distinguish cvs in the base level:
				Far::ConstIndexArray cvIndices = patchTable->GetPatchVertices(*handle);

				// Evaluate position
				Pos pos{0.0f, 0.0f, 0.0f};
				for (int cv = 0; cv < cvIndices.size(); ++cv) {
					int cvIndex = cvIndices[cv];
					if (cvIndex < numBaseVerts) {
						pos.AddWithWeight(basePositions[cvIndex],
							pWeights[cv]);
					} else {
						pos.AddWithWeight(localPositions[cvIndex - numBaseVerts],
							pWeights[cv]);
					}
				}

				// Update output
				tessPos[vertex] = pos;

			} // ~points

			tessTris.push_back(topotriangle);

		}  // ~triangles

	}  // ~faces

}  // ~Tessellate






#if 0
		auto faceVertices = topology.GetFaceVertices(f);
		assert(faceVertices.size() == 3);
		for (int triTemplate

		// Add vertex triangles (one per vertex)
		for (int v: faceVertices) {  // for each vertex of the face
			vector<int> triangle;
			for (int w: topology->GetFaceVertices(f)) {  // for each vertex of the face
				if (w == v) {
					triangle.push_back(w);  // We keep the current vertex
				} else {
					triangle.push_back(topology.FindEdge(v, w) + offset);  // We take the middle of the edge
				}
			tessTris.push_back(triangle);
		// Add interior triangle
		tessTris.emplace_back(topology.GetFaceEdges());

		// Evaluate points
		int faceId = ptex.GetFaceId(f);
		

#endif






static TopologyRefinerPtr createTopologyRefinerFromMesh(
		const ExtTriangleMesh* srcMesh,
		const Far::PatchTableFactory::Options& patchOptions,
		PosVector & posVector
) {

	// Initialize corresponding options for adaptive refinement
	Far::TopologyRefiner::AdaptiveOptions adaptiveOptions(8);  // TODO

	// Be sure patch options were intialized with the desired max level
	adaptiveOptions = patchOptions.GetRefineAdaptiveOptions();
	assert(adaptiveOptions.useInfSharpPatch == patchOptions.useInfSharpPatch);


	// Set topology refiner factory's type & options
	Sdc::SchemeType scheme_type = Sdc::SCHEME_LOOP;

	Sdc::Options sdc_options;
	sdc_options.SetVtxBoundaryInterpolation(Sdc::Options::VTX_BOUNDARY_EDGE_AND_CORNER);

	// Populate a topology descriptor with our raw data
	Far::TopologyDescriptor desc;
	desc.numVertices = srcMesh->GetTotalVertexCount();
	desc.numFaces = srcMesh->GetTotalTriangleCount();
	vector<int> vertPerFace(desc.numFaces, 3);
	desc.numVertsPerFace = &vertPerFace[0];
	desc.vertIndicesPerFace = reinterpret_cast<const int *>(srcMesh->GetTriangles());
	//desc.isLeftHanded = true;

#if 0
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
		desc.numCorners = cornerVertexIndices.size();
		desc.cornerVertexIndices = &cornerVertexIndices[0];
		desc.cornerWeights = &cornerWeights[0];
	}
#endif

	// Create refiner
	using RefinerFactory = Far::TopologyRefinerFactory<Far::TopologyDescriptor>;
    TopologyRefinerPtr refiner(
		RefinerFactory::Create(
			desc,
			RefinerFactory::Options(scheme_type, sdc_options)
		)
	);
	assert(refiner);

	// Copy mesh vertices into posVector
	int numVertices = srcMesh->GetTotalVertexCount();
	posVector.resize(numVertices);
	auto meshVertices = srcMesh->GetVertices();
	for (int i = 0; i < numVertices; ++i) {
		posVector[i][0] = meshVertices[i].x;
		posVector[i][1] = meshVertices[i].y;
		posVector[i][2] = meshVertices[i].z;
	}


	// TODO
	refiner->GetLevel(0).PrintTopology();

	return refiner;

}


static ExtTriangleMesh *ApplySubdivAdaptive(
	ExtTriangleMesh *srcMesh,
	const u_int maxLevel
) {
	typedef float Real;

	// https://graphics.pixar.com/opensubdiv/docs/far_tutorial_5_2.html
	PosVector basePositions;

	// Initialize patch table options
	Far::PatchTableFactory::Options patchOptions(maxLevel);
	//patchOptions.SetPatchPrecision<Real>();
	//patchOptions.useInfSharpPatch = true;
	patchOptions.generateVaryingTables = false;
	patchOptions.shareEndCapPatchPoints = true;
	patchOptions.endCapType =
		Far::PatchTableFactory::Options::ENDCAP_GREGORY_BASIS;

	// Construct base refiner
	TopologyRefinerPtr baseRefiner(
		createTopologyRefinerFromMesh(srcMesh, patchOptions, basePositions)
	);
	Far::PtexIndices basePtexIndices(*baseRefiner);

    // Construct the associated PatchTable to evaluate the limit surface:
	PatchTablePtr patchTable(Far::PatchTableFactory::Create(*baseRefiner, patchOptions));
	PatchMapPtr patchMap(new Far::PatchMap(*patchTable));

	// Refinement output constants
	const int nLocalPoints = patchTable->GetNumLocalPoints();

    //
    //  Determine the sizes of the patch groups specified -- there will be
    //  two sizes that differ by one to account for unequal division:
    //
    int numBaseFaces = baseRefiner->GetNumFacesTotal();

    int numPatchGroups = 1;
    int lesserGroupSize = numBaseFaces / numPatchGroups;
    int numLargerGroups = numBaseFaces - (numPatchGroups * lesserGroupSize);


    int objVertCount = 0;

    PosVector tessAllPoints;
    TriVector tessAllFaces;

    for (int i = 0; i < numPatchGroups; ++i) {
		SDL_LOG("Subdiv - Adaptive - Starting patch group " << i);

        //
        //  Initialize a vector with a group of base faces from which to
        //  create and evaluate patches:
        //
		Far::Index minFace = i * lesserGroupSize + std::min(i, numLargerGroups);
		Far::Index maxFace = minFace + lesserGroupSize + (i < numLargerGroups);

        std::vector<Far::Index> baseFaces(maxFace - minFace);
        for (int face = minFace; face < maxFace; ++face) {
            baseFaces[face - minFace] = face;
        }

        //
        //  Declare a PatchGroup and tessellate its base faces -- generating
        //  vertices and faces in Obj format to standard output:
        //
        PatchGroup patchGroup(
			patchOptions,
            baseRefiner,
			basePtexIndices,
			basePositions,
			baseFaces
		);

		PosVector tessPoints;
		TriVector tessFaces;

		patchGroup.Tessellate(tessAllPoints, tessAllFaces);

		for (auto t: tessAllFaces) {
			SDL_LOG("Tris: " << t[0] << " " << t[1] << " " << t[2]);
		}
	}

#if 0
		for (int j = 0; j < (int) baseFaces.size(); ++j) {
			// Tessellate
			patchGroup.Tessellate(j, tessPoints, tessFaces);


			// Append vertices
			tessAllPoints.insert(
				tessAllPoints.end(), tessPoints.begin(), tessPoints.end()
			);

			// Append faces
			int nFaces = (int) tessFaces.size();
			int nPoints = (int) tessPoints.size();
			size_t vBase = objVertCount;
			for (int k = 0; k < nFaces; ++k) {
				auto v = tessFaces[k];
				tessAllFaces.push_back(Tri(vBase + v[0], vBase + v[1], vBase + v[2]));
			}
			objVertCount += nPoints;
		}

    }
#endif

	SDL_LOG("Subdiv - Adaptive - Building new mesh");
	std::ofstream objfile; // TODO
	objfile.open("test.obj");

	// New vertices and triangles
	size_t pointCount = tessAllPoints.size();
	SDL_LOG("Subdiv - Adaptive - " << pointCount << " points");
	Point *newVerts = TriangleMesh::AllocVerticesBuffer(pointCount);
	for (size_t i = 0; i < pointCount; ++i) {
		Point* vert = newVerts + i;
		vert->x = tessAllPoints[i][0];
		vert->y = tessAllPoints[i][1];
		vert->z = tessAllPoints[i][2];
		objfile << "v " << vert->x << " " << vert->y << " " << vert->z << "\n";
	}

	// New triangles
	size_t triCount = tessAllFaces.size();
	SDL_LOG("Subdiv - Adaptive - " << triCount << " triangles");
	Triangle *newTris = TriangleMesh::AllocTrianglesBuffer(triCount);
	for (size_t face = 0; face < triCount; ++face) {
		auto tri = tessAllFaces[face];
		for (u_int vertex = 0; vertex < 3; ++vertex) {
			newTris[face].v[vertex] = tri[vertex];
			assert(tri[vertex] <= pointCount);
			assert(tri[vertex] >= 0);
		}
		// Debug
		objfile << "f " << tri[0] + 1 << " " << tri[1] + 1 << " " << tri[2] + 1 << "\n";
		// SDL_LOG("Triangle: " << tri[0] << " " << tri[1] << " " << tri[2]);
	}

	// Allocate the new mesh
	ExtTriangleMesh *newMesh =  new ExtTriangleMesh(
		pointCount, triCount, newVerts, newTris
	);

	return newMesh;

}



// vim: autoindent noexpandtab tabstop=4 shiftwidth=4
