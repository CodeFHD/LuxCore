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
#include <optional>
#include <thread>
#include <experimental/memory>

#include <boost/serialization/serialization.hpp>
#include <boost/archive/text_iarchive.hpp>
#include <boost/serialization/export.hpp>
#include <boost/serialization/optional.hpp>
#include <boost/serialization/access.hpp>
#include <boost/serialization/split_free.hpp>



namespace luxrays {

template<typename T>
bool operator==(std::experimental::observer_ptr<T> & lhs, T * rhs) {
    return lhs.get() == rhs;
}

template<typename T>
bool operator==(std::experimental::observer_ptr<T> & lhs, const T * rhs) {
    return lhs.get() == rhs;
}

template<typename T>
bool operator==(std::experimental::observer_ptr<const T> & lhs, const T * rhs) {
    return lhs.get() == rhs;
}

template<typename T>
bool operator==(const std::experimental::observer_ptr<const T> lhs, const T& rhs) {
    return lhs.get() == &rhs;
}


}


// For observer_ptr serialization
namespace boost {
namespace serialization {

template<class Archive, class T>
void save(Archive & ar, const std::experimental::observer_ptr<T> & ptr, const unsigned int /* version */)
{
    const T* raw_ptr = ptr.get();
    ar << raw_ptr;
}

template<class Archive, class T>
void load(Archive & ar, std::experimental::observer_ptr<T> & ptr, const unsigned int /* version */)
{
    T* raw_ptr;
    ar >> raw_ptr;
    ptr.reset(raw_ptr); // observer_ptr can be assigned from a raw pointer
}

// Macro to define useful associated types

template<class Archive, class T>
void serialize(Archive & ar, std::experimental::observer_ptr<T> & ptr, const unsigned int version)
{
    split_free(ar, ptr, version);
}

} // namespace serialization
} // namespace boost

#define DECLARE_SUBTYPES(T) \
class T; \
using T##Ref = T&; \
using T##ConstRef = const T&; \
using T##UPtr = std::unique_ptr<T>; \
using T##ConstUPtr = std::unique_ptr<const T>; \
using T##Ptr = const std::unique_ptr<T> &; \
using T##ConstPtr = const std::unique_ptr<const T> &; \
using T##SPtr = std::shared_ptr<T>; \
using T##OPtr = std::experimental::observer_ptr<T>; \
using T##ConstSPtr = std::shared_ptr<const T>; \
using T##ConstOPtr = std::experimental::observer_ptr<const T>; \




namespace luxrays {

DECLARE_SUBTYPES(Accelerator);
DECLARE_SUBTYPES(BBox);
DECLARE_SUBTYPES(Blob);
DECLARE_SUBTYPES(BSphere);
DECLARE_SUBTYPES(Context);
DECLARE_SUBTYPES(DataSet);
DECLARE_SUBTYPES(Device);
DECLARE_SUBTYPES(DeviceDescription);
DECLARE_SUBTYPES(HardwareDevice);
DECLARE_SUBTYPES(IntersectionDevice)
DECLARE_SUBTYPES(Mesh);
DECLARE_SUBTYPES(ExtMesh);
DECLARE_SUBTYPES(ExtTriangleMesh);
DECLARE_SUBTYPES(ExtInstanceTriangleMesh);
DECLARE_SUBTYPES(ExtMotionTriangleMesh);
DECLARE_SUBTYPES(Matrix4x4);
DECLARE_SUBTYPES(NamedObject);
DECLARE_SUBTYPES(Normal);
DECLARE_SUBTYPES(Point);
DECLARE_SUBTYPES(Ray);
DECLARE_SUBTYPES(RayHit);
DECLARE_SUBTYPES(RGBColor);
DECLARE_SUBTYPES(Triangle);
DECLARE_SUBTYPES(TriangleMesh);
DECLARE_SUBTYPES(UV);
DECLARE_SUBTYPES(Vector);
DECLARE_SUBTYPES(Property);
DECLARE_SUBTYPES(Properties);
DECLARE_SUBTYPES(RandomGenerator);

using JThread = std::jthread;
using JThreadUPtr = std::unique_ptr<std::jthread>;


}

// vim: autoindent noexpandtab tabstop=4 shiftwidth=4
