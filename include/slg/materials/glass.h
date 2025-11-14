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

#ifndef _SLG_GLASSMAT_H
#define	_SLG_GLASSMAT_H

#include "slg/materials/material.h"

namespace slg {

//------------------------------------------------------------------------------
// Glass material
//------------------------------------------------------------------------------

class GlassMaterial : public Material {
public:
	GlassMaterial(TextureConstPtr frontTransp, TextureConstPtr backTransp,
			TextureConstPtr emitted, TextureConstPtr bump,
			TextureConstPtr refl, TextureConstPtr trans,
			TextureConstPtr exteriorIorFact, TextureConstPtr interiorIorFact,
			TextureConstPtr B, 
			TextureConstPtr filmThickness, TextureConstPtr filmIor);

	virtual MaterialType GetType() const { return GLASS; }
	virtual BSDFEvent GetEventTypes() const { return SPECULAR | REFLECT | TRANSMIT; };

	virtual bool IsDelta() const { return true; }

	virtual luxrays::Spectrum Evaluate(const HitPoint &hitPoint,
		const luxrays::Vector &localLightDir, const luxrays::Vector &localEyeDir, BSDFEvent *event,
		float *directPdfW = NULL, float *reversePdfW = NULL) const;
	virtual luxrays::Spectrum Sample(const HitPoint &hitPoint,
		const luxrays::Vector &localFixedDir, luxrays::Vector *localSampledDir,
		const float u0, const float u1, const float passThroughEvent,
		float *pdfW, BSDFEvent *event) const;
	virtual void Pdf(const HitPoint &hitPoint,
		const luxrays::Vector &localLightDir, const luxrays::Vector &localEyeDir,
		float *directPdfW, float *reversePdfW) const;

	virtual void AddReferencedTextures(std::unordered_set<TextureConstPtr>  &referencedTexsreferencedTexs) const;
	virtual void UpdateTextureReferences(TextureConstPtr oldTex, TextureConstPtr newTex);

	virtual luxrays::Properties ToProperties(const ImageMapCache &imgMapCache, const bool useRealFileName) const;

	TextureConstPtr GetKr() const { return Kr; }
	TextureConstPtr GetKt() const { return Kt; }
	TextureConstPtr GetExteriorIOR() const { return exteriorIor; }
	TextureConstPtr GetInteriorIOR() const { return interiorIor; }
	TextureConstPtr GetCauchyB() const { return cauchyB; }
	TextureConstPtr GetFilmThickness() const { return filmThickness; }
	TextureConstPtr GetFilmIOR() const { return filmIor; }

	static luxrays::Spectrum EvalSpecularReflection(const HitPoint &hitPoint,
			const luxrays::Vector &localFixedDir,
			const luxrays::Spectrum &kr, const float nc, const float nt,
			luxrays::Vector *localSampledDir,
			const float localFilmThickness, const float localFilmIor);
	static luxrays::Spectrum EvalSpecularTransmission(const HitPoint &hitPoint,
			const luxrays::Vector &localFixedDir, const float u0,
			const luxrays::Spectrum &kt,
			const float nc, const float nt, const float cauchyB,
			luxrays::Vector *localSampledDir);

private:

	TextureConstPtr Kr;
	TextureConstPtr Kt;
	TextureConstPtr exteriorIor;
	TextureConstPtr interiorIor;
	TextureConstPtr cauchyB;
	TextureConstPtr filmThickness;
	TextureConstPtr filmIor;
};

}

#endif	/* _SLG_GLASSMAT_H */
// vim: autoindent noexpandtab tabstop=4 shiftwidth=4
