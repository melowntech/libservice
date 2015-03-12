#ifndef shared_service_signalguard_hpp_included_
#define shared_service_signalguard_hpp_included_

#include "utility/gccversion.hpp"
#include "utility/runnable.hpp"

namespace service {

class RunningUntilSignalled : public utility::Runnable {
public:
    RunningUntilSignalled();
    ~RunningUntilSignalled();

    virtual bool isRunning() UTILITY_OVERRIDE;
    virtual void stop() UTILITY_OVERRIDE;

private:
    struct Detail;
    friend struct Detail;
    std::unique_ptr<Detail> detail_;
    Detail& detail() { return *detail_; }
};

} // namespace service

#endif // shared_service_signalguard_hpp_included_
