/***************************************************************************
 * Copyright 1998-2025 by authors (see AUTHORS.txt)                        *
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

// This file is intended to gather all usings (pointers, refs etc.) for
// slg classes

#pragma once

#include <memory>

namespace slg {

class Camera;
using CameraPtr = std::shared_ptr<Camera>;
using CameraConstPtr = std::shared_ptr<const Camera>;
using CameraRef = Camera&;

class Scene;
using SceneConstPtr = std::shared_ptr<const Scene>;
using ScenePtr = std::shared_ptr<Scene>;
using SceneConstRef = const Scene&;

class SceneObject;
using SceneObjectRef = SceneObject&;
using SceneObjectPtr = std::shared_ptr<SceneObject>;
using SceneObjectConstPtr = std::shared_ptr<const SceneObject>;

class Shape;
using ShapeConstPtr = std::shared_ptr<const Shape>;
using ShapePtr = std::shared_ptr<Shape>;

class Film;
using FilmPtr = std::shared_ptr<Film>;
using FilmConstPtr = std::shared_ptr<const Film>;
using FilmConstRef = const Film&;
using FilmRef = Film&;

class ImageMap;
using ImageMapConstPtr = std::shared_ptr<const ImageMap>;
using ImageMapPtr = std::shared_ptr<ImageMap>;

class LightSource;
using LightSourceConstPtr = std::shared_ptr<const LightSource>;
using LightSourcePtr = std::shared_ptr<LightSource>;

class EnvLightSource;
using EnvLightSourceConstPtr = std::shared_ptr<const EnvLightSource>;
using EnvLightSourcePtr = std::shared_ptr<EnvLightSource>;

class LightStrategy;
using LightStrategyConstPtr = std::shared_ptr<const LightStrategy>;
using LightStrategyPtr = std::shared_ptr<LightStrategy>;

class TriangleLight;
using TriangleLightConstPtr = std::shared_ptr<const TriangleLight>;
using TriangleLightPtr = std::shared_ptr<TriangleLight>;

class Material;
using MaterialRef = Material&;
using MaterialPtr = std::shared_ptr<Material>;
using MaterialConstPtr = std::shared_ptr<const Material>;

class RenderConfig;
using RenderConfigConstPtr = std::shared_ptr<const RenderConfig>;
using RenderConfigPtr = std::shared_ptr<RenderConfig>;
using RenderConfigUPtr = std::unique_ptr<RenderConfig>;
using RenderConfigConstUPtr = std::unique_ptr<const RenderConfig>;
using RenderConfigRef = RenderConfig &;
using RenderConfigConstRef = const RenderConfig &;

class RenderSession;
using RenderSessionConstPtr = std::shared_ptr<const RenderSession>;
using RenderSessionPtr = std::shared_ptr<RenderSession>;
using RenderSessionRef = RenderSession &;
using RenderSessionConstRef = const RenderSession &;

class RenderState;
using RenderStateConstPtr = std::shared_ptr<const RenderState>;
using RenderStatePtr = std::shared_ptr<RenderState>;

class RenderEngine;
using RenderEngineConstPtr = std::shared_ptr<const RenderEngine>;
using RenderEnginePtr = std::shared_ptr<RenderEngine>;

class RenderEngine;
using RenderEngineConstPtr = std::shared_ptr<const RenderEngine>;
using RenderEnginePtr = std::shared_ptr<RenderEngine>;
using RenderEngineUPtr = std::unique_ptr<RenderEngine>;

class Texture;
using TexturePtr = std::shared_ptr<Texture>;
using TextureConstPtr = std::shared_ptr<const Texture>;
using TextureRef = Texture&;

class FresnelTexture;
using FresnelTextureConstPtr = std::shared_ptr<const FresnelTexture>;

class TextureMapping2D;
using TextureMapping2DPtr = std::shared_ptr<TextureMapping2D>;
using TextureMapping2DConstPtr = std::shared_ptr<const TextureMapping2D>;

class TextureMapping3D;
using TextureMapping3DPtr = std::shared_ptr<TextureMapping3D>;
using TextureMapping3DConstPtr = std::shared_ptr<const TextureMapping3D>;

class Volume;
using VolumePtr = std::shared_ptr<Volume>;
using VolumeConstPtr = std::shared_ptr<const Volume>;
using VolumeRef = Volume&;
using VolumeConstRef = const Volume&;

}  // namespace slg

// vim: autoindent noexpandtab tabstop=4 shiftwidth=4
