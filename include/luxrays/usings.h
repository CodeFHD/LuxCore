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
// luxrays classes

#pragma once

#include <memory>

namespace luxrays {

class Accelerator;
using AcceleratorPtr = std::shared_ptr<Accelerator>;
using AcceleratorConstPtr = std::shared_ptr<const Accelerator>;
using AcceleratorUPtr = std::unique_ptr<Accelerator>;
using AcceleratorConstUPtr = std::unique_ptr<const Accelerator>;

class BBox;
using BBoxPtr = std::shared_ptr<BBox>;
using BBoxConstPtr = std::shared_ptr<const BBox>;
using BBoxUPtr = std::unique_ptr<BBox>;
using BBoxConstUPtr = std::unique_ptr<const BBox>;

class Context;
using ContextPtr = std::shared_ptr<Context>;
using ContextConstPtr = std::shared_ptr<const Context>;
using ContextUPtr = std::unique_ptr<Context>;
using ContextConstUPtr = std::unique_ptr<const Context>;

class DataSet;
using DataSetPtr = std::shared_ptr<DataSet>;
using DataSetConstPtr = std::shared_ptr<const DataSet>;
using DataSetUPtr = std::unique_ptr<DataSet>;
using DataSetConstUPtr = std::unique_ptr<const DataSet>;

class Device;
using DevicePtr = std::shared_ptr<Device>;
using DeviceConstPtr = std::shared_ptr<const Device>;
using DeviceUPtr = std::unique_ptr<Device>;
using DeviceConstUPtr = std::unique_ptr<const Device>;

class DeviceDescription;
using DeviceDescriptionPtr = std::shared_ptr<DeviceDescription>;
using DeviceDescriptionConstPtr = std::shared_ptr<const DeviceDescription>;
using DeviceDescriptionUPtr = std::unique_ptr<DeviceDescription>;
using DeviceDescriptionConstUPtr = std::unique_ptr<const DeviceDescription>;

class HardwareDevice;
using HardwareDevicePtr = std::shared_ptr<HardwareDevice>;
using HardwareDeviceConstPtr = std::shared_ptr<const HardwareDevice>;
using HardwareDeviceUPtr = std::unique_ptr<HardwareDevice>;
using HardwareDeviceConstUPtr = std::unique_ptr<const HardwareDevice>;

class IntersectionDevice;
using IntersectionDevicePtr = std::shared_ptr<IntersectionDevice>;
using IntersectionDeviceConstPtr = std::shared_ptr<const IntersectionDevice>;
using IntersectionDeviceUPtr = std::unique_ptr<IntersectionDevice>;
using IntersectionDeviceConstUPtr = std::unique_ptr<const IntersectionDevice>;

class Mesh;
using MeshPtr = std::shared_ptr<Mesh>;
using MeshConstPtr = std::shared_ptr<const Mesh>;
using MeshUPtr = std::unique_ptr<Mesh>;
using MeshConstUPtr = std::unique_ptr<const Mesh>;

class Matrix4x4;
using Matrix4x4Ptr = std::shared_ptr<Matrix4x4>;
using Matrix4x4ConstPtr = std::shared_ptr<Matrix4x4>;
using Matrix4x4UPtr = std::unique_ptr<Matrix4x4>;
using Matrix4x4ConstUPtr = std::unique_ptr<Matrix4x4>;

class Normal;
using NormalPtr = std::shared_ptr<Normal>;
using NormalConstPtr = std::shared_ptr<Normal>;
using NormalUPtr = std::unique_ptr<Normal>;
using NormalConstUPtr = std::unique_ptr<Normal>;

class Point;
using PointPtr = std::shared_ptr<Point>;
using PointConstPtr = std::shared_ptr<const Point>;
using PointUPtr = std::unique_ptr<Point>;
using PointConstUPtr = std::unique_ptr<const Point>;

class Ray;
using RayPtr = std::shared_ptr<Ray>;
using RayConstPtr = std::shared_ptr<const Ray>;
using RayUPtr = std::unique_ptr<Ray>;
using RayConstUPtr = std::unique_ptr<const Ray>;

class RayHit;
using RayHitPtr = std::shared_ptr<RayHit>;
using RayHitConstPtr = std::shared_ptr<const RayHit>;
using RayHitUPtr = std::unique_ptr<RayHit>;
using RayHitConstUPtr = std::unique_ptr<const RayHit>;

class RGBColor;
using RGBColorPtr = std::shared_ptr<RGBColor>;
using RGBColorConstPtr = std::shared_ptr<const RGBColor>;
using RGBColorUPtr = std::unique_ptr<RGBColor>;
using RGBColorConstUPtr = std::unique_ptr<const RGBColor>;

class Triangle;
using TrianglePtr = std::shared_ptr<Triangle>;
using TriangleConstPtr = std::shared_ptr<const Triangle>;
using TriangleUPtr = std::unique_ptr<Triangle>;
using TriangleConstUPtr = std::unique_ptr<const Triangle>;

class TriangleMesh;
using TriangleMeshPtr = std::shared_ptr<TriangleMesh>;
using TriangleMeshConstPtr = std::shared_ptr<const TriangleMesh>;
using TriangleMeshUPtr = std::unique_ptr<TriangleMesh>;
using TriangleMeshConstUPtr = std::unique_ptr<const TriangleMesh>;

class UV;
using UVPtr = std::shared_ptr<UV>;
using UVConstPtr = std::shared_ptr<const UV>;
using UVUPtr = std::unique_ptr<UV>;
using UVConstUPtr = std::unique_ptr<const UV>;

class Vector;
using VectorPtr = std::shared_ptr<Vector>;
using VectorConstPtr = std::shared_ptr<const Vector>;
using VectorUPtr = std::unique_ptr<Vector>;
using VectorConstUPtr = std::unique_ptr<const Vector>;

class Properties;
using PropertiesPtr = std::shared_ptr<Properties>;
using PropertiesConstPtr = std::shared_ptr<const Properties>;
using PropertiesConstRef = const Properties &;

}
// vim: autoindent noexpandtab tabstop=4 shiftwidth=4
