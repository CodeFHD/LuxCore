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

#ifndef _SLG_DISTORTTEXTURE_H
#define	_SLG_DISTORTTEXTURE_H

#include "slg/textures/texture.h"

namespace slg {

//------------------------------------------------------------------------------
// Distort texture
//------------------------------------------------------------------------------

class DistortTexture : public Texture {
public:
	DistortTexture(TextureConstPtr tex, TextureConstPtr offset, const float strength) :
		tex(tex), offset(offset), strength(strength) { }
	virtual ~DistortTexture() { }

	virtual TextureType GetType() const { return DISTORT_TEX; }
	virtual float GetFloatValue(const HitPoint &hitPoint) const;
	virtual luxrays::Spectrum GetSpectrumValue(const HitPoint &hitPoint) const;
	virtual float Y() const {
		return tex->Y();
	}
	virtual float Filter() const {
		return tex->Filter();
	}

	virtual void AddReferencedTextures(std::unordered_set<TextureConstPtr>  &referencedTexs, TextureConstPtr self) const {
		Texture::AddReferencedTextures(referencedTexs, self);

		tex->AddReferencedTextures(referencedTexs, tex);
		offset->AddReferencedTextures(referencedTexs, offset);
	}
	virtual void AddReferencedImageMaps(std::unordered_set<ImageMapConstPtr > &referencedImgMaps) const {
		tex->AddReferencedImageMaps(referencedImgMaps);
		offset->AddReferencedImageMaps(referencedImgMaps);
	}

	virtual void UpdateTextureReferences(TextureConstPtr oldTex, TextureConstPtr newTex) {
		if (tex == oldTex)
			tex = newTex;
		if (offset == oldTex)
			offset = newTex;
	}

	TextureConstPtr GetTex() const { return tex; }
	TextureConstPtr GetOffset() const { return offset; }
	const float GetStrength() const { return strength; }

	virtual luxrays::Properties ToProperties(const ImageMapCache &imgMapCache, const bool useRealFileName) const;

private:
	void GetTmpHitPoint(const HitPoint &hitPoint, HitPoint &tmpHitPoint) const;

	TextureConstPtr tex;
	TextureConstPtr offset;
	const float strength;
};

}

#endif	/* _SLG_DISTORTTEXTURE_H */
// vim: autoindent noexpandtab tabstop=4 shiftwidth=4
