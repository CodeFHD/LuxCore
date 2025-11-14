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

#ifndef _SLG_DOTSTEX_H
#define	_SLG_DOTSTEX_H

#include "slg/textures/texture.h"

namespace slg {

//------------------------------------------------------------------------------
// Dots texture
//------------------------------------------------------------------------------

class DotsTexture : public Texture {
public:
	DotsTexture(TextureMapping2DConstPtr mp, TextureConstPtr insideTx, TextureConstPtr outsideTx) :
		mapping(mp), insideTex(insideTx), outsideTex(outsideTx) { }

	virtual TextureType GetType() const { return DOTS; }
	virtual float GetFloatValue(const HitPoint &hitPoint) const;
	virtual luxrays::Spectrum GetSpectrumValue(const HitPoint &hitPoint) const;
	virtual float Y() const {
		return (insideTex->Y() + outsideTex->Y()) * .5f;
	}
	virtual float Filter() const {
		return (insideTex->Filter() + outsideTex->Filter()) * .5f;
	}

	virtual void AddReferencedTextures(std::unordered_set<TextureConstPtr>  &referencedTexs, TextureConstPtr self) const {
		Texture::AddReferencedTextures(referencedTexs, self);

		insideTex->AddReferencedTextures(referencedTexs, insideTex);
		outsideTex->AddReferencedTextures(referencedTexs, outsideTex);
	}
	virtual void AddReferencedImageMaps(std::unordered_set<ImageMapConstPtr > &referencedImgMaps) const {
		insideTex->AddReferencedImageMaps(referencedImgMaps);
		outsideTex->AddReferencedImageMaps(referencedImgMaps);
	}

	virtual void UpdateTextureReferences(TextureConstPtr oldTex, TextureConstPtr newTex) {
		if (insideTex == oldTex)
			insideTex = newTex;
		if (outsideTex == oldTex)
			outsideTex = newTex;
	}

	TextureMapping2DConstPtr GetTextureMapping() const { return mapping; }
	TextureConstPtr GetInsideTex() const { return insideTex; }
	TextureConstPtr GetOutsideTex() const { return outsideTex; }

	virtual luxrays::Properties ToProperties(const ImageMapCache &imgMapCache, const bool useRealFileName) const;

private:
	bool Evaluate(const HitPoint &hitPoint) const;

	TextureMapping2DConstPtr mapping;
	TextureConstPtr insideTex;
	TextureConstPtr outsideTex;
};

}

#endif	/* _SLG_DOTSTEX_H */
// vim: autoindent noexpandtab tabstop=4 shiftwidth=4
