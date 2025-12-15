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

#ifndef _LUXCOREIMPL_H
#define	_LUXCOREIMPL_H

#include <format>

#include <luxcore/luxcore.h>
#include <slg/usings.h>
#include <slg/renderconfig.h>
#include <slg/rendersession.h>
#include <slg/renderstate.h>
#include <slg/scene/scene.h>
#include <slg/film/film.h>

namespace luxcore {
namespace detail {

class RenderSessionImpl;
using RenderSessionImplPtr = std::shared_ptr<RenderSessionImpl>;
using RenderSessionImplConstPtr = std::shared_ptr<const RenderSessionImpl>;
using RenderSessionImplUPtr = std::unique_ptr<RenderSessionImpl>;
using RenderSessionImplRef = RenderSessionImpl &;
using RenderSessionImplConstRef = const RenderSessionImpl &;

class RenderConfigImpl;
using RenderConfigImplPtr = std::shared_ptr<RenderConfigImpl>;

class RenderStateImpl;
using RenderStateImplPtr = std::shared_ptr<RenderStateImpl>;

class SceneImpl;
using SceneImplConstRef = const SceneImpl &;
using SceneImplPtr = std::shared_ptr<SceneImpl>;

class CameraImpl;
using CameraImplPtr = std::shared_ptr<CameraImpl>;
using CameraImplUPtr = std::unique_ptr<CameraImpl>;

class FilmImpl;
using FilmImplPtr = std::shared_ptr<FilmImpl>;

class FilmImplStandalone;
using FilmImplStandalonePtr = std::shared_ptr<FilmImplStandalone>;

// Disambiguation: there are luxcore::Film and slg:Film...
using LuxFilm = luxcore::Film;
using LuxFilmPtr = std::shared_ptr<luxcore::Film>;
using LuxFilmRef = luxcore::Film &;
using LuxFilmConstPtr = std::shared_ptr<const luxcore::Film>;

// Disambiguation: there are luxcore::Camera and slg:Camera...
using LuxCamera = luxcore::Camera;
using LuxCameraConstRef = const luxcore::Camera &;


//------------------------------------------------------------------------------
// FilmImpl
//------------------------------------------------------------------------------

class FilmImpl : public luxcore::Film {
public:

	// Standalone film
	static std::shared_ptr<FilmImpl> Create(slg::FilmPtr film);
	static std::shared_ptr<FilmImpl> Create(const std::string &fileName);
	static std::shared_ptr<FilmImpl> Create(
		luxrays::PropertiesConstPtr props,
		const bool hasPixelNormalizedChannel,
		const bool hasScreenNormalizedChannel
	);

	// Session film
	static std::shared_ptr<FilmImpl> Create(RenderSessionImplRef session);

	unsigned int GetWidth() const;
	unsigned int GetHeight() const;
	luxrays::Properties GetStats() const;
	float GetFilmY(const unsigned int imagePipelineIndex = 0) const;

	void Clear();
	void AddFilm(LuxFilmConstPtr film);
	void AddFilm(
		LuxFilmConstPtr film,
		const unsigned int srcOffsetX, const unsigned int srcOffsetY,
		const unsigned int srcWidth, const unsigned int srcHeight,
		const unsigned int dstOffsetX, const unsigned int dstOffsetY
	);

	virtual void SaveOutputs() const = 0;
	void SaveOutput(
		const std::string &fileName,
		const FilmOutputType type,
		luxrays::PropertiesConstPtr props
	) const;
	virtual void SaveFilm(const std::string &fileName) const = 0;

	double GetTotalSampleCount() const;

	size_t GetOutputSize(const FilmOutputType type) const;
	bool HasOutput(const FilmOutputType type) const;
	unsigned int GetOutputCount(const FilmOutputType type) const;

	unsigned int GetRadianceGroupCount() const;
	bool HasChannel(const FilmChannelType type) const;
	unsigned int GetChannelCount(const FilmChannelType type) const;

	virtual void GetOutputFloat(const FilmOutputType type, float *buffer,
			const unsigned int index, const bool executeImagePipeline) = 0;
	virtual void GetOutputUInt(const FilmOutputType type, unsigned int *buffer,
			const unsigned int index, const bool executeImagePipeline) = 0;
	void UpdateOutputFloat(const FilmOutputType type, const float *buffer,
			const unsigned int index, const bool executeImagePipeline) = 0;
	void UpdateOutputUInt(const FilmOutputType type, const unsigned int *buffer,
			const unsigned int index, const bool executeImagePipeline); // throw

	virtual const float *GetChannelFloat(const FilmChannelType type,
			const unsigned int index, const bool executeImagePipeline) = 0;
	virtual const unsigned int *GetChannelUInt(const FilmChannelType type,
			const unsigned int index, const bool executeImagePipeline) = 0;
	virtual float *UpdateChannelFloat(const FilmChannelType type,
			const unsigned int index, const bool executeImagePipeline) = 0;
	virtual unsigned int *UpdateChannelUInt(const FilmChannelType type,
			const unsigned int index, const bool executeImagePipeline);

	virtual void Parse(luxrays::PropertiesConstPtr props) = 0;

	virtual void DeleteAllImagePipelines() = 0;

	virtual void ExecuteImagePipeline(const u_int index) = 0;
	virtual void AsyncExecuteImagePipeline(const u_int index) = 0;
	virtual void WaitAsyncExecuteImagePipeline() = 0;
	virtual bool HasDoneAsyncExecuteImagePipeline() = 0;

	virtual void ApplyOIDN(const u_int index) = 0;

	friend class RenderSessionImpl;

protected:
	FilmImpl() {}

private:
	virtual slg::FilmPtr GetSLGFilm() const = 0;
};


// FilmImplStandalone is created from another Film
class FilmImplStandalone : public FilmImpl {
public:
	FilmImplStandalone(slg::FilmPtr film);
	FilmImplStandalone(const std::string &fileName);
	FilmImplStandalone(
		luxrays::PropertiesConstPtr props,
		const bool hasPixelNormalizedChannel,
		const bool hasScreenNormalizedChannel
	);

	FilmImplStandalone() = delete;

	virtual void SaveOutputs() const override;
	virtual void SaveFilm(const std::string &fileName) const override;
	virtual void GetOutputFloat(const FilmOutputType type, float *buffer,
			const unsigned int index, const bool executeImagePipeline) override;
	virtual void GetOutputUInt(const FilmOutputType type, unsigned int *buffer,
			const unsigned int index, const bool executeImagePipeline) override;
	void UpdateOutputFloat(const FilmOutputType type, const float *buffer,
			const unsigned int index, const bool executeImagePipeline) override;

	virtual const float *GetChannelFloat(const FilmChannelType type,
			const unsigned int index, const bool executeImagePipeline) override;
	virtual float *UpdateChannelFloat(const FilmChannelType type,
			const unsigned int index, const bool executeImagePipeline) override;
	virtual const unsigned int * GetChannelUInt(const FilmChannelType type,
		const unsigned int index, const bool executeImagePipeline) override;


	virtual void Parse(luxrays::PropertiesConstPtr props) override;

	virtual void DeleteAllImagePipelines() override;

	virtual void ExecuteImagePipeline(const u_int index) override;
	virtual void AsyncExecuteImagePipeline(const u_int index) override;
	virtual void WaitAsyncExecuteImagePipeline() override;
	virtual bool HasDoneAsyncExecuteImagePipeline() override;

	virtual void ApplyOIDN(const u_int index) override;

	slg::FilmPtr standAloneFilm;

private:

	virtual slg::FilmPtr GetSLGFilm() const override;
};


// FilmImplSession is created by RenderSessionImpl
class FilmImplSession : public FilmImpl {
public:
	FilmImplSession(RenderSessionImplRef session);

	FilmImplSession() = delete;

	virtual void SaveOutputs() const override;
	virtual void SaveFilm(const std::string &fileName) const override;
	virtual void GetOutputFloat(const FilmOutputType type, float *buffer,
			const unsigned int index, const bool executeImagePipeline) override;
	virtual void GetOutputUInt(const FilmOutputType type, unsigned int *buffer,
			const unsigned int index, const bool executeImagePipeline) override;
	void UpdateOutputFloat(const FilmOutputType type, const float *buffer,
			const unsigned int index, const bool executeImagePipeline) override;

	virtual const float *GetChannelFloat(const FilmChannelType type,
			const unsigned int index, const bool executeImagePipeline) override;
	virtual float *UpdateChannelFloat(const FilmChannelType type,
			const unsigned int index, const bool executeImagePipeline) override;
	virtual const unsigned int * GetChannelUInt(const FilmChannelType type,
		const unsigned int index, const bool executeImagePipeline) override;


	virtual void Parse(luxrays::PropertiesConstPtr props) override;

	virtual void DeleteAllImagePipelines() override;

	virtual void ExecuteImagePipeline(const u_int index) override;
	virtual void AsyncExecuteImagePipeline(const u_int index) override;
	virtual void WaitAsyncExecuteImagePipeline() override;
	virtual bool HasDoneAsyncExecuteImagePipeline() override;

	virtual void ApplyOIDN(const u_int index) override;

	RenderSessionImplRef renderSession;  // Back link, read/write

private:

	virtual slg::FilmPtr GetSLGFilm() const override;
};


//------------------------------------------------------------------------------
// CameraImpl
//------------------------------------------------------------------------------


class CameraImpl : public luxcore::Camera {
public:
	CameraImpl(SceneImplConstRef scene);
	~CameraImpl();

	const CameraType GetType() const;

	void Translate(const float x, const float y, const float z) const;
	void TranslateLeft(const float t) const;
	void TranslateRight(const float t) const;
	void TranslateForward(const float t) const;
	void TranslateBackward(const float t) const;

	void Rotate(const float angle, const float x, const float y, const float z) const;
	void RotateLeft(const float angle) const;
	void RotateRight(const float angle) const;
	void RotateUp(const float angle) const;
	void RotateDown(const float angle) const;

	friend class SceneImpl;

private:
	SceneImplConstRef scene;
};

//------------------------------------------------------------------------------
// SceneImpl
//------------------------------------------------------------------------------

class SceneImpl : public luxcore::Scene {
public:
	SceneImpl(slg::ScenePtr scn);
	SceneImpl(luxrays::PropertiesConstPtr resizePolicyProps = nullptr);
	SceneImpl(
		luxrays::PropertiesConstPtr props,
		luxrays::PropertiesConstPtr resizePolicyProps
	);
	SceneImpl(
		const std::string &fileName,
		luxrays::PropertiesConstPtr resizePolicyProps = nullptr
	);

	void GetBBox(float min[3], float max[3]) const;
	LuxCameraConstRef GetCamera() const;

	bool IsImageMapDefined(const std::string &imgMapName) const;

	void SetDeleteMeshData(const bool v);
	void SetMeshAppliedTransformation(const std::string &meshName,
			const float *appliedTransMat);

	void DefineMesh(const std::string &meshName,
		const long plyNbVerts, const long plyNbTris,
		float *p, unsigned int *vi, float *n,
		float *uvs,	float *cols, float *alphas);
	void DefineMeshExt(const std::string &meshName,
		const long plyNbVerts, const long plyNbTris,
		float *p, unsigned int *vi, float *n,
		std::array<float *, LC_MESH_MAX_DATA_COUNT> *uv,
		std::array<float *, LC_MESH_MAX_DATA_COUNT> *cols,
		std::array<float *, LC_MESH_MAX_DATA_COUNT> *alphas);
	void SetMeshVertexAOV(const std::string &meshName,
		const unsigned int index, float *data);
	void SetMeshTriangleAOV(const std::string &meshName,
		const unsigned int index, float *data);

	void SaveMesh(const std::string &meshName, const std::string &fileName);
	void DefineStrands(
		const std::string &shapeName,
		const luxrays::cyHairFile &strandsFile,
		const StrandsTessellationType tesselType,
		const unsigned int adaptiveMaxDepth,
		const float adaptiveError,
		const unsigned int solidSideCount,
		const bool solidCapBottom,
		const bool solidCapTop,
		const bool useCameraPosition
	);

	bool IsMeshDefined(const std::string &meshName) const;
	bool IsTextureDefined(const std::string &texName) const;
	bool IsMaterialDefined(const std::string &matName) const;

	const unsigned int GetLightCount() const;
	const unsigned int GetObjectCount() const;

	void Parse(luxrays::PropertiesConstPtr props);

	void DuplicateObject(
		const std::string &srcObjName, const std::string &dstObjName,
		const float transMat[16], const unsigned int objectID
	);
	void DuplicateObject(
		const std::string &srcObjName, const std::string &dstObjNamePrefix,
		const unsigned int count, const float *transMat, const unsigned int *objectIDs
	);
	void DuplicateObject(
		const std::string &srcObjName, const std::string &dstObjName,
		const unsigned int steps, const float *times, const float *transMats,
		const unsigned int objectID);
	void DuplicateObject(
		const std::string &srcObjName, const std::string &dstObjNamePrefix,
		const unsigned int count, const unsigned int steps, const float *times,
		const float *transMats, const unsigned int *objectIDs);
	void UpdateObjectTransformation(
		const std::string &objName, const float transMat[16]
	);
	void UpdateObjectMaterial(
		const std::string &objName, const std::string &matName
	);

	void DeleteObject(const std::string &objName);
	void DeleteObjects(std::vector<std::string> &objNames);
	void DeleteLight(const std::string &lightName);
	void DeleteLights(std::vector<std::string> &lightNames);

	void RemoveUnusedImageMaps();
	void RemoveUnusedTextures();
	void RemoveUnusedMaterials();
	void RemoveUnusedMeshes();

	void DefineImageMapUChar(
		const std::string &imgMapName,
		unsigned char *pixels,
		const float gamma,
		const unsigned int channels,
		const unsigned int width,
		const unsigned int height,
		ChannelSelectionType selectionType,
		WrapType wrapType
	);
	void DefineImageMapHalf(const std::string &imgMapName,
			unsigned short *pixels, const float gamma, const unsigned int channels,
			const unsigned int width, const unsigned int height,
			ChannelSelectionType selectionType, WrapType wrapType);
	void DefineImageMapFloat(const std::string &imgMapName,
			float *pixels, const float gamma, const unsigned int channels,
			const unsigned int width, const unsigned int height,
			ChannelSelectionType selectionType, WrapType wrapType);

	const luxrays::Properties &ToProperties() const;
	void Save(const std::string &fileName) const;

	// Note: this method is not part of LuxCore API and it is used only internally
	void DefineMesh(std::shared_ptr<luxrays::ExtTriangleMesh> mesh);

	static luxrays::Point *AllocVerticesBuffer(const unsigned int meshVertCount);
	static luxrays::Triangle *AllocTrianglesBuffer(const unsigned int meshTriCount);

	friend class CameraImpl;
	friend class RenderConfigImpl;
	friend class RenderSessionImpl;

private:
	mutable luxrays::Properties scenePropertiesCache;

	slg::ScenePtr scene;
	CameraImplUPtr camera;
	bool allocatedScene;
};

//------------------------------------------------------------------------------
// RenderConfigImpl
//------------------------------------------------------------------------------


class RenderConfigImpl : public luxcore::RenderConfig {
public:
	RenderConfigImpl(
		luxrays::PropertiesConstPtr props,
		SceneImplPtr scene = nullptr
	);
	RenderConfigImpl(const std::string &fileName);
	RenderConfigImpl(
		const std::string &fileName,
		std::shared_ptr<RenderStateImpl>& startState,  // Out
		std::shared_ptr<FilmImpl>& startFilm  // Out
	);

	const luxrays::Properties &GetProperties() const;
	const luxrays::Property GetProperty(const std::string &name) const;
	const luxrays::Properties &ToProperties() const;

	Scene &GetScene() const;

	bool HasCachedKernels() const;

	void Parse(luxrays::PropertiesConstPtr props);

	void Delete(const std::string &prefix);

	bool GetFilmSize(unsigned int *filmFullWidth, unsigned int *filmFullHeight,
		unsigned int *filmSubRegion) const;

	void DeleteSceneOnExit();

	void Save(const std::string &fileName) const;
	void Export(const std::string &dirName) const;
	void ExportGLTF(const std::string &fileName) const;

	static const luxrays::Properties &GetDefaultProperties();

	friend class RenderSessionImpl;


private:
	std::unique_ptr<slg::RenderConfig> renderConfig;
	std::shared_ptr<SceneImpl> scene;
	bool allocatedScene;
};

//------------------------------------------------------------------------------
// RenderStateImpl
//------------------------------------------------------------------------------

class RenderStateImpl : public RenderState {
public:
	RenderStateImpl(const std::string &fileName);
	RenderStateImpl(std::shared_ptr<slg::RenderState> state);

	void Save(const std::string &fileName) const;

	friend class RenderSessionImpl;

private:
	// RenderStateImpl does not create underlying slg::RenderState,
	// hence the shared pointer
	std::shared_ptr<slg::RenderState> renderState;
};

//------------------------------------------------------------------------------
// RenderSessionImpl
//------------------------------------------------------------------------------

class RenderSessionImpl : public luxcore::RenderSession
{
	// https://en.cppreference.com/w/cpp/memory/enable_shared_from_this.html
	// Need that to use std::make_unique
	struct Private{ explicit Private() = default; };

public:

	static RenderSessionImplUPtr Create(
		std::shared_ptr<RenderConfigImpl> config
	);
	static RenderSessionImplUPtr Create(
		std::shared_ptr<RenderConfigImpl> config,
		std::shared_ptr<RenderStateImpl>& startState,
		std::shared_ptr<FilmImplStandalone>& startFilm
	);
	static RenderSessionImplUPtr Create(
		std::shared_ptr<RenderConfigImpl> config,
		const std::string &startStateFileName,
		const std::string &startFilmFileName
	);

	// Public... but private constructors
	// https://en.cppreference.com/w/cpp/memory/enable_shared_from_this.html
	RenderSessionImpl(
		Private priv,
		std::shared_ptr<RenderConfigImpl> config,
		std::shared_ptr<RenderStateImpl> startState = nullptr,
		std::shared_ptr<FilmImplStandalone> startFilm = nullptr
	);
	RenderSessionImpl(
		Private priv,
		std::shared_ptr<RenderConfigImpl> config,
		const std::string &startStateFileName,
		const std::string &startFilmFileName
	);

	std::shared_ptr<RenderConfig> GetRenderConfig() override;
	std::shared_ptr<RenderState> GetRenderState() override;

	void Start() override;
	void Stop() override;
	bool IsStarted() const override;

	void BeginSceneEdit() override;
	void EndSceneEdit() override;
	bool IsInSceneEdit() const override;

	void Pause() override;
	void Resume() override;
	bool IsInPause() const override;

	bool HasDone() const override;
	void WaitForDone() const override;
	void WaitNewFrame() override;

	LuxFilmPtr GetFilm() override;

	void UpdateStats() override;
	const luxrays::Properties &GetStats() const override;

	void Parse(luxrays::PropertiesConstPtr props) override;

	void SaveResumeFile(const std::string &fileName) override;

	virtual slg::RenderSessionRef GetSLGRenderSession() const { return *renderSession; }

	virtual ~RenderSessionImpl() override {
	}

	friend class FilmImpl;

private:
	// RenderSessionImpl is created by RenderConfigImpl
	// It should be a reference, but it can't, due to serialization
	std::shared_ptr<RenderConfigImpl> renderConfig;  // Back link

	// RenderSessionImpl creates and owns a slg::RenderSession and a FilmImpl
	// RenderSession must not be shared
	std::unique_ptr<slg::RenderSession> renderSession;
	std::shared_ptr<FilmImpl> film;
	luxrays::Properties stats;

	void InitFilm();

};

}
}

template <> struct std::formatter<luxcore::Camera::CameraType>: formatter<string_view> {

  auto format(luxcore::Camera::CameraType cam, std::format_context& ctx) const
    -> format_context::iterator;
};



#endif	/* _LUXCOREIMPL_H */
// vim: autoindent noexpandtab tabstop=4 shiftwidth=4
