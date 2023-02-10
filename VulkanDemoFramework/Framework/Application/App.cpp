#include "App.hpp"

namespace Framework
{

void App::run(const ApplicationConfiguration& configuration)
{
  create(configuration);
  mainLoop();
  destroy();
}

} // namespace Framework
