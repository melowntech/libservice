#include "../program.hpp"

/** Licensing for OpenSource software. Set BUILDSYS_DEFAULT_CUSTOMER_NAME
 * variable to "oss" before including buildsys/cmake/buildsys.cmake in main
 * CMakeLists.txt:
 *
 *     # bootstrap build system
 *     cmake_minimum_required(VERSION 3.10)
 *     project(project-name)
 *     # by default, this is OSS build (can be overrided by customer machinery)
 *     set(BUILDSYS_DEFAULT_CUSTOMER_NAME oss)
 *     include(buildsys/cmake/buildsys.cmake)
 *
 */

namespace service {

// OSS Copyright holder
std::string Program::copyright() const
{
    return R"RAW(Copyright (C) 2011-2022 Melown Technologies SE
Strakonicka 3363/2d, 150 00 Praha 5, Czech Republic)RAW";
}

// OSS license (2 paragraph BSD)
std::string Program::licence() const
{
    return R"RAW(
Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

*  Redistributions of source code must retain the above copyright notice,
   this list of conditions and the following disclaimer.

*  Redistributions in binary form must reproduce the above copyright
   notice, this list of conditions and the following disclaimer in the
   documentation and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
POSSIBILITY OF SUCH DAMAGE.
)RAW";
}

// No specific licensee
std::string Program::licensee() const { return {}; }

// No license check (unrestricted)
void Program::licenceCheck() const {}

} // namespace service

