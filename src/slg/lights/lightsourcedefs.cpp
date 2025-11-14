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

#include <thread>

#include <boost/algorithm/string/predicate.hpp>

#include "slg/scene/scene.h"
#include "slg/lights/trianglelight.h"
#include "slg/lights/strategies/logpower.h"

using namespace std;
using namespace luxrays;
using namespace slg;

//------------------------------------------------------------------------------
// LightSourceDefinitions
//------------------------------------------------------------------------------

LightSourceDefinitions::LightSourceDefinitions() : lightTypeCount(LIGHT_SOURCE_TYPE_COUNT, 0) {
	emitLightStrategy = std::make_shared<LightStrategyLogPower>();
	illuminateLightStrategy = std::make_shared<LightStrategyLogPower>();
	infiniteLightStrategy = std::make_shared<LightStrategyLogPower>();
	lightGroupCount = 1;
}

LightSourceDefinitions::~LightSourceDefinitions() {
}

void LightSourceDefinitions::DefineLightSource(LightSourcePtr newLight) {
	const string &name = newLight->GetName();

	if (IsLightSourceDefined(name)) {
		auto oldLight = GetLightSource(name);

		// Update name/LightSource definition
		lightsByName.erase(name);
		lightsByName[name] = newLight;
	} else {
		// Add the new LightSource
		lightsByName[name] = newLight;
	}
}

bool LightSourceDefinitions::IsLightSourceDefined(const std::string &name) const {
	return (lightsByName.count(name) > 0);
}

LightSourceConstPtr LightSourceDefinitions::GetLightSource(const string &name) const {
	// Check if the LightSource has been already defined
	auto e = lightsByName.find(name);

	if (e == lightsByName.end())
		throw runtime_error("Reference to an undefined LightSource in LightSourceDefinitions::GetLightSource(): " + name);
	else
		return e->second;
}

LightSourcePtr LightSourceDefinitions::GetLightSource(const string &name) {
	// Check if the LightSource has been already defined
	auto e = lightsByName.find(name);

	if (e == lightsByName.end())
		throw runtime_error("Reference to an undefined LightSource in LightSourceDefinitions::GetLightSource(): " + name);
	else
		return e->second;
}

TriangleLightConstPtr LightSourceDefinitions::GetLightSourceByMeshAndTriIndex(const u_int meshIndex, const u_int triIndex) const {
	const u_int offset = lightIndexOffsetByMeshIndex[meshIndex];
	const u_int lightIndex = lightIndexByTriIndex[offset + triIndex];

	return static_pointer_cast<const TriangleLight>(lights[lightIndex]);
}

vector<string> LightSourceDefinitions::GetLightSourceNames() const {
	vector<string> names;
	names.reserve(lights.size());
	for (auto const &e : lightsByName)
		names.push_back(e.first);

	return names;
}

void LightSourceDefinitions::DeleteLightSource(const string &name) {
	auto e = lightsByName.find(name);

	if (e == lightsByName.end())
		throw runtime_error("Reference to an undefined LightSource in LightSourceDefinitions::DeleteLightSource(): " + name);
	else {
		lightsByName.erase(name);
	}
}

void LightSourceDefinitions::DeleteLightSourceStartWith(const string &namePrefix) {
	// Build the list of lights to delete
	vector<string> nameList;
	for (auto const &e : lightsByName) {
		const string &name = e.first;

		if (name.starts_with(namePrefix))
			nameList.push_back(name);
	}

	for (auto const &name : nameList)
		DeleteLightSource(name);
}

void LightSourceDefinitions::DeleteLightSourceByMaterial(MaterialConstPtr mat) {
	// Build the list of lights to delete
	vector<string> nameList;
	for (auto const &e : lightsByName) {
		const string &name = e.first;
		auto l = e.second;

		if ((l->GetType() == TYPE_TRIANGLE) && ((static_pointer_cast<const TriangleLight>(l))->lightMaterial == mat))
			nameList.push_back(name);
	}

	for (auto const &name : nameList)
		DeleteLightSource(name);
}

void LightSourceDefinitions::SetLightStrategy(const luxrays::Properties &props) {
	if (LightStrategy::GetType(props) != emitLightStrategy->GetType()) {
		emitLightStrategy = LightStrategy::FromProperties(props);
	}

	if (LightStrategy::GetType(props) != illuminateLightStrategy->GetType()) {
		illuminateLightStrategy = LightStrategy::FromProperties(props);
	}

	if (LightStrategy::GetType(props) != infiniteLightStrategy->GetType()) {
		infiniteLightStrategy = LightStrategy::FromProperties(props);
	}
}

void LightSourceDefinitions::UpdateVolumeReferences(VolumeConstPtr oldVol, VolumeConstPtr newVol) {
	for (auto const &e : lightsByName) {
		LightSourcePtr l = e.second;
		
		l->UpdateVolumeReferences(oldVol, newVol);
	}

}

void LightSourceDefinitions::Preprocess(SceneConstPtr scene, const bool useRTMode) {
	// Update lightGroupCount, envLightSources, intersectableLightSources,
	// lightIndexOffsetByMeshIndex, lightsDistribution, etc.

//	const double start = WallClockTime();
	SLG_LOG("Preprocessing light source definitions");

	lightGroupCount = 0;
	lights.clear();
	lights.resize(lightsByName.size());
	intersectableLightSources.clear();
	envLightSources.clear();
	fill(lightTypeCount.begin(), lightTypeCount.end(), 0);
	// To accelerate the light pointer to light index lookup
	robin_hood::unordered_flat_map<LightSourceConstPtr, u_int> light2indexLookupAccel;

	u_int i = 0;
	for (auto const &e : lightsByName) {
		LightSourcePtr l = e.second;
		assert(l);

		// Initialize the light source index
		l->lightSceneIndex = i;
		light2indexLookupAccel[l] = i;

		// Update the light group count
		lightGroupCount = Max(lightGroupCount, l->GetID() + 1);

		// Update the light type count
		++lightTypeCount[l->GetType()];

		// Update the list of all lights
		lights[i] = l;

		// Update the list of env. lights
		if (l->IsEnvironmental())
			envLightSources.push_back(static_pointer_cast<EnvLightSource>(l));

		auto tl = dynamic_pointer_cast<TriangleLight>(l);
		if (tl) {
			intersectableLightSources.push_back(tl);

			tl->meshIndex = scene->objDefs.GetSceneObjectIndex(tl->sceneObject);
		}

		++i;
	}

//	const double end1 = WallClockTime();
//	SLG_LOG("Light step #1 preprocessing time: " << (end1 - start) << "secs");

	for(u_int i = 0; i < lights.size(); ++i) {
		auto tl = dynamic_pointer_cast<TriangleLight>(lights[i]);
		if (tl) {
			intersectableLightSources.push_back(tl);

			tl->meshIndex = scene->objDefs.GetSceneObjectIndex(tl->sceneObject);
		}
	}

//	const double end2 = WallClockTime();
//	SLG_LOG("Light step #2 preprocessing time: " << (end2 - end1) << "secs");

	// Build 2 tables to go from mesh index and triangle index to light index
	lightIndexOffsetByMeshIndex.resize(scene->objDefs.GetSize());
	lightIndexByTriIndex.clear();
	
	// Step #1: initialize lightIndexOffsetByMeshIndex and set the lightIndexByTriIndex size
	
	const u_int meshCount = scene->objDefs.GetSize();
	for (u_int meshIndex = 0; meshIndex < meshCount; ++meshIndex) {
		SceneObjectConstPtr so = scene->objDefs.GetSceneObject(meshIndex);

		if (so->GetMaterial()->IsLightSource()) {
			lightIndexOffsetByMeshIndex[meshIndex] = lightIndexByTriIndex.size();

			auto mesh = so->GetExtMesh();
			lightIndexByTriIndex.resize(lightIndexByTriIndex.size() + mesh->GetTotalTriangleCount());
		} else
			lightIndexOffsetByMeshIndex[meshIndex] = NULL_INDEX;
	}

	// Step #2: initializelightIndexByTriIndex in parallel (to speed up the
	// execution in case of large number of light sources)

	#pragma omp parallel for
	for (
			// Visual C++ 2013 supports only OpenMP 2.5
#if _OPENMP >= 200805
			unsigned
#endif
			int meshIndex = 0; meshIndex < meshCount; ++meshIndex) {
		SceneObjectConstPtr so = scene->objDefs.GetSceneObject(meshIndex);

		if (so->GetMaterial()->IsLightSource()) {
			auto mesh = so->GetExtMesh();
			const string prefix = Scene::EncodeTriangleLightNamePrefix(so->GetName());
			for (u_int triIndex = 0; triIndex < mesh->GetTotalTriangleCount(); ++triIndex) {
				const string lightName = prefix + ToString(triIndex);

				lightIndexByTriIndex[lightIndexOffsetByMeshIndex[meshIndex] + triIndex] =
						light2indexLookupAccel[GetLightSource(lightName)];
			}
		}
	}

//	const double end3 = WallClockTime();
//	SLG_LOG("Light step #3 preprocessing time: " << (end3 - end2) << "secs");

	// I need to check all volume definitions for radiance group usage too
	for (u_int i = 0; i < scene->matDefs.GetSize(); ++i) {
		MaterialConstPtr mat = scene->matDefs.GetMaterial(i);

		auto vol = dynamic_pointer_cast<const Volume>(mat);
		if (vol && vol->GetVolumeEmissionTexture()) {
			// Update the light group count
			lightGroupCount = Max(lightGroupCount, vol->GetVolumeLightID() + 1);
		}
	}
	//SLG_LOG("Radiance group count: " << lightGroupCount);

//	const double end4 = WallClockTime();
//	SLG_LOG("Light step #4 preprocessing time: " << (end4 - end3) << "secs");

	// Build the light strategy
	if (lights.size() > 1024) {
		// Use a multi-thread initialization for large number of light sources
		std::thread emitLightStrategyThread([&, this, scene, useRTMode]() {
			emitLightStrategy->Preprocess(scene, TASK_EMIT, useRTMode);
		});
		std::thread illuminateLightStrategyThread([&, this, scene, useRTMode]() {
			illuminateLightStrategy->Preprocess(scene, TASK_ILLUMINATE, useRTMode);
		});
		std::thread infiniteLightStrategyThread([&, this, scene, useRTMode]() {
			infiniteLightStrategy->Preprocess(scene, TASK_INFINITE_ONLY, useRTMode);
		});

		emitLightStrategyThread.join();
		illuminateLightStrategyThread.join();
		infiniteLightStrategyThread.join();
	} else {
		emitLightStrategy->Preprocess(scene, TASK_EMIT, useRTMode);
		illuminateLightStrategy->Preprocess(scene, TASK_ILLUMINATE, useRTMode);
		infiniteLightStrategy->Preprocess(scene, TASK_INFINITE_ONLY, useRTMode);
	}

//	const double end5 = WallClockTime();
//	SLG_LOG("Light step #5 preprocessing time: " << (end5 - end4) << "secs");

//	const double endTotal = WallClockTime();
//	SLG_LOG("Light total preprocessing time: " << (endTotal - start) << "secs");
}

void LightSourceDefinitions::UpdateVisibilityMaps(SceneConstPtr scene, const bool useRTMode) {
	// Build visibility maps for Env. lights
	for (auto envLight : GetEnvLightSources())
		envLight->UpdateVisibilityMap(scene, useRTMode);
}
// vim: autoindent noexpandtab tabstop=4 shiftwidth=4
