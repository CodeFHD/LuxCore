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

#ifndef _SLG_PATHTRACER_H
#define	_SLG_PATHTRACER_H

#include "slg/slg.h"
#include "slg/engines/cpurenderengine.h"
#include "slg/samplers/sampler.h"
#include "slg/film/film.h"
#include "slg/film/filmsamplesplatter.h"
#include "slg/bsdf/bsdf.h"
#include "slg/engines/caches/photongi/photongicache.h"
#include "slg/utils/pathinfo.h"

namespace slg {

// OpenCL data types
namespace ocl {
#include "slg/engines/pathtracer_types.cl"
}

//------------------------------------------------------------------------------
// Path Tracing render code
//
// All the methods of this class must be thread-safe because they will be used
// by render threads	
//------------------------------------------------------------------------------

class PathTracer;
class VarianceClamping;

class PathTracerThreadState {
public:
	PathTracerThreadState(luxrays::IntersectionDevice *device,
			Sampler *eyeSampler, Sampler *lightSampler,
			SceneConstPtr scene, FilmPtr film,
			const VarianceClamping *varianceClamping,
			const bool useFilmSplat = false);
	virtual ~PathTracerThreadState();

	luxrays::IntersectionDevice *device;

	Sampler *eyeSampler, *lightSampler;
	SceneConstPtr scene;
	FilmPtr film;
	const VarianceClamping *varianceClamping;
	
	std::vector<SampleResult> eyeSampleResults, lightSampleResults;
	
	// Used for hybrid rendering
	double eyeSampleCount, lightSampleCount;
};

class PhotonGICache;

class PathTracer {
public:
	typedef enum {
		ILLUMINATED, SHADOWED, NOT_VISIBLE
	} DirectLightResult;

	typedef std::function<void(const LightPathInfo &pathInfo,
			const BSDF &, const u_int, const luxrays::Spectrum &,
			std::vector<SampleResult> &sampleResults)> ConnectToEyeCallBackType;

	PathTracer();
	virtual ~PathTracer();

	void InitPixelFilterDistribution(const Filter *pixelFilter);
	void DeletePixelFilterDistribution();

	void SetPhotonGICache(const PhotonGICache *cache) { photonGICache = cache; }
	const PhotonGICache *GetPhotonGICache() const { return photonGICache; }

	void ParseOptions(
		luxrays::PropertiesConstPtr cfg,
		const luxrays::Properties &defaultProps
	);

	DirectLightResult DirectLightSampling(
		luxrays::IntersectionDevice *device, SceneConstPtr scene,
		const float time, const float u0,
		const float u1, const float u2,
		const float u3, const float u4,
		const EyePathInfo &pathInfo, const luxrays::Spectrum &pathThrouput,
		const BSDF &bsdf, SampleResult *sampleResult,
		const bool useBSDFEVal = true) const;

	void RenderEyePath(luxrays::IntersectionDevice *device,
			SceneConstPtr scene, Sampler *sampler, EyePathInfo &pathInfo,
			luxrays::Ray &eyeRay, const luxrays::Spectrum &eyeTroughput,
			std::vector<SampleResult> &sampleResults) const;
	void RenderEyeSample(luxrays::IntersectionDevice *device,
			SceneConstPtr scene, FilmConstPtr film, Sampler *sampler,
			std::vector<SampleResult> &sampleResults) const;

	void RenderLightSample(luxrays::IntersectionDevice *device,
			SceneConstPtr scene, FilmConstPtr film, Sampler *sampler,
			std::vector<SampleResult> &sampleResults,
			const ConnectToEyeCallBackType &ConnectToEyeCallBack) const;
	void RenderLightSample(luxrays::IntersectionDevice *device,
			SceneConstPtr scene, FilmConstPtr film, Sampler *sampler,
			std::vector<SampleResult> &sampleResults) const {
		static const ConnectToEyeCallBackType noCallback;
		RenderLightSample(device, scene, film, sampler, sampleResults, noCallback);
	}
	
	bool HasToRenderEyeSample(PathTracerThreadState &state) const;
	void ApplyVarianceClamp(const PathTracerThreadState &state,
			std::vector<SampleResult> &sampleResults) const;
	void RenderSample(PathTracerThreadState &state) const;

	static void InitEyeSampleResults(FilmConstPtr film, std::vector<SampleResult> &sampleResults,
			const bool useFilmSplat = false);
	static void ResetEyeSampleResults(std::vector<SampleResult> &sampleResults);
	static SampleResult &AddLightSampleResult(std::vector<SampleResult> &sampleResults,
			FilmConstPtr film);

	static luxrays::Properties ToProperties(const luxrays::Properties &cfg);
	static const luxrays::Properties &GetDefaultProps();

	// Used for Sampler indices
	u_int eyeSampleBootSize, eyeSampleStepSize, eyeSampleSize;
	u_int lightSampleBootSize, lightSampleStepSize, lightSampleSize;

	// Path depth settings
	PathDepthInfo maxPathDepth;

	u_int rrDepth;
	float rrImportanceCap;

	// Clamping settings
	float sqrtVarianceClampMaxValue;

	// Hybrid backward/forward path tracing settings
	float hybridBackForwardPartition, hybridBackForwardGlossinessThreshold;

	// Albedo AOV settings
	AlbedoSpecularSetting albedoSpecularSetting;
	float albedoSpecularGlossinessThreshold;
	
	// Option flags
	bool forceBlackBackground, hybridBackForwardEnable;

private:
	void GenerateEyeRay(CameraConstPtr camera, FilmConstPtr film,
			luxrays::Ray &eyeRay, PathVolumeInfo &volInfo,
			Sampler *sampler, SampleResult &sampleResult) const;

	// RenderEyeSample methods

	void DirectHitFiniteLight(SceneConstPtr scene, const EyePathInfo &pathInfo,
			const luxrays::Spectrum &pathThrouput, const luxrays::Ray &ray,
			const float distance, const BSDF &bsdf,
			SampleResult *sampleResult) const;
	void DirectHitInfiniteLight(SceneConstPtr scene, const EyePathInfo &pathInfo,
			const luxrays::Spectrum &pathThrouput, const luxrays::Ray &ray,
			const BSDF *bsdf, SampleResult *sampleResult) const;
	bool CheckDirectHitVisibilityFlags(LightSourceConstPtr lightSource,
			const PathDepthInfo &depthInfo,	const BSDFEvent lastBSDFEvent) const;

	// RenderLightSample methods

	void ConnectToEye(luxrays::IntersectionDevice *device,
			SceneConstPtr scene,
			FilmConstPtr film, const float time,
			const float u0, const float u1, const float u2,
			const LightSource &light,  const BSDF &bsdf,
			const luxrays::Spectrum &flux, const LightPathInfo &pathInfo,
			std::vector<SampleResult> &sampleResults) const;

	FilterDistribution *pixelFilterDistribution;
	const PhotonGICache *photonGICache;

	static const Film::FilmChannels eyeSampleResultsChannels;
	static const Film::FilmChannels lightSampleResultsChannels;
};

}

#endif	/* _SLG_PATHTRACER_H */
// vim: autoindent noexpandtab tabstop=4 shiftwidth=4
