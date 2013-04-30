/***************************************************************************
 *   Copyright (C) 1998-2013 by authors (see AUTHORS.txt)                  *
 *                                                                         *
 *   This file is part of LuxRays.                                         *
 *                                                                         *
 *   LuxRays is free software; you can redistribute it and/or modify       *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 3 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   LuxRays is distributed in the hope that it will be useful,            *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program.  If not, see <http://www.gnu.org/licenses/>. *
 *                                                                         *
 *   LuxRays website: http://www.luxrender.net                             *
 ***************************************************************************/

#include "luxrays/core/accelerator.h"

using namespace luxrays;

std::string Accelerator::AcceleratorType2String(const AcceleratorType type) {
	switch(type) {
		case ACCEL_AUTO:
			return "AUTO";
		case ACCEL_BVH:
			return "BVH";
		case ACCEL_QBVH:
			return "QBVH";
		case ACCEL_MQBVH:
			return "MQBVH";
		case ACCEL_MBVH:
			return "MBVH";
		default:
			throw std::runtime_error("Unknown AcceleratorType in AcceleratorType2String()");
	}
}