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

#include "oneapi/tbb.h"
#include "oneapi/tbb/scalable_allocator.h"

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
    UnionFind(size_t count) {
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
    UnionFind operator+=(UnionFind& other) {

        for (const auto& pair : other.parent) {
            unite(pair.first, pair.second);
        }

        return (*this);
    }

	size_t size() {
		return parent.size();
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
using GridType = std::unordered_map<CellId, std::vector<u_int>>;

UnionFind ProcessGrid(const GridType& grid, const Point* points, u_int numPoints) {

	auto partitioner = tbb::affinity_partitioner();

    auto res = tbb::parallel_reduce(
		// Range
        tbb::blocked_range<size_t>(0, grid.size(), grid.size()),

		// Init
		UnionFind(numPoints),

		// Body
        [&](const tbb::blocked_range<size_t>& r, UnionFind&& dsu) {
            auto it = grid.begin();
            std::advance(it, r.begin());
            for (size_t i = r.begin(); i != r.end(); ++i, ++it) {
                auto& [cellId, cellPoints] = *it;

                // Check adjacent cells
                for (int16_t dx = -1; dx <= 1; ++dx) {
                    for (int16_t dy = -1; dy <= 1; ++dy) {
                        for (int16_t dz = -1; dz <= 1; ++dz) {
                            CellId adjCellId(
								cellId.x() + dx, cellId.y() + dy, cellId.z() + dz
							);
                            auto adjIt = grid.find(adjCellId);
                            if (adjIt == grid.end()) continue;

                            for (auto idx : cellPoints) {
								Point curPoint(points[idx]);
                                for (auto adjIdx : adjIt->second) {
                                    if (idx >= adjIdx) continue;
                                    auto dist = DistanceSquared(curPoint, points[adjIdx]);
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

		// Partitioner
		partitioner
    );
	return res;
}

// Spatial partioning
// Returns:
// - The merged points (dynamic array)
// - The number of merged points
// - A map between old points and new points (vector)
std::tuple<std::unique_ptr<Point>, u_int, std::vector<u_int>, std::vector<u_int>>
mergePoints(Point * points, u_int numPoints) {
	// Compute cellSize
	float minlimit = std::numeric_limits<float>::min();
	float maxlimit = std::numeric_limits<float>::max();

	using sixfloats = std::tuple<float, float, float, float, float, float>;
	auto boundingbox = parallel_reduce(
		blocked_range<Point*>(points, points+numPoints),

		std::make_tuple(maxlimit, maxlimit, maxlimit, minlimit, minlimit, minlimit),

		[](const blocked_range<Point*>& r, sixfloats init) -> sixfloats {
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

		[](sixfloats a, sixfloats b) {
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

	// Should be positive, eventually
	assert(cellSizeX > 0.f);
	assert(cellSizeY > 0.f);
	assert(cellSizeZ > 0.f);

	SDL_LOG(
		"Merge On Distance - Bounding box = "
		<< minX << " / " << maxX << "  "
		<< minY << " / " << maxY << "  "
		<< minZ << " / " << maxZ << "  "
		<< "- cell size "
		<< cellSizeX << " / " << cellSizeY << " / " << cellSizeZ
	);

	// Assign points to grid cells
	GridType grid;
	for (u_int i = 0; i < numPoints; ++i) {
		auto p = points[i] - midPoint;
		auto cellX = static_cast<int16_t>(p.x / cellSizeX);
		auto cellY = static_cast<int16_t>(p.y / cellSizeY);
		auto cellZ = static_cast<int16_t>(p.z / cellSizeZ);

		assert(cellX < (1 << 15));
		assert(cellY < (1 << 15));
		assert(cellZ < (1 << 15));

		CellId cellId(cellX, cellY, cellZ);
		grid[cellId].push_back(i);
	}

	// Debug
#if 0
	for (auto& [cellId, vect] : grid) {
		if (vect.size()) {
			for (auto v : vect) {
				SDL_LOG("Cell " << cellId << ":  " << v << " (" << points[v] << ")");
			}
		}
	}
#endif

	// For each cell, compare points within the cell and adjacent cells
	// and gather points in small distance
	SDL_LOG("Before process");
	auto dsu = ProcessGrid(grid, points, numPoints);
	SDL_LOG("After process");

	// Group points by their root parent
	std::unordered_map<u_int, std::vector<u_int>> clusters;
	std::vector<u_int> pointMap(numPoints);
	for (u_int i = 0; i < numPoints; ++i) {
		auto clusterIndex = dsu.find(i);
		clusters[clusterIndex].push_back(i);
	}

	// Replace each cluster with its centroid
	auto numNewPoints = clusters.size();
	std::unique_ptr<Point> newPoints{luxrays::ExtTriangleMesh::AllocVerticesBuffer(numNewPoints)};
	std::vector<u_int> histogram(numPoints);
	u_int iPoint = 0;
	for (auto& [root, cluster] : clusters) {
		float cx = 0, cy = 0, cz = 0;
		for (const auto& p : cluster) {
			cx += points[p].x;
			cy += points[p].y;
			cz += points[p].z;
			pointMap[p] = iPoint;
		}
		auto cluster_size = cluster.size();
		cx /= cluster_size;
		cy /= cluster_size;
		cz /= cluster_size;

		histogram[iPoint] = cluster_size;
		assert(iPoint < numNewPoints);
		newPoints.get()[iPoint++] = Point(cx, cy, cz);
	}

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

	// Compute cellSize
	float cellSize = 10; // TODO

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
	for (u_int i = 0; i < numTriangles; ++i) {
		auto oldTriangle = oldTriangles[i];
		auto newTriangle = luxrays::Triangle(
			pointMap[oldTriangle.v[0]],
			pointMap[oldTriangle.v[1]],
			pointMap[oldTriangle.v[2]]
		);
		newTriangles.get()[i] = newTriangle;
	}

	// Recompute normals
	std::unique_ptr<luxrays::Normal> newNormals;
	if (srcMesh->HasNormals()) {
		auto oldNormals = srcMesh->GetNormals();
		newNormals.reset(new luxrays::Normal[numNewPoints]);
		for (u_int i = 0; i < numOldPoints; ++i) {
			newNormals.get()[pointMap[i]] += oldNormals[i];
		}
		for (u_int j = 0; j < numNewPoints; ++j) {
			auto newNormal = newNormals.get()[j];
			newNormal /= newNormal.Length();
		}
	}
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
