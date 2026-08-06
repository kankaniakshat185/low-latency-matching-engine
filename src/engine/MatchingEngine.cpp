#include "engine/MatchingEngine.h"

namespace engine {

// Explicit instantiation of the concrete engine type used everywhere today.
// This is what actually compiles MatchingEngineT<OrderBook>'s method bodies
// into libengine.a — everything in MatchingEngine.h is a template now (a
// prerequisite for Phase 4's comparative study, which instantiates the same
// engine logic over multiple OrderBook implementations), so without this,
// each translation unit that uses MatchingEngine would silently re-compile
// its own copy instead of sharing one. Also surfaces any template error in
// the library build itself rather than only at whichever call site happens
// to instantiate it first.
template class MatchingEngineT<OrderBook>;

} // namespace engine
