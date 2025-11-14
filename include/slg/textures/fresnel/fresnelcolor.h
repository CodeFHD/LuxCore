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

#ifndef _SLG_FRESNELCOLORTEX_H
#define	_SLG_FRESNELCOLORTEX_H

#include "slg/textures/fresnel/fresneltexture.h"

namespace slg {

//------------------------------------------------------------------------------
// Fresnel color texture
//------------------------------------------------------------------------------

class FresnelColorTexture : public FresnelTexture {
public:
	FresnelColorTexture(TextureConstPtr c) : kr(c) { }
	virtual ~FresnelColorTexture() { }

	virtual TextureType GetType() const { return FRESNELCOLOR_TEX; }
	virtual float GetFloatValue(const HitPoint &hitPoint) const;
	virtual luxrays::Spectrum GetSpectrumValue(const HitPoint &hitPoint) const;
	virtual float Y() const;
	virtual float Filter() const;

	virtual luxrays::Spectrum Evaluate(const HitPoint &hitPoint, const float cosi) const;

	virtual void AddReferencedTextures(std::unordered_set<TextureConstPtr>  &referencedTexs, TextureConstPtr self) const {
		Texture::AddReferencedTextures(referencedTexs, self);

		kr->AddReferencedTextures(referencedTexs, kr);
	}

	virtual void UpdateTextureReferences(TextureConstPtr oldTex, TextureConstPtr newTex) {
		if (kr == oldTex)
			kr = newTex;
	}

	TextureConstPtr GetKr() const { return kr; };

	virtual luxrays::Properties ToProperties(const ImageMapCache &imgMapCache, const bool useRealFileName) const;

private:
	TextureConstPtr kr;
};

}

#endif	/* _SLG_FRESNELCOLORTEX_H */
// vim: autoindent noexpandtab tabstop=4 shiftwidth=4
