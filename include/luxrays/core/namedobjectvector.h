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

#ifndef _LUXRAYS_NAMEDOBJECTVECTOR_H
#define	_LUXRAYS_NAMEDOBJECTVECTOR_H

#include <string>
#include <vector>
#include <ostream>

#include <boost/bimap.hpp>
#include <boost/bimap/unordered_set_of.hpp>

#include "luxrays/luxrays.h"
#include "luxrays/core/namedobject.h"

namespace luxrays {

struct UndefinedNamedObjectError : public std::runtime_error {
	UndefinedNamedObjectError(const std::string& msg) : std::runtime_error(msg) {}
};

class NamedObjectVector {
public:
	NamedObjectVector();

	NamedObjectPtr DefineObj(NamedObjectPtr newObj);
	bool IsObjDefined(const std::string &name) const;

	NamedObjectConstPtr GetObj(const std::string &name) const;
	NamedObjectPtr GetObj(const std::string &name);
	NamedObjectConstPtr GetObj(const u_int index) const;
	NamedObjectPtr GetObj(const u_int index);

	u_int GetIndex(const std::string &name) const;
	u_int GetIndex(NamedObjectConstPtr o) const;

	const std::string &GetName(const u_int index) const;
	const std::string &GetName(NamedObjectConstPtr o) const;

	u_int GetSize()const;
	void GetNames(std::vector<std::string> &names) const;
	std::vector<NamedObjectPtr>& GetObjs();

	void DeleteObj(const std::string &name);
	void DeleteObjs(const std::vector<std::string> &names);

	std::string ToString() const;

private:
	typedef boost::bimap<boost::bimaps::unordered_set_of<std::string>,
			boost::bimaps::unordered_set_of<u_int> > Name2IndexType;
	typedef boost::bimap<boost::bimaps::unordered_set_of<u_int>,
			boost::bimaps::unordered_set_of<const NamedObject *> > Index2ObjType;

	std::vector<NamedObjectPtr> objs;

	Name2IndexType name2index;
	Index2ObjType index2obj;
};

}

#endif	/* _LUXRAYS_NAMEDOBJECTVECTOR_H */
// vim: autoindent noexpandtab tabstop=4 shiftwidth=4
