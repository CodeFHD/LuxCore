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

#include <boost/format.hpp>

#include "slg/scene/extmeshcache.h"

using namespace std;
using namespace luxrays;
using namespace slg;

//------------------------------------------------------------------------------
// ExtMeshCache
//------------------------------------------------------------------------------

ExtMeshCache::ExtMeshCache() {
	deleteMeshData = true;
}

ExtMeshCache::~ExtMeshCache() {
	for(auto& no: meshes.GetObjs()) {
		ExtMesh& mesh = static_cast<ExtMesh &>(*no);

		if (deleteMeshData)
			mesh.Delete();

		// Mesh are deleted by NameObjectVector destructor
	}
}

bool ExtMeshCache::IsExtMeshDefined(const std::string &meshName) const {
	return meshes.IsObjDefined(meshName);
}

void ExtMeshCache::DefineExtMesh(ExtMeshPtr mesh) {
	const string &meshName = mesh->GetName();

	if (!meshes.IsObjDefined(meshName)) {
		// It is a new mesh
		meshes.DefineObj(mesh);
	} else {
		// Check if the meshes are of the same type
		auto meshToReplace = static_pointer_cast<const ExtMesh>(meshes.GetObj(meshName));
		if (meshToReplace->GetType() != mesh->GetType()) {
			throw runtime_error(
				"Mesh " + meshName + " of type " + ToString(mesh->GetType()) +
				" can not replace a mesh of type " + ToString(meshToReplace->GetType())
				+ ". Delete the old mesh first.");
		}

		// Replace an old mesh
		auto oldMesh = static_pointer_cast<ExtMesh>(meshes.DefineObj(mesh));

		if (oldMesh->GetType() == TYPE_EXT_TRIANGLE) {
			// I have also to check/update all instances and motion blur meshes for
			// reference to the old mesh
			auto om = static_pointer_cast<ExtTriangleMesh>(oldMesh);
			auto nm = static_pointer_cast<ExtTriangleMesh>(mesh);

			for(auto& no: meshes.GetObjs()) {
				auto mesh = static_pointer_cast<ExtMesh>(no);

				switch (mesh->GetType()) {
					case TYPE_EXT_TRIANGLE_INSTANCE:
						static_pointer_cast<ExtInstanceTriangleMesh>(mesh)->UpdateMeshReferences(om, nm);
						break;
					case TYPE_EXT_TRIANGLE_MOTION:
						static_pointer_cast<ExtMotionTriangleMesh>(mesh)->UpdateMeshReferences(om, nm);
						break;
					default:
						break;
				}
			}
		}

		// TODO Should be automatic
		if (deleteMeshData)
			oldMesh->Delete();
	}
}

void ExtMeshCache::SetMeshVertexAOV(const string &meshName,
	const unsigned int index, float *data) {
	if (!meshes.IsObjDefined(meshName))
		throw runtime_error("Unknown mesh " + meshName + " while trying to set vertex AOV");

	auto mesh = static_pointer_cast<ExtMesh>(meshes.GetObj(meshName));
	if (mesh->GetType() != TYPE_EXT_TRIANGLE)
		throw runtime_error("Can not set vertex AOV of mesh " + meshName + " of type " + ToString(mesh->GetType()));

	auto triMesh = static_pointer_cast<ExtTriangleMesh>(mesh);
	triMesh->DeleteVertexAOV(index);
	triMesh->SetVertexAOV(index, data);
}

void ExtMeshCache::SetMeshTriangleAOV(const string &meshName,
	const unsigned int index, float *data) {
	if (!meshes.IsObjDefined(meshName))
		throw runtime_error("Unknown mesh " + meshName + " while trying to set triangle AOV");

	auto mesh = static_pointer_cast<ExtMesh>(meshes.GetObj(meshName));
	if (mesh->GetType() != TYPE_EXT_TRIANGLE)
		throw runtime_error("Can not set triangle AOV of mesh " + meshName + " of type " + ToString(mesh->GetType()));

	auto triMesh = static_pointer_cast<ExtTriangleMesh>(mesh);
	triMesh->DeleteTriAOV(index);
	triMesh->SetTriAOV(index, data);
}

void ExtMeshCache::DeleteExtMesh(const string &meshName) {
	if (deleteMeshData) {
		auto mesh = static_pointer_cast<ExtMesh>(meshes.GetObj(meshName));
		mesh->Delete();
	}
	meshes.DeleteObj(meshName);
}

u_int ExtMeshCache::GetSize() const {
	return meshes.GetSize();
}

void ExtMeshCache::GetExtMeshNames(std::vector<std::string> &names) const {
	meshes.GetNames(names);
}

ExtMeshPtr ExtMeshCache::GetExtMesh(const string &meshName) {
	return static_pointer_cast<ExtMesh>(meshes.GetObj(meshName));
}

ExtMeshPtr ExtMeshCache::GetExtMesh(const u_int index) {
	return static_pointer_cast<ExtMesh>(meshes.GetObj(index));
}

u_int ExtMeshCache::GetExtMeshIndex(const string &meshName) const {
	return meshes.GetIndex(meshName);
}

u_int ExtMeshCache::GetExtMeshIndex(ExtMeshConstPtr m) const {
	return meshes.GetIndex(m);
}

string ExtMeshCache::GetRealFileName(ExtMeshConstPtr m) const {
	ExtMeshConstPtr meshToFind;
	if (m->GetType() == TYPE_EXT_TRIANGLE_MOTION) {
		auto mot = static_pointer_cast<const ExtMotionTriangleMesh>(m);
		meshToFind = mot->GetExtTriangleMesh();
	} else if (m->GetType() == TYPE_EXT_TRIANGLE_INSTANCE) {
		auto inst = static_pointer_cast<const ExtInstanceTriangleMesh>(m);
		meshToFind = inst->GetExtTriangleMesh();
	} else
		meshToFind = m;

	return meshes.GetName(meshToFind);
}

string ExtMeshCache::GetSequenceFileName(ExtMeshConstPtr m) const {

	u_int meshIndex;
	if (m->GetType() == TYPE_EXT_TRIANGLE_MOTION) {
		auto mot = static_pointer_cast<const ExtMotionTriangleMesh>(m);
		meshIndex = GetExtMeshIndex(mot->GetExtTriangleMesh());
	} else if (m->GetType() == TYPE_EXT_TRIANGLE_INSTANCE) {
		auto inst = static_pointer_cast<const ExtInstanceTriangleMesh>(m);
		meshIndex = GetExtMeshIndex(inst->GetExtTriangleMesh());
	} else
		meshIndex = GetExtMeshIndex(m);

	return "mesh-" + (boost::format("%05d") % meshIndex).str() + ".ply";
}
// vim: autoindent noexpandtab tabstop=4 shiftwidth=4
