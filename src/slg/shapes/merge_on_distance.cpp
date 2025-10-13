/***************************************************************************
 * Copyright 1998-2025 by authors (see AUTHORS.txt)                        *
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

// This shape modifier will merge vertices whose interdistance is lower than
// a given threshold

#include <cassert>
#include <vector>
#include <cmath>
#include <algorithm>
#include <unordered_map>
#include <tuple>
#include <format>
#include <execution>

#include "oneapi/tbb.h"
#include "oneapi/tbb/scalable_allocator.h"
#include "oneapi/tbb/cache_aligned_allocator.h"

#include "luxrays/core/exttrianglemesh.h"
#include "slg/shapes/merge_on_distance.h"
#include "slg/scene/scene.h"
#include "luxrays/utils/utils.h"

using luxrays::Point;
using luxrays::WallClockTime;
using namespace oneapi::tbb;

namespace {

bool nearly_equal(float a, float b)
{
	return std::nextafter(a, std::numeric_limits<float>::lowest()) <= b
		&& std::nextafter(a, std::numeric_limits<float>::max()) >= b;
}

bool nearly_equal(float a, float b, int factor /* a factor of epsilon */)
{
	float min_a = a - (a - std::nextafter(a, std::numeric_limits<float>::lowest())) * factor;
	float max_a = a + (std::nextafter(a, std::numeric_limits<float>::max()) - a) * factor;

	return min_a <= b && max_a >= b;
}


class UnionFind {
public:
    UnionFind() {}
    explicit UnionFind(size_t count) {
		reserve(count);
	}
	UnionFind(const UnionFind& other) {
		parent = other.parent;
		rank = other.rank;
	}
	UnionFind(UnionFind&& other) {
		parent = std::move(other.parent);
		rank = std::move(other.rank);
	}
	UnionFind& operator=(const UnionFind& other) {
		parent = other.parent;
		rank = other.rank;
		return (*this);
	}
	UnionFind& operator=(UnionFind&& other) {
		parent = std::move(other.parent);
		rank = std::move(other.rank);
		return (*this);
	}

    // Find the root of the set containing element i
    u_int find(u_int i) {
        if (parent.find(i) == parent.end()) {
            parent[i] = i;
            rank[i] = 0;
        }
        if (parent[i] != i) {
            parent[i] = find(parent[i]); // Path compression
        }
        return parent[i];
    }

    // Union the sets containing elements i and j
    void unite(u_int i, u_int j) {
        u_int rootI = find(i);
        u_int rootJ = find(j);

        if (rootI != rootJ) {
            // Union by rank
            if (rank[rootI] > rank[rootJ]) {
                parent[rootJ] = rootI;
            } else if (rank[rootI] < rank[rootJ]) {
                parent[rootI] = rootJ;
            } else {
                parent[rootJ] = rootI;
                rank[rootI]++;
            }
        }
    }

	// Reserve space
	void reserve(size_t count) {
		parent.reserve(count);
		rank.reserve(count);
	}

    // Overload the += operator to merge two UnionFind instances
    UnionFind operator+=(const UnionFind& other) {

        for (const auto& pair : other.parent) {
            unite(pair.first, pair.second);
        }

        return (*this);
    }

	size_t size() {
		return parent.size();
	}

	// Find without compression
	u_int find_readonly(u_int i) const {
		auto res = parent.find(i);
        if (res != parent.end()) {
			return res->second;
		} else {
			return i;
		}

	}

private:
	using Allocator = tbb::scalable_allocator<std::pair<const u_int, u_int>>;
	using Hash = std::hash<u_int>;
	using Equal = std::equal_to<u_int>;
    std::unordered_map<u_int, u_int, Hash, Equal, Allocator> parent;
    std::unordered_map<u_int, u_int, Hash, Equal, Allocator> rank;

    friend std::ostream& operator<<(std::ostream& os, const UnionFind& uf);
};

std::ostream& operator<<(std::ostream& os, const UnionFind& uf) {
    os << "Parent: ";
    for (const auto& pair : uf.parent) {
        os << "(" << pair.first << ", " << pair.second << ") ";
    }
    os << "\nRank: ";
    for (const auto& pair : uf.rank) {
        os << "(" << pair.first << ", " << pair.second << ") ";
    }
    return os;
}

// Cell Id, for partition indexing
// This id can be handled in two ways:
// - 3-uple of signed int 16
// - an unsigned int 64 (with first 16 bits set to zero)
union CellId {
	CellId(int16_t x, int16_t y, int16_t z) {
		i16[0] = 0;
		i16[1] = x;
		i16[2] = y;
		i16[3] = z;
	}
	CellId(uint64_t id) : u64(id) {
		if (i16[0]) {
			throw std::runtime_error("Invalid CellId");
		}
	}
	CellId(const CellId& other) { u64 = other.u64; }

	int16_t x() const { return i16[1]; }
	int16_t y() const { return i16[2]; }
	int16_t z() const { return i16[3]; }
	uint64_t id() const { return u64; }

	bool operator==(const CellId& other) const {
		return u64 == other.u64;
	}
private:
	uint64_t u64;
	int16_t i16[4];
};

}

// Introduce hash into std namespace
namespace std {
  template <> struct hash<CellId>
  {
    size_t operator()(const CellId & x) const
    {
      return x.id();
    }
  };
}

namespace {

// Grid for spatial partitioning
class Grid {
public:
	Grid(
		const Point& p_origin,
		float p_cellSizeX,
		float p_cellSizeY,
		float p_cellSizeZ
	) :
		m_origin(p_origin),
		m_cellSizeX(p_cellSizeX),
		m_cellSizeY(p_cellSizeY),
		m_cellSizeZ(p_cellSizeZ)
	{};

	const Point& origin() const { return m_origin; }
	const float cellSizeX() const { return m_cellSizeX; }
	const float cellSizeY() const { return m_cellSizeY; }
	const float cellSizeZ() const { return m_cellSizeZ; }

private:
	Point m_origin;
	float m_cellSizeX, m_cellSizeY, m_cellSizeZ;

};



// Create a grid, ie give an origin point (or midPoint) and cell sizes on X, Y,
// Z
Grid ComputeGrid(const Point * points, u_int numPoints) {

	constexpr float minlimit = std::numeric_limits<float>::min();
	constexpr float maxlimit = std::numeric_limits<float>::max();

	using sixfloats = std::tuple<float, float, float, float, float, float>;

	constexpr size_t grain = 1024;

	// Compute bounding box
	auto boundingbox = parallel_reduce(
		// Range
		blocked_range<const Point*>(points, points+numPoints, grain),

		// Init
		std::make_tuple(maxlimit, maxlimit, maxlimit, minlimit, minlimit, minlimit),

		// Body
		[](const blocked_range<const Point*>& r, sixfloats init) -> sixfloats {
			auto [minX, minY, minZ, maxX, maxY, maxZ] = init;
			for(auto p=r.begin(); p!=r.end(); ++p) {
				minX = std::min(minX, p->x);
				minY = std::min(minY, p->y);
				minZ = std::min(minZ, p->z);
				maxX = std::max(maxX, p->x);
				maxY = std::max(maxY, p->y);
				maxZ = std::max(maxZ, p->z);
			}
			return std::make_tuple(minX, minY, minZ, maxX, maxY, maxZ);
		},

		// Reduce
		[](const sixfloats& a, const sixfloats& b) {
			auto [minXa, minYa, minZa, maxXa, maxYa, maxZa] = a;
			auto [minXb, minYb, minZb, maxXb, maxYb, maxZb] = b;
			return std::make_tuple(
				std::min(minXa, minXb),
				std::min(minYa, minYb),
				std::min(minZa, minZb),
				std::max(maxXa, maxXb),
				std::max(maxYa, maxYb),
				std::max(maxZa, maxZb)
			);
		}
	);

	auto [minX, minY, minZ, maxX, maxY, maxZ] = boundingbox;
	Point midPoint((minX + maxX) / 2, (minY + maxY) / 2, (minZ + maxZ) / 2);

	// Compute cell sizes
	constexpr float numCells = static_cast<float>(1 << 16);
	float cellSizeX = (maxX - minX) / numCells;
	float cellSizeY = (maxY - minY) / numCells;
	float cellSizeZ = (maxZ - minZ) / numCells;

	// Make the cells slightly larger than the bounding box
	cellSizeX = std::nextafterf(cellSizeX, INFINITY);
	cellSizeY = std::nextafterf(cellSizeY, INFINITY);
	cellSizeZ = std::nextafterf(cellSizeZ, INFINITY);

	SDL_LOG("Merge On Distance - Grid dimensions: Origin = "
		<< midPoint << " Cell size = ("
		<< cellSizeX << ", " << cellSizeY << ", " << cellSizeZ << ")"
	)

	return Grid(midPoint, cellSizeX, cellSizeY, cellSizeZ);

}

// The partition object (and subobjects)

// Partition element: a point number and its coordinates
struct alignas(std::hardware_destructive_interference_size)
PartitionElem : public std::pair<u_int, luxrays::Point> {
	PartitionElem(u_int id, const luxrays::Point& point)
		: std::pair<u_int, luxrays::Point>(id, point) {}
};

using PartitionBucket = std::vector<
	PartitionElem,
	tbb::cache_aligned_allocator<PartitionElem>
>;
using PartitionAllocator = tbb::scalable_allocator<
	std::pair<const CellId, PartitionBucket>
>;
using PartitionHash = std::hash<CellId>;
using PartitionEqual = std::equal_to<CellId>;

using Partition = std::unordered_map<
	CellId,
	PartitionBucket,
	PartitionHash,
	PartitionEqual,
	PartitionAllocator
>;

// Assign points to grid (do the partitioning)
Partition AssignPointsToGrid(
	const Grid& grid, const Point * points, u_int numPoints
) {
	// Avoid tiny sets of data for body
	constexpr size_t grain = 1024;

	auto partition = tbb::parallel_reduce(
		// Range
		blocked_range<u_int>(0, numPoints, grain),

		// Init
		Partition(numPoints),

		// Body
		[&](const blocked_range<u_int>& r, Partition&& partition) -> Partition {
			for (u_int i = r.begin(); i != r.end(); ++i) {
				auto p = points[i] - grid.origin();
				auto cellX = static_cast<int16_t>(p.x / grid.cellSizeX());
				auto cellY = static_cast<int16_t>(p.y / grid.cellSizeY());
				auto cellZ = static_cast<int16_t>(p.z / grid.cellSizeZ());

				CellId cellId(cellX, cellY, cellZ);
				partition[cellId].emplace_back(i, points[i]);
			}
			return partition;
		},

		// Reduce
		[](Partition&& partition1, Partition&& partition2) -> Partition {
			partition1.merge(partition2);
			for (auto& [cellId, partitionBucket2] : partition2) {
				auto& partitionBucket1 = partition1[cellId];
				partitionBucket1.reserve(partitionBucket1.size() + partitionBucket2.size());
				partitionBucket1.insert(
					partitionBucket1.end(), partitionBucket2.begin(), partitionBucket2.end()
				);
			}
			return partition1;
		}
	);

	// Debug
#if 0
	size_t sup = 0;
	size_t count = 0;
	for (auto const& [key, value] : partition) {
		sup = std::max(value.size(), sup);
		count += value.size();
	}
	SDL_LOG("Grid sup/total: " << sup << " " << count);
#endif

	return partition;
}


// Gather equivalent points ("equivalent" meaning located at zero distance from
// each others)
// Points have previously been assigned to grid cells, so that we
// just compare points within same cells (saves a lot of time)
UnionFind GroupPoints(const Partition& partition, u_int numPoints) {

	auto partitioner = tbb::auto_partitioner();

	// Debug
#if 0
	size_t sup = 0;
	size_t count = 0;
	for (auto const& [key, value] : partition) {
		sup = std::max(value.size(), sup);
		count += value.size();
	}
	SDL_LOG("Grid sup/total: " << sup << " " << count);
#endif

	// Avoid tiny sets of data for body
	constexpr size_t grain = 1024;

    auto res = tbb::parallel_reduce(
		// Range
        tbb::blocked_range<size_t>(0, partition.size(), grain),

		// Init
		UnionFind(numPoints),

		// Body
        [&partition](const tbb::blocked_range<size_t>& r, UnionFind&& dsu) -> UnionFind {
            auto it = partition.begin();
            std::advance(it, r.begin());
            for (size_t i = r.begin(); i != r.end(); ++i, ++it) {
                auto [cellId, cellPoints] = *it;

                // Check adjacent cells
                for (int16_t dx = -1; dx <= 1; ++dx) {
                    for (int16_t dy = -1; dy <= 1; ++dy) {
                        for (int16_t dz = -1; dz <= 1; ++dz) {

                            CellId adjCellId(
								cellId.x() + dx, cellId.y() + dy, cellId.z() + dz
							);
                            auto adjIt = partition.find(adjCellId);
                            if (adjIt == partition.end()) continue;

							// For each point in current cell and for each point
							// in adjacent cell, compute distance
                            for (auto [idx, curPoint] : cellPoints) {
                                for (auto [adjIdx, adjPoint] : adjIt->second) {
                                    if (idx >= adjIdx) continue;
                                    auto dist = DistanceSquared(curPoint, adjPoint);
                                    if (nearly_equal(dist, 0.f)) {
                                        dsu.unite(idx, adjIdx);
                                    }
                                }
                            }
                        }  // for dz
                    }  // for dy
                }  // for dx
            }  // for i
			return dsu;
        },  // lambda

		// Reduce
		[](UnionFind&& dsu1, UnionFind&& dsu2) -> UnionFind {
			if (dsu1.size() < dsu2.size()) {
				dsu2 += dsu1;
				return dsu2;
			} else {
				dsu1 += dsu2;
				return dsu1;
			}
		},

		// Partitioner (for tbb)
		partitioner
    );
	return res;
}


using Cluster = std::vector<u_int, tbb::scalable_allocator<u_int>>;
using ClusterVector = std::vector<Cluster, tbb::scalable_allocator<Cluster>>;

// Group equivalent points into clusters
ClusterVector CreateClusters(const UnionFind& dsu, u_int numPoints) {
	// Group equivalent points into map
	using ClusterMap = std::unordered_multimap<
		u_int,
		u_int,
		std::hash<u_int>,
		std::equal_to<u_int>,
		tbb::scalable_allocator<std::pair<const u_int, u_int>>
	>;
	using ClusterKeys = std::unordered_set<
		u_int,
		std::hash<u_int>,
		std::equal_to<u_int>,
		tbb::scalable_allocator<u_int>
	>;
	using ClusterMapAndKey = std::tuple<ClusterMap, ClusterKeys>;

	// Avoid tiny sets of data for body
	constexpr size_t grain = 1024;

	// Get clusters as a multimap and a set of keys
	auto [map, keys] = tbb::parallel_reduce(
		// Range
		tbb::blocked_range<u_int>(0, numPoints, grain),

		// Init
		std::tuple(ClusterMap(grain), ClusterKeys(grain)),

		// Body
		[&dsu](const tbb::blocked_range<u_int>& r, ClusterMapAndKey&& mapkey)
			-> ClusterMapAndKey
		{
			auto [map, keys] = mapkey;

			const auto number_of_elements = r.end() - r.begin();
			map.reserve(number_of_elements);
			keys.reserve(number_of_elements);

			for (u_int i = r.begin(); i != r.end(); ++i) {
				auto clusterIndex = dsu.find_readonly(i);
				keys.insert(clusterIndex);
				map.emplace(clusterIndex, i);
			}
			return std::make_tuple(map, keys);
		},

		// Reduce
		[](ClusterMapAndKey&& mapkey1, const ClusterMapAndKey& mapkey2)
			-> ClusterMapAndKey
		{
			auto [map1, keys1] = mapkey1;
			const auto& [map2, keys2] = mapkey2;

			map1.reserve(map1.size() + map2.size());
			keys1.reserve(keys1.size() + keys2.size());

			map1.insert(map2.cbegin(), map2.cend());
			keys1.insert(keys2.cbegin(), keys2.cend());

			return std::tuple(map1, keys1);
		}
	);

	// Transform the previous structure into a vector of vectors

	auto clusters = tbb::parallel_reduce(
		// Range
		tbb::blocked_range<size_t>(0, keys.size(), grain),

		// Init
		ClusterVector(),

		// Body
		[&](const auto& r, ClusterVector&& clustervect) -> ClusterVector {
			auto it = keys.begin();
			std::advance(it, r.begin());
			for (auto i = r.begin(); i != r.end(); ++i, ++it) {
				const auto& [begin, end] = map.equal_range(*it);
				Cluster cluster;
				cluster.reserve(std::distance(begin, end));
				for (auto itval = begin; itval != end; ++itval) {
					cluster.push_back(itval->second);
				}
				clustervect.push_back(cluster);
			}
			return clustervect;
		},

		// Reduce
		[](ClusterVector&& a, const ClusterVector& b) -> ClusterVector {
			std::copy(b.begin(), b.end(), std::back_inserter(a));
			return a;
		}

	);

	return clusters;
}


// Merge nearby points, with spatial partioning acceleration
//
// This is the entry point of the merging algorithm
//
// Spatial partioning means that the points are first partitioned into a grid,
// and the distance computations are made only between points within the same
// grid cell and adjacent cells. This saves a lot of computations in case of
// large collection of points
//
// Returns:
// - The merged points, in the form of clusters (vector of vectors)
//
ClusterVector mergePoints(const Point * points, u_int numPoints) {

	// Compute grid for spatial partitioning
	Grid grid{ComputeGrid(points, numPoints)};

	// Assign points to grids cells (in other words: partition)
	Partition partition{
		AssignPointsToGrid(grid, points, numPoints)
	};

	// For each cell, compare points within the cell and adjacent cells
	// and gather points at small distance from each others.
	// Gathering is made via a Union Find algo
	UnionFind dsu{GroupPoints(partition, numPoints)};

	// Finally, reformat result into convenient cluster format (vector of
	// vectors)
	ClusterVector clusters{CreateClusters(dsu, numPoints)};

	return clusters;

}


// Recreate a mesh, based on a source mesh and a clusterisation
// Replace each variable with interpolated value
luxrays::ExtTriangleMesh* RecreateMesh(
	const luxrays::ExtTriangleMesh& srcMesh,
	const ClusterVector& clusters
) {
	const auto numPoints = srcMesh.GetTotalVertexCount();
	const auto srcPoints = srcMesh.GetVertices();
	const auto numNewPoints = clusters.size();

	// Nota: we use smart pointers until the creation of the ExtTriangleMesh,
	// so that the memory is automatically released if something goes wrong in
	// the process. Please keep it so.

	std::vector<u_int> pointMap(numPoints);

	// Points
	std::unique_ptr<Point> newPoints{
		luxrays::ExtTriangleMesh::AllocVerticesBuffer(numNewPoints)
	};
	auto newPointsPtr = newPoints.get();

	// Normals
	std::unique_ptr<luxrays::Normal> newNormals;
	const auto srcNormals = srcMesh.GetNormals();
	if (srcMesh.HasNormals()) {
		newNormals.reset(new luxrays::Normal[numNewPoints]);
	}
	auto newNormalsPtr = newNormals.get();

	// UV
	std::array<std::unique_ptr<luxrays::UV>, EXTMESH_MAX_DATA_COUNT> newUVs;
	std::array<luxrays::UV*, EXTMESH_MAX_DATA_COUNT> srcUVs;
	for (u_int i = 0; i < EXTMESH_MAX_DATA_COUNT; ++i) {
		if (srcMesh.HasUVs(i)) {
			srcUVs[i] = srcMesh.GetUVs(i);
			newUVs[i].reset(new luxrays::UV[numNewPoints]);
		}
	}

	// Colors
	std::array<std::unique_ptr<luxrays::Spectrum>, EXTMESH_MAX_DATA_COUNT> newColors;
	std::array<luxrays::Spectrum*, EXTMESH_MAX_DATA_COUNT> srcColors;
	for (u_int i = 0; i < EXTMESH_MAX_DATA_COUNT; ++i) {
		if (srcMesh.HasColors(i)) {
			srcColors[i] = srcMesh.GetColors(i);
			newColors[i].reset(new luxrays::Spectrum[numNewPoints]);
		}
	}

	// Alphas
	std::array<std::unique_ptr<float>, EXTMESH_MAX_DATA_COUNT> newAlphas;
	std::array<float*, EXTMESH_MAX_DATA_COUNT> srcAlphas;
	for (u_int i = 0; i < EXTMESH_MAX_DATA_COUNT; ++i) {
		if (srcMesh.HasAlphas(i)) {
			srcAlphas[i] = srcMesh.GetAlphas(i);
			newAlphas[i].reset(new float[numNewPoints]);
		}
	}

	// VertexAOVs
	std::array<std::unique_ptr<float>, EXTMESH_MAX_DATA_COUNT> newVertexAOVs;
	std::array<float*, EXTMESH_MAX_DATA_COUNT> srcVertexAOVs;
	for (u_int i = 0; i < EXTMESH_MAX_DATA_COUNT; ++i) {
		if (srcMesh.HasVertexAOV(i)) {
			srcVertexAOVs[i] = srcMesh.GetVertexAOVs(i);
			newVertexAOVs[i].reset(new float[numNewPoints]);
		}
	}

	// Avoid tiny sets of data for tbb body
	constexpr size_t grain = 1024;

	// Compute merged values of points, normals, uv etc.
	tbb::parallel_for(
        tbb::blocked_range<size_t>(0, numNewPoints, grain),
		[&](tbb::blocked_range<size_t>& r) {
			for (auto newIdx = r.begin(); newIdx != r.end(); ++newIdx) {
				auto& cluster = clusters[newIdx];
				auto cluster_size = cluster.size();
				if (!cluster_size) continue;

				// Compute point map (mapping between old and new points)
				for (auto oldIdx: cluster) {
					pointMap[oldIdx] = newIdx;
				}

				// Compute merged points (centroids)
				Point newPoint = std::transform_reduce(
					cluster.cbegin(),
					cluster.cend(),
					Point(0, 0, 0),
					std::plus{},
					[&srcPoints](auto idx) -> Point { return srcPoints[idx]; }
				) / cluster_size;
				newPointsPtr[newIdx] = newPoint;

				// Compute merged normals
				if (srcMesh.HasNormals()) {
					luxrays::Normal newNormal = std::transform_reduce(
						cluster.cbegin(),
						cluster.cend(),
						luxrays::Normal(0, 0, 0),
						std::plus{},
						[&srcNormals](auto idx) -> luxrays::Normal {
							return srcNormals[idx];
						}
					) / cluster_size;
					const float newNormalLength = newNormal.Length();
					if (newNormalLength) {
						newNormal /= newNormalLength;
					}
					newNormalsPtr[newIdx] = newNormal;
				}

				// Compute merged uv
				for (u_int i = 0; i < EXTMESH_MAX_DATA_COUNT; ++i) {
					if (srcMesh.HasUVs(i)) {
						auto newUVPtr = newUVs[i].get();
						luxrays::UV newUV = std::transform_reduce(
							cluster.cbegin(),
							cluster.cend(),
							luxrays::UV(0, 0),
							std::plus{},
							[&srcUVs, &i](auto idx) -> luxrays::UV {
								return srcUVs[i][idx];
							}
						) / cluster_size;
						newUVs[i].get()[newIdx] = newUV;
					}
				}

				// Compute merged colors
				for (u_int i = 0; i < EXTMESH_MAX_DATA_COUNT; ++i) {
					if (srcMesh.HasColors(i)) {
						auto newColorsPtr = newColors[i].get();
						luxrays::Spectrum newColor = std::transform_reduce(
							cluster.cbegin(),
							cluster.cend(),
							luxrays::Spectrum(0, 0, 0),
							std::plus{},
							[&srcColors, &i](auto idx) -> luxrays::Spectrum {
								return srcColors[i][idx];
							}
						) / cluster_size;
						newColorsPtr[newIdx] = newColor;
					}
				}

				// Compute merged alphas
				for (u_int i = 0; i < EXTMESH_MAX_DATA_COUNT; ++i) {
					if (srcMesh.HasAlphas(i)) {
						auto newAlphasPtr = newAlphas[i].get();
						float newAlpha = std::transform_reduce(
							cluster.cbegin(),
							cluster.cend(),
							0.f,
							std::plus{},
							[&srcAlphas, &i](auto idx) -> float {
								return srcAlphas[i][idx];
							}
						) / cluster_size;
						newAlphasPtr[newIdx] = newAlpha;
					}
				}

				// Compute merged vertex AOVs
				for (u_int i = 0; i < EXTMESH_MAX_DATA_COUNT; ++i) {
					if (srcMesh.HasVertexAOV(i)) {
						auto newVertexAOVsPtr = newVertexAOVs[i].get();
						float newVertexAOV = std::transform_reduce(
							cluster.cbegin(),
							cluster.cend(),
							0.f,
							std::plus{},
							[&srcVertexAOVs, &i](auto idx) -> float {
								return srcVertexAOVs[i][idx];
							}
						) / cluster_size;
						newVertexAOVsPtr[newIdx] = newVertexAOV;
					}
				}
			}  // lambda main for loop
		}  // Body lambda
	);  // tbb::parallel_for

	// Recompute triangles
	u_int numTriangles = srcMesh.GetTotalTriangleCount();
	auto oldTriangles = srcMesh.GetTriangles();
	auto newTriangles = std::unique_ptr<luxrays::Triangle>(
		luxrays::ExtTriangleMesh::AllocTrianglesBuffer(numTriangles)
	);
	auto newTrianglesPtr = newTriangles.get();
	tbb::parallel_for(
		tbb::blocked_range<u_int>(0, numTriangles),
		[&](const tbb::blocked_range<u_int>& r) {
			for (u_int i = r.begin(); i != r.end(); ++i) {
				auto oldTriangle = oldTriangles[i];
				auto newTriangle = luxrays::Triangle(
					pointMap[oldTriangle.v[0]],
					pointMap[oldTriangle.v[1]],
					pointMap[oldTriangle.v[2]]
				);
				newTrianglesPtr[i] = newTriangle;
			}
		}
	);

	// Create layer arrays (release smart pointers...)
	auto meshUVs = new std::array<luxrays::UV *, EXTMESH_MAX_DATA_COUNT>;
	auto meshCols = new std::array<luxrays::Spectrum *, EXTMESH_MAX_DATA_COUNT>;
	auto meshAlphas = new std::array<float *, EXTMESH_MAX_DATA_COUNT>;

	for (u_int i = 0; i < EXTMESH_MAX_DATA_COUNT; ++i) {
		if (srcMesh.HasUVs(i)) {
			(*meshUVs)[i] = newUVs[i].release();
		} else {
			(*meshUVs)[i] = nullptr;
		}

		if (srcMesh.HasColors(i)) {
			(*meshCols)[i] = newColors[i].release();
		} else {
			(*meshCols)[i] = nullptr;
		}

		if (srcMesh.HasAlphas(i)) {
			(*meshAlphas)[i] = newAlphas[i].release();
		} else {
			(*meshAlphas)[i] = nullptr;
		}
	}

	// Create new mesh
	auto newMesh = new luxrays::ExtTriangleMesh(
		numNewPoints,
		numTriangles,
		newPoints.release(),
		newTriangles.release(),
		newNormals.release(),
		meshUVs,
		meshCols,
		meshAlphas,
		srcMesh.GetBevelRadius()
	);

	// Copy AOV to new mesh
	for (u_int i = 0; i < EXTMESH_MAX_DATA_COUNT; ++i) {
		if (srcMesh.HasVertexAOV(i)) {
			newMesh->SetVertexAOV(i, newVertexAOVs[i].release());
		}
	}

	return newMesh;
}



} // namespace


namespace slg {

MergeOnDistanceShape::MergeOnDistanceShape(luxrays::ExtTriangleMesh * srcMesh) {

	SDL_LOG("Merge On Distance - Applying to " << srcMesh->GetName());

	const double startTime = WallClockTime();

	mesh = ApplyMergeOnDistance(srcMesh);

	const double endTime = WallClockTime();
	SDL_LOG(
		std::format(
			"Merge On Distance - Merging time: {:.3f} secs", endTime - startTime
		)
	);
}

MergeOnDistanceShape::~MergeOnDistanceShape() {
	if (!refined)
		delete mesh;
}

luxrays::ExtTriangleMesh*
MergeOnDistanceShape::ApplyMergeOnDistance(luxrays::ExtTriangleMesh * srcMesh) {


	// Get merged points
	auto clusters = mergePoints(
		srcMesh->GetVertices(),
		srcMesh->GetTotalVertexCount()
	);

	auto dstMesh = RecreateMesh(*srcMesh, clusters);


	SDL_LOG(
		"Merge On Distance - Reduced from "
		<< srcMesh->GetTotalVertexCount()
		<< " to "
		<< dstMesh->GetTotalVertexCount()
		<< " vertices"
	);

	return dstMesh;
}

luxrays::ExtTriangleMesh *
MergeOnDistanceShape::RefineImpl(const slg::Scene *scene) {
	return mesh;
}

}  // namespace slg

// vim: autoindent noexpandtab tabstop=4 shiftwidth=4
