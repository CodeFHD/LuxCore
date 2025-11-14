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

#ifndef _SLG_HSVTEX_H
#define	_SLG_HSVTEX_H

#include "slg/textures/texture.h"

namespace slg {

//------------------------------------------------------------------------------
// Hue saturation value texture
//------------------------------------------------------------------------------

class HsvTexture : public Texture {
public:
	HsvTexture(TextureConstPtr t, TextureConstPtr h, 
			TextureConstPtr s, TextureConstPtr v) : tex(t), hue(h), sat(s), val(v) { }
	virtual ~HsvTexture() { }

	virtual TextureType GetType() const { return HSV_TEX; }
	virtual float GetFloatValue(const HitPoint &hitPoint) const;
	virtual luxrays::Spectrum GetSpectrumValue(const HitPoint &hitPoint) const;
	virtual float Y() const;
	virtual float Filter() const;

	virtual void AddReferencedTextures(std::unordered_set<TextureConstPtr>  &referencedTexs, TextureConstPtr self) const {
		Texture::AddReferencedTextures(referencedTexs, self);

		tex->AddReferencedTextures(referencedTexs, tex);
		hue->AddReferencedTextures(referencedTexs, hue);
		sat->AddReferencedTextures(referencedTexs, sat);
		val->AddReferencedTextures(referencedTexs, val);
	}
	virtual void AddReferencedImageMaps(std::unordered_set<ImageMapConstPtr > &referencedImgMaps) const {
		tex->AddReferencedImageMaps(referencedImgMaps);
		hue->AddReferencedImageMaps(referencedImgMaps);
		sat->AddReferencedImageMaps(referencedImgMaps);
		val->AddReferencedImageMaps(referencedImgMaps);
	}

	virtual void UpdateTextureReferences(TextureConstPtr oldTex, TextureConstPtr newTex) {
		if (tex == oldTex)
			tex = newTex;
		if (hue == oldTex)
			hue = newTex;
		if (sat == oldTex)
			sat = newTex;
		if (val == oldTex)
			val = newTex;
	}

	TextureConstPtr GetTexture() const { return tex; }
	TextureConstPtr GetHue() const { return hue; }
	TextureConstPtr GetSaturation() const { return sat; }
	TextureConstPtr GetValue() const { return val; }

	virtual luxrays::Properties ToProperties(const ImageMapCache &imgMapCache, const bool useRealFileName) const;

private:
	luxrays::Spectrum RgbToHsv(const luxrays::Spectrum &rgb) const;
	luxrays::Spectrum HsvToRgb(const luxrays::Spectrum &hsv) const;
	luxrays::Spectrum ApplyTransformation(const luxrays::Spectrum &colorHitpoint,
			const float &hueHitpoint, const float &satHitpoint,
			const float &valHitpoint) const;

	TextureConstPtr tex;
	TextureConstPtr hue;
	TextureConstPtr sat;
	TextureConstPtr val;
};

}

#endif	/* _SLG_HSVTEX_H */
// vim: autoindent noexpandtab tabstop=4 shiftwidth=4
