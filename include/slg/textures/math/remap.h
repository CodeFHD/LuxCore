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

#ifndef _SLG_REMAPTEX_H
#define	_SLG_REMAPTEX_H

#include "slg/textures/texture.h"

namespace slg {

//------------------------------------------------------------------------------
// Remap texture
//------------------------------------------------------------------------------

class RemapTexture : public Texture {
public:
	RemapTexture(TextureConstPtr value, TextureConstPtr sourceMin,
			TextureConstPtr sourceMax, TextureConstPtr targetMin,
			TextureConstPtr targetMax)
		: valueTex(value), sourceMinTex(sourceMin), sourceMaxTex(sourceMax),
		  targetMinTex(targetMin), targetMaxTex(targetMax) { }
	virtual ~RemapTexture() { }

	virtual TextureType GetType() const { return REMAP_TEX; }
	virtual float GetFloatValue(const HitPoint &hitPoint) const;
	virtual luxrays::Spectrum GetSpectrumValue(const HitPoint &hitPoint) const;
	virtual float Y() const;
	virtual float Filter() const;

	virtual void AddReferencedTextures(std::unordered_set<TextureConstPtr>  &referencedTexs, TextureConstPtr self) const {
		Texture::AddReferencedTextures(referencedTexs, self);

		valueTex->AddReferencedTextures(referencedTexs, valueTex);
		sourceMinTex->AddReferencedTextures(referencedTexs, sourceMinTex);
		sourceMaxTex->AddReferencedTextures(referencedTexs, sourceMaxTex);
		targetMinTex->AddReferencedTextures(referencedTexs, targetMinTex);
		targetMaxTex->AddReferencedTextures(referencedTexs, targetMaxTex);
	}
	virtual void AddReferencedImageMaps(std::unordered_set<ImageMapConstPtr > &referencedImgMaps) const {
		valueTex->AddReferencedImageMaps(referencedImgMaps);
		sourceMinTex->AddReferencedImageMaps(referencedImgMaps);
		sourceMaxTex->AddReferencedImageMaps(referencedImgMaps);
		targetMinTex->AddReferencedImageMaps(referencedImgMaps);
		targetMaxTex->AddReferencedImageMaps(referencedImgMaps);
	}

	virtual void UpdateTextureReferences(TextureConstPtr oldTex, TextureConstPtr newTex) {
		if (valueTex == oldTex)
			valueTex = newTex;
		if (sourceMinTex == oldTex)
			sourceMinTex = newTex;
		if (sourceMaxTex == oldTex)
			sourceMaxTex = newTex;
		if (targetMinTex == oldTex)
			targetMinTex = newTex;
		if (targetMaxTex == oldTex)
			targetMaxTex = newTex;
	}

	TextureConstPtr GetValueTex() const { return valueTex; }
	TextureConstPtr GetSourceMinTex() const { return sourceMinTex; }
	TextureConstPtr GetSourceMaxTex() const { return sourceMaxTex; }
	TextureConstPtr GetTargetMinTex() const { return targetMinTex; }
	TextureConstPtr GetTargetMaxTex() const { return targetMaxTex; }

	virtual luxrays::Properties ToProperties(const ImageMapCache &imgMapCache,
	                                         const bool useRealFileName) const;

private:
	TextureConstPtr valueTex;
	TextureConstPtr sourceMinTex;
	TextureConstPtr sourceMaxTex;
	TextureConstPtr targetMinTex;
	TextureConstPtr targetMaxTex;

	static float ClampedRemap(float value,
	                          const float sourceMin, const float sourceMax,
	                          const float targetMin, const float targetMax);

	static luxrays::Spectrum ClampedRemap(luxrays::Spectrum value,
	                                      const float sourceMin, const float sourceMax,
	                                      const float targetMin, const float targetMax);
};

}

#endif	/* _SLG_REMAPTEX_H */
// vim: autoindent noexpandtab tabstop=4 shiftwidth=4
