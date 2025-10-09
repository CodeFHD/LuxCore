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

#include <vector>
#include <cmath>
#include <algorithm>
#include <unordered_map>
#include <tuple>
#include <format>
#include <omp.h>

#include "luxrays/core/exttrianglemesh.h"
#include "slg/shapes/merge_on_distance.h"
#include "slg/scene/scene.h"
#include "luxrays/utils/utils.h"

using luxrays::Point;
using luxrays::WallClockTime;

namespace {
class DSU {
	std::vector<int> parent;
public:
	DSU(int n) : parent(n) {
		for (int i = 0; i < n; ++i) parent[i] = i;
	}
	int find(int u) {
		while (parent[u] != u) {
			parent[u] = parent[parent[u]];
			u = parent[u];
		}
		return u;
	}
	void unite(int u, int v) {
		u = find(u);
		v = find(v);
		if (u != v) parent[v] = u;
	}
};

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

// Spatial partioning
// Returns:
// - The merged points (dynamic array)
// - The number of merged points
// - A map between old points and new points (vector)
std::tuple<std::unique_ptr<Point>, u_int, std::vector<u_int>, std::vector<u_int>>
mergePoints(
	Point * points,
	u_int numPoints,
	float cellSize
) {
	DSU dsu(numPoints);

	// Compute cellSize
	float minX, minY, minZ;
	float maxX, maxY, maxZ;
	minX = minY = minZ = std::numeric_limits<float>::max();
	maxX = maxY = maxZ = std::numeric_limits<float>::min();

	for (u_int i = 0; i < numPoints; ++i) {
		auto p = points[i];
		minX = std::min(minX, p.x);
		minY = std::min(minY, p.y);
		minZ = std::min(minZ, p.z);
		maxX = std::max(maxX, p.x);
		maxY = std::max(maxY, p.y);
		maxZ = std::max(maxZ, p.z);
	}
	float delta = std::max({maxX - minX, maxY - minY, maxZ - minZ});
	cellSize = delta / static_cast<float>(1 << 10);
	cellSize = 10;  // TODO

	// Assign points to grid cells
	std::unordered_map<int, std::vector<int>> grid;
	for (u_int i = 0; i < numPoints; ++i) {
		int cellX = static_cast<int>(points[i].x / cellSize);
		int cellY = static_cast<int>(points[i].y / cellSize);
		int cellZ = static_cast<int>(points[i].z / cellSize);
		int cellId = (cellX << 20) | (cellY << 10) | cellZ; // 10+10+10=30 ~ 32 bits
		grid[cellId].push_back(i);
	}

	// For each cell, compare points within the cell and adjacent cells
	for (auto& [cellId, cellPoints] : grid) {
		int cellX = (cellId >> 20) & 0x3FF;
		int cellY = (cellId >> 10) & 0x3FF;
		int cellZ = cellId & 0x3FF;

		// Check adjacent cells
		for (int dx = -1; dx <= 1; ++dx) {
			for (int dy = -1; dy <= 1; ++dy) {
				for (int dz = -1; dz <= 1; ++dz) {
					int adjCellId = ((cellX + dx) << 20) | ((cellY + dy) << 10) | (cellZ + dz);
					if (grid.find(adjCellId) == grid.end()) continue;

					for (int i : cellPoints) {
						for (int j : grid[adjCellId]) {
							if (i >= j) continue; // Avoid duplicate checks
							float dist = DistanceSquared(points[i], points[j]);
							if (nearly_equal(dist, 0.f, 1)) {
								dsu.unite(i, j);
							}
						}
					}
				}
			}
		}
	}

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

	SDL_LOG("Enter ApplyMergeOnDistance");
	// Get points
	u_int numOldPoints = srcMesh->GetTotalVertexCount();
	auto [newPoints, numNewPoints, pointMap, histogram] = mergePoints(
		srcMesh->GetVertices(),
		numOldPoints,
		cellSize
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
