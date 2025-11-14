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

#ifndef _SLG_POWERTEX_H
#define	_SLG_POWERTEX_H

#include "slg/textures/texture.h"

namespace slg {

//------------------------------------------------------------------------------
// Power texture
//------------------------------------------------------------------------------

class PowerTexture : public Texture {
public:
	PowerTexture(TextureConstPtr base, TextureConstPtr exponent) : base(base), exponent(exponent) { }
	virtual ~PowerTexture() { }

	virtual TextureType GetType() const { return POWER_TEX; }
	virtual float GetFloatValue(const HitPoint &hitPoint) const;
	virtual luxrays::Spectrum GetSpectrumValue(const HitPoint &hitPoint) const;
	virtual float Y() const { 
		return SafePow(base->Y(), exponent->Y()); 
	}
	virtual float Filter() const { 
		return SafePow(base->Filter(), exponent->Filter()); 
	}

	virtual void AddReferencedTextures(std::unordered_set<TextureConstPtr>  &referencedTexs, TextureConstPtr self) const {
		Texture::AddReferencedTextures(referencedTexs, self);

		base->AddReferencedTextures(referencedTexs, base);
		exponent->AddReferencedTextures(referencedTexs, exponent);
	}
	virtual void AddReferencedImageMaps(std::unordered_set<ImageMapConstPtr > &referencedImgMaps) const {
		base->AddReferencedImageMaps(referencedImgMaps);
		exponent->AddReferencedImageMaps(referencedImgMaps);
	}

	virtual void UpdateTextureReferences(TextureConstPtr oldTex, TextureConstPtr newTex) {
		if (base == oldTex)
			base = newTex;
		if (exponent == oldTex)
			exponent = newTex;
	}

	TextureConstPtr GetBase() const { return base; }
	TextureConstPtr GetExponent() const { return exponent; }

	virtual luxrays::Properties ToProperties(const ImageMapCache &imgMapCache, const bool useRealFileName) const;

private:
	TextureConstPtr base;
	TextureConstPtr exponent;

	inline float SafePow(const float base, const float exponent) const {
		if (base < 0.f && exponent != static_cast<int>(exponent))
			return 0.f;
		return powf(base, exponent);
	}
};

}

#endif	/* _SLG_POWERTEX_H */
// vim: autoindent noexpandtab tabstop=4 shiftwidth=4
