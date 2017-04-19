/**
 * Copyright (c) 2017 Melown Technologies SE
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * *  Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *
 * *  Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */
#ifndef service_verbosity_hpp_included_
#define service_verbosity_hpp_included_

#include <string>
#include <vector>

#include <boost/program_options.hpp>

namespace service {

namespace po = boost::program_options;

struct Verbosity {
    int level;
    Verbosity(int level = 0) : level(level) {}
    operator int() const { return level; }
};

inline void validate(boost::any &v, const std::vector<std::string>&
                     , Verbosity*, int)
{
    if (v.empty()) {
        v = Verbosity(1);
    } else {
        ++boost::any_cast<Verbosity&>(v).level;
    }
}

inline void verbosityConfiguration(po::options_description &options)
{
    options.add_options()
        ("verbose,V", po::value<Verbosity>()->zero_tokens()
         , "Verbose output. Use multiple times to inclrease verbosity level.")
        ;
}

inline Verbosity verbosityConfigure(const po::variables_map &vars)
{
    if (!vars.count("verbose")) { return {}; }
    return vars["verbose"].as<Verbosity>();
}

} // namespace service

#endif // service_verbosity_hpp_included_
