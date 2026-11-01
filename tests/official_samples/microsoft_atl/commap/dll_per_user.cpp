// Policy for these existing test DLLs; original module sources are unchanged.
// Set metadata only during image initialization. Registry/COM work remains in
// the original exports, called explicitly after LoadLibrary has returned.
#include <atlbase.h>
namespace {
struct PerUserRegistration {
    PerUserRegistration() noexcept { ATL::AtlSetPerUserRegistration(true); }
} perUserRegistration;
}
