#include "reid/reid_factory.hpp"
#include <stdexcept>

std::unique_ptr<ReidFactory> ReidFactory::for_model(const ReidModelConfig&)
{
    throw std::runtime_error("ReidFactory requires a build with LWIR_ENABLE_DX_APP=ON");
}
