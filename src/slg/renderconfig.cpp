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

#include <memory>
#include <filesystem>
#include <regex>
#include <mutex>

#include <boost/lexical_cast.hpp>
#include <boost/algorithm/string.hpp> 
#include <boost/serialization/shared_ptr.hpp>
#include <boost/serialization/unique_ptr.hpp>

#include "luxrays/utils/serializationutils.h"
#include "slg/usings.h"
#include "slg/renderconfig.h"
#include "slg/engines/renderengine.h"
#include "slg/film/film.h"

#include "slg/samplers/random.h"
#include "slg/samplers/sobol.h"
#include "slg/samplers/metropolis.h"

#include "slg/film/filters/box.h"
#include "slg/film/filters/gaussian.h"
#include "slg/film/filters/mitchell.h"
#include "slg/film/filters/mitchellss.h"
#include "slg/film/filters/blackmanharris.h"

#include "slg/engines/rtpathocl/rtpathocl.h"
#include "slg/engines/lightcpu/lightcpu.h"
#include "slg/engines/pathcpu/pathcpu.h"
#include "slg/engines/bidircpu/bidircpu.h"
#include "slg/engines/bidirvmcpu/bidirvmcpu.h"
#include "slg/engines/filesaver/filesaver.h"
#include "slg/engines/tilepathcpu/tilepathcpu.h"
#include "slg/engines/tilepathocl/tilepathocl.h"
#include "slg/lights/strategies/lightstrategyregistry.h"
#include "slg/utils/filenameresolver.h"

using namespace std;
using namespace luxrays;
using namespace slg;

//------------------------------------------------------------------------------
// RenderConfig
//------------------------------------------------------------------------------

static std::mutex defaultPropertiesMutex;
static std::unique_ptr<Properties> defaultProperties;

BOOST_CLASS_EXPORT_IMPLEMENT(slg::RenderConfig)

RenderConfigUPtr RenderConfig::Create(luxrays::PropertiesConstPtr props, ScenePtr scene) {
	return std::make_unique<RenderConfig>(Private(), props, scene);
}

RenderConfig::RenderConfig(Private p, PropertiesConstPtr props, ScenePtr scn) : cfg(std::make_shared<Properties>()), scene(scn) {
	InitDefaultProperties();

	SLG_LOG("Configuration: ");
	const vector<string> &keys = props->GetAllNames();
	for (vector<string>::const_iterator i = keys.begin(); i != keys.end(); ++i)
		SLG_LOG("  " << props->Get(*i));

	SLG_FileNameResolver.Print();

	// Set the Scene
	if (scn) {
		scene = scn;
		allocatedScene = false;
	} else {
		// Create the Scene
		const string defaultSceneName = GetDefaultProperties().Get("scene.file").Get<string>();
		const string sceneFileName = SLG_FileNameResolver.ResolveFile(props->Get(Property("scene.file")(defaultSceneName)).Get<string>());

		SDL_LOG("Reading scene: " << sceneFileName);
		scene = std::make_shared<Scene>(
			std::make_shared<Properties>(sceneFileName),
			props
		);
		allocatedScene = true;
	}

	if (!scene->camera)
		throw runtime_error("You can not build a RenderConfig with a scene not including a camera");
	
	// Parse the configuration
	Parse(*props);
}

void RenderConfig::InitDefaultProperties() {
	// Check if I have to initialize the default Properties
	if (!defaultProperties.get()) {
		std::unique_lock<std::mutex> lock(defaultPropertiesMutex);
		if (!defaultProperties.get()) {
			auto props = std::make_unique<Properties>();
			*props << RenderConfig::ToProperties(Properties());

			defaultProperties = std::move(props);
		}
	}
}

const Properties &RenderConfig::GetDefaultProperties() {
	InitDefaultProperties();

	return *defaultProperties;
}


bool RenderConfig::HasCachedKernels() {
#if !defined(LUXRAYS_DISABLE_OPENCL)
	const string type = cfg->Get(Property("renderengine.type")(PathCPURenderEngine::GetObjectTag())).Get<string>();
	if ((type == "PATHOCL") ||
			(type == "RTPATHOCL") ||
			(type == "TILEPATHOCL")) {
		return PathOCLBaseRenderEngine::HasCachedKernels(*this);
	} else
		return true;
#else
	return true;
#endif
}

const Property RenderConfig::GetProperty(const string &name) const {
	return ToProperties().Get(name);
}

void RenderConfig::Parse(const Properties &props) {
	// I can not use GetProperty() here because it triggers a ToProperties() and it can
	// be a problem with OpenCL disabled (PATHOCL is not defined, etc.)
	assert(cfg);
	if (cfg->Get(Property("debug.renderconfig.parse.print")(false)).Get<bool>()) {
		SDL_LOG("====================RenderConfig::Parse()======================"
				<< endl <<
				props);
		SDL_LOG("===============================================================");
	}

	// Reset the properties cache
	propsCache.Clear();

	cfg->Set(props);
	// I can not use GetProperty() here because it triggers a ToProperties() and it can
	// be a problem with OpenCL disabled (PATHOCL is not defined, etc.)
	scene->enableParsePrint = cfg->Get(Property("debug.scene.parse.print")(false)).Get<bool>();

	UpdateFilmProperties(props);

	// Scene epsilon is read directly from the cfg properties inside
	// render engine Start() method

	// Accelerator settings are read directly from the cfg properties inside
	// the render engine

	// Light strategy
	scene->lightDefs.SetLightStrategy(*cfg);

	// Update the Camera
	u_int filmFullWidth, filmFullHeight, filmSubRegion[4];
	u_int *subRegion = Film::GetFilmSize(*cfg, &filmFullWidth, &filmFullHeight, filmSubRegion) ?
		filmSubRegion : NULL;
	scene->camera->Update(filmFullWidth, filmFullHeight, subRegion);
}

void RenderConfig::DeleteAllFilmImagePipelinesProperties() {
	cfg->DeleteAll(cfg->GetAllNamesRE("film\\.imagepipeline\\.[0-9]+\\..*"));
	cfg->DeleteAll(cfg->GetAllNamesRE("film\\.imagepipelines\\.[0-9]+\\.[0-9]+\\..*")); 
}

void RenderConfig::UpdateFilmProperties(const luxrays::Properties &props) {
	// I can not use GetProperty() here because it triggers a ToProperties() and it can
	// be a problem with OpenCL disabled (PATHOCL is not defined, etc.)
	if (cfg->Get(Property("debug.renderconfig.parse.print")(false)).Get<bool>()) {
		SDL_LOG("=============RenderConfig::UpdateFilmProperties()==============" << endl <<
				props);
		SDL_LOG("===============================================================");
	}

	//--------------------------------------------------------------------------
	// Check if there was a new image pipeline definition
	//--------------------------------------------------------------------------

	if (props.HaveNamesRE("film\\.imagepipeline\\.[0-9]+\\.type") ||
			props.HaveNamesRE("film\\.imagepipelines\\.[0-9]+\\.[0-9]+\\.type")) {
		// Delete the old image pipeline properties
		cfg->DeleteAll(cfg->GetAllNamesRE("film\\.imagepipeline\\.[0-9]+\\..*"));
		cfg->DeleteAll(cfg->GetAllNamesRE("film\\.imagepipelines\\.[0-9]+\\.[0-9]+\\..*"));

		// Update the RenderConfig properties with the new image pipeline definition
		std::regex reOldSyntax("film\\.imagepipeline\\.[0-9]+\\..*");
		std::regex reNewSyntax("film\\.imagepipelines\\.[0-9]+\\.[0-9]+\\..*");
		for(string propName: props.GetAllNames()) {
			if (std::regex_match(propName, reOldSyntax) ||
					std::regex_match(propName, reNewSyntax))
				cfg->Set(props.Get(propName));
		}
		
		// Reset the properties cache
		propsCache.Clear();
	}

	//--------------------------------------------------------------------------
	// Check if there are new radiance group scales
	//--------------------------------------------------------------------------

	if (props.HaveNames("film.imagepipeline.radiancescales.") ||
			props.HaveNamesRE("film\\.imagepipelines\\.[0-9]+\\.radiancescales\\..*")) {
		// Delete the old image pipeline properties
		cfg->DeleteAll(cfg->GetAllNames("film.imagepipeline.radiancescales."));
		cfg->DeleteAll(cfg->GetAllNamesRE("film\\.imagepipelines\\.[0-9]+\\.radiancescales\\..*"));

		// Update the RenderConfig properties with the new image pipeline definition
		std::regex reNewSyntax("film\\.imagepipelines\\.[0-9]+\\.radiancescales\\..*");
		for(string propName: props.GetAllNames()) {
			if (propName.starts_with("film.imagepipeline.radiancescales.") ||
					std::regex_match(propName, reNewSyntax))
				cfg->Set(props.Get(propName));
		}

		// Reset the properties cache
		propsCache.Clear();
	}

	//--------------------------------------------------------------------------
	// Check if there were new outputs definition
	//--------------------------------------------------------------------------

	if (props.HaveNames("film.outputs.")) {
		// Delete old radiance groups scale properties
		cfg->DeleteAll(cfg->GetAllNames("film.outputs."));
		
		// Update the RenderConfig properties with the new outputs definition properties
		for(string propName: props.GetAllNames()) {
			if (propName.starts_with("film.outputs."))
				cfg->Set(props.Get(propName));
		}

		// Reset the properties cache
		propsCache.Clear();
	}

	//--------------------------------------------------------------------------
	// Check if there is a new film size definition
	//--------------------------------------------------------------------------

	const bool filmWidthDefined = props.IsDefined("film.width");
	const bool filmHeightDefined = props.IsDefined("film.height");
	if (filmWidthDefined || filmHeightDefined) {
		if (filmWidthDefined)
			cfg->Set(props.Get("film.width"));
		if (filmHeightDefined)
			cfg->Set(props.Get("film.height"));
		
		// Reset the properties cache
		propsCache.Clear();
	}
}

void RenderConfig::Delete(const string &prefix) {
	// Reset the properties cache
	propsCache.Clear();

	cfg->DeleteAll(cfg->GetAllNames(prefix));
}

Filter *RenderConfig::AllocPixelFilter() const {
	return Filter::FromProperties(*cfg);
}

FilmPtr RenderConfig::AllocFilm() const {
	auto film = Film::FromProperties(cfg);

	// Add the channels required by the Sampler
	Film::FilmChannels channels;
	Sampler::AddRequiredChannels(channels, *cfg);
	for (auto const c : channels)
		film->AddChannel(c);

	return film;
}

SamplerSharedData *RenderConfig::AllocSamplerSharedData(RandomGenerator *rndGen, FilmPtr film) const {
	return SamplerSharedData::FromProperties(*cfg, rndGen, film);
}

Sampler *RenderConfig::AllocSampler(RandomGenerator *rndGen, FilmPtr film, const FilmSampleSplatter *flmSplatter,
		SamplerSharedData *sharedData, const Properties &additionalProps) const {
	auto& props = *cfg;
	props << additionalProps;

	return Sampler::FromProperties(props, rndGen, film, flmSplatter, sharedData);
}

RenderEngineUPtr RenderConfig::AllocRenderEngine() const {
#if defined(LUXRAYS_DISABLE_OPENCL)
	// This is a specific test for OpenCL-less version in order to print
	// a more clear error
	const string type = cfg->Get(Property("renderengine.type")(PathCPURenderEngine::GetObjectTag())).Get<string>();
	if ((type == "PATHOCL") ||
			(type == "RTPATHOCL") ||
			(type == "TILEPATHOCL"))
		throw runtime_error(type + " render engine is not supported by OpenCL-less version of the binaries. Download the OpenCL-enabled version or change the render engine used.");
#endif

	return RenderEngine::FromProperties(*this);
}

const Properties &RenderConfig::ToProperties() const {
	if (!propsCache.GetSize())
		propsCache = ToProperties(*cfg);

	return propsCache;
}

Properties RenderConfig::ToProperties(const Properties &cfg) {
	Properties props;

	// LuxRays context
	props << cfg.Get(Property("context.verbose")(true));

	// Ray intersection accelerators
	props << cfg.Get(Property("accelerator.type")("AUTO"));
	props << cfg.Get(Property("accelerator.instances.enable")(true));
	props << cfg.Get(Property("accelerator.motionblur.enable")(true));
	// (M)BVH accelerator
	props << cfg.Get(Property("accelerator.bvh.builder.type")("EMBREE_BINNED_SAH"));
	props << cfg.Get(Property("accelerator.bvh.treetype")(4));
	props << cfg.Get(Property("accelerator.bvh.costsamples")(0));
	props << cfg.Get(Property("accelerator.bvh.isectcost")(80));
	props << cfg.Get(Property("accelerator.bvh.travcost")(10));
	props << cfg.Get(Property("accelerator.bvh.emptybonus")(.5));

	// Scene epsilon
	props << cfg.Get(Property("scene.epsilon.min")(DEFAULT_EPSILON_MIN));
	props << cfg.Get(Property("scene.epsilon.max")(DEFAULT_EPSILON_MAX));

	props << cfg.Get(Property("scene.file")("scenes/luxball/luxball.scn"));
	props << cfg.Get(Property("scene.images.resizepolicy.type")("NONE"));

	// LightStrategy
	props << LightStrategy::ToProperties(cfg);

	// RenderEngine (includes PixelFilter and Sampler where applicable)
	props << RenderEngine::ToProperties(cfg);

	// Film
	props << Film::ToProperties(cfg);

	// Periodic saving
	props << cfg.Get(Property("periodicsave.film.outputs.period")(0.f));
	props << cfg.Get(Property("periodicsave.film.period")(0.f));
	props << cfg.Get(Property("periodicsave.film.filename")("film.flm"));
	props << cfg.Get(Property("periodicsave.resumerendering.period")(0.f));
	props << cfg.Get(Property("periodicsave.resumerendering.filename")("rendering.rsm"));

	props << cfg.Get(Property("resumerendering.filesafe")(true));

	// Debug
	props << cfg.Get(Property("debug.renderconfig.parse.print")(false));
	props << cfg.Get(Property("debug.scene.parse.print")(false));

	//--------------------------------------------------------------------------

	// This property isn't really used by LuxCore but is useful for GUIs.
	props << cfg.Get(Property("screen.refresh.interval")(100u));
	// This property isn't really used by LuxCore but is useful for GUIs.
	props << cfg.Get(Property("screen.tool.type")("CAMERA_EDIT"));

	props << cfg.Get(Property("screen.tiles.pending.show")(true));
	props << cfg.Get(Property("screen.tiles.converged.show")(false));
	props << cfg.Get(Property("screen.tiles.notconverged.show")(false));

	props << cfg.Get(Property("screen.tiles.passcount.show")(false));
	props << cfg.Get(Property("screen.tiles.error.show")(false));

	return props;
}

//------------------------------------------------------------------------------
// Serialization methods
//------------------------------------------------------------------------------

RenderConfigUPtr RenderConfig::LoadSerialized(const std::string &fileName) {
	SerializationInputFile sif(fileName);

	RenderConfigUPtr renderConfig;
	sif.GetArchive() >> renderConfig;

	if (!sif.IsGood())
		throw runtime_error("Error while loading serialized render configuration: " + fileName);

	return renderConfig;
}

// Save serialized method - pointer argument
void RenderConfig::SaveSerialized(
	const std::string &fileName,
	const RenderConfigUPtr& renderConfig
) {
	Properties emptyProps;
	SaveSerialized(fileName, renderConfig, emptyProps);
}

void RenderConfig::SaveSerialized(
	const std::string &fileName,
	const RenderConfigUPtr& renderConfig,
	const luxrays::Properties &additionalCfg
) {
	SerializationOutputFile sof(fileName);

	// This is quite a trick
	renderConfig->saveAdditionalCfg.Clear();
	renderConfig->saveAdditionalCfg.Set(additionalCfg);

	sof.GetArchive() << renderConfig;

	renderConfig->saveAdditionalCfg.Clear();

	if (!sof.IsGood())
		throw runtime_error("Error while saving serialized render configuration: " + fileName);

	sof.Flush();

	SLG_LOG("Render configuration saved: " << (sof.GetPosition() / 1024) << " Kbytes");
}

// Save serialized method - reference argument
void RenderConfig::SaveSerialized(
	const std::string &fileName,
	const RenderConfigConstRef renderConfig,
	const luxrays::Properties &additionalCfg
) {
	SerializationOutputFile sof(fileName);

	// This is quite a trick
	renderConfig.saveAdditionalCfg.Clear();
	renderConfig.saveAdditionalCfg.Set(additionalCfg);

	sof.GetArchive() << renderConfig;

	renderConfig.saveAdditionalCfg.Clear();

	if (!sof.IsGood())
		throw runtime_error("Error while saving serialized render configuration: " + fileName);

	sof.Flush();

	SLG_LOG("Render configuration saved: " << (sof.GetPosition() / 1024) << " Kbytes");
}


template<class Archive> void RenderConfig::save(Archive &ar, const unsigned int version) const {
	Properties completeCfg;
	completeCfg.Set(*cfg);
	completeCfg.Set(saveAdditionalCfg);

	ar & completeCfg;
	ar & scene;
}

template<class Archive>	void RenderConfig::load(Archive &ar, const unsigned int version) {
	// In case there is an error while reading the archive
	scene = NULL;
	allocatedScene = true;

	ar & cfg;
	ar & scene;

	// Reset the properties cache
	propsCache.Clear();
}

namespace slg {
// Explicit instantiations for portable archives
template void RenderConfig::save(LuxOutputArchive &ar, const u_int version) const;
template void RenderConfig::load(LuxInputArchive &ar, const u_int version);
}
// vim: autoindent noexpandtab tabstop=4 shiftwidth=4
