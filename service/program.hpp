#ifndef shared_service_program_hpp_included_
#define shared_service_program_hpp_included_

#include <vector>
#include <string>
#include <ostream>
#include <ctime>
#include <functional>
#include <set>

#include <boost/program_options.hpp>
#include <boost/filesystem/path.hpp>
#include <boost/noncopyable.hpp>
#include <boost/optional.hpp>

#include <dbglog/dbglog.hpp>

#include "utility/duration.hpp"

namespace service {

namespace po = boost::program_options;

struct immediate_exit {
    immediate_exit(int code) : code(code) {}
    int code;
};

/** Throws immediate_exit.
 *  TODO: mark as noreturn.
 */
inline void immediateExit(int code) { throw immediate_exit(code); }

constexpr int DISABLE_CONFIG_HELP = 0x01;
constexpr int ENABLE_UNRECOGNIZED_OPTIONS = 0x02;
constexpr int DISABLE_EXCESSIVE_LOGGING = 0x04;
constexpr int SHOW_LICENCE_INFO = 0x08;
constexpr int ENABLE_CONFIG_UNRECOGNIZED_OPTIONS = 0x10;

struct UnrecognizedOptions;

struct UnrecognizedParser {
    typedef std::function<void(const po::variables_map&)> Configure;

    UnrecognizedParser(const std::string &help
                       , Configure configure = Configure())
        : options(help), configure(configure), extraParser()
    {}

    po::options_description options;
    po::positional_options_description positional;
    Configure configure;
    po::ext_parser extraParser;

    typedef boost::optional<UnrecognizedParser> optional;
};

class Program : boost::noncopyable {
public:
    virtual ~Program();

    std::string identity() const;
    std::string versionInfo() const;

    const std::string name;
    const std::string version;

    boost::filesystem::path logFile() const { return logFile_; }

    int flags() const { return flags_; }

    utility::Duration uptime();

    std::time_t upSince() const;

    void defaultConfigFile(const boost::filesystem::path &defaultConfigFile);

protected:
    Program(const std::string &name, const std::string &version, int flags);

    virtual void configuration(po::options_description &cmdline
                               , po::options_description &config
                               , po::positional_options_description &pd) = 0;

    virtual void configure(const po::variables_map &vars) = 0;

    /** Called if there were any unrecognized options. Allows us to parse them
     *  and return extra parser run on unrecongized options.
     */
    virtual UnrecognizedParser::optional
    configure(const po::variables_map &vars
              , const UnrecognizedOptions &unrecognized);

    /** Original version of the function above. Gets called when configure(vars,
     *  unrecognized) is not overridden.
     */
    virtual void configure(const std::vector<std::string> &unrecognized) = 0;

    /** Original version of the function above. Gets called when configure(vars,
     *  unrecognized) is not overridden.
     */
    virtual UnrecognizedParser::optional
    configure(const po::variables_map &vars
              , const std::vector<std::string> &unrecognized);

    /** Sets extra parser. Not used if function is empty.
     */
    virtual po::ext_parser extraParser() { return {}; }

    /** Produces help for *what*.
     *  Returns false if help for *what* is not supported.
     *
     *  Called with \param what empty to obtain program's description. Return
     *  value is ignored in this case.
     *
     *  You should register --help-what in cmdline configuration to inform
     *  user about such help availability.
     */
    virtual bool help(std::ostream &out, const std::string &what)
        const { (void) out; (void) what; return false; }

    /** Returns list of available help information.
     *  Used in --help-all.
     */
    virtual std::vector<std::string> listHelps() const { return {}; }

    dbglog::module log_;

    po::variables_map
    configure(int argc, char *argv[]
              , const po::options_description &genericConfig);

    po::variables_map
    configure(int argc, char *argv[]
              , const po::options_description &genericCmdline
              , const po::options_description &genericConfig);

    /** Hook before parsing config file.
     */
    virtual void preNotifyHook(const po::variables_map &vars);

    /** Hook before notify(vars) and configure(vars) is called.
     */
    virtual void preConfigHook(const po::variables_map &vars);

    bool noExcessiveLogging() const {
        return flags_ & DISABLE_EXCESSIVE_LOGGING;
    }

    /** Return copyright notice (used in `--version' too).
     */
    virtual std::string copyright() const;

    /** Return licence (empty by default).
     */
    virtual std::string licence() const;

    /** Licence holder (us by default).
     */
    virtual std::string licensee() const;

    /** Check whether this program is licensed to run.
     *
     * SHOW unlicensed use info and THROW immediate_exit if unlicensed use is
     * detected.
     */
    virtual void licenceCheck() const;

private:
    po::variables_map
    configureImpl(int argc, char *argv[]
                  , po::options_description genericCmdline
                  , po::options_description genericConfig);

    int flags_;
    boost::filesystem::path logFile_;

    utility::DurationMeter uptime_;
    std::time_t upSince_;

    /** Optional path to default config. Used if set and no config file was
     *  specified on the command line.
     */
    boost::optional<boost::filesystem::path> defaultConfigFile_;
};

struct UnrecognizedOptions {
    // cmdline stuff
    typedef std::vector<std::string> OptionList;
    OptionList cmdline;

    // config stuff
    typedef std::map<std::string, OptionList> ConfigOptions;
    typedef std::vector<ConfigOptions> MultipleConfigOptions;
    MultipleConfigOptions config;

    typedef std::set<std::string> Keys;

    /** Collects all keys used in the config.
     */
    Keys configKeys() const;

    /** Returns first occurence of key in config.
     *  Throws boost::program_options::required_value if not found.
     *
     *  Throws throw boost::program_options::multiple_values more than one
     *  value is enoutered.
     */
    const std::string& singleConfigOption(const std::string &key) const;

    /** Returns first occurence of key in config.
     *  Throws boost::program_options::required_value if not found.
     */
    OptionList multiConfigOption(const std::string &key) const;

    UnrecognizedOptions() {}
};

// inlines

inline UnrecognizedParser::optional
Program::configure(const po::variables_map&
                   , const std::vector<std::string> &unrecognized)
{
    configure(unrecognized);
    return {};
}

inline UnrecognizedParser::optional
Program::configure(const po::variables_map &vars
                   , const UnrecognizedOptions &unrecognized)
{
    return configure(vars, unrecognized.cmdline);
}

} // namespace service

#endif // shared_service_program_hpp_included_
