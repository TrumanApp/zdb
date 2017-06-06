/*
 * ZDB Copyright 2017 Regents of the University of Michigan
 *
 * Licensed under the Apache License, Version 2.0 (the "License"); you may not
 * use this file except in compliance with the License. You may obtain a copy
 * of the License at http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or
 * implied. See the License for the specific language governing
 * permissions and limitations under the License.
 */

#ifndef ZDB_SRC_CERTIFICATES_H
#define ZDB_SRC_CERTIFICATES_H

#include <algorithm>
#include <ctime>
#include <memory>
#include <set>
#include <string>

#include "zsearch_definitions/search.pb.h"

#include "macros.h"

namespace zdb {

bool certificate_valid_at(const zsearch::Certificate& cert, std::time_t now);

void expire_status(zsearch::RootStoreStatus* expired);

}  // namespace zdb

#endif /* ZDB_SRC_CERTIFICATES_H */
