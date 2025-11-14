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

#ifndef _SLG_IMAGEMAPTEX_H
#define	_SLG_IMAGEMAPTEX_H

#include <memory>

#include "slg/textures/texture.h"

namespace slg {

//------------------------------------------------------------------------------
// ImageMap texture
//------------------------------------------------------------------------------

class ImageMapTexture : public Texture {
public:
	ImageMapTexture(
		const std::string &texName,
		ImageMapConstPtr img,
		TextureMapping2DConstPtr mp,
		const float g,
		const bool rt);

	virtual TextureType GetType() const { return IMAGEMAP; }
	virtual float GetFloatValue(const HitPoint &hitPoint) const;
	virtual luxrays::Spectrum GetSpectrumValue(const HitPoint &hitPoint) const;
	virtual luxrays::Normal Bump(const HitPoint &hitPoint, const float sampleDistance) const;
	virtual float Y() const { return gain * imageMap->GetSpectrumMeanY(); }
	virtual float Filter() const { return gain * imageMap->GetSpectrumMean(); }

	ImageMapConstPtr GetImageMap() const { return imageMap; }
	TextureMapping2DConstPtr GetTextureMapping() const { return mapping; }
	const float GetGain() const { return gain; }

	bool HasRandomizedTiling() const { return randomizedTiling; }
	ImageMapConstPtr GetRandomizedTilingLUT() const { return randomizedTilingLUT; }
	ImageMapConstPtr GetRandomizedTilingInvLUT() const { return randomizedTilingInvLUT; }

	virtual void AddReferencedImageMaps(std::unordered_set<ImageMapConstPtr> &referencedImgMaps) const;

	virtual luxrays::Properties ToProperties(const ImageMapCache &imgMapCache, const bool useRealFileName) const;

	static std::shared_ptr<ImageMapTexture> AllocImageMapTexture(const std::string &texName,
		ImageMapCache &imgMapCache, ImageMapConstPtr img,
		TextureMapping2DConstPtr mp, const float g, const bool rt);

	static std::shared_ptr<ImageMap> randomImageMap;

	virtual ~ImageMapTexture();

private:
	luxrays::Spectrum SampleTile(const luxrays::UV &vertex, const luxrays::UV &offset) const;
	luxrays::Spectrum RandomizedTilingGetSpectrumValue(const luxrays::UV &pos) const;

	ImageMapConstPtr imageMap;
	TextureMapping2DConstPtr mapping;
	float gain;

	// Used for randomized tiling
	bool randomizedTiling;
	ImageMapPtr randomizedTilingLUT;
	ImageMapPtr randomizedTilingInvLUT;
};

}

#endif	/* _SLG_IMAGEMAPTEX_H */
// vim: autoindent noexpandtab tabstop=4 shiftwidth=4
