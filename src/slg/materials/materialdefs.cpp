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

#include <boost/lexical_cast.hpp>

#include "slg/materials/materialdefs.h"

using namespace std;
using namespace luxrays;
using namespace slg;

//------------------------------------------------------------------------------
// MaterialDefinitions
//------------------------------------------------------------------------------

void MaterialDefinitions::DefineMaterial(MaterialPtr newMat) {
	auto oldMat = dynamic_pointer_cast<const Material>(mats.DefineObj(newMat));

	if (oldMat) {
		// Update all references
		for(auto& obj: mats.GetObjs()) {
			// Update all references in material/volume (note: volume is also a material)
			dynamic_pointer_cast<Material>(obj)->UpdateMaterialReferences(oldMat, newMat);
		}
	}
}

void MaterialDefinitions::UpdateTextureReferences(TextureConstPtr oldTex, TextureConstPtr newTex) {
	for(auto& mat: mats.GetObjs())
		dynamic_pointer_cast<Material>(mat)->UpdateTextureReferences(oldTex, newTex);
}

void MaterialDefinitions::GetMaterialSortedNames(vector<std::string> &names) const {
	std::unordered_set<string> doneNames;

	for (u_int i = 0; i < GetSize(); ++i) {
		auto mat = GetMaterial(i);

		GetMaterialSortedNamesImpl(mat, names, doneNames);
	}
}

void MaterialDefinitions::GetMaterialSortedNamesImpl(
	MaterialConstPtr mat,
	vector<std::string> &names,
	std::unordered_set<string> &doneNames
) const {
	// Check it has not been already added
	const string &matName = mat->GetName();
	if (doneNames.count(matName) != 0)
		return;

	// Get the list of reference materials by this one
	std::unordered_set<MaterialConstPtr> referencedTexs;
	mat->AddReferencedMaterials(referencedTexs, mat);

	// Add all referenced texture names
	for (auto& refMat : referencedTexs) {
		// AddReferencedMaterials() adds also itself to the list of referenced materials
		if (refMat != mat)
			GetMaterialSortedNamesImpl(refMat, names, doneNames);
	}

	// I can now add the name of this texture name
	names.push_back(matName);
	doneNames.insert(matName);
}
// vim: autoindent noexpandtab tabstop=4 shiftwidth=4
