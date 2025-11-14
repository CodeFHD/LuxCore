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

#ifndef _SLG_TRIPLANAR_H
#define	_SLG_TRIPLANAR_H

#include "slg/textures/texture.h"

namespace slg {

//------------------------------------------------------------------------------
// Triplanar Mapping texture
//------------------------------------------------------------------------------

class TriplanarTexture : public Texture {
public:
	TriplanarTexture(TextureMapping3DConstPtr mp, TextureConstPtr t1, TextureConstPtr t2, 
    TextureConstPtr t3, const bool uvlessBumpMap) :
    mapping(mp), texX(t1), texY(t2), texZ(t3),
	enableUVlessBumpMap(uvlessBumpMap) {}

	virtual ~TriplanarTexture() {}

	virtual TextureType GetType() const {
		return TRIPLANAR_TEX;
	}
	virtual float GetFloatValue(const HitPoint &hitPoint) const;
	virtual luxrays::Spectrum GetSpectrumValue(const HitPoint &hitPoint) const;

	virtual float Y() const {
		return (texX->Y() + texY->Y() + texZ->Y()) * (1.f / 3.f);
	}

	virtual float Filter() const {
		return (texX->Filter() + texY->Filter() + texZ->Filter()) * (1.f / 3.f);
	}

	virtual luxrays::Normal Bump(const HitPoint &hitPoint, const float sampleDistance) const;

	virtual void AddReferencedTextures(std::unordered_set<TextureConstPtr>  &referencedTexs, TextureConstPtr self) const {
		Texture::AddReferencedTextures(referencedTexs, self);

		texX->AddReferencedTextures(referencedTexs, texX);
		texY->AddReferencedTextures(referencedTexs, texY);
		texZ->AddReferencedTextures(referencedTexs, texZ);
	}

	virtual void AddReferencedImageMaps(std::unordered_set<ImageMapConstPtr > &referencedImgMaps) const {
		texX->AddReferencedImageMaps(referencedImgMaps);
		texY->AddReferencedImageMaps(referencedImgMaps);
		texZ->AddReferencedImageMaps(referencedImgMaps);
	}

	virtual void UpdateTextureReferences(TextureConstPtr oldTex, TextureConstPtr newTex) {
		if (texX == oldTex)
			texX = newTex;
		if (texY == oldTex)
			texY = newTex;
		if (texZ == oldTex)
			texZ = newTex;
	}

	TextureMapping3DConstPtr GetTextureMapping() const { return mapping; }
	TextureConstPtr GetTexture1() const { return texX; }
	TextureConstPtr GetTexture2() const { return texY; }
    TextureConstPtr GetTexture3() const { return texZ; }
	const bool IsUVlessBumpMap() const { return enableUVlessBumpMap; }


	virtual luxrays::Properties ToProperties(const ImageMapCache &imgMapCache, const bool useRealFileName) const;

private:
	TextureMapping3DConstPtr mapping;
	TextureConstPtr texX;
	TextureConstPtr texY;
    TextureConstPtr texZ;

	const bool enableUVlessBumpMap;
};

}

#endif	/* _SLG_TRIPLANAR_H */
// vim: autoindent noexpandtab tabstop=4 shiftwidth=4
