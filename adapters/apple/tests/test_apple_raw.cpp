// Apple RAW module: the shared camera RAW suite.
#include "still_conformance.hpp"
#include "vstudio/apple/apple_raw.hpp"

using namespace vstudio;

static vtest::StillSuite suite("apple.raw", [] { return apple::createRawDecoder(); });
