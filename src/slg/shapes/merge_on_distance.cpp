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

// Cell Id, for grid indexing
union CellId {
	CellId(int16_t x, int16_t y, int16_t z) {
		i16[0] = 0;
		i16[1] = x;
		i16[2] = y;
		i16[3] = z;
	}
	CellId(uint64_t id) : u64(id) {}
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

// Grid definition
struct alignas(std::hardware_destructive_interference_size)
GridElem : public std::pair<u_int, luxrays::Point> {
	GridElem(u_int id, const luxrays::Point& point)
		: std::pair<u_int, luxrays::Point>(id, point) {}
};

using GridBucket = std::vector<GridElem, tbb::cache_aligned_allocator<GridElem>>;
using GridAllocator = tbb::scalable_allocator<std::pair<const CellId, GridBucket>>;
using GridHash = std::hash<CellId>;
using GridEqual = std::equal_to<CellId>;
using GridType = std::unordered_map<CellId, GridBucket, GridHash, GridEqual, GridAllocator>;


// Compute grid, ie origin point (or midPoint) and cell sizes on X, Y, Z
std::tuple<Point, float, float, float>
ComputeGrid(const Point * points, u_int numPoints) {

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
		[](sixfloats&& a, sixfloats&& b) {
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

	SDL_LOG("Merge On Distance: Grid dimensions - "
		<< midPoint << " ("
		<< cellSizeX << ", " << cellSizeY << ", " << cellSizeZ << ")"
	)

	return std::tuple(midPoint, cellSizeX, cellSizeY, cellSizeZ);

}


GridType AssignPointsToGrid(
	Point midPoint,
	float cellSizeX,
	float cellSizeY,
	float cellSizeZ,
	const Point * points,
	u_int numPoints
) {
	// Avoid tiny sets of data for body
	constexpr size_t grain = 1024;

	auto grid = tbb::parallel_reduce(
		// Range
		blocked_range<u_int>(0, numPoints, grain),

		// Init
		GridType(numPoints),

		// Body
		[&](const blocked_range<u_int>& r, GridType&& grid) -> GridType {
			for (u_int i = r.begin(); i != r.end(); ++i) {
				auto p = points[i] - midPoint;
				auto cellX = static_cast<int16_t>(p.x / cellSizeX);
				auto cellY = static_cast<int16_t>(p.y / cellSizeY);
				auto cellZ = static_cast<int16_t>(p.z / cellSizeZ);

				CellId cellId(cellX, cellY, cellZ);
				grid[cellId].emplace_back(i, points[i]);
			}
			return grid;
		},

		// Reduce
		[](GridType&& grid1, GridType&& grid2) -> GridType {
			grid1.merge(grid2);
			for (auto& [cellId, gridBucket2] : grid2) {
				auto& gridBucket1 = grid1[cellId];
				gridBucket1.reserve(gridBucket1.size() + gridBucket2.size());
				gridBucket1.insert(
					gridBucket1.end(), gridBucket2.begin(), gridBucket2.end()
				);
			}
			return grid1;
		}
	);

	// Debug
#if 0
	size_t sup = 0;
	size_t count = 0;
	for (auto const& [key, value] : grid) {
		sup = std::max(value.size(), sup);
		count += value.size();
	}
	SDL_LOG("Grid sup/total: " << sup << " " << count);
#endif

	return grid;
}


// Gather similar points (located at zero distance from each others)
UnionFind ProcessGrid(const GridType& grid, u_int numPoints) {

	auto partitioner = tbb::auto_partitioner();

	// Debug
#if 0
	size_t sup = 0;
	size_t count = 0;
	for (auto const& [key, value] : grid) {
		sup = std::max(value.size(), sup);
		count += value.size();
	}
	SDL_LOG("Grid sup/total: " << sup << " " << count);
#endif

	// Avoid tiny sets of data for body
	constexpr size_t grain = 1024;

    auto res = tbb::parallel_reduce(
		// Range
        tbb::blocked_range<size_t>(0, grid.size(), grain),

		// Init
		UnionFind(numPoints),

		// Body
        [&grid](const tbb::blocked_range<size_t>& r, UnionFind&& dsu) -> UnionFind {
			//SDL_LOG("Begin body: " << r.begin() << " / " << r.end() );  // TODO
            auto it = grid.begin();
            std::advance(it, r.begin());
            for (size_t i = r.begin(); i != r.end(); ++i, ++it) {
                auto [cellId, cellPoints] = *it;
				//SDL_LOG("#points: " << cellPoints.size()); // TODO

                // Check adjacent cells
                for (int16_t dx = -1; dx <= 1; ++dx) {
                    for (int16_t dy = -1; dy <= 1; ++dy) {
                        for (int16_t dz = -1; dz <= 1; ++dz) {

                            CellId adjCellId(
								cellId.x() + dx, cellId.y() + dy, cellId.z() + dz
							);
                            auto adjIt = grid.find(adjCellId);
                            if (adjIt == grid.end()) continue;

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
			//SDL_LOG("End body");  // TODO
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

		// Partitioner
		partitioner
    );
	return res;
}

// Group points into clusters
tbb::concurrent_vector<std::vector<u_int, tbb::scalable_allocator<u_int>>>
CreateClusters(const UnionFind& dsu, u_int numPoints) {
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

	auto [map, keys] = tbb::parallel_reduce(
		// Range
		tbb::blocked_range<u_int>(0, numPoints, grain),

		// Init
		//std::tuple(ClusterMap(numPoints), ClusterKeys(numPoints)),
		std::tuple(ClusterMap(), ClusterKeys()),

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
		[](ClusterMapAndKey&& mapkey1, const ClusterMapAndKey& mapkey2) -> ClusterMapAndKey
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

	SDL_LOG("Middle group");

	// Copy set to vector
	using KeyVector = std::vector<u_int>;
	KeyVector keyvect;
	keyvect.reserve(keys.size());
	std::copy(keys.begin(), keys.end(), std::back_inserter(keyvect));

	// Get an array of clusters
	// TODO Reduce
	tbb::concurrent_vector<std::vector<u_int, tbb::scalable_allocator<u_int>>> clusters;
	clusters.reserve(map.size());
	tbb::parallel_for(
		tbb::blocked_range<KeyVector::iterator>(keyvect.begin(), keyvect.end(), grain),
		[&](const auto& r) {
			for (auto it = r.begin(); it != r.end(); ++it) {
				const auto& [begin, end] = map.equal_range(*it);
				auto new_cluster = clusters.emplace_back();
				new_cluster->reserve(std::distance(begin, end));
				for (auto it = begin; it != end; ++it) {
					auto [key, value] = *it;
					new_cluster->push_back(value);
				}
			}
		}
	);
	return clusters;
}


// Merge point, with spatial partioning acceleration
// This is the entry point of the algorithm
// Returns:
// - The merged points (dynamic array)
// - The number of merged points
// - A map between old points and new points (vector)
std::tuple<std::unique_ptr<Point>, u_int, std::vector<u_int>, std::vector<u_int>>
mergePoints(const Point * points, u_int numPoints) {

	// Compute grid dimensions
	auto [midPoint, cellSizeX, cellSizeY, cellSizeZ] = ComputeGrid(points, numPoints);
	SDL_LOG("After compute grid");
	// Assign points to grid cells
	// We could have written something smarter with tbb::unordered_multimap
	// but it may not be worth spending time on that, as the most common
	// use case should be to call it with low or medium poly
	GridType grid{
		AssignPointsToGrid(midPoint, cellSizeX, cellSizeZ, cellSizeZ, points, numPoints)
	};

	SDL_LOG("After assignment");

	// For each cell, compare points within the cell and adjacent cells
	// and gather points in small distance
	SDL_LOG("Before process");
	auto dsu = ProcessGrid(grid, numPoints);
	SDL_LOG("After process");

	auto clusters = CreateClusters(dsu, numPoints);
	SDL_LOG("After group");

	// Replace each cluster with its centroid
	auto numNewPoints = clusters.size();
	std::vector<u_int> pointMap(numPoints);
	std::unique_ptr<Point> newPoints{
		luxrays::ExtTriangleMesh::AllocVerticesBuffer(numNewPoints)
	};

	auto newPointsPtr = newPoints.get();
	std::vector<u_int> histogram(numPoints);

	// Avoid tiny sets of data for body
	constexpr size_t grain = 1024;

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

				// Compute equivalent point (centroid)
				Point newPoint = std::transform_reduce(
					cluster.cbegin(),
					cluster.cend(),
					Point(0, 0, 0),
					std::plus{},
					[&points](auto idx) -> Point { return points[idx]; }
				) / cluster_size;

				histogram[newIdx] = cluster_size;
				newPointsPtr[newIdx] = newPoint;
			}
		}
	);

	auto out = std::make_tuple(
		std::move(newPoints),
		numNewPoints,
		pointMap,
		histogram
	);
	return out;
}
} // namespace


luxrays::ExtTriangleMesh* slg::MergeOnDistanceShape::ApplyMergeOnDistance(
	luxrays::ExtTriangleMesh * srcMesh
) {
	const double startTime = WallClockTime();

	// Get points
	u_int numOldPoints = srcMesh->GetTotalVertexCount();
	auto [newPoints, numNewPoints, pointMap, histogram] = mergePoints(
		srcMesh->GetVertices(),
		numOldPoints
	);

	// Recompute triangles
	u_int numTriangles = srcMesh->GetTotalTriangleCount();
	auto oldTriangles = srcMesh->GetTriangles();
	auto newTriangles = std::unique_ptr<luxrays::Triangle>(
		luxrays::ExtTriangleMesh::AllocTrianglesBuffer(numTriangles)
	);
	auto newTrianglesPtr = newTriangles.get();
	for (u_int i = 0; i < numTriangles; ++i) {
		auto oldTriangle = oldTriangles[i];
		auto newTriangle = luxrays::Triangle(
			pointMap[oldTriangle.v[0]],
			pointMap[oldTriangle.v[1]],
			pointMap[oldTriangle.v[2]]
		);
		newTrianglesPtr[i] = newTriangle;
	}

	//// Recompute normals
	//std::unique_ptr<luxrays::Normal> newNormals;
	//if (srcMesh->HasNormals()) {
		//auto oldNormals = srcMesh->GetNormals();
		//newNormals.reset(new luxrays::Normal[numNewPoints]);
		//for (u_int i = 0; i < numOldPoints; ++i) {
			//newNormals.get()[pointMap[i]] += oldNormals[i];
		//}
		//for (u_int j = 0; j < numNewPoints; ++j) {
			//auto newNormal = newNormals.get()[j];
			//newNormal /= newNormal.Length();
		//}
	//}
	// Recompute uv
	// Recompute colors
	// Recompute alphas
	// Recompute AOV

	auto newMesh = new luxrays::ExtTriangleMesh(
		numNewPoints,
		numTriangles,
		newPoints.release(),
		newTriangles.release(),
		(luxrays::Normal *) nullptr,
		(luxrays::UV *) nullptr,
		nullptr,
		nullptr,
		0.f
	);

	SDL_LOG(
		"Merge On Distance - Reducing from "
		<< srcMesh->GetTotalVertexCount()
		<< " to "
		<< numNewPoints
		<< " vertices"
	);

	const double endTime = WallClockTime();
	SDL_LOG(std::format("Merging time: {:.3f} secs", endTime - startTime));

	return newMesh;
}

luxrays::ExtTriangleMesh *
slg::MergeOnDistanceShape::RefineImpl(const slg::Scene *scene) {
	return mesh;
}

// vim: autoindent noexpandtab tabstop=4 shiftwidth=4
