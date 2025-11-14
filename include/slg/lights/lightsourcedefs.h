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

#ifndef _SLG_LIGHTSOURCEDEFINITIONS_H
#define	_SLG_LIGHTSOURCEDEFINITIONS_H

#include <robin_hood.h>

#include "luxrays/utils/properties.h"
#include "slg/lights/light.h"
#include "slg/lights/trianglelight.h"
#include "slg/lights/strategies/lightstrategy.h"

namespace slg {

//------------------------------------------------------------------------------
// LightSourceDefinitions
//------------------------------------------------------------------------------

class TriangleLight;
class Scene;

class LightSourceDefinitions {
public:
	LightSourceDefinitions();
	~LightSourceDefinitions();

	void SetLightStrategy(const luxrays::Properties &props);

	void UpdateVisibilityMaps(SceneConstPtr scene, const bool useRTMode);

	void DefineLightSource(LightSourcePtr l);
	bool IsLightSourceDefined(const std::string &name) const;

	LightSourceConstPtr GetLightSource(const std::string &name) const;
	LightSourcePtr GetLightSource(const std::string &name);

	u_int GetSize() const { return static_cast<u_int>(lightsByName.size()); }
	std::vector<std::string> GetLightSourceNames() const;

	void DeleteLightSource(const std::string &name);
	void DeleteLightSourceStartWith(const std::string &namePrefix);
	void DeleteLightSourceByMaterial(MaterialConstPtr mat);

	void UpdateVolumeReferences(VolumeConstPtr oldVol, VolumeConstPtr newVol);

	//--------------------------------------------------------------------------
	// Following methods require Preprocess()
	//--------------------------------------------------------------------------

	TriangleLightConstPtr GetLightSourceByMeshAndTriIndex(const u_int meshIndex, const u_int triIndex) const;

	u_int GetLightGroupCount() const { return lightGroupCount; }
	const u_int GetLightTypeCount(const LightSourceType type) const { return lightTypeCount[type]; }
	const std::vector<u_int> &GetLightTypeCounts() const { return lightTypeCount; }

	LightSourcePtr GetLightSource(size_t n) const {
		//return lights[n];
		return lights.at(n);
	}
	const std::vector<EnvLightSourcePtr> &GetEnvLightSources() const {
		return envLightSources;
	}
	const std::vector<TriangleLightPtr> &GetIntersectableLightSources() const {
		return intersectableLightSources;
	}
	const std::vector<u_int> &GetLightIndexOffsetByMeshIndex() const { return lightIndexOffsetByMeshIndex; }
	const std::vector<u_int> &GetLightIndexByTriIndex() const { return lightIndexByTriIndex; }
	LightStrategyConstPtr GetEmitLightStrategy() const { return emitLightStrategy; }
	LightStrategyConstPtr GetIlluminateLightStrategy() const { return illuminateLightStrategy; }
	LightStrategyConstPtr GetInfiniteLightStrategy() const { return infiniteLightStrategy; }

	friend class Scene;

private:
	// Update lightGroupCount, envLightSources, intersectableLightSources,
	// lightIndexOffsetByMeshIndex, lightStrategyType, etc.
	// This is called by Scene::Preprocess()
	void Preprocess(SceneConstPtr scene, const bool useRTMode);

	robin_hood::unordered_flat_map<std::string, LightSourcePtr> lightsByName;

	//--------------------------------------------------------------------------
	// Following fields are updated with Preprocess() method
	//--------------------------------------------------------------------------

	u_int lightGroupCount;

	std::vector<u_int> lightTypeCount;

	std::vector<LightSourcePtr> lights;
	// Only intersectable light sources
	std::vector<TriangleLightPtr> intersectableLightSources;
	// Only env. light sources (i.e. sky, sun and infinite light, etc.)
	std::vector<EnvLightSourcePtr> envLightSources;

	// 2 tables to go from mesh index and triangle index to light index
	std::vector<u_int> lightIndexOffsetByMeshIndex;
	std::vector<u_int> lightIndexByTriIndex;

	LightStrategyPtr emitLightStrategy;
	LightStrategyPtr illuminateLightStrategy;
	LightStrategyPtr infiniteLightStrategy;
};

}

#endif	/* _SLG_LIGHTSOURCEDEFINITIONS_H */
// vim: autoindent noexpandtab tabstop=4 shiftwidth=4
