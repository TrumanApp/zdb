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

#include "channel.h"

using namespace zdb;
using namespace std;

WaitGroup::WaitGroup() : m_counter(0) {}

void WaitGroup::add(size_t delta) {
    m_counter += delta;
}

void WaitGroup::done() {
    auto previous = m_counter.fetch_sub(1);
    if (previous == 1) {
        // Current is now zero, wake everything up
        m_cv.notify_all();
    }
}

void WaitGroup::wait() {
    unique_lock<mutex> lock(m_mutex);
    m_cv.wait(lock, [&]() { return m_counter == 0; });
}