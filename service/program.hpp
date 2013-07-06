#ifndef shared_service_program_hpp_included_
#define shared_service_program_hpp_included_

#include <vector>
#include <string>
#include <ostream>

#include <boost/program_options.hpp>
#include <boost/filesystem/path.hpp>
#include <boost/noncopyable.hpp>

#include <dbglog/dbglog.hpp>

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

class program : boost::noncopyable {
public:
    virtual ~program();

    std::string identity() const;

    const std::string name;
    const std::string version;

    boost::filesystem::path logFile() const { return logFile_; }

    int flags() const { return flags_; }

protected:
    program(const std::string &name, const std::string &version, int flags);

    virtual void configuration(po::options_description &cmdline
                               , po::options_description &config
                               , po::positional_options_description &pd) = 0;

    virtual void configure(const po::variables_map &vars) = 0;

    virtual void configure(const std::vector<std::string> &unrecognized) = 0;

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

private:
    po::variables_map
    configureImpl(int argc, char *argv[]
                  , po::options_description genericCmdline
                  , po::options_description genericConfig);

    int flags_;
    boost::filesystem::path logFile_;
};

} // namespace service

#endif // shared_service_program_hpp_included_
