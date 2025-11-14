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

#ifndef _SLG_WIREFRAMETEX_H
#define	_SLG_WIREFRAMETEX_H

#include "slg/textures/texture.h"

namespace slg {

//------------------------------------------------------------------------------
// WireFrame texture
//------------------------------------------------------------------------------

class WireFrameTexture : public Texture {
public:
	WireFrameTexture(const float w,
			TextureConstPtr borderTx, TextureConstPtr insideTx) :
		width(w), borderTex(borderTx), insideTex(insideTx) { }
	virtual ~WireFrameTexture() { }

	virtual TextureType GetType() const { return WIREFRAME_TEX; }
	virtual float GetFloatValue(const HitPoint &hitPoint) const;
	virtual luxrays::Spectrum GetSpectrumValue(const HitPoint &hitPoint) const;
	virtual float Y() const {
		return (borderTex->Y() + insideTex->Y()) * .5f;
	}
	virtual float Filter() const {
		return (borderTex->Filter() + insideTex->Filter()) * .5f;
	}

	virtual void AddReferencedTextures(std::unordered_set<TextureConstPtr>  &referencedTexs, TextureConstPtr self) const {
		Texture::AddReferencedTextures(referencedTexs, self);

		borderTex->AddReferencedTextures(referencedTexs, borderTex);
		insideTex->AddReferencedTextures(referencedTexs, insideTex);
	}
	virtual void AddReferencedImageMaps(std::unordered_set<ImageMapConstPtr > &referencedImgMaps) const {
		borderTex->AddReferencedImageMaps(referencedImgMaps);
		insideTex->AddReferencedImageMaps(referencedImgMaps);
	}

	virtual void UpdateTextureReferences(TextureConstPtr oldTex, TextureConstPtr newTex) {
		if (borderTex == oldTex)
			borderTex = newTex;
		if (insideTex == oldTex)
			insideTex = newTex;
	}

	float GetWidth() const { return width; }
	TextureConstPtr GetBorderTex() const { return borderTex; }
	TextureConstPtr GetInsideTex() const { return insideTex; }

	virtual luxrays::Properties ToProperties(const ImageMapCache &imgMapCache, const bool useRealFileName) const;

private:
	bool Evaluate(const HitPoint &hitPoint) const;

	const float width;
	TextureConstPtr borderTex;
	TextureConstPtr insideTex;
};

}

#endif	/* _SLG_WIREFRAMETEX_H */
// vim: autoindent noexpandtab tabstop=4 shiftwidth=4
