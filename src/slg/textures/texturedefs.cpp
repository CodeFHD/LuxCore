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

#include "slg/textures/texturedefs.h"

using namespace std;
using namespace luxrays;
using namespace slg;

//------------------------------------------------------------------------------
// TextureDefinitions
//------------------------------------------------------------------------------

void TextureDefinitions::DefineTexture(TexturePtr newTex) {
	auto oldTex = static_pointer_cast<const Texture>(texs.DefineObj(newTex));

	if (oldTex) {
		// Update all references
		for(auto& tex: texs.GetObjs())
			static_pointer_cast<Texture>(tex)->UpdateTextureReferences(oldTex, newTex);
	}
}

void TextureDefinitions::GetTextureSortedNames(vector<std::string> &names) const {
	std::unordered_set<string> doneNames;

	for (u_int i = 0; i < GetSize(); ++i) {
		TextureConstPtr tex = GetTexture(i);
		
		GetTextureSortedNamesImpl(tex, names, doneNames);
	}
}

void TextureDefinitions::GetTextureSortedNamesImpl(TextureConstPtr tex,
		vector<std::string> &names, std::unordered_set<string> &doneNames) const {
	// Check it has not been already added
	const string &texName = tex->GetName();
	if (doneNames.count(texName) != 0)
		return;

	// Get the list of reference textures by this one
	std::unordered_set<TextureConstPtr> referencedTexs;
	tex->AddReferencedTextures(referencedTexs, tex);

	// Add all referenced texture names
	for (auto refTex : referencedTexs) {
		// AddReferencedTextures() adds also itself to the list of referenced textures
		if (refTex != tex)
			GetTextureSortedNamesImpl(refTex, names, doneNames);
	}

	// I can now add the name of this texture name
	names.push_back(texName);
	doneNames.insert(texName);
}
// vim: autoindent noexpandtab tabstop=4 shiftwidth=4
