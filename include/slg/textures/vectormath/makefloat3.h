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

#ifndef _SLG_MAKEFLOAT3TEX_H
#define	_SLG_MAKEFLOAT3TEX_H

#include "slg/textures/texture.h"

namespace slg {

//------------------------------------------------------------------------------
// Make float3 texture
//------------------------------------------------------------------------------

class MakeFloat3Texture : public Texture {
public:
	MakeFloat3Texture(TextureConstPtr tex1, TextureConstPtr tex2, TextureConstPtr tex3) 
		: tex1(tex1), tex2(tex2), tex3(tex3) { }
	virtual ~MakeFloat3Texture() { }

	virtual TextureType GetType() const { return MAKE_FLOAT3; }
	virtual float GetFloatValue(const HitPoint &hitPoint) const;
	virtual luxrays::Spectrum GetSpectrumValue(const HitPoint &hitPoint) const;
	// The following methods don't make very much sense in this case. I have no
	// information about the color.
	virtual float Y() const { return 1.f; }
	virtual float Filter() const { return 1.f; }

	virtual void AddReferencedTextures(std::unordered_set<TextureConstPtr>  &referencedTexs, TextureConstPtr self) const {
		Texture::AddReferencedTextures(referencedTexs, self);

		tex1->AddReferencedTextures(referencedTexs, tex1);
		tex2->AddReferencedTextures(referencedTexs, tex2);
		tex3->AddReferencedTextures(referencedTexs, tex3);
	}
	virtual void AddReferencedImageMaps(std::unordered_set<ImageMapConstPtr > &referencedImgMaps) const {
		tex1->AddReferencedImageMaps(referencedImgMaps);
		tex2->AddReferencedImageMaps(referencedImgMaps);
		tex3->AddReferencedImageMaps(referencedImgMaps);
	}

	virtual void UpdateTextureReferences(TextureConstPtr oldTex, TextureConstPtr newTex) {
		if (tex1 == oldTex)
			tex1 = newTex;
		if (tex2 == oldTex)
			tex2 = newTex;
		if (tex3 == oldTex)
			tex3 = newTex;
	}

	TextureConstPtr GetTexture1() const { return tex1; }
	TextureConstPtr GetTexture2() const { return tex2; }
	TextureConstPtr GetTexture3() const { return tex3; }

	virtual luxrays::Properties ToProperties(const ImageMapCache &imgMapCache, const bool useRealFileName) const;

private:
	TextureConstPtr tex1;
	TextureConstPtr tex2;
	TextureConstPtr tex3;
};

}

#endif	/* _SLG_MAKEFLOAT3TEX_H */
// vim: autoindent noexpandtab tabstop=4 shiftwidth=4
