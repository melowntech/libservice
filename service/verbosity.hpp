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
