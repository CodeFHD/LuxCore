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

#ifndef _SLG_CLAMPTEX_H
#define	_SLG_CLAMPTEX_H

#include "slg/textures/texture.h"

namespace slg {

//------------------------------------------------------------------------------
// Clamp texture
//------------------------------------------------------------------------------

class ClampTexture : public Texture {
public:
	ClampTexture(TextureConstPtr t, const float minv, const float maxv) : tex(t),
			minVal(minv), maxVal(maxv) { }
	virtual ~ClampTexture() { }

	virtual TextureType GetType() const { return CLAMP_TEX; }
	virtual float GetFloatValue(const HitPoint &hitPoint) const;
	virtual luxrays::Spectrum GetSpectrumValue(const HitPoint &hitPoint) const;
	virtual float Y() const { return luxrays::Clamp(tex->Y(), minVal, maxVal); } // This can be not correct
	virtual float Filter() const { return luxrays::Clamp(tex->Filter(), minVal, maxVal); } // This can be not correct

	virtual void AddReferencedTextures(std::unordered_set<TextureConstPtr>  &referencedTexs, TextureConstPtr self) const {
		Texture::AddReferencedTextures(referencedTexs, self);

		tex->AddReferencedTextures(referencedTexs, tex);
	}
	virtual void AddReferencedImageMaps(std::unordered_set<ImageMapConstPtr > &referencedImgMaps) const {
		tex->AddReferencedImageMaps(referencedImgMaps);
	}

	virtual void UpdateTextureReferences(TextureConstPtr oldTex, TextureConstPtr newTex) {
		if (tex == oldTex)
			tex = newTex;
	}

	TextureConstPtr GetTexture() const { return tex; }
	float GetMinVal() const { return minVal; }
	float GetMaxVal() const { return maxVal; }

	virtual luxrays::Properties ToProperties(const ImageMapCache &imgMapCache, const bool useRealFileName) const;

private:
	TextureConstPtr tex;
	const float minVal, maxVal;
};

}

#endif	/* _SLG_CLAMPTEX_H */
// vim: autoindent noexpandtab tabstop=4 shiftwidth=4
